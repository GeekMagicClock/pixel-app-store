#include "ui/lvgl_stock_screen5.h"

#include "esp_log.h"

#include <cstdio>
#include <cstring>

extern "C" {
#include "lvgl.h"
}

static const char *kTag = "stock_screen5";

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
  const char *price;
  bool up;
};

static const Item kItems[] = {
    {"AAPL", "$323.10", true},
    {"TSLA", "$191.20", false},
    {"META", "$468.05", true},
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

static void DrawItemAt(lv_layer_t *layer, int idx, int x_off) {
  idx %= kItemCount;
  const Item &it = kItems[idx];

  // Shift the whole layout down a bit for better vertical centering on this panel.
  // Keep everything within 0..31 to avoid clipping.
  constexpr int kYOffset = 6;
  constexpr int kAvailH = 32 - kYOffset;  // 26
  constexpr int kHalfH = kAvailH / 2;     // 13
  constexpr int kTopY0 = kYOffset;
  constexpr int kTopY1 = kYOffset + kHalfH - 1;  // 6..18
  constexpr int kBotY0 = kYOffset + kHalfH;      // 19
  constexpr int kBotY1 = 31;                     // 19..31

  // Name (top)
  {
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.font = g_font;
    dsc.color = lv_color_white();
    dsc.text = it.name;
    dsc.opa = LV_OPA_COVER;
    dsc.align = LV_TEXT_ALIGN_CENTER;

    lv_area_t a = {x_off + 0, kTopY0, x_off + 63, kTopY1};
    lv_draw_label(layer, &dsc, &a);
  }

  // Price (bottom, color indicates up/down)
  {
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.font = g_font;
    dsc.color = it.up ? lv_color_make(80, 255, 80) : lv_color_make(255, 80, 80);
    dsc.text = it.price;
    dsc.opa = LV_OPA_COVER;
    dsc.align = LV_TEXT_ALIGN_CENTER;

    lv_area_t a = {x_off + 0, kBotY0, x_off + 63, kBotY1};
    lv_draw_label(layer, &dsc, &a);
  }
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

void LvglStockScreen5PreloadFont() {
  if (g_font) return;
  g_font = lv_tiny_ttf_create_file_ex("S:/littlefs/fonts/ari-w9500-bold.ttf",
                                     11,
                                     LV_FONT_KERNING_NONE,
                                     64);
  if (!g_font) {
    ESP_LOGE(kTag, "Failed to preload ari-w9500-bold.ttf");
  }
}

void LvglShowStockScreen5() {
  ESP_LOGI(kTag, "Creating stock screen5");

  StopRotateTimer();
  StopAnimTimer();

  if (!g_font) {
    LvglStockScreen5PreloadFont();
    if (!g_font) return;
  }

  g_scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(g_scr, lv_color_hex(0x300000), 0);  // distinguishable dark red
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
  ESP_LOGI(kTag, "Stock screen5 displayed");
}

void LvglStopStockScreen5() {
  StopRotateTimer();
  StopAnimTimer();

  if (g_font) {
    lv_tiny_ttf_destroy(g_font);
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
  ESP_LOGI(kTag, "Stock screen5 stopped");
}
