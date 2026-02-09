#include "ui/lvgl_smoke_test.h"
#include "ui/fonts/lv_font_digits_thin_8.h"

extern "C" {
#include "lvgl.h"
}

namespace {

static lv_obj_t *g_scr = nullptr;
static lv_obj_t *g_bar = nullptr;
static lv_obj_t *g_box = nullptr;
static lv_timer_t *g_timer = nullptr;
static int g_dx = 1;
static int g_bar_v = 0;

static void TickCb(lv_timer_t *t) {
  (void)t;
  if (!g_scr || !g_bar || !g_box) return;
  if (!lv_obj_is_valid(g_scr) || !lv_obj_is_valid(g_bar) || !lv_obj_is_valid(g_box)) return;
  if (lv_screen_active() != g_scr) return;

  g_bar_v = (g_bar_v + 2) % 101;
  lv_bar_set_value(g_bar, g_bar_v, LV_ANIM_OFF);

  const int32_t w = lv_obj_get_width(g_scr);
  const int32_t box_w = lv_obj_get_width(g_box);
  int32_t x = lv_obj_get_x(g_box) + g_dx;
  if (x <= 0) {
    x = 0;
    g_dx = 1;
  } else if (x >= (w - box_w)) {
    x = w - box_w;
    g_dx = -1;
  }
  lv_obj_set_x(g_box, x);
}

} // namespace

void LvglStopSmokeTest() {
  if (g_timer) {
    lv_timer_delete(g_timer);
    g_timer = nullptr;
  }

  // Don't delete the currently active screen. Other demos might assume
  // `lv_screen_active()` is valid and will clean it up themselves.
  if (g_scr && lv_obj_is_valid(g_scr) && lv_screen_active() != g_scr) {
    lv_obj_del(g_scr);
  }
  g_scr = nullptr;
  g_bar = nullptr;
  g_box = nullptr;
}

void LvglRunSmokeTest() {
  LvglStopSmokeTest();

  g_scr = lv_obj_create(nullptr);
  lv_obj_clear_flag(g_scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(g_scr, 0, 0);
  lv_obj_set_style_border_width(g_scr, 0, 0);
  lv_obj_set_style_bg_color(g_scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g_scr, LV_OPA_COVER, 0);

  lv_obj_t *label = lv_label_create(g_scr);
  lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_label_set_text(label, "LVGL SMOKE TEST");
  lv_obj_set_style_text_font(label, &lv_font_digits_thin_8, 0);
  lv_obj_set_width(label, 64);
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);

  g_bar = lv_bar_create(g_scr);
  lv_obj_set_size(g_bar, 64, 8);
  lv_obj_align(g_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_bar_set_range(g_bar, 0, 100);
  lv_bar_set_value(g_bar, 0, LV_ANIM_OFF);

  g_box = lv_obj_create(g_scr);
  lv_obj_set_size(g_box, 8, 8);
  lv_obj_set_style_bg_color(g_box, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(g_box, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(g_box, 0, 0);
  lv_obj_set_pos(g_box, 0, 12);

  g_dx = 1;
  g_bar_v = 0;
  g_timer = lv_timer_create(TickCb, 30, nullptr);

  lv_screen_load(g_scr);
}
