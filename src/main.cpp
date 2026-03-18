#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <ctime>
#include <cstring>
#include <string>
#include <atomic>
#include <vector>

#include "esp_log.h"
#include "esp_littlefs.h"
#include "esp_private/esp_clk.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_sntp.h"
#include "soc/rtc.h"

extern "C" {
#include "lvgl.h"
}

#include "cJSON.h"

#include "ESP32-HUB75-MatrixPanel-I2S-DMA.h"

#include "app/hub75_config.h"
#include "app/app_update_server.h"
#include "app/display_control.h"
#include "app/user_button.h"
#include "app/wifi_manager.h"
#include "ui/lvgl_boot_wifi_screen.h"
#include "ui/lvgl_hub75_port.h"
#include "ui/lvgl_lua_app_carousel.h"
#include "ui/lvgl_lua_app_screen.h"
#include "ui/lvgl_smoke_test.h"

static const char *kTag = "app";
static constexpr bool kEnableHeapLogTask = false;
static constexpr bool kDebugBootLvglOnly = false;
static constexpr bool kDebugDisableLuaStartup = false;
static constexpr bool kDebugDisableOtaServer = false;
static constexpr const char* kPreferredStartupAppId = "openmeteo_3day";

static TaskHandle_t g_lvgl_task_handle = nullptr;
static std::atomic_bool g_lvgl_ready{false};
static char g_pending_switch_app_id[49] = {};
static char g_active_app_id[49] = {};
static std::atomic_bool g_active_app_shown{false};
static std::atomic_uint32_t g_request_show_active_seq{0};
static std::atomic_uint32_t g_done_show_active_seq{0};
static std::atomic_uint32_t g_request_switch_app_seq{0};
static std::atomic_uint32_t g_done_switch_app_seq{0};
static std::atomic_bool g_boot_flow_done{false};
static void ShowActiveLuaAppOnLvglThread();

static bool RunOnLvglThread(void (*fn)(void), uint32_t retry_ms = 2000) {
  if (!fn) return false;

  if (!g_lvgl_ready.load(std::memory_order_acquire) || !g_lvgl_task_handle) {
    ESP_LOGW(kTag, "RunOnLvglThread ignored: LVGL not ready yet");
    return false;
  }

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

static bool IsDir(const std::string& path) {
  struct stat st = {};
  if (stat(path.c_str(), &st) != 0) return false;
  return S_ISDIR(st.st_mode);
}

static bool IsFile(const std::string& path) {
  struct stat st = {};
  if (stat(path.c_str(), &st) != 0) return false;
  return S_ISREG(st.st_mode);
}

static bool SelectStartupLuaAppId(char* out_app_id, size_t out_app_id_sz) {
  if (!out_app_id || out_app_id_sz == 0) return false;
  out_app_id[0] = '\0';

  std::vector<std::string> app_ids;
  const char* root = "/littlefs/apps";
  DIR* d = opendir(root);
  if (!d) {
    ESP_LOGW(kTag, "select startup app: opendir failed: %s", root);
    return false;
  }

  while (true) {
    struct dirent* e = readdir(d);
    if (!e) break;
    const char* name = e->d_name;
    if (!name || !*name || name[0] == '.') continue;

    const std::string app_id = name;
    const std::string app_dir = std::string(root) + "/" + app_id;
    if (!IsDir(app_dir)) continue;
    if (!IsFile(app_dir + "/main.lua") && !IsFile(app_dir + "/manifest.json")) continue;
    app_ids.push_back(app_id);
  }

  closedir(d);

  if (app_ids.empty()) {
    ESP_LOGW(kTag, "select startup app: no apps found under %s", root);
    return false;
  }

  std::sort(app_ids.begin(), app_ids.end());
  auto preferred = std::find(app_ids.begin(), app_ids.end(), kPreferredStartupAppId);
  if (preferred != app_ids.end()) {
    snprintf(out_app_id, out_app_id_sz, "%s", preferred->c_str());
    ESP_LOGI(kTag, "select startup app: %s (preferred)", out_app_id);
    return true;
  }

  snprintf(out_app_id, out_app_id_sz, "%s", app_ids.front().c_str());
  ESP_LOGI(kTag, "select startup app: %s (fallback first app, preferred=%s missing)", out_app_id,
           kPreferredStartupAppId);
  return true;
}

static void LogInstalledLuaApps() {
  const char* root = "/littlefs/apps";
  DIR* d = opendir(root);
  if (!d) {
    ESP_LOGW(kTag, "installed apps: opendir failed: %s", root);
    return;
  }

  std::vector<std::string> app_ids;
  while (true) {
    struct dirent* e = readdir(d);
    if (!e) break;
    const char* name = e->d_name;
    if (!name || !*name || name[0] == '.') continue;

    const std::string app_id = name;
    const std::string app_dir = std::string(root) + "/" + app_id;
    if (!IsDir(app_dir)) continue;
    if (!IsFile(app_dir + "/main.lua") && !IsFile(app_dir + "/manifest.json")) continue;
    app_ids.push_back(app_id);
  }
  closedir(d);

  std::sort(app_ids.begin(), app_ids.end());
  ESP_LOGI(kTag, "installed apps count=%u", static_cast<unsigned>(app_ids.size()));
  for (size_t i = 0; i < app_ids.size(); ++i) {
    ESP_LOGI(kTag, "installed app[%u]=%s", static_cast<unsigned>(i), app_ids[i].c_str());
  }
}

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

  WifiStaInfo sta = {};
  const bool ok = WifiManagerConnectSta(20000, &sta);
  if (ok) {
    snprintf(g_boot_sta_ssid, sizeof(g_boot_sta_ssid), "%s", sta.ssid);
    snprintf(g_boot_sta_ip, sizeof(g_boot_sta_ip), "%s", sta.ip);
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

  return false;
}

static void BootFlowTask(void* arg) {
  (void)arg;
  ESP_LOGI(kTag, "boot_flow: begin");

  if (kDebugBootLvglOnly) {
    ESP_LOGW(kTag, "boot_flow: debug LVGL-only mode, skip Wi-Fi/Lua/OTA");
    (void)RunOnLvglThreadCritical(LvglRunSmokeTest, "lvgl_smoke_test");
    g_boot_flow_done.store(true, std::memory_order_release);
    ESP_LOGI(kTag, "boot_flow: done (lvgl-only)");
    vTaskDelete(nullptr);
    return;
  }

  // Boot WiFi flow:
  // - Try saved STA config (20s)
  // - On failure, start setup AP and keep the setup screen
  static constexpr bool kDebugSkipWifiFlow = false;
  static constexpr bool kDebugSimulateWifiBootUi = false;
  bool wifi_ok = true;

  if (kDebugSimulateWifiBootUi) {
    ESP_LOGI(kTag, "boot_flow: simulate ui");
    BootWifiUiSimulation();
    wifi_ok = true;
  } else {
    if (!kDebugSkipWifiFlow) {
      ESP_LOGI(kTag, "boot_flow: show connecting");
      (void)RunOnLvglThreadCritical(LvglBootShowConnecting, "boot_show_connecting_initial");
    }
    ESP_LOGI(kTag, "boot_flow: before wifi flow");
    wifi_ok = kDebugSkipWifiFlow ? true : BootWifiFlow();
    ESP_LOGI(kTag, "boot_flow: after wifi flow ok=%d", wifi_ok ? 1 : 0);
    if (wifi_ok) {
      ESP_LOGI(kTag, "boot_flow: show success");
      (void)RunOnLvglThreadCritical(LvglBootShowSuccess, "boot_show_success");
    } else {
      ESP_LOGI(kTag, "boot_flow: show failed");
      (void)RunOnLvglThreadCritical(LvglBootShowFailed, "boot_show_failed");
    }
  }

  if (wifi_ok) {
    if (kDebugDisableLuaStartup) {
      ESP_LOGW(kTag, "boot_flow: Lua startup disabled for Wi-Fi-only isolation");
    } else {
      ESP_LOGI(kTag, "boot_flow: request active app");
      RequestShowActiveLuaApp();
      ApplyTimezoneFromConfig();
      (void)SyncTimeByNtp(15000);
    }
  }

  if (kDebugDisableOtaServer) {
    ESP_LOGW(kTag, "boot_flow: OTA server disabled for Wi-Fi-only isolation");
  } else if (AppUpdateServerStart(ReloadLuaAppsOnLvglThread, RequestSwitchLuaAppOnLvglThread)) {
    ESP_LOGI(kTag, "HTTP API: GET /f.html, GET /api/system/status, GET /api/system/wifi/scan, POST /api/system/wifi/config, POST /api/system/display/brightness, GET /api/firmware, POST /api/firmware/ota, POST /api/system/reboot, GET /api/apps/list, PUT /api/apps/<app_id>/<file>, DELETE /api/apps/<app_id>[/*], POST /api/apps/reload, POST /api/apps/switch/<app_id> or ?app_id=");
  } else {
    ESP_LOGW(kTag, "App update API start failed");
  }

  g_boot_flow_done.store(true, std::memory_order_release);
  ESP_LOGI(kTag, "boot_flow: done");
  vTaskDelete(nullptr);
}

}  // namespace

static void LvglTask(void *arg) {
  (void)arg;
  uint32_t prev_tick = lv_tick_get();
  ESP_LOGI(kTag, "lvgl task started");
  while (true) {
    const uint32_t show_req = g_request_show_active_seq.load(std::memory_order_relaxed);
    const uint32_t show_done = g_done_show_active_seq.load(std::memory_order_relaxed);
    if (show_req != show_done) {
      ShowActiveLuaAppOnLvglThread();
      g_done_show_active_seq.store(show_req, std::memory_order_relaxed);
    }

    const uint32_t switch_req = g_request_switch_app_seq.load(std::memory_order_relaxed);
    const uint32_t switch_done = g_done_switch_app_seq.load(std::memory_order_relaxed);
    if (switch_req != switch_done) {
      SwitchLuaAppOnLvglThread();
      g_done_switch_app_seq.store(switch_req, std::memory_order_relaxed);
      g_active_app_shown.store(true, std::memory_order_relaxed);
    }

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
    ESP_LOGW(kTag, "Hint: run `pio run -e esp32-s3-n16r8 -t uploadfs`");
    return;
  }

  size_t total = 0;
  size_t used = 0;
  if (esp_littlefs_info(conf.partition_label, &total, &used) == ESP_OK) {
    ESP_LOGI(kTag, "LittleFS: used=%u, total=%u", static_cast<unsigned>(used), static_cast<unsigned>(total));
  }

  LogInstalledLuaApps();
  if (!SelectStartupLuaAppId(g_active_app_id, sizeof(g_active_app_id))) {
    ESP_LOGW(kTag, "no startup Lua app selected");
  }
}

extern "C" void app_main(void) {
  ESP_LOGI(kTag, "=== Font Test Start ===");

  // The generated board config is still booting at 160MHz, so force the CPU
  // to the S3's nominal 240MHz here for a direct runtime validation.
  rtc_cpu_freq_config_t cpu_config;
  if (rtc_clk_cpu_freq_mhz_to_config(240, &cpu_config)) {
    rtc_clk_cpu_freq_set_config_fast(&cpu_config);
  } else {
    ESP_LOGW(kTag, "failed to build 240MHz cpu config; keeping default clock");
  }
  ESP_LOGI(kTag, "cpu freq forced to %lu Hz", (unsigned long)esp_clk_cpu_freq());
  
  MatrixPanel_I2S_DMA display(MakePanelConfig());
  if (!display.begin()) {
    ESP_LOGE(kTag, "Display init failed!");
    while (true) vTaskDelay(pdMS_TO_TICKS(1000));
  }

  auto display_mutex = xSemaphoreCreateMutex();
  // Keep startup current low; a bright white splash can brown out the board
  // right when Wi-Fi/PSRAM are also ramping up.
  DisplayControlBind(&display, display_mutex, 64);
  display.fillScreenRGB888(0, 0, 0);

  LvglHub75Start({
      .display = &display,
      .display_mutex = display_mutex,
      .hor_res = display.width(),
      .ver_res = display.height(),
  });

  // 启动按键任务：短按切换 app，长按重启（见 app/user_button.cpp）
  StartUserButtonTask(display, display_mutex, {
    .request_aapl_ticker = nullptr,
    .request_raw_benchmark = nullptr,
  });

  ESP_LOGI(kTag, "=== Preloading fonts ===");
  LvglBootWifiScreenPreloadFont();

  ESP_LOGI(kTag, "=== Boot Complete ===");

  xTaskCreatePinnedToCore(LvglTask, "lvgl", 8192, nullptr, 5, &g_lvgl_task_handle, 0);
  g_lvgl_ready.store(true, std::memory_order_release);

  MountLittlefs();
  xTaskCreatePinnedToCore(BootFlowTask, "boot_flow", 6144, nullptr, 4, nullptr, 1);

  if (kEnableHeapLogTask) {
  xTaskCreatePinnedToCore(HeapLogTask, "heap_log", 3072, nullptr, 1, nullptr, 0);
  }

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
