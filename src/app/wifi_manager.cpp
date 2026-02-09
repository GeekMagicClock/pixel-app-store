#include "app/wifi_manager.h"

#include <cstring>

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

static void WifiEventHandler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  (void)arg;
  (void)event_data;
  if (event_base != WIFI_EVENT) return;

  if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (g_evt) xEventGroupClearBits(g_evt, kBitGotIp);
  }
}

static void IpEventHandler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  (void)arg;
  if (event_base != IP_EVENT) return;
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
  if (esp_wifi_get_config(WIFI_IF_STA, &cfg) != ESP_OK) return false;
  if (!cfg.sta.ssid[0]) return false;

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
  if (esp_wifi_get_config(WIFI_IF_STA, &sta_cfg) != ESP_OK) {
    ESP_LOGW(kTag, "no saved sta config");
    return false;
  }
  if (!sta_cfg.sta.ssid[0]) {
    ESP_LOGW(kTag, "saved sta ssid empty");
    return false;
  }

  xEventGroupClearBits(g_evt, kBitGotIp);

  {
    const esp_err_t stop_ret = esp_wifi_stop();
    if (stop_ret != ESP_OK && stop_ret != ESP_ERR_WIFI_NOT_STARTED) ESP_ERROR_CHECK(stop_ret);
  }
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_ERROR_CHECK(esp_wifi_connect());

  const TickType_t to = pdMS_TO_TICKS(timeout_ms);
  const EventBits_t bits = xEventGroupWaitBits(g_evt, kBitGotIp, pdFALSE, pdTRUE, to);
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
