#include "ui/lvgl_screen_carousel.h"

#include "esp_log.h"

extern "C" {
#include "lvgl.h"
}

#include "ui/lvgl_stock_screen.h"
#include "ui/lvgl_stock_screen2.h"
#include "ui/lvgl_stock_screen3.h"
#include "ui/lvgl_stock_screen4.h"
#include "ui/lvgl_stock_screen5.h"
#include "ui/lvgl_stock_screen6.h"
#include "ui/lvgl_stock_screen7.h"

static const char *kTag = "screen_carousel";

namespace {

static lv_timer_t *g_timer = nullptr;
static int g_index = 0;
static unsigned g_interval_ms = 15000;

static void StopCurrentScreen(int idx) {
  switch (idx) {
    case 0: LvglStopStockScreen(); break;
    case 1: LvglStopStockScreen2(); break;
    case 2: LvglStopStockScreen3(); break;
    case 3: LvglStopStockScreen4(); break;
    case 4: LvglStopStockScreen5(); break;
    case 5: LvglStopStockScreen6(); break;
    case 6: LvglStopStockScreen7(); break;
    default: break;
  }
}

static void ShowScreen(int idx) {
  switch (idx) {
    case 0: LvglShowStockScreen(); break;
    case 1: LvglShowStockScreen2(); break;
    case 2: LvglShowStockScreen3(); break;
    case 3: LvglShowStockScreen4(); break;
    case 4: LvglShowStockScreen5(); break;
    case 5: LvglShowStockScreen6(); break;
    case 6: LvglShowStockScreen7(); break;
    default: LvglShowStockScreen(); break;
  }
}

static void TimerCb(lv_timer_t *t) {
  (void)t;

  const int prev = g_index;
  g_index = (g_index + 1) % 7;

  ESP_LOGI(kTag, "Switch screen %d -> %d", prev + 1, g_index + 1);

  // Stop previous screen to kill its timers and free canvas buffers.
  StopCurrentScreen(prev);

  // Show next screen.
  ShowScreen(g_index);
}

}  // namespace

void LvglStartScreenCarousel(unsigned interval_ms) {
  if (interval_ms < 1000) interval_ms = 1000;
  g_interval_ms = interval_ms;

  // Restart if already running.
  if (g_timer) {
    lv_timer_del(g_timer);
    g_timer = nullptr;
  }

  // Start from screen1 each time for determinism.
  g_index = 0;
  ESP_LOGI(kTag, "Start carousel interval=%ums", g_interval_ms);
  ShowScreen(g_index);

  g_timer = lv_timer_create(TimerCb, g_interval_ms, nullptr);
}

void LvglStopScreenCarousel() {
  if (g_timer) {
    lv_timer_del(g_timer);
    g_timer = nullptr;
    ESP_LOGI(kTag, "Carousel stopped");
  }
}

void LvglScreenCarouselNext() {
  const int prev = g_index;
  g_index = (g_index + 1) % 7;
  ESP_LOGI(kTag, "Manual next screen %d -> %d", prev + 1, g_index + 1);
  StopCurrentScreen(prev);
  ShowScreen(g_index);
  if (g_timer) lv_timer_reset(g_timer);
}

void LvglRequestScreenCarouselNext() {
  lv_async_call(
      [](void *p) {
        (void)p;
        LvglScreenCarouselNext();
      },
      nullptr);
}
