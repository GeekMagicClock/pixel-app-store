#include "ui/lvgl_hub75_port.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

// For taskYIELD() inside flush_cb to avoid starving IDLE task on heavy redraws.
#include "freertos/task.h"

extern "C" {
#include "lvgl.h"
#include "benchmark/lv_demo_benchmark.h"
}

// FS driver init APIs are included via `lvgl.h` when enabled.

#include "ESP32-HUB75-MatrixPanel-I2S-DMA.h"

static const char *kTag = "lvgl";

namespace {

struct DriverContext {
  MatrixPanel_I2S_DMA *display;
  SemaphoreHandle_t display_mutex;
};

void FlushCb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  auto *ctx = static_cast<DriverContext *>(lv_display_get_user_data(disp));
  if (!ctx || !ctx->display) {
    lv_display_flush_ready(disp);
    return;
  }

  const int x1 = area->x1 < 0 ? 0 : area->x1;
  const int y1 = area->y1 < 0 ? 0 : area->y1;
  const int x2 = area->x2;
  const int y2 = area->y2;

  if (x1 > x2 || y1 > y2) {
    lv_display_flush_ready(disp);
    return;
  }

  if (ctx->display_mutex) {
    xSemaphoreTake(ctx->display_mutex, portMAX_DELAY);
  }

  auto *color_p = reinterpret_cast<uint16_t *>(px_map);
  int idx = 0;
  for (int y = y1; y <= y2; y++) {
    for (int x = x1; x <= x2; x++) {
      const uint16_t c565 = color_p[idx++];
      ctx->display->drawPixel(x, y, c565);
    }
    // Break up long flushes so CPU0 IDLE can run (prevents task watchdog warnings).
    if (((y - y1) & 0x03) == 0) {
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }

  if (ctx->display_mutex) {
    xSemaphoreGive(ctx->display_mutex);
  }

  lv_display_flush_ready(disp);
}

void TickTimerCb(void *arg) {
  const uint32_t period_ms = reinterpret_cast<uintptr_t>(arg);
  lv_tick_inc(period_ms);
}

} // namespace

void LvglHub75Start(const LvglHub75PortConfig &cfg) {
  lv_init();

#if LV_USE_FS_STDIO
  lv_fs_stdio_init();
  ESP_LOGI(kTag, "LVGL stdio FS registered (letter=%c)", static_cast<char>(LV_FS_STDIO_LETTER));
#endif

  static DriverContext ctx;
  ctx.display = cfg.display;
  ctx.display_mutex = cfg.display_mutex;

  static void *buf1 = nullptr;
  static void *buf2 = nullptr;

  // Partial buffer: a few lines to reduce RAM.
  const int buf_lines = 16;
  const size_t buf_pixels = static_cast<size_t>(cfg.hor_res) * static_cast<size_t>(buf_lines);

  lv_display_t *disp = lv_display_create(cfg.hor_res, cfg.ver_res);
  if (!disp) {
    ESP_LOGE(kTag, "lv_display_create failed");
    abort();
  }

  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);

  const size_t bytes_per_pixel = LV_COLOR_FORMAT_GET_SIZE(lv_display_get_color_format(disp));
  const size_t buf_bytes = buf_pixels * bytes_per_pixel;

  buf1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  buf2 = heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!buf1 || !buf2) {
    ESP_LOGE(kTag, "Failed to allocate LVGL draw buffers");
    abort();
  }

  lv_display_set_user_data(disp, &ctx);
  lv_display_set_flush_cb(disp, FlushCb);
  lv_display_set_buffers(disp, buf1, buf2, static_cast<uint32_t>(buf_bytes),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

  // LVGL tick (5ms).
  constexpr uint32_t kTickMs = 5;
  esp_timer_handle_t tick_timer;
  const esp_timer_create_args_t tick_args = {
      .callback = &TickTimerCb,
      .arg = reinterpret_cast<void *>(static_cast<uintptr_t>(kTickMs)),
      .dispatch_method = ESP_TIMER_TASK,
      .name = "lv_tick",
      .skip_unhandled_events = true,
  };
  ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
  ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, kTickMs * 1000));
  ESP_LOGI(kTag, "LVGL initialized (%dx%d)", cfg.hor_res, cfg.ver_res);
}

void LvglRunBenchmarkDemo() {
#if LV_USE_DEMO_BENCHMARK
  lv_demo_benchmark();
#else
  ESP_LOGW(kTag, "LVGL benchmark demo disabled (set CONFIG_LV_USE_DEMO_BENCHMARK=y)");
#endif
}
