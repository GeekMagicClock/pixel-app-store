#include "ui/lvgl_stock_screen4.h"

#include "esp_log.h"

#include <cstdio>
#include <cstring>
#include <algorithm>

extern "C" {
#include "lvgl.h"
}

#include "third_party/animatedgif/AnimatedGIF.h"

static const char *kTag = "stock_screen4";

namespace {

static lv_obj_t *g_scr = nullptr;
static lv_obj_t *g_canvas = nullptr;
static lv_draw_buf_t *g_canvas_buf = nullptr;

static lv_font_t *g_time_font = nullptr;
static lv_font_t *g_line_font = nullptr;

static lv_timer_t *g_rotate_timer = nullptr;
static lv_timer_t *g_anim_timer = nullptr;
static lv_timer_t *g_weather_timer = nullptr;

static int g_index = 0;
static int g_from = 0;
static int g_to = 0;
static uint8_t g_step = 0;

// 5s rotate; animation ~300ms
static constexpr uint32_t kRotateMs = 5000;
static constexpr uint32_t kAnimPeriodMs = 16;  // ~60fps timer tick
static constexpr uint8_t kAnimSteps = 18;      // ~288ms total

// Layout tuning for 64x32
static constexpr int kTimeH = 12;       // time occupies y=0..11
static constexpr int kBottomY0 = 12;    // bottom block starts right after time
static constexpr int kBottomH = 32 - kBottomY0;  // 20px
// Bottom text layout (computed at runtime from font metrics)
static constexpr int kBottomPadTop = 0;
static constexpr int kBottomLineGap = 1;

// Weather GIF region (top-left)
static constexpr int kWeatherX = 0;
static constexpr int kWeatherY = 0;
static constexpr int kWeatherW = 16;
static constexpr int kWeatherH = 16;
static constexpr int kContentX0 = kWeatherW;
static constexpr int kContentW = 64 - kContentX0;

static AnimatedGIF g_weather_gif;
static uint8_t *g_weather_fb = nullptr;  // cooked mode framebuf: w*(h+2)
static int g_weather_w = 0;
static int g_weather_h = 0;
static bool g_weather_open = false;
static bool g_weather_has_frame = false;
static uint16_t g_weather_last[kWeatherW * kWeatherH] = {};

// ESP-IDF LittleFS is mounted at "/littlefs" (see src/main.cpp).
static constexpr const char *kWeatherGifSrc = "/littlefs/wea/01.gif";

static inline int EaseInOutDx(int step, int steps, int width) {
  // Smoothstep easing: t^2*(3-2t)
  const float t = (steps <= 0) ? 1.0f : (static_cast<float>(step) / static_cast<float>(steps));
  const float s = t * t * (3.0f - 2.0f * t);
  return static_cast<int>(s * static_cast<float>(width) + 0.5f);
}

struct TickerItem {
  const char *sym;
  const char *chg;
  bool up;
  const char *price;
};

// Max 10 items (can tweak later)
static const TickerItem kItems[] = {
    {"APPLE", "+1.9%", true, "$123.80"},
    {"TSLA", "-0.8%", false, "$191.20"},
    {"META", "+0.4%", true, "$468.10"},
    {"NVDA", "+2.3%", true, "$702.55"},
    {"AMZN", "-0.2%", false, "$172.04"},
};
static constexpr int kItemCount = sizeof(kItems) / sizeof(kItems[0]);

static void StopAnimTimer() {
  if (g_anim_timer) {
    lv_timer_del(g_anim_timer);
    g_anim_timer = nullptr;
  }
}

static void StopRotateTimer() {
  if (g_rotate_timer) {
    lv_timer_del(g_rotate_timer);
    g_rotate_timer = nullptr;
  }
}

static void StopWeatherTimer() {
  if (g_weather_timer) {
    lv_timer_del(g_weather_timer);
    g_weather_timer = nullptr;
  }
}

static void StopWeatherGif() {
  StopWeatherTimer();
  if (g_weather_open) {
    g_weather_gif.close();
    g_weather_open = false;
  }
  if (g_weather_fb) {
    lv_free(g_weather_fb);
    g_weather_fb = nullptr;
  }
  g_weather_w = 0;
  g_weather_h = 0;
  g_weather_has_frame = false;
}

static void InvalidateWeatherArea() {
  if (!g_canvas) return;
  lv_area_t a = {kWeatherX, kWeatherY, kWeatherX + kWeatherW - 1, kWeatherY + kWeatherH - 1};
  lv_obj_invalidate_area(g_canvas, &a);
}

static void RestoreWeatherToCanvas() {
  if (!g_canvas_buf || !g_weather_has_frame) return;

  const int stride = static_cast<int>(g_canvas_buf->header.stride);
  for (int y = 0; y < kWeatherH; y++) {
    uint8_t *dst = g_canvas_buf->data + (kWeatherY + y) * stride + (kWeatherX * 2);
    const uint16_t *src = &g_weather_last[y * kWeatherW];
    std::memcpy(dst, src, kWeatherW * 2);
  }
}

static void *GifOpenFile(const char *szFilename, int32_t *pFileSize) {
  FILE *f = std::fopen(szFilename, "rb");
  if (!f) return nullptr;
  std::fseek(f, 0, SEEK_END);
  long sz = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (pFileSize) *pFileSize = static_cast<int32_t>(sz);
  return f;
}

static void GifCloseFile(void *pHandle) {
  if (!pHandle) return;
  std::fclose(reinterpret_cast<FILE *>(pHandle));
}

static int32_t GifReadFile(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen) {
  auto *f = reinterpret_cast<FILE *>(pFile->fHandle);
  if (!f) return 0;

  int32_t to_read = iLen;
  const int32_t remain = pFile->iSize - pFile->iPos;
  if (remain < to_read) to_read = remain;
  if (to_read <= 0) return 0;

  const int32_t n = static_cast<int32_t>(std::fread(pBuf, 1, static_cast<size_t>(to_read), f));
  pFile->iPos += n;
  return n;
}

static int32_t GifSeekFile(GIFFILE *pFile, int32_t iPosition) {
  auto *f = reinterpret_cast<FILE *>(pFile->fHandle);
  if (!f) return 0;

  if (iPosition < 0) iPosition = 0;
  else if (iPosition >= pFile->iSize) iPosition = pFile->iSize - 1;
  pFile->iPos = iPosition;
  std::fseek(f, iPosition, SEEK_SET);
  return iPosition;
}

static void GifDraw(GIFDRAW *pDraw) {
  if (!g_canvas_buf || !g_canvas) return;
  if (!pDraw || !pDraw->pPixels) return;

  // COOKED mode: pPixels points to RGB565 line buffer.
  const int y = pDraw->iY + pDraw->y;
  if (y < 0 || y >= kWeatherH) return;

  const int x = pDraw->iX;
  if (x >= kWeatherW) return;

  const int src_w = pDraw->iWidth;
  const int copy_w = std::min(src_w, kWeatherW - x);

  const int stride = static_cast<int>(g_canvas_buf->header.stride);
  uint8_t *dst = g_canvas_buf->data + (kWeatherY + y) * stride + (kWeatherX + x) * 2;
  std::memcpy(dst, pDraw->pPixels, static_cast<size_t>(copy_w) * 2);

  // Keep a copy so ticker animation (which clears the canvas) won't wipe the icon.
  std::memcpy(&g_weather_last[y * kWeatherW + x], pDraw->pPixels, static_cast<size_t>(copy_w) * 2);
  g_weather_has_frame = true;

  // If this is the last line of the frame, ask LVGL to refresh that area.
  if (pDraw->y == (pDraw->iHeight - 1)) {
    InvalidateWeatherArea();
  }
}

static void DrawTime(lv_layer_t *layer, const char *hhmm) {
  lv_draw_label_dsc_t dsc;
  lv_draw_label_dsc_init(&dsc);
  dsc.font = g_time_font ? g_time_font : g_line_font;
  dsc.color = lv_color_white();
  dsc.text = hhmm;
  dsc.opa = LV_OPA_COVER;

  // Top area: shift down a bit to match font ascent
  static constexpr int kTimeYOff = 4;  // move down by 1px more
  lv_area_t a = {kContentX0, kTimeYOff, 63, (kTimeH - 1) + kTimeYOff};
  dsc.align = LV_TEXT_ALIGN_CENTER;
  lv_draw_label(layer, &dsc, &a);
}

// (note) drawing for the bottom block is implemented by DrawBottomBlockAt()

static void DrawBottomBlockAt(lv_layer_t *layer, int idx, int xOff, lv_opa_t opa) {
  idx %= kItemCount;
  const TickerItem &it = kItems[idx];

  const int line_h = (g_line_font && g_line_font->line_height > 0) ? (int)g_line_font->line_height : 8;
  const int content_h = line_h + kBottomLineGap + line_h;
  int pad_top = (kBottomH - content_h) / 2;
  if (pad_top < 0) pad_top = 0;

  static char line1[32];
  static char line2[32];
  snprintf(line1, sizeof(line1), "%s %s", it.sym, it.chg);
  snprintf(line2, sizeof(line2), "%s", it.price);

  // Bottom block should be centered on the full 64px canvas width.
  const int x0 = xOff;
  const int x1 = xOff + 63;
  const int y1 = kBottomY0 + pad_top + 3;  // move (symbol+chg) down by 2px
  const int y2 = kBottomY0 + pad_top + line_h + kBottomLineGap;  // keep price Y relative to centering

  // Line 1 (sym + change)
  {
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.font = g_line_font;
    dsc.color = it.up ? lv_color_make(80, 255, 80) : lv_color_make(255, 80, 80);
    dsc.text = line1;
    dsc.opa = opa;
    dsc.align = LV_TEXT_ALIGN_CENTER;

    lv_area_t a = {x0,
                   y1,
                   x1,
                   y1 + line_h - 1};
    lv_draw_label(layer, &dsc, &a);
  }

  // Line 2 (price)
  {
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.font = g_line_font;
    dsc.color = lv_color_white();
    dsc.text = line2;
    dsc.opa = opa;
    dsc.align = LV_TEXT_ALIGN_CENTER;

    lv_area_t a = {x0,
                   y2,
                   x1,
                   y2 + line_h - 1};
    lv_draw_label(layer, &dsc, &a);
  }
}

static void Redraw(int top_time_fixed_idx, int line_idx, int line_xOff, lv_opa_t line_opa) {
  (void)top_time_fixed_idx;

  lv_canvas_fill_bg(g_canvas, lv_color_black(), LV_OPA_COVER);

  lv_layer_t layer;
  lv_canvas_init_layer(g_canvas, &layer);

  // For now, fixed time as requested.
  DrawTime(&layer, "12:04");
  DrawBottomBlockAt(&layer, line_idx, line_xOff, line_opa);

  lv_canvas_finish_layer(g_canvas, &layer);
  RestoreWeatherToCanvas();
  lv_obj_invalidate(g_canvas);
}

static void AnimTimerCb(lv_timer_t *t) {
  (void)t;

  if (g_step > kAnimSteps) {
    StopAnimTimer();
    g_index = g_to;
    Redraw(0, g_index, 0, LV_OPA_COVER);
    return;
  }

  // Horizontal slide: old x=0 -> -64, new x=64 -> 0 (ease in/out)
  const int dx = EaseInOutDx(g_step, kAnimSteps, 64);

  lv_canvas_fill_bg(g_canvas, lv_color_black(), LV_OPA_COVER);
  lv_layer_t layer;
  lv_canvas_init_layer(g_canvas, &layer);

  // We clear the whole canvas each frame, so the time must be repainted to avoid flicker.
  DrawTime(&layer, "12:04");
  DrawBottomBlockAt(&layer, g_from, -dx, LV_OPA_COVER);
  DrawBottomBlockAt(&layer, g_to, 64 - dx, LV_OPA_COVER);

  lv_canvas_finish_layer(g_canvas, &layer);
  RestoreWeatherToCanvas();
  lv_obj_invalidate(g_canvas);

  g_step++;
}

static void StartLineSlideTo(int next_idx) {
  StopAnimTimer();

  g_from = g_index;
  g_to = next_idx;
  g_step = 0;

  g_anim_timer = lv_timer_create(AnimTimerCb, kAnimPeriodMs, nullptr);
}

static void RotateTimerCb(lv_timer_t *t) {
  (void)t;
  const int next_idx = (g_index + 1) % kItemCount;
  StartLineSlideTo(next_idx);
}

static void WeatherTimerCb(lv_timer_t *t) {
  (void)t;
  if (!g_weather_open) return;

  int ms_delay_next = 0;
  int has_next = g_weather_gif.playFrame(false, &ms_delay_next, nullptr);
  if (has_next <= 0) {
    g_weather_gif.reset();
    has_next = g_weather_gif.playFrame(false, &ms_delay_next, nullptr);
  }

  if (ms_delay_next <= 0) ms_delay_next = 80;
  if (g_weather_timer) lv_timer_set_period(g_weather_timer, static_cast<uint32_t>(ms_delay_next));
}

static void StartWeatherGifIfNeeded() {
  if (g_weather_open && g_weather_timer) return;

  StopWeatherGif();

  g_weather_gif.begin(GIF_PALETTE_RGB565_LE);
  g_weather_open = (g_weather_gif.open(kWeatherGifSrc, GifOpenFile, GifCloseFile, GifReadFile, GifSeekFile, GifDraw) != 0);
  if (!g_weather_open) {
    ESP_LOGW(kTag, "Weather GIF open failed: %s", kWeatherGifSrc);
    return;
  }

  g_weather_w = g_weather_gif.getCanvasWidth();
  g_weather_h = g_weather_gif.getCanvasHeight();
  if (g_weather_w <= 0 || g_weather_h <= 0) {
    ESP_LOGW(kTag, "Weather GIF invalid canvas size: %dx%d", g_weather_w, g_weather_h);
    StopWeatherGif();
    return;
  }

  // COOKED mode needs: canvas as 8bpp (w*h) + one cooked line (w*2) => w*(h+2).
  const size_t w = static_cast<size_t>(g_weather_w);
  const size_t h = static_cast<size_t>(g_weather_h);
  g_weather_fb = static_cast<uint8_t *>(lv_malloc(w * (h + 2)));
  if (!g_weather_fb) {
    ESP_LOGW(kTag, "Weather GIF framebuffer alloc failed");
    StopWeatherGif();
    return;
  }

  g_weather_gif.setDrawType(GIF_DRAW_COOKED);
  g_weather_gif.setFrameBuf(g_weather_fb);

  StopWeatherTimer();
  g_weather_timer = lv_timer_create(WeatherTimerCb, 10, nullptr);
  lv_timer_resume(g_weather_timer);
  lv_timer_reset(g_weather_timer);

  // Decode one frame immediately so the icon shows up on first paint.
  WeatherTimerCb(g_weather_timer);
}

}  // namespace

void LvglStockScreen4PreloadFont() {
  if (g_line_font && g_time_font) return;

  // Reuse Silkscreen; pick two sizes that read well on 64x32.
  if (!g_line_font) {
    g_line_font = lv_tiny_ttf_create_file_ex(
        "S:/littlefs/fonts/Silkscreen-Regular.ttf",
        8,
        LV_FONT_KERNING_NONE,
        64);
    if (!g_line_font) ESP_LOGE(kTag, "Failed to load 8px font");
    else {
      ESP_LOGI(kTag, "line_font metrics: size=%d line_height=%d base_line=%d", 9,
               (int)g_line_font->line_height, (int)g_line_font->base_line);
    }
  }

  if (!g_time_font) {
    static constexpr int kTimeFontPx = 11;
    g_time_font = lv_tiny_ttf_create_file_ex(
        "S:/littlefs/fonts/ari-w9500-bold.ttf",
        kTimeFontPx,
        LV_FONT_KERNING_NONE,
        64);
    if (!g_time_font) ESP_LOGW(kTag, "Failed to load DIN time font, fallback to line font");
  }
}

void LvglShowStockScreen4() {
  ESP_LOGI(kTag, "Creating stock screen4");

  if (!g_line_font) {
    LvglStockScreen4PreloadFont();
    if (!g_line_font) return;
  }

  if (g_scr) {
    // If already created, just reload.
    lv_screen_load(g_scr);
    StartWeatherGifIfNeeded();
    return;
  }

  g_scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(g_scr, lv_color_black(), 0);

  g_canvas_buf = lv_draw_buf_create(64, 32, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
  if (!g_canvas_buf) {
    ESP_LOGE(kTag, "Failed to create canvas buffer");
    return;
  }

  g_canvas = lv_canvas_create(g_scr);
  lv_canvas_set_draw_buf(g_canvas, g_canvas_buf);
  lv_obj_clear_flag(g_canvas, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(g_canvas, 0, 0);

  StartWeatherGifIfNeeded();

  g_index = 0;
  Redraw(0, g_index, 0, LV_OPA_COVER);

  StopRotateTimer();
  g_rotate_timer = lv_timer_create(RotateTimerCb, kRotateMs, nullptr);

  lv_screen_load(g_scr);
  ESP_LOGI(kTag, "Stock screen4 displayed");
}

void LvglStopStockScreen4() {
  StopAnimTimer();
  StopRotateTimer();
  StopWeatherGif();

  if (g_canvas_buf) {
    lv_draw_buf_destroy(g_canvas_buf);
    g_canvas_buf = nullptr;
  }

  if (g_scr) {
    lv_obj_del(g_scr);
    g_scr = nullptr;
  }

  g_canvas = nullptr;
  ESP_LOGI(kTag, "Stock screen4 stopped");
}
