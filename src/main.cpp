#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <ctime>
#include <cstring>
#include <string>
#include <atomic>

#include "esp_log.h"
#include "esp_littlefs.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_sntp.h"

extern "C" {
#include "lvgl.h"
}

#include "cJSON.h"

#include "ESP32-HUB75-MatrixPanel-I2S-DMA.h"

#include "app/hub75_config.h"
#include "app/app_update_server.h"
#include "app/user_button.h"
#include "app/wifi_manager.h"
#include "ui/lvgl_boot_wifi_screen.h"
#include "ui/lvgl_hub75_port.h"
#include "ui/lvgl_lua_app_carousel.h"
#include "ui/lvgl_lua_app_screen.h"

static const char *kTag = "app";

static TaskHandle_t g_lvgl_task_handle = nullptr;
static char g_pending_switch_app_id[49] = {};
static char g_active_app_id[49] = "sunrise_sunset_owm";
static std::atomic_bool g_active_app_shown{false};
static std::atomic_uint32_t g_request_show_active_seq{0};
static std::atomic_uint32_t g_done_show_active_seq{0};
static std::atomic_uint32_t g_request_switch_app_seq{0};
static std::atomic_uint32_t g_done_switch_app_seq{0};

static void ShowActiveLuaAppOnLvglThread();

static bool RunOnLvglThread(void (*fn)(void), uint32_t retry_ms = 2000) {
  if (!fn) return false;

  // If already on LVGL task, execute directly to avoid async allocation failure.
  if (xTaskGetCurrentTaskHandle() == g_lvgl_task_handle) {
    fn();
    return true;
  }

  constexpr uint32_t kStepMs = 20;
  uint32_t waited = 0;
  while (waited <= retry_ms) {
    // lv_async_call needs a void(*)(void*) callback.
    const lv_res_t r = lv_async_call(
        [](void* p) {
          auto f = reinterpret_cast<void (*)(void)>(p);
          if (f) f();
        },
        reinterpret_cast<void*>(fn));
    if (r == LV_RES_OK) return true;

    ESP_LOGW(kTag, "lv_async_call failed, retry after %ums", static_cast<unsigned>(kStepMs));
    vTaskDelay(pdMS_TO_TICKS(kStepMs));
    waited += kStepMs;
  }

  ESP_LOGE(kTag, "RunOnLvglThread timeout after %ums", static_cast<unsigned>(retry_ms));
  return false;
}

static bool RunOnLvglThreadCritical(void (*fn)(void), const char* name, uint32_t total_timeout_ms = 30000) {
  constexpr uint32_t kTryMs = 3000;
  uint32_t waited = 0;
  while (waited < total_timeout_ms) {
    if (RunOnLvglThread(fn, kTryMs)) return true;
    waited += kTryMs;
    ESP_LOGW(kTag, "critical lvgl call retry: %s waited=%ums", name ? name : "unknown",
             static_cast<unsigned>(waited));
  }
  ESP_LOGE(kTag, "critical lvgl call failed: %s timeout=%ums", name ? name : "unknown",
           static_cast<unsigned>(total_timeout_ms));
  return false;
}

static void RequestShowActiveLuaApp() {
  g_active_app_shown.store(false, std::memory_order_relaxed);
  const uint32_t req = g_request_show_active_seq.fetch_add(1, std::memory_order_relaxed) + 1;
  ESP_LOGI(kTag, "request show active app seq=%u", static_cast<unsigned>(req));
}

static void ShowActiveLuaAppOnLvglThread() {
  if (!g_active_app_id[0]) return;

  char app_dir[96];
  snprintf(app_dir, sizeof(app_dir), "/littlefs/apps/%s", g_active_app_id);
  ESP_LOGI(kTag, "show active lua app -> %s", app_dir);

  LvglStopLuaAppCarousel();
  LvglShowLuaAppDirScreen(app_dir);
  g_active_app_shown.store(true, std::memory_order_relaxed);
}

static void ReloadLuaAppsOnLvglThread() { (void)g_request_show_active_seq.fetch_add(1, std::memory_order_relaxed); }

static void SwitchLuaAppOnLvglThread() {
  if (!g_pending_switch_app_id[0]) return;

  snprintf(g_active_app_id, sizeof(g_active_app_id), "%s", g_pending_switch_app_id);
  char app_dir[96];
  snprintf(app_dir, sizeof(app_dir), "/littlefs/apps/%s", g_active_app_id);
  ESP_LOGI(kTag, "switch lua app -> %s", app_dir);

  LvglStopLuaAppCarousel();
  LvglShowLuaAppDirScreen(app_dir);
}

static void RequestSwitchLuaAppOnLvglThread(const char* app_id, unsigned app_id_len) {
  if (!app_id || app_id_len == 0) return;

  const unsigned max_copy = static_cast<unsigned>(sizeof(g_pending_switch_app_id) - 1);
  const unsigned n = app_id_len < max_copy ? app_id_len : max_copy;
  memcpy(g_pending_switch_app_id, app_id, n);
  g_pending_switch_app_id[n] = '\0';
  (void)g_request_switch_app_seq.fetch_add(1, std::memory_order_relaxed);
}

static void HeapLogTask(void *arg) {
  (void)arg;
  while (true) {
    const uint32_t free_heap = esp_get_free_heap_size();
    const uint32_t min_free_heap = esp_get_minimum_free_heap_size();
    ESP_LOGI(kTag, "heap free=%u min=%u", static_cast<unsigned>(free_heap), static_cast<unsigned>(min_free_heap));
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

namespace {

static bool ReadFileToString(const char* path, std::string* out) {
  if (out) out->clear();
  if (!path || !out) return false;
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  std::string s;
  char buf[256];
  while (true) {
    const size_t n = fread(buf, 1, sizeof(buf), f);
    if (n > 0) s.append(buf, n);
    if (n < sizeof(buf)) break;
  }
  fclose(f);
  *out = std::move(s);
  return true;
}

static std::string GetTzFromLittlefsConfig() {
  std::string cfg;
  if (!ReadFileToString("/littlefs/.sys/config.json", &cfg)) return {};

  cJSON* root = cJSON_ParseWithLength(cfg.c_str(), cfg.size());
  if (!root) return {};

  auto get_str = [&](const char* key) -> const char* {
    cJSON* it = cJSON_GetObjectItem(root, key);
    if (cJSON_IsString(it) && it->valuestring && it->valuestring[0]) return it->valuestring;
    return nullptr;
  };

  const char* tz = get_str("tz");
  if (!tz) tz = get_str("timezone");
  if (!tz) tz = get_str("time_zone");

  if (!tz) {
    cJSON* sys = cJSON_GetObjectItem(root, "sys");
    if (cJSON_IsObject(sys)) {
      cJSON* t = cJSON_GetObjectItem(sys, "tz");
      if (cJSON_IsString(t) && t->valuestring && t->valuestring[0]) tz = t->valuestring;
    }
  }

  std::string out = tz ? tz : "";
  cJSON_Delete(root);
  return out;
}

static void ApplyTimezoneFromConfig() {
  std::string tz = GetTzFromLittlefsConfig();
  if (tz.empty()) {
    // Default to China Standard Time if user setting is absent.
    tz = "CST-8";
  }
  setenv("TZ", tz.c_str(), 1);
  tzset();
  ESP_LOGI(kTag, "timezone applied: %s", tz.c_str());
}

static bool SyncTimeByNtp(uint32_t timeout_ms) {
  esp_sntp_stop();
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_setservername(1, "time.cloudflare.com");
  esp_sntp_init();

  const uint32_t step_ms = 500;
  uint32_t waited = 0;
  time_t now = 0;
  struct tm tm_now = {};
  bool ok = false;

  while (waited <= timeout_ms) {
    time(&now);
    localtime_r(&now, &tm_now);
    if (tm_now.tm_year >= (2024 - 1900)) {
      ok = true;
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(step_ms));
    waited += step_ms;
  }

  if (ok) {
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_now);
    ESP_LOGI(kTag, "ntp synced: %s", ts);
  } else {
    ESP_LOGW(kTag, "ntp sync timeout (%ums)", static_cast<unsigned>(timeout_ms));
  }

  return ok;
}

static char g_boot_try_ssid[33] = {};
static char g_boot_sta_ssid[33] = {};
static char g_boot_sta_ip[16] = {};
static char g_boot_ap_ssid[33] = {};
static char g_boot_ap_ip[16] = {};

static constexpr uint32_t kBootUiProgressMs = 5000;
static void LvglBootShowConnecting() { LvglShowBootWifiConnecting(g_boot_try_ssid, kBootUiProgressMs); }
static void LvglBootShowSuccess() { LvglShowBootWifiSuccess(g_boot_sta_ssid, g_boot_sta_ip); }
static void LvglBootShowFailed() { LvglShowBootWifiFailed(g_boot_ap_ssid, g_boot_ap_ip); }

static void BootWifiUiSimulation() {
  // Simulate: 5s progress -> success for 5s -> exit boot screen.
  snprintf(g_boot_try_ssid, sizeof(g_boot_try_ssid), "%s", "MyWiFi");
  snprintf(g_boot_sta_ssid, sizeof(g_boot_sta_ssid), "%s", "MyWiFi");
  snprintf(g_boot_sta_ip, sizeof(g_boot_sta_ip), "%s", "192.168.123.234");

  (void)RunOnLvglThreadCritical(LvglBootShowConnecting, "boot_show_connecting");
  vTaskDelay(pdMS_TO_TICKS(kBootUiProgressMs));

  (void)RunOnLvglThreadCritical(LvglBootShowSuccess, "boot_show_success");
  vTaskDelay(pdMS_TO_TICKS(5000));

  (void)RunOnLvglThreadCritical(LvglStopBootWifiScreen, "boot_stop_screen");
}

static bool BootWifiFlow() {
  g_boot_try_ssid[0] = '\0';
  g_boot_sta_ssid[0] = '\0';
  g_boot_sta_ip[0] = '\0';
  g_boot_ap_ssid[0] = '\0';
  g_boot_ap_ip[0] = '\0';

  (void)WifiManagerInit();

  (void)WifiManagerGetSavedStaSsid(g_boot_try_ssid, sizeof(g_boot_try_ssid));
  if (!g_boot_try_ssid[0]) snprintf(g_boot_try_ssid, sizeof(g_boot_try_ssid), "%s", "--");

  (void)RunOnLvglThreadCritical(LvglBootShowConnecting, "boot_show_connecting");

  WifiStaInfo sta = {};
  const bool ok = WifiManagerConnectSta(20000, &sta);
  if (ok) {
    snprintf(g_boot_sta_ssid, sizeof(g_boot_sta_ssid), "%s", sta.ssid);
    snprintf(g_boot_sta_ip, sizeof(g_boot_sta_ip), "%s", sta.ip);
    (void)RunOnLvglThreadCritical(LvglBootShowSuccess, "boot_show_success");
    vTaskDelay(pdMS_TO_TICKS(1500));
    (void)RunOnLvglThreadCritical(LvglStopBootWifiScreen, "boot_stop_screen");
    return true;
  }

  WifiApInfo ap = {};
  if (WifiManagerStartSetupAp(&ap)) {
    snprintf(g_boot_ap_ssid, sizeof(g_boot_ap_ssid), "%s", ap.ssid);
    snprintf(g_boot_ap_ip, sizeof(g_boot_ap_ip), "%s", ap.ip);
  } else {
    snprintf(g_boot_ap_ssid, sizeof(g_boot_ap_ssid), "%s", "PIXEL-SETUP");
    snprintf(g_boot_ap_ip, sizeof(g_boot_ap_ip), "%s", "--");
  }

  (void)RunOnLvglThreadCritical(LvglBootShowFailed, "boot_show_failed");
  return false;
}

}  // namespace

static void LvglTask(void *arg) {
  (void)arg;
  uint32_t prev_tick = lv_tick_get();
  while (true) {
    const uint32_t switch_req = g_request_switch_app_seq.load(std::memory_order_relaxed);
    if (switch_req != g_done_switch_app_seq.load(std::memory_order_relaxed)) {
      ESP_LOGI(kTag, "lvgl consume switch request seq=%u", static_cast<unsigned>(switch_req));
      SwitchLuaAppOnLvglThread();
      g_done_switch_app_seq.store(switch_req, std::memory_order_relaxed);
    }

    const uint32_t show_req = g_request_show_active_seq.load(std::memory_order_relaxed);
    if (show_req != g_done_show_active_seq.load(std::memory_order_relaxed)) {
      ESP_LOGI(kTag, "lvgl consume show-active request seq=%u", static_cast<unsigned>(show_req));
      ShowActiveLuaAppOnLvglThread();
      g_done_show_active_seq.store(show_req, std::memory_order_relaxed);
    }

    // Consume cross-thread "next app" requests on the LVGL thread.
    LvglLuaAppCarouselPumpRequests();

    const uint32_t now_tick = lv_tick_get();
    const uint32_t dt = now_tick - prev_tick;
    prev_tick = now_tick;

    uint32_t wait_ms = lv_timer_handler();
    if (wait_ms == LV_NO_TIMER_READY) wait_ms = 20;
    if (wait_ms < 5) wait_ms = 5;
    if (wait_ms > 20) wait_ms = 20;

    // Debug: if LVGL is starved, animations degrade into "fade/flash".
    if (dt > 50) {
      ESP_LOGW(kTag, "lvgl_task jitter: dt=%lums handler_wait=%lums", (unsigned long)dt, (unsigned long)wait_ms);
    }

    vTaskDelay(pdMS_TO_TICKS(wait_ms));
  }
}

static void MountLittlefs() {
  esp_vfs_littlefs_conf_t conf = {};
  conf.base_path = "/littlefs";
  conf.partition_label = "littlefs";
  conf.format_if_mount_failed = false;
  conf.dont_mount = false;

  const esp_err_t ret = esp_vfs_littlefs_register(&conf);
  if (ret != ESP_OK) {
    ESP_LOGW(kTag, "LittleFS mount failed: %s", esp_err_to_name(ret));
    ESP_LOGW(kTag, "Hint: run `pio run -e hub75_idf -t uploadfs`");
    return;
  }

  size_t total = 0;
  size_t used = 0;
  if (esp_littlefs_info(conf.partition_label, &total, &used) == ESP_OK) {
    ESP_LOGI(kTag, "LittleFS: used=%u, total=%u", static_cast<unsigned>(used), static_cast<unsigned>(total));
  }
}

extern "C" void app_main(void) {
  ESP_LOGI(kTag, "=== Font Test Start ===");
  
  MatrixPanel_I2S_DMA display(MakePanelConfig());
  if (!display.begin()) {
    ESP_LOGE(kTag, "Display init failed!");
    while (true) vTaskDelay(pdMS_TO_TICKS(1000));
  }

  display.setBrightness8(64);
  display.fillScreenRGB888(0, 0, 32);
  auto display_mutex = xSemaphoreCreateMutex();

  MountLittlefs();

  LvglHub75Start({
      .display = &display,
      .display_mutex = display_mutex,
      .hor_res = display.width(),
      .ver_res = display.height(),
  });

  // 启动按键任务：当前逻辑为单击重启（见 app/user_button.cpp）
  StartUserButtonTask(display, display_mutex, {
    .request_aapl_ticker = nullptr,
    .request_raw_benchmark = nullptr,
  });
  
  ESP_LOGI(kTag, "=== Preloading fonts ===");
  LvglBootWifiScreenPreloadFont();
  
  ESP_LOGI(kTag, "=== Boot Complete ===");

  xTaskCreatePinnedToCore(LvglTask, "lvgl", 8192, nullptr, 5, &g_lvgl_task_handle, 1);

  // Boot WiFi flow:
  // - Try saved STA config (20s)
  // - On failure, start setup AP and keep the setup screen
  static constexpr bool kDebugSkipWifiFlow = false;
  static constexpr bool kDebugSimulateWifiBootUi = false;
  bool wifi_ok = true;
  if (kDebugSimulateWifiBootUi) {
    BootWifiUiSimulation();
    wifi_ok = true;
  } else {
    wifi_ok = kDebugSkipWifiFlow ? true : BootWifiFlow();
  }

  // Startup app is fixed; runtime switching is handled via HTTP API.
  // POST /api/apps/switch/<app_id> or POST /api/apps/switch?app_id=<app_id>
  if (!wifi_ok) {
    // Keep setup screen.
  } else {
    // Don't block startup on app-show handshake; request once and let LVGL task consume it.
    RequestShowActiveLuaApp();
    ApplyTimezoneFromConfig();
    (void)SyncTimeByNtp(15000);
  }

  if (AppUpdateServerStart(ReloadLuaAppsOnLvglThread, RequestSwitchLuaAppOnLvglThread)) {
    ESP_LOGI(kTag, "App update API: PUT /api/apps/<app_id>/<file>, DELETE /api/apps/<app_id>[/*], POST /api/apps/reload, POST /api/apps/switch/<app_id> or ?app_id=");
  } else {
    ESP_LOGW(kTag, "App update API start failed");
  }

  xTaskCreatePinnedToCore(HeapLogTask, "heap_log", 3072, nullptr, 1, nullptr, 1);

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
