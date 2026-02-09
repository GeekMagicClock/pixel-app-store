#include "app/raw_bench.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "ESP32-HUB75-MatrixPanel-I2S-DMA.h"

static const char *kTag = "app";

struct BenchResult {
  const char *name;
  int iterations;
  int64_t total_us;
};

static BenchResult BenchFillScreenRGB888(MatrixPanel_I2S_DMA &display, int iterations) {
  const int64_t start = esp_timer_get_time();
  for (int i = 0; i < iterations; i++) {
    display.fillScreenRGB888(i & 1 ? 255 : 0, i & 2 ? 255 : 0, i & 4 ? 255 : 0);
  }
  const int64_t end = esp_timer_get_time();
  return {.name = "fillScreenRGB888", .iterations = iterations, .total_us = end - start};
}

static BenchResult BenchFillRects(MatrixPanel_I2S_DMA &display, int iterations) {
  const int w = display.width();
  const int h = display.height();
  const int w3 = w / 3;

  const uint16_t black = MatrixPanel_I2S_DMA::color565(0, 0, 0);
  const uint16_t red = MatrixPanel_I2S_DMA::color565(255, 0, 0);
  const uint16_t green = MatrixPanel_I2S_DMA::color565(0, 255, 0);
  const uint16_t blue = MatrixPanel_I2S_DMA::color565(0, 0, 255);

  const int64_t start = esp_timer_get_time();
  for (int i = 0; i < iterations; i++) {
    display.fillScreen(black);
    display.fillRect(0, 0, w3, h, red);
    display.fillRect(w3, 0, w3, h, green);
    display.fillRect(w3 * 2, 0, w - (w3 * 2), h, blue);
  }
  const int64_t end = esp_timer_get_time();
  return {.name = "fillRect(3 bars)", .iterations = iterations, .total_us = end - start};
}

static BenchResult BenchDrawPixelsRGB888(MatrixPanel_I2S_DMA &display, int iterations) {
  const int w = display.width();
  const int h = display.height();
  const int64_t start = esp_timer_get_time();

  for (int iter = 0; iter < iterations; iter++) {
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        const uint8_t r = (x * 255) / (w - 1);
        const uint8_t g = (y * 255) / (h - 1);
        const uint8_t b = ((x + y + iter) & 1) ? 255 : 0;
        display.drawPixelRGB888(x, y, r, g, b);
      }
    }
  }

  const int64_t end = esp_timer_get_time();
  return {.name = "drawPixelRGB888(full frame)", .iterations = iterations, .total_us = end - start};
}

static void LogBench(const BenchResult &result, int pixels_per_iter) {
  const double us_per_iter = static_cast<double>(result.total_us) / result.iterations;
  const double fps = 1e6 / us_per_iter;
  if (pixels_per_iter > 0) {
    const double ns_per_pixel = (static_cast<double>(result.total_us) * 1000.0) /
                                (static_cast<double>(result.iterations) * pixels_per_iter);
    ESP_LOGI(kTag, "%s: %.1f us/iter (%.1f FPS), %.1f ns/pixel", result.name, us_per_iter, fps,
             ns_per_pixel);
  } else {
    ESP_LOGI(kTag, "%s: %.1f us/iter (%.1f FPS)", result.name, us_per_iter, fps);
  }
}

void RunRawHub75Bench(MatrixPanel_I2S_DMA &display) {
  const auto &cfg = display.getCfg();
  ESP_LOGI(kTag,
           "HUB75 cfg: %dx%d chain=%d depth=%u latch_blank=%u min_refresh=%u i2s=%uHz calc_refresh=%dHz",
           cfg.mx_width, cfg.mx_height, cfg.chain_length, cfg.getPixelColorDepthBits(),
           cfg.latch_blanking, cfg.min_refresh_rate, static_cast<unsigned>(cfg.i2sspeed),
           display.calculated_refresh_rate);

  const int w = display.width();
  const int h = display.height();
  const int pixels = w * h;

  const int n_fast = 400;
  const int n_fullframe = 40;

  LogBench(BenchFillScreenRGB888(display, n_fast), /*pixels_per_iter=*/pixels);
  LogBench(BenchFillRects(display, n_fast), /*pixels_per_iter=*/pixels);
  LogBench(BenchDrawPixelsRGB888(display, n_fullframe), /*pixels_per_iter=*/pixels);

  ESP_LOGI(kTag,
           "Tip: try build_flags like -DHUB75_I2S_SPEED=HUB75_I2S_CFG::HZ_20M "
           "-DHUB75_LATCH_BLANKING=1 -DHUB75_MIN_REFRESH_RATE=120, then re-run this bench.");
}

