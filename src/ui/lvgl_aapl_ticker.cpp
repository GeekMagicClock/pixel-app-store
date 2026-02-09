#include "ui/lvgl_aapl_ticker.h"
#include "ui/fonts/lv_font_digits_thin_8.h"

extern "C" {
#include "lvgl.h"
}

namespace {

static lv_obj_t *g_scr = nullptr;
static lv_obj_t *g_lbl_symbol = nullptr;
static lv_obj_t *g_lbl_price = nullptr;
static lv_obj_t *g_lbl_change = nullptr;
static lv_obj_t *g_lbl_percent = nullptr;
static lv_obj_t *g_chart = nullptr;
static lv_chart_series_t *g_series = nullptr;
static lv_timer_t *g_timer = nullptr;

static uint32_t g_rng = 0x12345678u;
static int32_t g_last_cents = 19234;     // $192.34
static int32_t g_open_cents = 19110;     // $191.10

static uint32_t XorShift32() {
  uint32_t x = g_rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  g_rng = x;
  return x;
}

static void UpdateLabels() {
  const int32_t price = g_last_cents;
  const int32_t delta = price - g_open_cents;
  const int32_t abs_delta = LV_ABS(delta);

  // Percent with 0.1% precision (avoid float).
  const int32_t pct_tenths = (g_open_cents != 0) ? (delta * 1000) / g_open_cents : 0;
  const int32_t abs_pct_tenths = LV_ABS(pct_tenths);

  lv_label_set_text(g_lbl_symbol, "AAPL");
  lv_label_set_text_fmt(g_lbl_price, "%ld.%02ld", price / 100, price % 100);
  lv_label_set_text_fmt(g_lbl_change, "%c%ld.%02ld",
                        delta >= 0 ? '+' : '-',
                        abs_delta / 100, abs_delta % 100);
  lv_label_set_text_fmt(g_lbl_percent, "%ld.%01ld%%", abs_pct_tenths / 10, abs_pct_tenths % 10);

  if (delta >= 0) {
    lv_obj_set_style_text_color(g_lbl_change, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_set_style_text_color(g_lbl_percent, lv_palette_main(LV_PALETTE_GREEN), 0);
  } else {
    lv_obj_set_style_text_color(g_lbl_change, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_text_color(g_lbl_percent, lv_palette_main(LV_PALETTE_RED), 0);
  }
}

static void UpdateAxisRange() {
  // Determine min/max from series points.
  const uint32_t n = lv_chart_get_point_count(g_chart);
  const int32_t *y = lv_chart_get_y_array(g_chart, g_series);
  int32_t minv = y[0];
  int32_t maxv = y[0];
  for (uint32_t i = 1; i < n; i++) {
    if (y[i] < minv) minv = y[i];
    if (y[i] > maxv) maxv = y[i];
  }
  if (minv == maxv) {
    minv -= 10;
    maxv += 10;
  } else {
    const int32_t pad = LV_MAX(5, (maxv - minv) / 10);
    minv -= pad;
    maxv += pad;
  }
  lv_chart_set_axis_range(g_chart, LV_CHART_AXIS_PRIMARY_Y, minv, maxv);
}

static void TickCb(lv_timer_t *t) {
  (void)t;
  if (!g_chart || !g_series) return;

  // Simulate a small random walk (+/- 0.30).
  const int r = static_cast<int>(XorShift32() % 61) - 30; // [-30..+30] cents
  g_last_cents = LV_CLAMP(18500, g_last_cents + r, 19999);

  lv_chart_set_next_value(g_chart, g_series, g_last_cents);
  UpdateAxisRange();
  UpdateLabels();
  lv_chart_refresh(g_chart);
}

static void Stop() {
  if (g_timer) {
    lv_timer_delete(g_timer);
    g_timer = nullptr;
  }
  if (g_scr && lv_obj_is_valid(g_scr)) {
    lv_obj_del(g_scr);
  }
  g_scr = nullptr;
  g_lbl_symbol = nullptr;
  g_lbl_price = nullptr;
  g_lbl_change = nullptr;
  g_lbl_percent = nullptr;
  g_chart = nullptr;
  g_series = nullptr;
}

} // namespace

void LvglRunAaplTicker() {
  Stop();

  g_scr = lv_obj_create(nullptr);
  lv_obj_clear_flag(g_scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(g_scr, 0, 0);
  lv_obj_set_style_border_width(g_scr, 0, 0);
  lv_obj_set_style_bg_color(g_scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g_scr, LV_OPA_COVER, 0);

  // Layout for 64x32:
  // - Row0 (8px):  symbol (left) + price (right)
  // - Row1 (8px):  change + percent (right aligned)
  // - Chart (16px)
  g_lbl_symbol = lv_label_create(g_scr);
  lv_obj_set_pos(g_lbl_symbol, 0, 0);
  lv_obj_set_size(g_lbl_symbol, 24, 8);
  lv_obj_set_style_text_color(g_lbl_symbol, lv_color_white(), 0);
  lv_obj_set_style_text_font(g_lbl_symbol, &lv_font_digits_thin_8, 0);

  g_lbl_price = lv_label_create(g_scr);
  lv_obj_set_pos(g_lbl_price, 24, 0);
  lv_obj_set_size(g_lbl_price, 40, 8);
  lv_label_set_long_mode(g_lbl_price, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(g_lbl_price, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_color(g_lbl_price, lv_palette_main(LV_PALETTE_YELLOW), 0);
  lv_obj_set_style_text_font(g_lbl_price, &lv_font_digits_thin_8, 0);

  g_lbl_change = lv_label_create(g_scr);
  lv_obj_set_pos(g_lbl_change, 0, 8);
  lv_obj_set_size(g_lbl_change, 40, 8);
  lv_label_set_long_mode(g_lbl_change, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(g_lbl_change, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_set_style_text_font(g_lbl_change, &lv_font_digits_thin_8, 0);

  g_lbl_percent = lv_label_create(g_scr);
  lv_obj_set_pos(g_lbl_percent, 40, 8);
  lv_obj_set_size(g_lbl_percent, 24, 8);
  lv_label_set_long_mode(g_lbl_percent, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(g_lbl_percent, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_font(g_lbl_percent, &lv_font_digits_thin_8, 0);

  g_chart = lv_chart_create(g_scr);
  lv_obj_set_pos(g_chart, 0, 16);
  lv_obj_set_size(g_chart, 64, 16);
  lv_obj_set_style_bg_opa(g_chart, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(g_chart, 0, 0);
  lv_obj_set_style_pad_all(g_chart, 0, 0);
  lv_obj_set_style_line_width(g_chart, 1, LV_PART_ITEMS);
  lv_obj_set_style_line_color(g_chart, lv_palette_main(LV_PALETTE_LIGHT_BLUE), LV_PART_ITEMS);
  lv_obj_set_style_line_opa(g_chart, LV_OPA_COVER, LV_PART_ITEMS);

  lv_chart_set_type(g_chart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(g_chart, 64);
  lv_chart_set_update_mode(g_chart, LV_CHART_UPDATE_MODE_SHIFT);
  lv_chart_set_div_line_count(g_chart, 0, 0);

  g_series = lv_chart_add_series(g_chart, lv_palette_main(LV_PALETTE_LIGHT_BLUE), LV_CHART_AXIS_PRIMARY_Y);

  // Seed initial series (flat-ish).
  for (int i = 0; i < 64; i++) {
    const int r = static_cast<int>(XorShift32() % 21) - 10;
    lv_chart_set_next_value(g_chart, g_series, g_last_cents + r);
  }
  UpdateAxisRange();
  UpdateLabels();

  lv_screen_load(g_scr);

  // Update twice per second.
  g_timer = lv_timer_create(TickCb, 500, nullptr);
}
