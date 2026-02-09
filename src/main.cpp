#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_littlefs.h"
#include "esp_system.h"

extern "C" {
#include "lvgl.h"
}

#include "ESP32-HUB75-MatrixPanel-I2S-DMA.h"

#include "app/hub75_config.h"
#include "app/user_button.h"
#include "app/wifi_manager.h"
#include "ui/lvgl_boot_wifi_screen.h"
#include "ui/lvgl_hub75_port.h"
#include "ui/lvgl_screen_carousel.h"
#include "ui/lvgl_stock_screen.h"
#include "ui/lvgl_stock_screen2.h"
#include "ui/lvgl_stock_screen3.h"
#include "ui/lvgl_stock_screen4.h"
#include "ui/lvgl_stock_screen5.h"
#include "ui/lvgl_stock_screen6.h"
#include "ui/lvgl_stock_screen7.h"

static const char *kTag = "app";

static TaskHandle_t g_lvgl_task_handle = nullptr;

static void RunOnLvglThread(void (*fn)(void)) {
  // lv_async_call 需要一个 void(*)(void*) 形式的回调
  lv_async_call(
      [](void *p) {
        auto f = reinterpret_cast<void (*)(void)>(p);
        if (f) f();
      },
      reinterpret_cast<void *>(fn));
}

namespace {

static char g_boot_try_ssid[33] = {};
static char g_boot_sta_ssid[33] = {};
static char g_boot_sta_ip[16] = {};
static char g_boot_ap_ssid[33] = {};
static char g_boot_ap_ip[16] = {};

static void LvglBootShowConnecting() { LvglShowBootWifiConnecting(g_boot_try_ssid, 20000); }
static void LvglBootShowSuccess() { LvglShowBootWifiSuccess(g_boot_sta_ssid, g_boot_sta_ip); }
static void LvglBootShowFailed() { LvglShowBootWifiFailed(g_boot_ap_ssid, g_boot_ap_ip); }

static void BootWifiUiSimulation() {
  // Simulate: 20s progress -> success for 5s -> exit boot screen.
  snprintf(g_boot_try_ssid, sizeof(g_boot_try_ssid), "%s", "MyWiFi");
  snprintf(g_boot_sta_ssid, sizeof(g_boot_sta_ssid), "%s", "MyWiFi");
  snprintf(g_boot_sta_ip, sizeof(g_boot_sta_ip), "%s", "192.168.123.234");

  RunOnLvglThread(LvglBootShowConnecting);
  vTaskDelay(pdMS_TO_TICKS(20000));

  RunOnLvglThread(LvglBootShowSuccess);
  vTaskDelay(pdMS_TO_TICKS(5000));

  RunOnLvglThread(LvglStopBootWifiScreen);
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

  RunOnLvglThread(LvglBootShowConnecting);

  WifiStaInfo sta = {};
  const bool ok = WifiManagerConnectSta(20000, &sta);
  if (ok) {
    snprintf(g_boot_sta_ssid, sizeof(g_boot_sta_ssid), "%s", sta.ssid);
    snprintf(g_boot_sta_ip, sizeof(g_boot_sta_ip), "%s", sta.ip);
    RunOnLvglThread(LvglBootShowSuccess);
    vTaskDelay(pdMS_TO_TICKS(1500));
    RunOnLvglThread(LvglStopBootWifiScreen);
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

  RunOnLvglThread(LvglBootShowFailed);
  return false;
}

}  // namespace

static void LvglTask(void *arg) {
  (void)arg;
  uint32_t prev_tick = lv_tick_get();
  while (true) {
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
  
  // 预加载所有字体（在 LVGL 任务启动前，避免后续切换时阻塞）
  LvglStockScreenPreloadFont();
  LvglStockScreen2PreloadFont();
  LvglStockScreen3PreloadFont();
  LvglStockScreen4PreloadFont();
  LvglStockScreen5PreloadFont();
  LvglStockScreen6PreloadFont();
  LvglStockScreen7PreloadFont();
  LvglBootWifiScreenPreloadFont();
  
  ESP_LOGI(kTag, "=== Boot Complete ===");

  xTaskCreatePinnedToCore(LvglTask, "lvgl", 8192, nullptr, 5, &g_lvgl_task_handle, 1);

  // Boot WiFi flow:
  // - Try saved STA config (20s)
  // - On failure, start setup AP and keep the setup screen
  static constexpr bool kDebugSkipWifiFlow = false;
  static constexpr bool kDebugSimulateWifiBootUi = true;
  bool wifi_ok = true;
  if (kDebugSimulateWifiBootUi) {
    BootWifiUiSimulation();
    wifi_ok = true;
  } else {
    wifi_ok = kDebugSkipWifiFlow ? true : BootWifiFlow();
  }

  // Screen cycling: screens, 15s each.
  // Tip: set kDebugSingleScreen=true to lock to one screen while tuning UI.
  static constexpr bool kDebugSingleScreen = true;
  //static constexpr bool kDebugSingleScreen = false;
  if (!wifi_ok) {
    // Keep setup screen.
  } else if (kDebugSingleScreen) {
    RunOnLvglThread(LvglShowStockScreen7);
  } else {
    RunOnLvglThread([]() { LvglStartScreenCarousel(15000); });
  }

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
