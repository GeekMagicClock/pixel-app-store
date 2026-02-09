#include "ui/lvgl_stock_screen7.h"

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_timer.h"
#include "esp_wifi.h"

extern "C" {
#include "lvgl.h"
}

static const char *kTag = "stock_screen7";

namespace {

static lv_obj_t *g_scr = nullptr;
static lv_obj_t *g_canvas = nullptr;
static lv_draw_buf_t *g_canvas_buf = nullptr;
static lv_font_t *g_font = nullptr;
static lv_timer_t *g_refresh_timer = nullptr;

static void StopRefreshTimer() {
  if (!g_refresh_timer) return;
  lv_timer_del(g_refresh_timer);
  g_refresh_timer = nullptr;
}

static void FormatUptime(char *out, size_t out_sz) {
  if (!out || out_sz == 0) return;

  const uint64_t sec_total = static_cast<uint64_t>(esp_timer_get_time() / 1000000);
  uint64_t sec = sec_total;

  const uint32_t days = static_cast<uint32_t>(sec / 86400);
  sec %= 86400;
  const uint32_t hours = static_cast<uint32_t>(sec / 3600);
  sec %= 3600;
  const uint32_t mins = static_cast<uint32_t>(sec / 60);
  const uint32_t secs = static_cast<uint32_t>(sec % 60);

  if (days > 0) {
    // "UP 1d02:03" (d hh:mm)
    snprintf(out, out_sz, "UP %" PRIu32 "d%02" PRIu32 ":%02" PRIu32, days, hours, mins);
  } else {
    // "UP 12:34:56"
    snprintf(out, out_sz, "UP %02" PRIu32 ":%02" PRIu32 ":%02" PRIu32, hours, mins, secs);
  }
}

static esp_netif_t *FindNetifIfKeyContains(const char *needle) {
  if (!needle || !*needle) return nullptr;
  for (esp_netif_t *n = esp_netif_next(nullptr); n; n = esp_netif_next(n)) {
    const char *key = esp_netif_get_ifkey(n);
    if (key && strstr(key, needle)) return n;
  }
  return nullptr;
}

static bool GetIpForNetif(esp_netif_t *netif, char *out_ip, size_t out_ip_sz) {
  if (out_ip && out_ip_sz) out_ip[0] = '\0';
  if (!netif || !out_ip || out_ip_sz == 0) return false;

  esp_netif_ip_info_t ip_info = {};
  if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) return false;
  if (ip_info.ip.addr == 0) return false;

  snprintf(out_ip, out_ip_sz, IPSTR, IP2STR(&ip_info.ip));
  return true;
}

static bool GetAnyIp(char *out_ip, size_t out_ip_sz) {
  if (out_ip && out_ip_sz) out_ip[0] = '\0';
  if (!out_ip || out_ip_sz == 0) return false;

  for (esp_netif_t *n = esp_netif_next(nullptr); n; n = esp_netif_next(n)) {
    if (GetIpForNetif(n, out_ip, out_ip_sz)) return true;
  }
  return false;
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

static bool GetStaSsid(char *out_ssid, size_t out_ssid_sz, bool *out_connected) {
  if (out_ssid && out_ssid_sz) out_ssid[0] = '\0';
  if (out_connected) *out_connected = false;
  if (!out_ssid || out_ssid_sz == 0) return false;

  wifi_ap_record_t ap = {};
  const esp_err_t ap_ret = esp_wifi_sta_get_ap_info(&ap);
  if (ap_ret == ESP_OK && ap.ssid[0]) {
    if (out_connected) *out_connected = true;
    snprintf(out_ssid, out_ssid_sz, "%s", reinterpret_cast<const char *>(ap.ssid));
    return true;
  }

  wifi_config_t cfg = {};
  const esp_err_t cfg_ret = esp_wifi_get_config(WIFI_IF_STA, &cfg);
  if (cfg_ret == ESP_OK && cfg.sta.ssid[0]) {
    snprintf(out_ssid, out_ssid_sz, "%s", reinterpret_cast<const char *>(cfg.sta.ssid));
    return true;
  }

  return false;
}

static bool GetApSsid(char *out_ssid, size_t out_ssid_sz) {
  if (out_ssid && out_ssid_sz) out_ssid[0] = '\0';
  if (!out_ssid || out_ssid_sz == 0) return false;

  wifi_config_t cfg = {};
  const esp_err_t cfg_ret = esp_wifi_get_config(WIFI_IF_AP, &cfg);
  if (cfg_ret == ESP_OK && cfg.ap.ssid[0]) {
    snprintf(out_ssid, out_ssid_sz, "%s", reinterpret_cast<const char *>(cfg.ap.ssid));
    return true;
  }

  return false;
}

static void DrawRow(lv_layer_t *layer, int row, const char *text, lv_color_t color) {
  constexpr int kRowH = 8;
  const int y0 = row * kRowH;
  const int y1 = y0 + kRowH - 1;

  lv_draw_label_dsc_t dsc;
  lv_draw_label_dsc_init(&dsc);
  dsc.font = g_font;
  dsc.opa = LV_OPA_COVER;
  dsc.color = color;
  dsc.text = text ? text : "";
  dsc.align = LV_TEXT_ALIGN_LEFT;

  lv_area_t a = {0, y0, 63, y1};
  lv_draw_label(layer, &dsc, &a);
}

static void Render() {
  if (!g_canvas || !g_canvas_buf || !g_font) return;

  lv_canvas_fill_bg(g_canvas, lv_color_black(), LV_OPA_COVER);

  lv_layer_t layer;
  lv_canvas_init_layer(g_canvas, &layer);

  char uptime[20] = {};
  FormatUptime(uptime, sizeof(uptime));

  char mode_line[24] = {};
  char ssid_line[32] = {};
  char ip_line[24] = {};
  bool online = false;

  wifi_mode_t wifi_mode = WIFI_MODE_NULL;
  const esp_err_t mode_ret = esp_wifi_get_mode(&wifi_mode);
  const bool has_sta = (mode_ret == ESP_OK) && ((wifi_mode == WIFI_MODE_STA) || (wifi_mode == WIFI_MODE_APSTA));
  const bool has_ap = (mode_ret == ESP_OK) && ((wifi_mode == WIFI_MODE_AP) || (wifi_mode == WIFI_MODE_APSTA));

  if (has_sta) {
    bool sta_connected = false;
    char sta_ssid[33] = {};
    (void)GetStaSsid(sta_ssid, sizeof(sta_ssid), &sta_connected);

    esp_netif_t *sta_netif = FindNetifIfKeyContains("WIFI_STA");
    char ip[16] = {};
    const bool ip_ok = GetIpForNetif(sta_netif, ip, sizeof(ip));
    online = sta_connected && ip_ok;

    snprintf(mode_line, sizeof(mode_line), "STA %s", online ? "ONLINE" : "OFFLINE");

    char ssid_short[16] = {};
    if (sta_ssid[0]) {
      TruncCopy(ssid_short, sizeof(ssid_short), sta_ssid, 12);
      snprintf(ssid_line, sizeof(ssid_line), "SSID %s", ssid_short);
    } else {
      snprintf(ssid_line, sizeof(ssid_line), "SSID --");
    }

    snprintf(ip_line, sizeof(ip_line), "%s", ip_ok ? ip : "--");
  } else if (has_ap) {
    char ap_ssid[33] = {};
    (void)GetApSsid(ap_ssid, sizeof(ap_ssid));

    esp_netif_t *ap_netif = FindNetifIfKeyContains("WIFI_AP");
    char ip[16] = {};
    const bool ip_ok = GetIpForNetif(ap_netif, ip, sizeof(ip));
    online = ip_ok;

    snprintf(mode_line, sizeof(mode_line), "AP %s", online ? "ONLINE" : "OFFLINE");

    char ssid_short[16] = {};
    if (ap_ssid[0]) {
      TruncCopy(ssid_short, sizeof(ssid_short), ap_ssid, 12);
      snprintf(ssid_line, sizeof(ssid_line), "SSID %s", ssid_short);
    } else {
      snprintf(ssid_line, sizeof(ssid_line), "SSID --");
    }

    snprintf(ip_line, sizeof(ip_line), "%s", ip_ok ? ip : "--");
  } else {
    char ip[16] = {};
    const bool ip_ok = GetAnyIp(ip, sizeof(ip));
    online = ip_ok;
    snprintf(mode_line, sizeof(mode_line), "NET %s", online ? "ONLINE" : "OFFLINE");
    snprintf(ssid_line, sizeof(ssid_line), "SSID --");
    snprintf(ip_line, sizeof(ip_line), "%s", online ? ip : "--");
  }

  DrawRow(&layer, 0, uptime, lv_color_white());
  DrawRow(&layer, 1, mode_line, online ? lv_color_make(80, 255, 80) : lv_color_make(255, 80, 80));
  DrawRow(&layer, 2, ssid_line, lv_color_make(180, 220, 255));
  DrawRow(&layer, 3, ip_line, lv_color_make(180, 220, 255));

  lv_canvas_finish_layer(g_canvas, &layer);
}

static void RefreshCb(lv_timer_t *t) {
  (void)t;
  Render();
}

}  // namespace

void LvglStockScreen7PreloadFont() {
  if (g_font) return;

  ESP_LOGI(kTag, "Preloading Silkscreen-Regular font...");
  g_font = lv_tiny_ttf_create_file_ex("S:/littlefs/fonts/Silkscreen-Regular.ttf", 8, LV_FONT_KERNING_NONE, 64);
  if (!g_font) {
    ESP_LOGE(kTag, "Failed to preload Silkscreen-Regular.ttf");
  }
}

void LvglShowStockScreen7() {
  ESP_LOGI(kTag, "Creating screen7 (uptime+net)");

  if (!g_font) {
    LvglStockScreen7PreloadFont();
    if (!g_font) return;
  }

  StopRefreshTimer();

  g_scr = lv_obj_create(nullptr);
  // Dark green background for easy identification.
  lv_obj_set_style_bg_color(g_scr, lv_color_hex(0x003000), 0);

  g_canvas_buf = lv_draw_buf_create(64, 32, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
  if (!g_canvas_buf) {
    ESP_LOGE(kTag, "Failed to create canvas buffer");
    return;
  }

  g_canvas = lv_canvas_create(g_scr);
  lv_canvas_set_draw_buf(g_canvas, g_canvas_buf);
  lv_obj_clear_flag(g_canvas, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(g_canvas, 0, 0);
  lv_obj_center(g_canvas);

  Render();
  lv_screen_load(g_scr);

  g_refresh_timer = lv_timer_create(RefreshCb, 1000, nullptr);
}

void LvglStopStockScreen7() {
  StopRefreshTimer();

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

  ESP_LOGI(kTag, "screen7 stopped");
}
