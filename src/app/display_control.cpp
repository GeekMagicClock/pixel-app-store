#include "app/display_control.h"

#include <atomic>

#include "ESP32-HUB75-MatrixPanel-I2S-DMA.h"
#include "esp_log.h"

namespace {

static const char* kTag = "display_ctl";
static MatrixPanel_I2S_DMA* g_display = nullptr;
static SemaphoreHandle_t g_display_mutex = nullptr;
static std::atomic_uint32_t g_brightness{64};

}  // namespace

void DisplayControlBind(MatrixPanel_I2S_DMA* display, SemaphoreHandle_t display_mutex, uint8_t initial_brightness) {
  g_display = display;
  g_display_mutex = display_mutex;
  g_brightness.store(initial_brightness, std::memory_order_release);
  (void)DisplayControlSetBrightness(initial_brightness);
}

bool DisplayControlSetBrightness(uint8_t brightness) {
  g_brightness.store(brightness, std::memory_order_release);
  if (!g_display) return false;

  bool locked = false;
  if (g_display_mutex) {
    locked = xSemaphoreTake(g_display_mutex, pdMS_TO_TICKS(50)) == pdTRUE;
    if (!locked) {
      ESP_LOGW(kTag, "set brightness without display mutex");
    }
  }

  g_display->setBrightness8(brightness);

  if (locked) {
    xSemaphoreGive(g_display_mutex);
  }
  return true;
}

uint8_t DisplayControlGetBrightness() {
  return static_cast<uint8_t>(g_brightness.load(std::memory_order_acquire));
}

bool DisplayControlIsReady() {
  return g_display != nullptr;
}

bool DisplayControlShowSleepCountdown(int seconds_left) {
  if (!g_display) return false;
  if (seconds_left < 0) seconds_left = 0;

  bool locked = false;
  if (g_display_mutex) {
    locked = xSemaphoreTake(g_display_mutex, pdMS_TO_TICKS(50)) == pdTRUE;
    if (!locked) {
      ESP_LOGW(kTag, "draw countdown without display mutex");
    }
  }

  const int total = 10;
  int clamped = seconds_left;
  if (clamped > total) clamped = total;
  const int filled = (total - clamped) * 64 / total;

  g_display->clearScreen();
  g_display->fillRect(0, 0, 64, 4, g_display->color565(30, 30, 30));
  if (filled > 0) {
    g_display->fillRect(0, 0, filled, 4, g_display->color565(255, 200, 80));
  }
  g_display->fillRect(0, 12, 64, 1, g_display->color565(80, 80, 80));
  g_display->fillRect(0, 20, 64, 1, g_display->color565(80, 80, 80));

  if (locked) {
    xSemaphoreGive(g_display_mutex);
  }
  return true;
}
