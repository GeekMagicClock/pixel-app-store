#include "ui/lvgl_boot_wifi_screen.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "esp_timer.h"

extern "C" {
#include "lvgl.h"
}

LV_FONT_DECLARE(lv_font_silkscreen_regular_8);

static const char *kTag = "boot_wifi_ui";

namespace {

enum class UiState : uint8_t { kNone = 0, kConnecting, kSuccess, kFailed };

static lv_obj_t *g_scr = nullptr;
static lv_obj_t *g_canvas = nullptr;
static lv_draw_buf_t *g_canvas_buf = nullptr;
static lv_font_t *g_font = nullptr;
static lv_timer_t *g_timer = nullptr;

static UiState g_state = UiState::kNone;
static uint64_t g_start_us = 0;
static uint32_t g_timeout_ms = 20000;

static char g_try_ssid[33] = {};
static char g_sta_ssid[33] = {};
static char g_sta_ip[16] = {};
static char g_ap_ssid[33] = {};
static char g_ap_ip[16] = {};

static void StopTimer() {
  if (!g_timer) return;
  lv_timer_del(g_timer);
  g_timer = nullptr;
}

static void TruncCopy(char *dst, size_t dst_sz, const char *src, size_t max_chars) {
  if (!dst || dst_sz == 0) return;
  dst[0] = '\0';
  if (!src) return;

  const size_t src_len = strlen(src);
  if (src_len <= max_chars) {
    snprintf(dst, dst_sz, "%s", src);
    return;
  }

  if (max_chars < 2) return;
  const size_t n = (max_chars < (dst_sz - 1)) ? max_chars : (dst_sz - 1);
  memcpy(dst, src, n);
  dst[n - 1] = '~';
  dst[n] = '\0';
}

static void SplitIp2Lines(const char *ip, char *line_a, size_t line_a_sz, char *line_b, size_t line_b_sz) {
  if (line_a && line_a_sz) line_a[0] = '\0';
  if (line_b && line_b_sz) line_b[0] = '\0';
  if (!ip || !*ip) return;

  // Split after the 2nd dot: "192.168." + "1.10" fits well on 64px.
  const char *dot1 = strchr(ip, '.');
  const char *dot2 = dot1 ? strchr(dot1 + 1, '.') : nullptr;
  if (!dot2) {
    if (line_a && line_a_sz) snprintf(line_a, line_a_sz, "%s", ip);
    return;
  }

  const size_t a_len = static_cast<size_t>(dot2 - ip + 1);
  if (line_a && line_a_sz) {
    const size_t n = (a_len < (line_a_sz - 1)) ? a_len : (line_a_sz - 1);
    memcpy(line_a, ip, n);
    line_a[n] = '\0';
  }
  if (line_b && line_b_sz) snprintf(line_b, line_b_sz, "%s", dot2 + 1);
}

static void DrawTextLine(lv_layer_t *layer, int y0, const char *text, lv_color_t color) {
  lv_draw_label_dsc_t dsc;
  lv_draw_label_dsc_init(&dsc);
  dsc.font = g_font;
  dsc.opa = LV_OPA_COVER;
  dsc.color = color;
  dsc.text = text ? text : "";
  dsc.align = LV_TEXT_ALIGN_LEFT;
  lv_area_t a = {0, y0, 63, y0 + 7};
  lv_draw_label(layer, &dsc, &a);
}

static int TextWidthPx(const char *text) {
  if (!g_font || !text) return 0;
  lv_point_t sz = {0, 0};
  lv_txt_get_size(&sz, text, g_font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return static_cast<int>(sz.x);
}

static void DrawMarqueeLine(lv_layer_t *layer, int y0, const char *text, lv_color_t color, uint32_t t_ms) {
  if (!text || !*text) {
    DrawTextLine(layer, y0, "", color);
    return;
  }

  // If it fits, draw normally.
  int w = TextWidthPx(text);
  // Fallback when LVGL can't measure (some runtime fonts may return 0):
  // an IPv4 like "192.168.123.234" is 15 chars and usually won't fit 64px at 8px font.
  const size_t len = strlen(text);
  if (w <= 0) {
    // Rough estimate for monospace/pixel fonts; better to over-estimate so we scroll enough.
    w = static_cast<int>(len) * 6;
  }
  const bool fits = (w > 0) ? (w <= 64) : (len <= 12);
  if (fits) {
    DrawTextLine(layer, y0, text, color);
    return;
  }

  // Scroll left with a small gap.
  static constexpr int kGapPx = 12;
  static constexpr int kSpeedPxPerSec = 28;
  const int period_px = w + kGapPx;
  const int off = (period_px <= 0) ? 0 : static_cast<int>((static_cast<uint64_t>(t_ms) * kSpeedPxPerSec / 1000) % period_px);
  const int x = -off;

  lv_draw_label_dsc_t dsc;
  lv_draw_label_dsc_init(&dsc);
  dsc.font = g_font;
  dsc.opa = LV_OPA_COVER;
  dsc.color = color;
  dsc.text = text;
  dsc.align = LV_TEXT_ALIGN_LEFT;

  // Make the label draw-area wide enough to render the whole string, while the
  // canvas layer clip will keep it inside 64x32. If the area is only 64px wide,
  // LVGL may stop early and we never see the tail while scrolling.
  lv_area_t a = {x, y0, x + w + 64, y0 + 7};
  lv_draw_label(layer, &dsc, &a);

  // Second copy for seamless wrap.
  const int x2 = x + period_px;
  if (x2 < 64) {
    lv_area_t a2 = {x2, y0, x2 + w + 64, y0 + 7};
    lv_draw_label(layer, &dsc, &a2);
  }
}

static void DrawRect(lv_draw_buf_t *buf, int x0, int y0, int x1, int y1, lv_color_t c) {
  if (!buf || !buf->data) return;
  if (x0 > x1) std::swap(x0, x1);
  if (y0 > y1) std::swap(y0, y1);
  x0 = (x0 < 0) ? 0 : x0;
  y0 = (y0 < 0) ? 0 : y0;
  x1 = (x1 > 63) ? 63 : x1;
  y1 = (y1 > 31) ? 31 : y1;

  const int stride_px = static_cast<int>(buf->header.stride / 2);
  auto *p = reinterpret_cast<uint16_t *>(buf->data);
  const uint16_t c16 = lv_color_to_u16(c);
  for (int y = y0; y <= y1; y++) {
    for (int x = x0; x <= x1; x++) {
      p[y * stride_px + x] = c16;
    }
  }
}

static void DrawFrameRect(lv_draw_buf_t *buf, int x0, int y0, int x1, int y1, lv_color_t c) {
  DrawRect(buf, x0, y0, x1, y0, c);
  DrawRect(buf, x0, y1, x1, y1, c);
  DrawRect(buf, x0, y0, x0, y1, c);
  DrawRect(buf, x1, y0, x1, y1, c);
}

static void RenderConnecting() {
  const uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
  const uint64_t elapsed_ms = (now_us - g_start_us) / 1000;
  const uint32_t timeout_ms = (g_timeout_ms < 1000) ? 1000 : g_timeout_ms;
  const uint32_t pct = (elapsed_ms >= timeout_ms) ? 100 : static_cast<uint32_t>((elapsed_ms * 100) / timeout_ms);
  const uint32_t left_ms = (elapsed_ms >= timeout_ms) ? 0 : static_cast<uint32_t>(timeout_ms - elapsed_ms);

  lv_canvas_fill_bg(g_canvas, lv_color_black(), LV_OPA_COVER);

  lv_layer_t layer;
  lv_canvas_init_layer(g_canvas, &layer);

  char ssid_short[16] = {};
  TruncCopy(ssid_short, sizeof(ssid_short), g_try_ssid[0] ? g_try_ssid : "--", 12);

  char line0[20] = {};
  snprintf(line0, sizeof(line0), "Try SSID");

  char line1[24] = {};
  snprintf(line1, sizeof(line1), "%s", ssid_short);

  char line2[20] = {};
  snprintf(line2, sizeof(line2), "%2u%% %2us", static_cast<unsigned>(pct), static_cast<unsigned>((left_ms + 999) / 1000));

  DrawTextLine(&layer, 0, line0, lv_color_make(180, 220, 255));
  DrawTextLine(&layer, 8, line1, lv_color_white());
  DrawTextLine(&layer, 16, line2, lv_color_make(200, 200, 200));

  // Progress bar: y=24..31, inside 62px.
  constexpr int kBarX0 = 1;
  constexpr int kBarX1 = 62;
  constexpr int kBarY0 = 24;
  constexpr int kBarY1 = 31;
  DrawFrameRect(g_canvas_buf, kBarX0, kBarY0, kBarX1, kBarY1, lv_color_make(100, 100, 100));

  const int inner_w = (kBarX1 - 1) - (kBarX0 + 1) + 1;
  const int fill_w = (inner_w * static_cast<int>(pct) + 50) / 100;
  if (fill_w > 0) {
    DrawRect(g_canvas_buf, kBarX0 + 1, kBarY0 + 1, (kBarX0 + 1) + (fill_w - 1), kBarY1 - 1,
             lv_color_make(80, 255, 80));
  }

  lv_canvas_finish_layer(g_canvas, &layer);
}

static void RenderSuccess() {
  lv_canvas_fill_bg(g_canvas, lv_color_black(), LV_OPA_COVER);

  lv_layer_t layer;
  lv_canvas_init_layer(g_canvas, &layer);

  char ssid_short[16] = {};
  TruncCopy(ssid_short, sizeof(ssid_short), g_sta_ssid[0] ? g_sta_ssid : "--", 12);

  char line0[24] = {};
  snprintf(line0, sizeof(line0), "WiFi OK");
  char line1[24] = {};
  snprintf(line1, sizeof(line1), "%s", ssid_short);
  char line2[24] = {};
  snprintf(line2, sizeof(line2), "LOCAL IP");

  DrawTextLine(&layer, 0, line0, lv_color_make(80, 255, 80));
  DrawTextLine(&layer, 8, line1, lv_color_white());
  DrawTextLine(&layer, 16, line2, lv_color_make(180, 220, 255));
  const uint32_t t_ms = static_cast<uint32_t>((static_cast<uint64_t>(esp_timer_get_time()) - g_start_us) / 1000);
  DrawMarqueeLine(&layer, 24, g_sta_ip[0] ? g_sta_ip : "--", lv_color_make(180, 220, 255), t_ms);

  lv_canvas_finish_layer(g_canvas, &layer);
}

static void RenderFailed() {
  lv_canvas_fill_bg(g_canvas, lv_color_black(), LV_OPA_COVER);

  lv_layer_t layer;
  lv_canvas_init_layer(g_canvas, &layer);

  char ap_short[16] = {};
  TruncCopy(ap_short, sizeof(ap_short), g_ap_ssid[0] ? g_ap_ssid : "--", 12);

  char ip_a[20] = {};
  char ip_b[20] = {};
  SplitIp2Lines(g_ap_ip[0] ? g_ap_ip : "--", ip_a, sizeof(ip_a), ip_b, sizeof(ip_b));

  char line0[24] = {};
  snprintf(line0, sizeof(line0), "AP SETUP");
  char line1[24] = {};
  snprintf(line1, sizeof(line1), "%s", ap_short);
  char line2[24] = {};
  snprintf(line2, sizeof(line2), "IP %s", ip_a[0] ? ip_a : "--");

  DrawTextLine(&layer, 0, line0, lv_color_make(255, 220, 80));
  DrawTextLine(&layer, 8, line1, lv_color_white());
  DrawTextLine(&layer, 16, line2, lv_color_make(180, 220, 255));
  DrawTextLine(&layer, 24, ip_b[0] ? ip_b : "CONNECT TO CFG", lv_color_make(180, 220, 255));

  lv_canvas_finish_layer(g_canvas, &layer);
}

static void Render() {
  if (!g_canvas || !g_canvas_buf || !g_font) return;
  switch (g_state) {
    case UiState::kConnecting: RenderConnecting(); break;
    case UiState::kSuccess: RenderSuccess(); break;
    case UiState::kFailed: RenderFailed(); break;
    default: break;
  }
}

static void TimerCb(lv_timer_t *t) {
  (void)t;
  Render();
}

static void EnsureScreen() {
  if (g_scr) return;

  g_scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(g_scr, lv_color_hex(0x000000), 0);

  g_canvas_buf = lv_draw_buf_create(64, 32, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
  if (!g_canvas_buf) {
    ESP_LOGE(kTag, "Failed to create canvas buffer");
    return;
  }

  g_canvas = lv_canvas_create(g_scr);
  lv_canvas_set_draw_buf(g_canvas, g_canvas_buf);
  lv_obj_clear_flag(g_canvas, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(g_canvas, 0, 0);
  lv_obj_set_size(g_canvas, 64, 32);
  lv_obj_center(g_canvas);
}

}  // namespace

void LvglBootWifiScreenPreloadFont() {
  if (g_font) return;
  // TinyTTF is disabled on this target to reduce heap/FD usage.
  g_font = const_cast<lv_font_t*>(&lv_font_silkscreen_regular_8);
  ESP_LOGI(kTag, "Using lv_font_silkscreen_regular_8");
}

void LvglShowBootWifiConnecting(const char *try_ssid, uint32_t timeout_ms) {
  if (!g_font) {
    LvglBootWifiScreenPreloadFont();
    if (!g_font) return;
  }

  EnsureScreen();
  if (!g_scr) return;

  StopTimer();

  g_state = UiState::kConnecting;
  g_timeout_ms = timeout_ms;
  g_start_us = static_cast<uint64_t>(esp_timer_get_time());
  snprintf(g_try_ssid, sizeof(g_try_ssid), "%s", try_ssid ? try_ssid : "");

  Render();
  lv_screen_load(g_scr);

  g_timer = lv_timer_create(TimerCb, 200, nullptr);
}

void LvglShowBootWifiSuccess(const char *sta_ssid, const char *sta_ip) {
  if (!g_font) {
    LvglBootWifiScreenPreloadFont();
    if (!g_font) return;
  }
  EnsureScreen();
  if (!g_scr) return;

  StopTimer();
  g_start_us = static_cast<uint64_t>(esp_timer_get_time());

  g_state = UiState::kSuccess;
  snprintf(g_sta_ssid, sizeof(g_sta_ssid), "%s", sta_ssid ? sta_ssid : "");
  snprintf(g_sta_ip, sizeof(g_sta_ip), "%s", sta_ip ? sta_ip : "");

  Render();
  lv_screen_load(g_scr);

  // Keep a light timer running for marquee scrolling if needed.
  g_timer = lv_timer_create(TimerCb, 120, nullptr);
}

void LvglShowBootWifiFailed(const char *ap_ssid, const char *ap_ip) {
  if (!g_font) {
    LvglBootWifiScreenPreloadFont();
    if (!g_font) return;
  }
  EnsureScreen();
  if (!g_scr) return;

  StopTimer();
  g_start_us = static_cast<uint64_t>(esp_timer_get_time());

  g_state = UiState::kFailed;
  snprintf(g_ap_ssid, sizeof(g_ap_ssid), "%s", ap_ssid ? ap_ssid : "");
  snprintf(g_ap_ip, sizeof(g_ap_ip), "%s", ap_ip ? ap_ip : "");

  Render();
  lv_screen_load(g_scr);

  // Keep refresh timer for any future animations.
  g_timer = lv_timer_create(TimerCb, 250, nullptr);
}

void LvglStopBootWifiScreen() {
  StopTimer();

  g_font = nullptr;

  if (g_canvas_buf) {
    lv_draw_buf_destroy(g_canvas_buf);
    g_canvas_buf = nullptr;
  }

  if (g_scr) {
    lv_obj_delete(g_scr);
    g_scr = nullptr;
  }

  g_canvas = nullptr;
  g_state = UiState::kNone;
}
