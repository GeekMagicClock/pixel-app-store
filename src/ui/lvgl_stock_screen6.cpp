#include "ui/lvgl_stock_screen6.h"

#include "esp_log.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

extern "C" {
#include "lvgl.h"
}

static const char *kTag = "stock_screen6";

LV_FONT_DECLARE(lv_font_silkscreen_regular_8);

namespace {

static lv_obj_t *g_scr = nullptr;
static lv_obj_t *g_canvas = nullptr;
static lv_draw_buf_t *g_canvas_buf = nullptr;

static lv_font_t *g_font = nullptr;

static lv_timer_t *g_rotate_timer = nullptr;
static lv_timer_t *g_anim_timer = nullptr;

static int g_index = 0;
static int g_from = 0;
static int g_to = 0;
static uint8_t g_step = 0;

static constexpr uint32_t kRotateMs = 5000;
static constexpr uint32_t kAnimPeriodMs = 16;
static constexpr uint8_t kAnimSteps = 18;

static inline int EaseInOutDx(int step, int steps, int width) {
  const float t = (steps <= 0) ? 1.0f : (static_cast<float>(step) / static_cast<float>(steps));
  const float s = t * t * (3.0f - 2.0f * t);
  return static_cast<int>(s * static_cast<float>(width) + 0.5f);
}

struct Item {
  const char *name;
  const char *chg;
  bool up;
  const char *price;
};

static const Item kItems[] = {
    {"AAPL", "+1.9%", true, "$323.10"},
    {"TSLA", "-0.8%", false, "$191.20"},
    {"META", "+0.4%", true, "$468.05"},
};
static constexpr int kItemCount = sizeof(kItems) / sizeof(kItems[0]);

// Chart uses 17 points across 64px (every 4px), values are "height" 0..15.
static constexpr int kChartY0 = 16;
static constexpr int kChartY1 = 31;
static constexpr int kChartH = kChartY1 - kChartY0 + 1;  // 16
static_assert(kChartH == 16, "chart height expected 16");

static const uint8_t kChartPts[kItemCount][17] = {
    // AAPL: rising mountain
    {1, 2, 3, 5, 7, 9, 11, 12, 11, 10, 9, 9, 10, 12, 14, 13, 12},
    // TSLA: choppy, lower
    {10, 9, 8, 9, 7, 6, 8, 7, 9, 8, 7, 6, 7, 6, 5, 6, 7},
    // META: one peak in the middle
    {3, 4, 5, 6, 7, 9, 11, 13, 14, 13, 11, 9, 7, 6, 5, 4, 3},
};

static inline uint16_t Color565(lv_color_t c) { return lv_color_to_u16(c); }

static inline void PutPixel565(lv_draw_buf_t *buf, int x, int y, uint16_t c16) {
  if (!buf || !buf->data) return;
  if (x < 0 || x >= 64 || y < 0 || y >= 32) return;
  const int stride_px = static_cast<int>(buf->header.stride / 2);
  auto *p = reinterpret_cast<uint16_t *>(buf->data);
  p[y * stride_px + x] = c16;
}

static int ChartValueAt(int idx, int x) {
  idx %= kItemCount;
  if (idx < 0) idx = 0;
  x = std::clamp(x, 0, 63);
  const uint8_t *p = kChartPts[idx];
  const int seg = x >> 2;       // /4, 0..15
  const int t = x & 0x03;       // 0..3
  const int v0 = p[seg];
  const int v1 = p[seg + 1];
  const int v = (v0 * (4 - t) + v1 * t + 2) / 4;  // rounded lerp
  return std::clamp(v, 0, kChartH - 1);
}

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

static void DrawTextLines(lv_layer_t *layer, const Item &it, int x_off) {
  // Line1: name (left) + change (right, colored).
  // Shift the percent block left a bit so it doesn't hug the edge.
  constexpr int kChgX0 = 28;  // was 40; move left by 12px
  // Tighten vertical layout:
  // - line1 up by 2px (may go slightly negative; LVGL will clip)
  // - line2 up by 3px
  constexpr int kLine1Y0 = -2;
  constexpr int kLine1Y1 = 5;
  constexpr int kLine2Y0 = 5;
  constexpr int kLine2Y1 = 12;
  {
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.font = g_font;
    dsc.opa = LV_OPA_COVER;
    dsc.color = lv_color_white();
    dsc.text = it.name;
    dsc.align = LV_TEXT_ALIGN_LEFT;
    lv_area_t a = {x_off + 0, kLine1Y0, x_off + (kChgX0 - 1), kLine1Y1};
    lv_draw_label(layer, &dsc, &a);
  }
  {
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.font = g_font;
    dsc.opa = LV_OPA_COVER;
    dsc.color = it.up ? lv_color_make(80, 255, 80) : lv_color_make(255, 80, 80);
    dsc.text = it.chg;
    dsc.align = LV_TEXT_ALIGN_RIGHT;
    lv_area_t a = {x_off + kChgX0, kLine1Y0, x_off + 63, kLine1Y1};
    lv_draw_label(layer, &dsc, &a);
  }

  // Line2: price (left, white)
  {
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.font = g_font;
    dsc.opa = LV_OPA_COVER;
    dsc.color = lv_color_white();
    dsc.text = it.price;
    dsc.align = LV_TEXT_ALIGN_LEFT;
    lv_area_t a = {x_off + 0, kLine2Y0, x_off + 63, kLine2Y1};
    lv_draw_label(layer, &dsc, &a);
  }
}

static void DrawChart(int idx, bool up, int x_off) {
  if (!g_canvas_buf) return;

  const uint16_t fill = Color565(up ? lv_color_make(0, 90, 0) : lv_color_make(90, 0, 0));
  const uint16_t stroke = Color565(up ? lv_color_make(80, 255, 80) : lv_color_make(255, 80, 80));

  for (int x = 0; x < 64; x++) {
    const int v = ChartValueAt(idx, x);
    const int peak_y = kChartY1 - v;
    const int sx = x_off + x;
    if (sx < 0 || sx >= 64) continue;

    // Fill from peak to bottom.
    for (int y = peak_y; y <= kChartY1; y++) {
      PutPixel565(g_canvas_buf, sx, y, fill);
    }
    // Outline point.
    PutPixel565(g_canvas_buf, sx, peak_y, stroke);
  }
}

static void DrawItemAt(lv_layer_t *layer, int idx, int x_off) {
  idx %= kItemCount;
  const Item &it = kItems[idx];
  DrawTextLines(layer, it, x_off);
  DrawChart(idx, it.up, x_off);
}

static void Redraw(int idx, int x_off) {
  if (!g_canvas) return;
  lv_canvas_fill_bg(g_canvas, lv_color_black(), LV_OPA_COVER);
  lv_layer_t layer;
  lv_canvas_init_layer(g_canvas, &layer);
  DrawItemAt(&layer, idx, x_off);
  lv_canvas_finish_layer(g_canvas, &layer);
  lv_obj_invalidate(g_canvas);
}

static void AnimTimerCb(lv_timer_t *t) {
  (void)t;
  if (!g_canvas) {
    StopAnimTimer();
    return;
  }

  if (g_step > kAnimSteps) {
    StopAnimTimer();
    g_index = g_to;
    Redraw(g_index, 0);
    return;
  }

  const int dx = EaseInOutDx(g_step, kAnimSteps, 64);

  lv_canvas_fill_bg(g_canvas, lv_color_black(), LV_OPA_COVER);
  lv_layer_t layer;
  lv_canvas_init_layer(g_canvas, &layer);

  DrawItemAt(&layer, g_from, -dx);
  DrawItemAt(&layer, g_to, 64 - dx);

  lv_canvas_finish_layer(g_canvas, &layer);
  lv_obj_invalidate(g_canvas);

  g_step++;
}

static void StartSlideTo(int next_idx) {
  StopAnimTimer();
  g_from = g_index;
  g_to = next_idx;
  g_step = 0;
  g_anim_timer = lv_timer_create(AnimTimerCb, kAnimPeriodMs, nullptr);
}

static void RotateTimerCb(lv_timer_t *t) {
  (void)t;
  const int next = (g_index + 1) % kItemCount;
  StartSlideTo(next);
}

}  // namespace

void LvglStockScreen6PreloadFont() {
  if (g_font) return;
  g_font = lv_tiny_ttf_create_file_ex("S:/littlefs/fonts/Silkscreen-Regular.ttf",
                                     8,
                                     LV_FONT_KERNING_NONE,
                                     64);
  if (!g_font) {
    ESP_LOGW(kTag, "Failed to preload Silkscreen-Regular.ttf, fallback to builtin font");
    g_font = const_cast<lv_font_t *>(&lv_font_silkscreen_regular_8);
  }
}

void LvglShowStockScreen6() {
  ESP_LOGI(kTag, "Creating stock screen6");

  StopRotateTimer();
  StopAnimTimer();

  if (!g_font) {
    LvglStockScreen6PreloadFont();
    if (!g_font) return;
  }

  g_scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(g_scr, lv_color_hex(0x001030), 0);  // distinguishable dark navy
  lv_obj_set_style_bg_opa(g_scr, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(g_scr, 0, 0);
  lv_obj_clear_flag(g_scr, LV_OBJ_FLAG_SCROLLABLE);

  g_canvas_buf = lv_draw_buf_create(64, 32, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
  if (!g_canvas_buf) {
    ESP_LOGE(kTag, "Failed to create canvas buffer");
    return;
  }

  g_canvas = lv_canvas_create(g_scr);
  lv_canvas_set_draw_buf(g_canvas, g_canvas_buf);
  lv_obj_clear_flag(g_canvas, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(g_canvas, 0, 0);
  lv_obj_set_style_border_width(g_canvas, 0, 0);
  lv_obj_set_style_radius(g_canvas, 0, 0);
  lv_obj_set_pos(g_canvas, 0, 0);
  lv_obj_set_size(g_canvas, 64, 32);

  g_index = 0;
  Redraw(g_index, 0);

  g_rotate_timer = lv_timer_create(RotateTimerCb, kRotateMs, nullptr);

  lv_screen_load(g_scr);
  lv_obj_invalidate(g_scr);
  ESP_LOGI(kTag, "Stock screen6 displayed");
}

void LvglStopStockScreen6() {
  StopRotateTimer();
  StopAnimTimer();

  if (g_font) {
    if (g_font != &lv_font_silkscreen_regular_8) {
      lv_tiny_ttf_destroy(g_font);
    }
    g_font = nullptr;
  }

  if (g_canvas_buf) {
    lv_draw_buf_destroy(g_canvas_buf);
    g_canvas_buf = nullptr;
  }

  if (g_scr) {
    lv_obj_delete(g_scr);
    g_scr = nullptr;
  }

  g_canvas = nullptr;
  ESP_LOGI(kTag, "Stock screen6 stopped");
}
