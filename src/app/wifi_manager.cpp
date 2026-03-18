#include "app/wifi_manager.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include <cstdio>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"

static const char *kTag = "wifi_mgr";

namespace {

static bool g_inited = false;

static esp_netif_t *g_sta_netif = nullptr;
static esp_netif_t *g_ap_netif = nullptr;

static EventGroupHandle_t g_evt = nullptr;
static esp_event_handler_instance_t g_wifi_any_id = nullptr;
static esp_event_handler_instance_t g_ip_got = nullptr;

static constexpr EventBits_t kBitGotIp = (1 << 0);
static constexpr EventBits_t kBitStaConnected = (1 << 1);
//static constexpr const char *kDefaultStaSsid = "google";
static constexpr const char *kDefaultStaSsid = "10242";
static constexpr const char *kDefaultStaPassword = "2048@@@@";
// esp_wifi_set_max_tx_power() unit is 0.25 dBm. 80 => 20 dBm actual.
static constexpr int8_t kWifiMaxTxPower = 80;
static constexpr bool kDebugStopAfterStaConnected = false;

static const char* AuthModeName(wifi_auth_mode_t authmode) {
  switch (authmode) {
    case WIFI_AUTH_OPEN:
      return "OPEN";
    case WIFI_AUTH_WEP:
      return "WEP";
    case WIFI_AUTH_WPA_PSK:
      return "WPA_PSK";
    case WIFI_AUTH_WPA2_PSK:
      return "WPA2_PSK";
    case WIFI_AUTH_WPA_WPA2_PSK:
      return "WPA_WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE:
      return "WPA2_ENT";
    case WIFI_AUTH_WPA3_PSK:
      return "WPA3_PSK";
    case WIFI_AUTH_WPA2_WPA3_PSK:
      return "WPA2_WPA3";
    case WIFI_AUTH_WAPI_PSK:
      return "WAPI_PSK";
    default:
      return "UNKNOWN";
  }
}

static const char* WifiModeName(wifi_mode_t mode) {
  switch (mode) {
    case WIFI_MODE_NULL:
      return "null";
    case WIFI_MODE_STA:
      return "sta";
    case WIFI_MODE_AP:
      return "ap";
    case WIFI_MODE_APSTA:
      return "apsta";
    default:
      return "unknown";
  }
}

static uint8_t RssiToStrengthPercent(int8_t rssi) {
  if (rssi <= -90) return 0;
  if (rssi >= -30) return 100;
  return static_cast<uint8_t>(((static_cast<int>(rssi) + 90) * 100) / 60);
}

static void WifiEventHandler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  (void)arg;
  if (event_base != WIFI_EVENT) return;

  if (event_id == WIFI_EVENT_STA_CONNECTED) {
    auto *e = reinterpret_cast<wifi_event_sta_connected_t *>(event_data);
    ESP_LOGI(kTag, "wifi evt: STA_CONNECTED ssid=%.*s channel=%u auth=%u", e ? e->ssid_len : 0,
             e ? reinterpret_cast<const char *>(e->ssid) : "", e ? static_cast<unsigned>(e->channel) : 0,
             e ? static_cast<unsigned>(e->authmode) : 0);
    if (g_evt) xEventGroupSetBits(g_evt, kBitStaConnected);
    return;
  }

  if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
    auto *e = reinterpret_cast<wifi_event_sta_disconnected_t *>(event_data);
    ESP_LOGW(kTag, "wifi evt: STA_DISCONNECTED reason=%u rssi=%d", e ? static_cast<unsigned>(e->reason) : 0,
             e ? static_cast<int>(e->rssi) : 0);
    if (g_evt) xEventGroupClearBits(g_evt, kBitGotIp | kBitStaConnected);
    return;
  }

  ESP_LOGI(kTag, "wifi evt: id=%ld", static_cast<long>(event_id));
}

static void IpEventHandler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  (void)arg;
  if (event_base != IP_EVENT) return;

  ESP_LOGI(kTag, "ip evt: id=%ld", static_cast<long>(event_id));

  if (event_id != IP_EVENT_STA_GOT_IP) return;

  auto *e = reinterpret_cast<ip_event_got_ip_t *>(event_data);
  ESP_LOGI(kTag, "got ip: " IPSTR, IP2STR(&e->ip_info.ip));
  if (g_evt) xEventGroupSetBits(g_evt, kBitGotIp);
}

static bool GetIpForNetif(esp_netif_t *netif, char *out_ip, uint32_t out_ip_sz) {
  if (out_ip && out_ip_sz) out_ip[0] = '\0';
  if (!netif || !out_ip || out_ip_sz == 0) return false;

  esp_netif_ip_info_t ip_info = {};
  if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) return false;
  if (ip_info.ip.addr == 0) return false;

  snprintf(out_ip, out_ip_sz, IPSTR, IP2STR(&ip_info.ip));
  return true;
}

static void MakeSetupSsid(char out[33]) {
  if (!out) return;
  uint8_t mac[6] = {};
  if (esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP) != ESP_OK) {
    snprintf(out, 33, "PIXEL-SETUP");
    return;
  }
  snprintf(out, 33, "PIXEL-SETUP-%02X%02X", mac[4], mac[5]);
}

static void ApplyWifiTxPower(const char *phase) {
  int8_t before = -1;
  const esp_err_t before_ret = esp_wifi_get_max_tx_power(&before);
  if (before_ret == ESP_OK) {
    ESP_LOGI(kTag, "wifi tx power before %s: raw=%d (%.2f dBm)", phase ? phase : "set",
             static_cast<int>(before), static_cast<double>(before) / 4.0);
  } else {
    ESP_LOGW(kTag, "wifi tx power get-before failed at %s: %s", phase ? phase : "set",
             esp_err_to_name(before_ret));
  }

  ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(kWifiMaxTxPower));

  int8_t after = -1;
  const esp_err_t after_ret = esp_wifi_get_max_tx_power(&after);
  if (after_ret == ESP_OK) {
    ESP_LOGI(kTag, "wifi tx power after %s: raw=%d (%.2f dBm)", phase ? phase : "set",
             static_cast<int>(after), static_cast<double>(after) / 4.0);
  } else {
    ESP_LOGW(kTag, "wifi tx power get-after failed at %s: %s", phase ? phase : "set",
             esp_err_to_name(after_ret));
  }
}

static void PopulateStaConfig(wifi_config_t* cfg, const char* ssid, const char* password) {
  if (!cfg) return;
  memset(cfg, 0, sizeof(*cfg));
  cfg->sta.bssid_set = 0;
  memset(cfg->sta.bssid, 0, sizeof(cfg->sta.bssid));
  cfg->sta.channel = 0;
  cfg->sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
  cfg->sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
  cfg->sta.threshold.rssi = -127;
  snprintf(reinterpret_cast<char*>(cfg->sta.ssid), sizeof(cfg->sta.ssid), "%s", ssid ? ssid : "");
  snprintf(reinterpret_cast<char*>(cfg->sta.password), sizeof(cfg->sta.password), "%s", password ? password : "");
}

static bool LoadSavedStaConfig(wifi_config_t* out_cfg) {
  if (!out_cfg) return false;
  memset(out_cfg, 0, sizeof(*out_cfg));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
  const esp_err_t get_ret = esp_wifi_get_config(WIFI_IF_STA, out_cfg);
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  if (get_ret != ESP_OK) return false;
  return out_cfg->sta.ssid[0] != 0;
}

static void LoadFallbackStaConfig(wifi_config_t* out_cfg) {
  PopulateStaConfig(out_cfg, kDefaultStaSsid, kDefaultStaPassword);
}

static void FillStatusString(char* dst, size_t dst_sz, const char* src) {
  if (!dst || dst_sz == 0) return;
  snprintf(dst, dst_sz, "%s", src ? src : "");
}

}  // namespace

bool WifiManagerInit() {
  if (g_inited) return true;

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(kTag, "nvs init needs erase: %s", esp_err_to_name(ret));
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  if (ret != ESP_OK) {
    ESP_LOGE(kTag, "nvs init failed: %s", esp_err_to_name(ret));
    return false;
  }

  ESP_ERROR_CHECK(esp_netif_init());
  {
    const esp_err_t loop_ret = esp_event_loop_create_default();
    if (loop_ret != ESP_OK && loop_ret != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(loop_ret);
  }

  g_sta_netif = esp_netif_create_default_wifi_sta();
  g_ap_netif = esp_netif_create_default_wifi_ap();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  // Use flash storage so previous config persists.
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));

  g_evt = xEventGroupCreate();
  if (!g_evt) {
    ESP_LOGE(kTag, "failed to create event group");
    return false;
  }

  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &WifiEventHandler, nullptr,
                                                      &g_wifi_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &IpEventHandler, nullptr,
                                                      &g_ip_got));

  g_inited = true;
  return true;
}

bool WifiManagerGetSavedStaSsid(char *out_ssid, uint32_t out_ssid_sz) {
  if (out_ssid && out_ssid_sz) out_ssid[0] = '\0';
  if (!out_ssid || out_ssid_sz == 0) return false;

  if (!WifiManagerInit()) return false;

  wifi_config_t cfg = {};
  if (!LoadSavedStaConfig(&cfg)) {
    snprintf(out_ssid, out_ssid_sz, "%s", kDefaultStaSsid);
    return true;
  }

  snprintf(out_ssid, out_ssid_sz, "%s", reinterpret_cast<const char *>(cfg.sta.ssid));
  return true;
}

bool WifiManagerConnectSta(uint32_t timeout_ms, WifiStaInfo *sta_out) {
  if (sta_out) {
    sta_out->ssid[0] = '\0';
    sta_out->ip[0] = '\0';
  }

  if (!WifiManagerInit()) return false;

  wifi_config_t sta_cfg = {};
  if (LoadSavedStaConfig(&sta_cfg)) {
    ESP_LOGI(kTag, "STA connect using saved ssid=%s", reinterpret_cast<const char*>(sta_cfg.sta.ssid));
  } else {
    LoadFallbackStaConfig(&sta_cfg);
    ESP_LOGW(kTag, "STA connect using fallback ssid=%s", kDefaultStaSsid);
  }

  xEventGroupClearBits(g_evt, kBitGotIp | kBitStaConnected);

  {
    ESP_LOGI(kTag, "sta: before esp_wifi_stop");
    const esp_err_t stop_ret = esp_wifi_stop();
    ESP_LOGI(kTag, "sta: after esp_wifi_stop ret=%s", esp_err_to_name(stop_ret));
    if (stop_ret != ESP_OK && stop_ret != ESP_ERR_WIFI_NOT_STARTED) ESP_ERROR_CHECK(stop_ret);
  }
  ESP_LOGI(kTag, "sta: before set_storage(RAM)");
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_LOGI(kTag, "sta: before set_mode(STA)");
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_LOGI(kTag, "sta: before set_config(STA)");
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
  ESP_LOGI(kTag, "sta: before esp_wifi_start");
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_LOGI(kTag, "sta: after esp_wifi_start");
  ApplyWifiTxPower("sta start");
  ESP_LOGI(kTag, "sta: before esp_wifi_connect");
  ESP_ERROR_CHECK(esp_wifi_connect());
  ESP_LOGI(kTag, "sta: after esp_wifi_connect");

  if (kDebugStopAfterStaConnected) {
    ESP_LOGW(kTag, "sta: isolation mode active -> waiting only for STA_CONNECTED, skipping GOT_IP");
    EventBits_t connected_bits = 0;
    const TickType_t connect_step = pdMS_TO_TICKS(100);
    const TickType_t connect_max_ticks = pdMS_TO_TICKS(timeout_ms);
    TickType_t connect_waited = 0;
    while (connect_waited < connect_max_ticks) {
      connected_bits = xEventGroupWaitBits(g_evt, kBitStaConnected, pdFALSE, pdTRUE, connect_step);
      if ((connected_bits & kBitStaConnected) != 0) break;
      connect_waited += connect_step;
      if ((connect_waited % pdMS_TO_TICKS(1000)) == 0) {
        ESP_LOGI(kTag, "sta: waiting connected... %u/%u ms",
                 static_cast<unsigned>(connect_waited * portTICK_PERIOD_MS), static_cast<unsigned>(timeout_ms));
      }
      vTaskDelay(1);
    }
    ESP_LOGI(kTag, "sta: isolation wait connected bits=0x%02x waited_ms=%u",
             static_cast<unsigned>(connected_bits), static_cast<unsigned>(connect_waited * portTICK_PERIOD_MS));
    if ((connected_bits & kBitStaConnected) == 0) {
      ESP_LOGW(kTag, "sta: isolation timeout before STA_CONNECTED");
      return false;
    }
    if (sta_out) {
      snprintf(sta_out->ssid, sizeof(sta_out->ssid), "%s", reinterpret_cast<const char *>(sta_cfg.sta.ssid));
      snprintf(sta_out->ip, sizeof(sta_out->ip), "%s", "SKIPPED");
    }
    ESP_LOGW(kTag, "sta: isolation branch returning before DHCP/GOT_IP");
    return true;
  }

  ESP_LOGI(kTag, "sta: before wait got_ip timeout_ms=%u", static_cast<unsigned>(timeout_ms));
  EventBits_t bits = 0;
  const TickType_t step = pdMS_TO_TICKS(100);
  const TickType_t max_ticks = pdMS_TO_TICKS(timeout_ms);
  TickType_t waited = 0;
  while (waited < max_ticks) {
    bits = xEventGroupWaitBits(g_evt, kBitGotIp, pdFALSE, pdTRUE, step);
    if ((bits & kBitGotIp) != 0) break;
    waited += step;
    if ((waited % pdMS_TO_TICKS(1000)) == 0) {
      ESP_LOGI(kTag, "sta: waiting got_ip... %u/%u ms", static_cast<unsigned>(waited * portTICK_PERIOD_MS),
               static_cast<unsigned>(timeout_ms));
    }
    vTaskDelay(1);
  }
  ESP_LOGI(kTag, "sta: after wait bits=0x%02x waited_ms=%u", static_cast<unsigned>(bits),
           static_cast<unsigned>(waited * portTICK_PERIOD_MS));
  if ((bits & kBitGotIp) == 0) {
    ESP_LOGW(kTag, "sta connect timeout");
    return false;
  }

  // Connected.
  if (sta_out) {
    snprintf(sta_out->ssid, sizeof(sta_out->ssid), "%s", reinterpret_cast<const char *>(sta_cfg.sta.ssid));
    if (!GetIpForNetif(g_sta_netif, sta_out->ip, sizeof(sta_out->ip))) {
      snprintf(sta_out->ip, sizeof(sta_out->ip), "--");
    }
  }
  return true;
}

bool WifiManagerStartSetupAp(WifiApInfo *ap_out) {
  if (ap_out) {
    ap_out->ssid[0] = '\0';
    ap_out->ip[0] = '\0';
  }

  if (!WifiManagerInit()) return false;

  char ap_ssid[33] = {};
  MakeSetupSsid(ap_ssid);

  wifi_config_t ap_cfg = {};
  const size_t ssid_len = strnlen(ap_ssid, sizeof(ap_cfg.ap.ssid));
  memcpy(ap_cfg.ap.ssid, ap_ssid, ssid_len);
  ap_cfg.ap.ssid_len = static_cast<uint8_t>(ssid_len);
  ap_cfg.ap.channel = 1;
  ap_cfg.ap.max_connection = 4;
  ap_cfg.ap.authmode = WIFI_AUTH_OPEN;

  {
    const esp_err_t stop_ret = esp_wifi_stop();
    if (stop_ret != ESP_OK && stop_ret != ESP_ERR_WIFI_NOT_STARTED) ESP_ERROR_CHECK(stop_ret);
  }
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
  ESP_ERROR_CHECK(esp_wifi_start());
  ApplyWifiTxPower("ap start");

  if (ap_out) {
    snprintf(ap_out->ssid, sizeof(ap_out->ssid), "%s", ap_ssid);
    // IP may not be ready immediately; poll briefly.
    bool ip_ok = false;
    for (int i = 0; i < 20; i++) {
      ip_ok = GetIpForNetif(g_ap_netif, ap_out->ip, sizeof(ap_out->ip));
      if (ip_ok) break;
      vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (!ip_ok) snprintf(ap_out->ip, sizeof(ap_out->ip), "--");
  }
  return true;
}

bool WifiManagerGetStatus(WifiStatusInfo* out_status) {
  if (out_status) {
    memset(out_status, 0, sizeof(*out_status));
  }
  if (!out_status) return false;
  if (!WifiManagerInit()) return false;

  wifi_mode_t mode = WIFI_MODE_NULL;
  if (esp_wifi_get_mode(&mode) != ESP_OK) {
    FillStatusString(out_status->mode, sizeof(out_status->mode), "unknown");
  } else {
    FillStatusString(out_status->mode, sizeof(out_status->mode), WifiModeName(mode));
  }

  wifi_config_t saved_cfg = {};
  if (LoadSavedStaConfig(&saved_cfg)) {
    snprintf(out_status->saved_ssid, sizeof(out_status->saved_ssid), "%s",
             reinterpret_cast<const char*>(saved_cfg.sta.ssid));
  } else {
    snprintf(out_status->saved_ssid, sizeof(out_status->saved_ssid), "%s", kDefaultStaSsid);
  }

  wifi_ap_record_t ap = {};
  if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK && ap.ssid[0]) {
    out_status->sta_connected = true;
    snprintf(out_status->sta_ssid, sizeof(out_status->sta_ssid), "%s", reinterpret_cast<const char*>(ap.ssid));
  } else if (saved_cfg.sta.ssid[0]) {
    snprintf(out_status->sta_ssid, sizeof(out_status->sta_ssid), "%s",
             reinterpret_cast<const char*>(saved_cfg.sta.ssid));
  }

  if (!GetIpForNetif(g_sta_netif, out_status->sta_ip, sizeof(out_status->sta_ip))) {
    FillStatusString(out_status->sta_ip, sizeof(out_status->sta_ip), "--");
  }

  out_status->ap_active = (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA);
  if (out_status->ap_active) {
    wifi_config_t ap_cfg = {};
    if (esp_wifi_get_config(WIFI_IF_AP, &ap_cfg) == ESP_OK && ap_cfg.ap.ssid[0]) {
      snprintf(out_status->ap_ssid, sizeof(out_status->ap_ssid), "%s",
               reinterpret_cast<const char*>(ap_cfg.ap.ssid));
    }
    if (!GetIpForNetif(g_ap_netif, out_status->ap_ip, sizeof(out_status->ap_ip))) {
      FillStatusString(out_status->ap_ip, sizeof(out_status->ap_ip), "--");
    }
  } else {
    FillStatusString(out_status->ap_ip, sizeof(out_status->ap_ip), "--");
  }

  return true;
}

int WifiManagerScanNetworks(WifiScanResult* out_results, int max_results, uint32_t timeout_ms) {
  if (out_results && max_results > 0) {
    memset(out_results, 0, sizeof(*out_results) * static_cast<size_t>(max_results));
  }
  if (!WifiManagerInit()) return 0;
  if (!out_results || max_results <= 0) return 0;

  wifi_mode_t mode = WIFI_MODE_NULL;
  if (esp_wifi_get_mode(&mode) != ESP_OK) {
    mode = WIFI_MODE_NULL;
  }

  if (mode == WIFI_MODE_NULL) {
    ESP_LOGI(kTag, "scan: set mode STA");
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  } else if (mode == WIFI_MODE_AP) {
    ESP_LOGI(kTag, "scan: switch mode APSTA");
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
  }

  const esp_err_t start_ret = esp_wifi_start();
  if (start_ret != ESP_OK && start_ret != ESP_ERR_WIFI_CONN) {
    ESP_LOGW(kTag, "scan: esp_wifi_start failed: %s", esp_err_to_name(start_ret));
    return 0;
  }

  wifi_scan_config_t scan_cfg = {};
  scan_cfg.show_hidden = true;
  scan_cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
  scan_cfg.scan_time.active.min = 60;
  scan_cfg.scan_time.active.max = 120;

  ESP_LOGI(kTag, "scan: start timeout_ms=%u", static_cast<unsigned>(timeout_ms));
  const esp_err_t scan_ret = esp_wifi_scan_start(&scan_cfg, true);
  if (scan_ret != ESP_OK) {
    ESP_LOGW(kTag, "scan: esp_wifi_scan_start failed: %s", esp_err_to_name(scan_ret));
    return 0;
  }

  uint16_t ap_count = 0;
  if (esp_wifi_scan_get_ap_num(&ap_count) != ESP_OK || ap_count == 0) {
    return 0;
  }

  std::vector<wifi_ap_record_t> aps(ap_count);
  if (esp_wifi_scan_get_ap_records(&ap_count, aps.data()) != ESP_OK || ap_count == 0) {
    return 0;
  }

  std::sort(aps.begin(), aps.begin() + ap_count, [](const wifi_ap_record_t& a, const wifi_ap_record_t& b) {
    return a.rssi > b.rssi;
  });

  int written = 0;
  for (uint16_t i = 0; i < ap_count && written < max_results; ++i) {
    const wifi_ap_record_t& ap_rec = aps[i];
    if (!ap_rec.ssid[0]) continue;

    bool duplicate = false;
    for (int j = 0; j < written; ++j) {
      if (strncmp(out_results[j].ssid, reinterpret_cast<const char*>(ap_rec.ssid), sizeof(out_results[j].ssid)) == 0) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) continue;

    snprintf(out_results[written].ssid, sizeof(out_results[written].ssid), "%s",
             reinterpret_cast<const char*>(ap_rec.ssid));
    snprintf(out_results[written].auth, sizeof(out_results[written].auth), "%s", AuthModeName(ap_rec.authmode));
    out_results[written].rssi = ap_rec.rssi;
    out_results[written].strength = RssiToStrengthPercent(ap_rec.rssi);
    written++;
  }

  ESP_LOGI(kTag, "scan: found=%u returned=%d", static_cast<unsigned>(ap_count), written);
  return written;
}

bool WifiManagerSaveStaCredentials(const char* ssid, const char* password) {
  if (!ssid || !ssid[0]) return false;
  if (strlen(ssid) >= 33) return false;
  if (password && strlen(password) >= 65) return false;
  if (!WifiManagerInit()) return false;

  wifi_config_t sta_cfg = {};
  PopulateStaConfig(&sta_cfg, ssid, password ? password : "");

  ESP_LOGI(kTag, "save sta config ssid=%s", ssid);
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
  const esp_err_t set_ret = esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  if (set_ret != ESP_OK) {
    ESP_LOGE(kTag, "save sta config failed: %s", esp_err_to_name(set_ret));
    return false;
  }
  return true;
}
