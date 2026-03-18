#include "ui/lvgl_hub75_port.h"

#include "ui/lvgl_mem_utils.h"

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

static bool g_flush_enabled = true;

struct DriverContext {
  MatrixPanel_I2S_DMA *display;
  SemaphoreHandle_t display_mutex;
};

void FlushCb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  if (!g_flush_enabled) {
    lv_display_flush_ready(disp);
    return;
  }

  auto *ctx = static_cast<DriverContext *>(lv_display_get_user_data(disp));
  if (!ctx || !ctx->display) {
    lv_display_flush_ready(disp);
    return;
  }

  const int32_t hor_res = lv_display_get_horizontal_resolution(disp);
  const int32_t ver_res = lv_display_get_vertical_resolution(disp);

  // Clip to display bounds, but keep correct indexing into px_map by using the
  // original area's width as the source stride.
  const int32_t clip_x1 = (area->x1 < 0) ? 0 : area->x1;
  const int32_t clip_y1 = (area->y1 < 0) ? 0 : area->y1;
  const int32_t clip_x2 = (area->x2 >= hor_res) ? (hor_res - 1) : area->x2;
  const int32_t clip_y2 = (area->y2 >= ver_res) ? (ver_res - 1) : area->y2;

  if (clip_x1 > clip_x2 || clip_y1 > clip_y2) {
    lv_display_flush_ready(disp);
    return;
  }

  if (ctx->display_mutex) {
    xSemaphoreTake(ctx->display_mutex, portMAX_DELAY);
  }

  auto *color_p = reinterpret_cast<uint16_t *>(px_map);
  const int32_t src_w = area->x2 - area->x1 + 1;
  for (int32_t y = clip_y1; y <= clip_y2; y++) {
    int32_t src_idx = (y - area->y1) * src_w + (clip_x1 - area->x1);
    for (int32_t x = clip_x1; x <= clip_x2; x++) {
      const uint16_t c565 = color_p[src_idx++];
      ctx->display->drawPixel(static_cast<int16_t>(x), static_cast<int16_t>(y), c565);
    }
    // `taskYIELD()` only switches to equal-priority tasks, which does not help
    // the IDLE task monitored by the watchdog. Give back one scheduler tick
    // periodically during large HUB75 flushes.
    if (((y - clip_y1) & 0x07) == 0x07) {
      vTaskDelay(1);
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

  // ESP32-S3 HUB75 uses LCD_CAM + GDMA. Keeping LVGL draw buffers in PSRAM can
  // stall the first `lv_timer_handler()` and trip the task watchdog, so require
  // internal DMA-capable RAM here. Higher-level, non-DMA UI allocations can
  // still prefer PSRAM elsewhere.
  buf1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  buf2 = heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (!buf1 || !buf2) {
    ESP_LOGE(kTag, "Failed to allocate LVGL draw buffers");
    abort();
  }

  ESP_LOGI(kTag, "LVGL draw buffers in internal DMA RAM (%u bytes each)",
           static_cast<unsigned>(buf_bytes));

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

void LvglHub75SetFlushEnabled(bool enabled) {
  g_flush_enabled = enabled;
  ESP_LOGI(kTag, "flush gate: %s", enabled ? "enabled" : "disabled");
}

void LvglRunBenchmarkDemo() {
#if LV_USE_DEMO_BENCHMARK
  lv_demo_benchmark();
#else
  ESP_LOGW(kTag, "LVGL benchmark demo disabled (set CONFIG_LV_USE_DEMO_BENCHMARK=y)");
#endif
}
