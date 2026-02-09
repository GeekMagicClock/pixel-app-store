#include "ui/lvgl_runtime_fonts.h"

#include <stddef.h>

#include "esp_log.h"

extern "C" {
#include "lvgl.h"
}

// TinyTTF API is included via `lvgl.h` when enabled.

static const char *kTag = "ttf";

namespace {

static lv_font_t *g_font_small = nullptr;
static lv_font_t *g_font_time = nullptr;
static lv_font_t *g_font_simple = nullptr;     // 最简单的字体 - VT323
static lv_font_t *g_font_bold = nullptr;        // 粗体字体 - PressStart2P
static lv_font_t *g_font_mono = nullptr;        // 等宽字体 - ShareTechMono
static lv_font_t *g_font_pixel_bold = nullptr;  // 像素粗体 - Silkscreen Bold
static bool g_inited = false;

} // namespace

void LvglRuntimeFontsInit() {
  if (g_inited) return;
  g_inited = true;

#if LV_USE_TINY_TTF
  // Drop your TTFs into the LittleFS partition:
  // - `data_idf/fonts/small.ttf` -> "/littlefs/fonts/small.ttf"
  // - `data_idf/fonts/time.ttf`  -> "/littlefs/fonts/time.ttf"
  //
  // LVGL stdio FS driver opens them via: "S:/littlefs/fonts/xxx.ttf"
  g_font_small = lv_tiny_ttf_create_file_ex("S:/littlefs/fonts/small.ttf", 8, LV_FONT_KERNING_NONE, 64);
  if (g_font_small) {
    ESP_LOGI(kTag, "loaded small.ttf");
  } else {
    ESP_LOGW(kTag, "failed to load S:/littlefs/fonts/small.ttf");
  }

  g_font_time = lv_tiny_ttf_create_file_ex("S:/littlefs/fonts/time.ttf", 14, LV_FONT_KERNING_NONE, 96);
  if (g_font_time) {
    ESP_LOGI(kTag, "loaded time.ttf");
  } else {
    ESP_LOGW(kTag, "failed to load S:/littlefs/fonts/time.ttf");
  }

  // 最简单的字体 - VT323 终端风格，非常清晰
  g_font_simple = lv_tiny_ttf_create_file_ex("S:/littlefs/fonts/VT323-Regular.ttf", 10, LV_FONT_KERNING_NONE, 64);
  if (g_font_simple) {
    ESP_LOGI(kTag, "loaded VT323-Regular.ttf (simple)");
  } else {
    ESP_LOGW(kTag, "failed to load VT323-Regular.ttf");
  }

  // 粗体字体 - PressStart2P 像素游戏风格
  g_font_bold = lv_tiny_ttf_create_file_ex("S:/littlefs/fonts/PressStart2P-Regular.ttf", 8, LV_FONT_KERNING_NONE, 96);
  if (g_font_bold) {
    ESP_LOGI(kTag, "loaded PressStart2P-Regular.ttf (bold)");
  } else {
    ESP_LOGW(kTag, "failed to load PressStart2P-Regular.ttf");
  }

  // 等宽字体 - ShareTechMono 清晰等宽
  g_font_mono = lv_tiny_ttf_create_file_ex("S:/littlefs/fonts/ShareTechMono-Regular.ttf", 8, LV_FONT_KERNING_NONE, 64);
  if (g_font_mono) {
    ESP_LOGI(kTag, "loaded ShareTechMono-Regular.ttf (mono)");
  } else {
    ESP_LOGW(kTag, "failed to load ShareTechMono-Regular.ttf");
  }

  // 像素粗体 - Silkscreen Bold
  g_font_pixel_bold = lv_tiny_ttf_create_file_ex("S:/littlefs/fonts/Silkscreen-Bold.ttf", 8, LV_FONT_KERNING_NONE, 96);
  if (g_font_pixel_bold) {
    ESP_LOGI(kTag, "loaded Silkscreen-Bold.ttf (pixel bold)");
  } else {
    ESP_LOGW(kTag, "failed to load Silkscreen-Bold.ttf");
  }
#else
  ESP_LOGW(kTag, "LV_USE_TINY_TTF=0, runtime TTF disabled");
#endif
}

const lv_font_t *LvglRuntimeFontSmall() { return g_font_small; }

const lv_font_t *LvglRuntimeFontTime() { return g_font_time; }

const lv_font_t *LvglRuntimeFontSimple() { return g_font_simple; }

const lv_font_t *LvglRuntimeFontBold() { return g_font_bold; }

const lv_font_t *LvglRuntimeFontMono() { return g_font_mono; }

const lv_font_t *LvglRuntimeFontPixelBold() { return g_font_pixel_bold; }
