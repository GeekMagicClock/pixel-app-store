#include "ui/lvgl_stock_carousel.h"

#include "ui/fonts/lv_font_digits_thin_8.h"

extern "C" {
#include "lvgl.h"
}

namespace {

struct StockScreen {
  const char *symbol = nullptr;
  lv_obj_t *scr = nullptr;
  lv_obj_t *lbl_symbol = nullptr;
  lv_obj_t *lbl_price = nullptr;
  lv_obj_t *lbl_change = nullptr;
  lv_obj_t *lbl_percent_sign = nullptr;
  lv_obj_t *lbl_percent = nullptr;
  lv_obj_t *chart = nullptr;
  lv_chart_series_t *series = nullptr;
  lv_timer_t *tick_timer = nullptr;

  uint32_t rng = 0;
  int32_t last_cents = 0;
  int32_t open_cents = 0;
  int32_t clamp_min = 0;
  int32_t clamp_max = 0;

  lv_palette_t up_palette = LV_PALETTE_GREEN;
  lv_palette_t down_palette = LV_PALETTE_RED;
};

static constexpr int kScreenCount = 5;
static StockScreen g_screens[kScreenCount];
static lv_timer_t *g_switch_timer = nullptr;
static int g_active_idx = 0;
static uint32_t g_anim_idx = 0;

static constexpr lv_scr_load_anim_t kAnims[] = {
    LV_SCR_LOAD_ANIM_MOVE_LEFT,
    LV_SCR_LOAD_ANIM_MOVE_RIGHT,
    LV_SCR_LOAD_ANIM_OVER_LEFT,
    LV_SCR_LOAD_ANIM_OVER_RIGHT,
    LV_SCR_LOAD_ANIM_FADE_IN,
    LV_SCR_LOAD_ANIM_FADE_OUT,
    LV_SCR_LOAD_ANIM_OUT_LEFT,
    LV_SCR_LOAD_ANIM_OUT_RIGHT,
};

static uint32_t XorShift32(uint32_t &state) {
  uint32_t x = state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  state = x;
  return x;
}

static void UpdateAxisRange(StockScreen &s) {
  const uint32_t n = lv_chart_get_point_count(s.chart);
  const int32_t *y = lv_chart_get_y_array(s.chart, s.series);
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
  lv_chart_set_axis_range(s.chart, LV_CHART_AXIS_PRIMARY_Y, minv, maxv);
}

static void UpdateLabels(StockScreen &s) {
  const int32_t price = s.last_cents;
  const int32_t delta = price - s.open_cents;
  const int32_t abs_delta = LV_ABS(delta);

  const int32_t pct_tenths = (s.open_cents != 0) ? (delta * 1000) / s.open_cents : 0;
  const int32_t abs_pct_tenths = LV_ABS(pct_tenths);

  lv_label_set_text(s.lbl_symbol, s.symbol);
  lv_label_set_text_fmt(s.lbl_price, "%ld.%02ld", price / 100, price % 100);
  lv_label_set_text_fmt(s.lbl_change, "%c%ld.%02ld", delta >= 0 ? '+' : '-', abs_delta / 100, abs_delta % 100);
  lv_label_set_text(s.lbl_percent_sign, (pct_tenths >= 0) ? "+" : "-");
  // Keep this short to avoid clipping: show 0.1% only when < 10%.
  if (abs_pct_tenths < 100) lv_label_set_text_fmt(s.lbl_percent, "%ld.%01ld%%", abs_pct_tenths / 10, abs_pct_tenths % 10);
  else lv_label_set_text_fmt(s.lbl_percent, "%ld%%", abs_pct_tenths / 10);

  // Make "+/-" stick to the left edge of the percent numeric text (no gap).
  // The percent label is right-aligned inside a fixed-width box, so compute where its text starts.
  if (s.lbl_percent_sign && s.lbl_percent && lv_obj_is_valid(s.lbl_percent_sign) && lv_obj_is_valid(s.lbl_percent)) {
    const lv_font_t *font = lv_obj_get_style_text_font(s.lbl_percent, LV_PART_MAIN);
    lv_text_align_t align = lv_obj_get_style_text_align(s.lbl_percent, LV_PART_MAIN);
    if (align != LV_TEXT_ALIGN_RIGHT) {
      lv_obj_set_style_text_align(s.lbl_percent, LV_TEXT_ALIGN_RIGHT, 0);
    }

    const char *pct_text = lv_label_get_text(s.lbl_percent);
    const int32_t letter_space = lv_obj_get_style_text_letter_space(s.lbl_percent, LV_PART_MAIN);
    const int32_t line_space = lv_obj_get_style_text_line_space(s.lbl_percent, LV_PART_MAIN);
    lv_point_t pct_size;
    lv_text_get_size(&pct_size, pct_text, font, letter_space, line_space, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    const int32_t pct_text_w = pct_size.x;
    const int32_t pct_x = lv_obj_get_x(s.lbl_percent);
    const int32_t pct_w = lv_obj_get_width(s.lbl_percent);
    const int32_t sign_w = lv_obj_get_width(s.lbl_percent_sign);
    const int32_t pct_left = pct_x + pct_w - pct_text_w;
    const int32_t sign_x = pct_left - sign_w;
    lv_obj_set_x(s.lbl_percent_sign, sign_x);
  }

  const lv_color_t c = (delta >= 0) ? lv_palette_main(s.up_palette) : lv_palette_main(s.down_palette);
  lv_obj_set_style_text_color(s.lbl_change, c, 0);
  lv_obj_set_style_text_color(s.lbl_percent_sign, c, 0);
  lv_obj_set_style_text_color(s.lbl_percent, c, 0);
}

static void TickCb(lv_timer_t *t) {
  auto &s = *static_cast<StockScreen *>(lv_timer_get_user_data(t));
  if (!s.scr || !s.chart || !s.series) return;
  if (!lv_obj_is_valid(s.scr) || !lv_obj_is_valid(s.chart)) return;
  if (lv_screen_active() != s.scr) return;

  const int r = static_cast<int>(XorShift32(s.rng) % 61) - 30; // [-30..+30] cents
  s.last_cents = LV_CLAMP(s.clamp_min, s.last_cents + r, s.clamp_max);

  lv_chart_set_next_value(s.chart, s.series, s.last_cents);
  UpdateAxisRange(s);
  UpdateLabels(s);
  lv_chart_refresh(s.chart);
}

static void CreateScreen(StockScreen &s) {
  s.scr = lv_obj_create(nullptr);
  lv_obj_clear_flag(s.scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(s.scr, 0, 0);
  lv_obj_set_style_border_width(s.scr, 0, 0);
  lv_obj_set_style_bg_color(s.scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s.scr, LV_OPA_COVER, 0);

  // Layout for 64x32:
  // - Row0 (8px):  SYMBOL (left) + PRICE (right)
  // - Row1 (8px):  CHANGE (left) + PERCENT (right)
  // - Chart (16px)
  s.lbl_symbol = lv_label_create(s.scr);
  lv_obj_set_pos(s.lbl_symbol, 0, 0);
  lv_obj_set_size(s.lbl_symbol, 24, 8);
  lv_obj_set_style_text_color(s.lbl_symbol, lv_color_white(), 0);
  lv_obj_set_style_text_font(s.lbl_symbol, &lv_font_digits_thin_8, 0);

  s.lbl_price = lv_label_create(s.scr);
  lv_obj_set_pos(s.lbl_price, 24, 0);
  lv_obj_set_size(s.lbl_price, 40, 8);
  lv_label_set_long_mode(s.lbl_price, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(s.lbl_price, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_color(s.lbl_price, lv_palette_main(LV_PALETTE_YELLOW), 0);
  lv_obj_set_style_text_font(s.lbl_price, &lv_font_digits_thin_8, 0);

  s.lbl_change = lv_label_create(s.scr);
  lv_obj_set_pos(s.lbl_change, 0, 8);
  lv_obj_set_size(s.lbl_change, 34, 8);
  lv_label_set_long_mode(s.lbl_change, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(s.lbl_change, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_set_style_text_font(s.lbl_change, &lv_font_digits_thin_8, 0);

  s.lbl_percent_sign = lv_label_create(s.scr);
  lv_obj_set_pos(s.lbl_percent_sign, 34, 8);
  lv_obj_set_size(s.lbl_percent_sign, 6, 8);
  lv_label_set_long_mode(s.lbl_percent_sign, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(s.lbl_percent_sign, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_font(s.lbl_percent_sign, &lv_font_digits_thin_8, 0);

  s.lbl_percent = lv_label_create(s.scr);
  lv_obj_set_pos(s.lbl_percent, 40, 8);
  lv_obj_set_size(s.lbl_percent, 24, 8);
  lv_label_set_long_mode(s.lbl_percent, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(s.lbl_percent, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_font(s.lbl_percent, &lv_font_digits_thin_8, 0);

  s.chart = lv_chart_create(s.scr);
  lv_obj_set_pos(s.chart, 0, 16);
  lv_obj_set_size(s.chart, 64, 16);
  lv_obj_set_style_bg_opa(s.chart, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s.chart, 0, 0);
  lv_obj_set_style_pad_all(s.chart, 0, 0);
  lv_obj_set_style_line_width(s.chart, 1, LV_PART_ITEMS);
  lv_obj_set_style_line_color(s.chart, lv_palette_main(LV_PALETTE_LIGHT_BLUE), LV_PART_ITEMS);
  lv_obj_set_style_line_opa(s.chart, LV_OPA_COVER, LV_PART_ITEMS);

  lv_chart_set_type(s.chart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(s.chart, 64);
  lv_chart_set_update_mode(s.chart, LV_CHART_UPDATE_MODE_SHIFT);
  lv_chart_set_div_line_count(s.chart, 0, 0);

  s.series = lv_chart_add_series(s.chart, lv_palette_main(LV_PALETTE_LIGHT_BLUE), LV_CHART_AXIS_PRIMARY_Y);

  for (int i = 0; i < 64; i++) {
    const int r = static_cast<int>(XorShift32(s.rng) % 21) - 10;
    lv_chart_set_next_value(s.chart, s.series, s.last_cents + r);
  }

  UpdateAxisRange(s);
  UpdateLabels(s);

  s.tick_timer = lv_timer_create(TickCb, 500, &s);
}

static void DestroyScreen(StockScreen &s) {
  if (s.tick_timer) {
    lv_timer_delete(s.tick_timer);
    s.tick_timer = nullptr;
  }
  if (s.scr && lv_obj_is_valid(s.scr)) {
    lv_obj_del(s.scr);
  }
  s.scr = nullptr;
  s.lbl_symbol = nullptr;
  s.lbl_price = nullptr;
  s.lbl_change = nullptr;
  s.lbl_percent_sign = nullptr;
  s.lbl_percent = nullptr;
  s.chart = nullptr;
  s.series = nullptr;
}

static void SwitchCb(lv_timer_t *t) {
  (void)t;
  if (!g_screens[g_active_idx].scr || !lv_obj_is_valid(g_screens[g_active_idx].scr)) return;
  const int next_idx = (g_active_idx + 1) % kScreenCount;
  if (!g_screens[next_idx].scr || !lv_obj_is_valid(g_screens[next_idx].scr)) return;

  const lv_scr_load_anim_t anim = kAnims[g_anim_idx % (sizeof(kAnims) / sizeof(kAnims[0]))];
  g_anim_idx++;

  lv_screen_load_anim(g_screens[next_idx].scr, anim, 350, 0, false);
  g_active_idx = next_idx;
}

} // namespace

void LvglStopStockCarousel() {
  if (g_switch_timer) {
    lv_timer_delete(g_switch_timer);
    g_switch_timer = nullptr;
  }

  // Avoid deleting the currently active screen directly.
  lv_obj_t *active = lv_screen_active();
  for (int i = 0; i < kScreenCount; i++) {
    if (active == g_screens[i].scr) {
      lv_obj_t *tmp = lv_obj_create(nullptr);
      lv_obj_clear_flag(tmp, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_style_bg_color(tmp, lv_color_black(), 0);
      lv_obj_set_style_bg_opa(tmp, LV_OPA_COVER, 0);
      lv_screen_load(tmp);
      break;
    }
  }

  for (int i = 0; i < kScreenCount; i++) {
    DestroyScreen(g_screens[i]);
  }
}

void LvglStartStockCarousel() {
  LvglStopStockCarousel();

  g_screens[0].symbol = "AAPL";
  g_screens[0].rng = 0x12345678u;
  g_screens[0].last_cents = 19234;
  g_screens[0].open_cents = 19110;
  g_screens[0].clamp_min = 18500;
  g_screens[0].clamp_max = 19999;

  g_screens[1].symbol = "TSLA";
  g_screens[1].rng = 0x87654321u;
  g_screens[1].last_cents = 24850;
  g_screens[1].open_cents = 24600;
  g_screens[1].clamp_min = 22000;
  g_screens[1].clamp_max = 28000;

  g_screens[2].symbol = "NVDA";
  g_screens[2].rng = 0x13579bdfu;
  g_screens[2].last_cents = 52010;
  g_screens[2].open_cents = 51800;
  g_screens[2].clamp_min = 48000;
  g_screens[2].clamp_max = 56000;

  g_screens[3].symbol = "MSFT";
  g_screens[3].rng = 0x2468ace0u;
  g_screens[3].last_cents = 40120;
  g_screens[3].open_cents = 39950;
  g_screens[3].clamp_min = 37000;
  g_screens[3].clamp_max = 43000;

  g_screens[4].symbol = "AMZN";
  g_screens[4].rng = 0xfedcba98u;
  g_screens[4].last_cents = 16430;
  g_screens[4].open_cents = 16320;
  g_screens[4].clamp_min = 15000;
  g_screens[4].clamp_max = 18000;

  for (int i = 0; i < kScreenCount; i++) {
    CreateScreen(g_screens[i]);
  }

  g_active_idx = 0;
  g_anim_idx = 0;
  lv_screen_load(g_screens[g_active_idx].scr);

  g_switch_timer = lv_timer_create(SwitchCb, 8000, nullptr);
}
