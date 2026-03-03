#include "ui/lvgl_lua_app_screen.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"

extern "C" {
#include "lvgl.h"
}

LV_FONT_DECLARE(lv_font_silkscreen_regular_8);

#include "cJSON.h"

#include "app/lua_app_runtime.h"

static const char* kTag = "lua_screen";

namespace {

static lv_font_t* g_font = nullptr;
static constexpr uint32_t kTickMsText = 200;
static constexpr uint32_t kTickMsFb = 33;

enum class WidgetType {
  kText,
  kImage,
};

struct WidgetSpec {
  WidgetType type = WidgetType::kText;
  int x = 0;
  int y = 0;
  int w = 64;
  int h = 8;

  // Text
  lv_color_t color = lv_color_white();
  lv_text_align_t align = LV_TEXT_ALIGN_LEFT;
  std::string font;  // e.g. "Silkscreen-Regular@8"
  std::string bind;  // e.g. "line1"
  std::string color_bind;  // e.g. "line3_color"

  // Image
  std::string src;       // static LVGL image src (e.g. "S:/littlefs/icon/apple-24.png")
  std::string src_bind;  // dynamic src key (e.g. "icon_src")
};

struct ScreenState {
  lv_obj_t* scr = nullptr;
  lv_obj_t* canvas = nullptr;
  lv_draw_buf_t* canvas_buf = nullptr;
  lv_timer_t* timer = nullptr;
  std::unique_ptr<LuaAppRuntime> app;
  std::vector<std::string> lines;
  std::string app_dir;
  uint32_t last_ms = 0;
  bool first_frame_drawn = false;
  bool fb_mode = false;
  std::vector<WidgetSpec> widgets;

  std::vector<std::string> bind_keys;
  std::vector<std::string> bind_values;
};

static ScreenState g_state;

static const lv_font_t* FontOrFallback() {
  if (g_font) return g_font;
  return &lv_font_silkscreen_regular_8;
}

static bool ReadFileToString(const std::string& path, std::string* out) {
  if (out) out->clear();
  if (!out) return false;
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return false;
  std::string s;
  char buf[512];
  while (true) {
    const size_t n = fread(buf, 1, sizeof(buf), f);
    if (n > 0) s.append(buf, n);
    if (n < sizeof(buf)) break;
  }
  fclose(f);
  *out = std::move(s);
  return true;
}

static void EnsureFontLoaded() {
  (void)g_font;
}

static void StopTimer() {
  if (!g_state.timer) return;
  lv_timer_del(g_state.timer);
  g_state.timer = nullptr;
}

static void StopScreen() {
  StopTimer();

  if (g_state.canvas_buf) {
    lv_draw_buf_destroy(g_state.canvas_buf);
    g_state.canvas_buf = nullptr;
  }

  if (g_state.scr && lv_obj_is_valid(g_state.scr)) {
    lv_obj_delete(g_state.scr);
  }

  g_state.scr = nullptr;
  g_state.canvas = nullptr;
  g_state.lines.clear();
  g_state.app_dir.clear();
  g_state.last_ms = 0;
  g_state.first_frame_drawn = false;
  g_state.fb_mode = false;
  g_state.widgets.clear();
  g_state.bind_keys.clear();
  g_state.bind_values.clear();
  if (g_state.app) {
    g_state.app->FullGcNow();
    ESP_LOGI(kTag,
             "app exit full gc free=%u min=%u",
             static_cast<unsigned>(esp_get_free_heap_size()),
             static_cast<unsigned>(esp_get_minimum_free_heap_size()));
  }
  g_state.app.reset();
}

static bool EnsureAppLoaded(const std::string& app_dir) {
  if (g_state.app_dir == app_dir && !g_state.app_dir.empty()) return true;

  g_state.app_dir = app_dir;
  constexpr int kMaxAttempts = 3;
  for (int attempt = 1; attempt <= kMaxAttempts; attempt++) {
    g_state.app = std::make_unique<LuaAppRuntime>();
    const bool ok = g_state.app->LoadFromDir(app_dir);
    if (ok) {
      ESP_LOGI(kTag,
               "loaded %s (attempt=%d free=%u min=%u)",
               app_dir.c_str(),
               attempt,
               static_cast<unsigned>(esp_get_free_heap_size()),
               static_cast<unsigned>(esp_get_minimum_free_heap_size()));
      g_state.fb_mode = g_state.app->SupportsFrameBuffer();
      if (g_state.fb_mode) ESP_LOGI(kTag, "app framebuffer mode enabled (%s)", app_dir.c_str());
      return true;
    }

    const char* err = g_state.app->last_error();
    ESP_LOGE(kTag,
             "load failed dir=%s attempt=%d/%d err=%s free=%u min=%u",
             app_dir.c_str(),
             attempt,
             kMaxAttempts,
             err ? err : "",
             static_cast<unsigned>(esp_get_free_heap_size()),
             static_cast<unsigned>(esp_get_minimum_free_heap_size()));

    // OOM while switching can be transient; retry after a short delay.
    if (err && strstr(err, "not enough memory") && attempt < kMaxAttempts) {
      vTaskDelay(pdMS_TO_TICKS(150));
      continue;
    }
    break;
  }

  g_state.fb_mode = false;
  return false;
}

static void DrawLine(lv_layer_t* layer, int row, const char* text, lv_color_t color) {
  constexpr int kRowH = 8;
  const int y0 = row * kRowH;
  const int y1 = y0 + kRowH - 1;

  lv_draw_label_dsc_t dsc;
  lv_draw_label_dsc_init(&dsc);
  dsc.font = FontOrFallback();
  dsc.opa = LV_OPA_COVER;
  dsc.color = color;
  dsc.text = text ? text : "";
  dsc.align = LV_TEXT_ALIGN_LEFT;

  lv_area_t a = {0, y0, 63, y1};
  lv_draw_label(layer, &dsc, &a);
}

static bool ParseHexColor(const char* s, lv_color_t* out) {
  if (!out) return false;
  *out = lv_color_white();
  if (!s || !*s) return false;
  if (s[0] != '#') return false;
  unsigned v = 0;
  if (sscanf(s + 1, "%06x", &v) != 1) return false;
  const uint8_t r = (v >> 16) & 0xFF;
  const uint8_t g = (v >> 8) & 0xFF;
  const uint8_t b = (v >> 0) & 0xFF;
  *out = lv_color_make(r, g, b);
  return true;
}

static lv_text_align_t ParseAlign(const char* s) {
  if (!s) return LV_TEXT_ALIGN_LEFT;
  if (strcmp(s, "right") == 0) return LV_TEXT_ALIGN_RIGHT;
  if (strcmp(s, "center") == 0) return LV_TEXT_ALIGN_CENTER;
  return LV_TEXT_ALIGN_LEFT;
}

static void LoadWidgetsFromUiJson(const std::string& app_dir) {
  g_state.widgets.clear();
  g_state.bind_keys.clear();
  g_state.bind_values.clear();

  std::string json;
  const std::string path = app_dir + "/ui.json";
  if (!ReadFileToString(path, &json)) {
    ESP_LOGW(kTag, "ui.json missing (%s), fallback to default layout", path.c_str());
    return;
  }

  cJSON* root = cJSON_ParseWithLength(json.c_str(), json.size());
  if (!root) {
    ESP_LOGW(kTag, "ui.json parse failed (%s)", path.c_str());
    return;
  }

  cJSON* widgets = cJSON_GetObjectItem(root, "widgets");
  if (!cJSON_IsArray(widgets)) {
    cJSON_Delete(root);
    ESP_LOGW(kTag, "ui.json invalid widgets[] (%s)", path.c_str());
    return;
  }

  const int n = cJSON_GetArraySize(widgets);
  for (int i = 0; i < n; i++) {
    cJSON* w = cJSON_GetArrayItem(widgets, i);
    if (!cJSON_IsObject(w)) continue;

    const cJSON* type = cJSON_GetObjectItem(w, "type");
    if (!cJSON_IsString(type)) continue;

    WidgetSpec spec;

    const cJSON* x = cJSON_GetObjectItem(w, "x");
    const cJSON* y = cJSON_GetObjectItem(w, "y");
    const cJSON* ww = cJSON_GetObjectItem(w, "w");
    const cJSON* hh = cJSON_GetObjectItem(w, "h");
    if (cJSON_IsNumber(x)) spec.x = static_cast<int>(x->valuedouble);
    if (cJSON_IsNumber(y)) spec.y = static_cast<int>(y->valuedouble);
    if (cJSON_IsNumber(ww)) spec.w = static_cast<int>(ww->valuedouble);
    if (cJSON_IsNumber(hh)) spec.h = static_cast<int>(hh->valuedouble);

    if (strcmp(type->valuestring, "text") == 0) {
      spec.type = WidgetType::kText;

      const cJSON* bind = cJSON_GetObjectItem(w, "bind");
      if (cJSON_IsString(bind)) spec.bind = bind->valuestring;

      const cJSON* color_bind = cJSON_GetObjectItem(w, "color_bind");
      if (cJSON_IsString(color_bind)) spec.color_bind = color_bind->valuestring;

      const cJSON* font = cJSON_GetObjectItem(w, "font");
      if (cJSON_IsString(font)) spec.font = font->valuestring;

      const cJSON* color = cJSON_GetObjectItem(w, "color");
      if (cJSON_IsString(color)) (void)ParseHexColor(color->valuestring, &spec.color);

      const cJSON* align = cJSON_GetObjectItem(w, "align");
      if (cJSON_IsString(align)) spec.align = ParseAlign(align->valuestring);

      if (!spec.bind.empty()) g_state.widgets.push_back(std::move(spec));
    } else if (strcmp(type->valuestring, "image") == 0) {
      spec.type = WidgetType::kImage;

      const cJSON* src = cJSON_GetObjectItem(w, "src");
      if (cJSON_IsString(src)) spec.src = src->valuestring;

      const cJSON* src_bind = cJSON_GetObjectItem(w, "src_bind");
      if (cJSON_IsString(src_bind)) spec.src_bind = src_bind->valuestring;

      if (!spec.src.empty() || !spec.src_bind.empty()) g_state.widgets.push_back(std::move(spec));
    }
  }

  cJSON_Delete(root);
}

static int GetOrAddKeyIndex(const std::string& key) {
  if (key.empty()) return -1;
  for (int i = 0; i < static_cast<int>(g_state.bind_keys.size()); i++) {
    if (g_state.bind_keys[i] == key) return i;
  }
  g_state.bind_keys.push_back(key);
  return static_cast<int>(g_state.bind_keys.size()) - 1;
}

static int FindKeyIndex(const std::string& key) {
  if (key.empty()) return -1;
  for (int i = 0; i < static_cast<int>(g_state.bind_keys.size()); i++) {
    if (g_state.bind_keys[i] == key) return i;
  }
  return -1;
}

static void PrepareBindKeys() {
  g_state.bind_keys.clear();
  for (const auto& w : g_state.widgets) {
    if (w.type == WidgetType::kText) {
      (void)GetOrAddKeyIndex(w.bind);
      (void)GetOrAddKeyIndex(w.color_bind);
    } else if (w.type == WidgetType::kImage) {
      (void)GetOrAddKeyIndex(w.src_bind);
    }
  }
}

static void DrawTextWidget(lv_layer_t* layer, const WidgetSpec& w, const char* text) {
  lv_draw_label_dsc_t dsc;
  lv_draw_label_dsc_init(&dsc);
  // V1: only support Silkscreen-Regular@8 or fallback.
  dsc.font = FontOrFallback();
  dsc.opa = LV_OPA_COVER;
  dsc.color = w.color;
  dsc.text = text ? text : "";
  dsc.align = w.align;

  lv_area_t a = {
      static_cast<lv_coord_t>(w.x),
      static_cast<lv_coord_t>(w.y),
      static_cast<lv_coord_t>(w.x + w.w - 1),
      static_cast<lv_coord_t>(w.y + w.h - 1),
  };
  lv_draw_label(layer, &dsc, &a);
}

static void DrawImageWidget(lv_layer_t* layer, const WidgetSpec& w, const char* src) {
  if (!layer) return;
  if (!src || !*src) return;

  lv_draw_image_dsc_t imgd;
  lv_draw_image_dsc_init(&imgd);
  imgd.src = src;
  imgd.opa = LV_OPA_COVER;

  lv_area_t a = {
      static_cast<lv_coord_t>(w.x),
      static_cast<lv_coord_t>(w.y),
      static_cast<lv_coord_t>(w.x + w.w - 1),
      static_cast<lv_coord_t>(w.y + w.h - 1),
  };
  lv_draw_image(layer, &imgd, &a);
}

static void RenderFrame(uint32_t dt_ms) {
  if (!g_state.scr || !g_state.canvas || !g_state.canvas_buf) return;
  if (!lv_obj_is_valid(g_state.scr) || !lv_obj_is_valid(g_state.canvas)) return;

  if (!g_state.app) {
    g_state.lines = {"Lua error", "runtime missing", "", ""};
  } else {
    g_state.app->Tick(dt_ms);
    if (g_state.fb_mode) {
      if (!g_state.canvas_buf->data || g_state.canvas_buf->header.w != 64 || g_state.canvas_buf->header.h != 32 ||
          g_state.canvas_buf->header.cf != LV_COLOR_FORMAT_RGB565 || g_state.canvas_buf->header.stride < 64 * 2) {
        g_state.lines = {"Lua error", "bad draw buf", "", ""};
      } else {
        lv_layer_t layer;
        lv_canvas_init_layer(g_state.canvas, &layer);
        const bool ok = g_state.app->RenderFrameBufferTo(
            64,
            32,
            g_state.canvas_buf->data,
            g_state.canvas_buf->header.stride,
            &layer,
            FontOrFallback());
        lv_canvas_finish_layer(g_state.canvas, &layer);
        g_state.app->ClearFrameBufferTemps();

        if (!ok) {
          g_state.lines = {"Lua error", g_state.app->last_error(), "", ""};
        } else {
          lv_obj_invalidate(g_state.canvas);
          return;
        }
      }
    } else if (!g_state.widgets.empty()) {
      g_state.bind_values.clear();
      if (!g_state.app->RenderBinds(g_state.bind_keys, &g_state.bind_values)) {
        g_state.lines = {"Lua error", g_state.app->last_error(), "", ""};
      }
    } else {
      g_state.lines.clear();
      if (!g_state.app->Render(&g_state.lines)) {
        g_state.lines = {"Lua error", g_state.app->last_error(), "", ""};
      }
    }
  }

  bool all_empty = true;
  for (const auto& s : g_state.lines) {
    if (!s.empty()) {
      all_empty = false;
      break;
    }
  }
  if (all_empty) {
    g_state.lines = {"(empty render)", g_state.app_dir, "", ""};
  }

  lv_canvas_fill_bg(g_state.canvas, lv_color_black(), LV_OPA_COVER);

  lv_layer_t layer;
  lv_canvas_init_layer(g_state.canvas, &layer);

  if (!g_state.widgets.empty()) {
    for (const auto& w : g_state.widgets) {
      if (w.type == WidgetType::kText) {
        const int t_idx = FindKeyIndex(w.bind);
        const int c_idx = FindKeyIndex(w.color_bind);

        const char* txt = "";
        if (t_idx >= 0 && t_idx < static_cast<int>(g_state.bind_values.size())) {
          txt = g_state.bind_values[t_idx].c_str();
        }

        WidgetSpec draw_spec = w;
        if (c_idx >= 0 && c_idx < static_cast<int>(g_state.bind_values.size())) {
          lv_color_t c;
          if (ParseHexColor(g_state.bind_values[c_idx].c_str(), &c)) {
            draw_spec.color = c;
          }
        }

        DrawTextWidget(&layer, draw_spec, txt);
      } else if (w.type == WidgetType::kImage) {
        const char* src = w.src.c_str();
        if (w.src.empty()) src = "";

        if (!w.src_bind.empty()) {
          const int s_idx = FindKeyIndex(w.src_bind);
          if (s_idx >= 0 && s_idx < static_cast<int>(g_state.bind_values.size())) {
            src = g_state.bind_values[s_idx].c_str();
          }
        }

        DrawImageWidget(&layer, w, src);
      }
    }
  } else {
    for (int i = 0; i < 4; i++) {
      static const std::string kEmpty;
      const std::string& s = (i < static_cast<int>(g_state.lines.size())) ? g_state.lines[i] : kEmpty;
      DrawLine(&layer, i, s.c_str(), lv_color_white());
    }
  }

  lv_canvas_finish_layer(g_state.canvas, &layer);

  // Ensure LVGL refreshes this object even though we're drawing to the buffer directly.
  lv_obj_invalidate(g_state.canvas);
}

static void TickCb(lv_timer_t* t) {
  (void)t;
  if (!g_state.scr || !g_state.canvas || !g_state.canvas_buf) return;
  if (!lv_obj_is_valid(g_state.scr) || !lv_obj_is_valid(g_state.canvas)) return;
  if (lv_screen_active() != g_state.scr) return;

  const uint32_t now = lv_tick_get();
  const uint32_t dt = (g_state.last_ms == 0) ? 0 : (now - g_state.last_ms);
  g_state.last_ms = now;

  RenderFrame(dt);
}

static void ShowApp(const std::string& app_dir) {
  StopScreen();
  if (!EnsureAppLoaded(app_dir)) return;
  EnsureFontLoaded();
  if (!g_state.fb_mode) {
    LoadWidgetsFromUiJson(app_dir);
    PrepareBindKeys();
  } else {
    g_state.widgets.clear();
    g_state.bind_keys.clear();
    g_state.bind_values.clear();
  }

  g_state.scr = lv_obj_create(nullptr);
  lv_obj_clear_flag(g_state.scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(g_state.scr, 0, 0);
  lv_obj_set_style_border_width(g_state.scr, 0, 0);
  lv_obj_set_style_bg_color(g_state.scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g_state.scr, LV_OPA_COVER, 0);

  g_state.canvas_buf = lv_draw_buf_create(64, 32, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
  if (!g_state.canvas_buf) {
    ESP_LOGE(kTag, "Failed to create canvas buffer");
    StopScreen();
    return;
  }

  g_state.canvas = lv_canvas_create(g_state.scr);
  lv_canvas_set_draw_buf(g_state.canvas, g_state.canvas_buf);
  lv_obj_clear_flag(g_state.canvas, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(g_state.canvas, 0, 0);
  lv_obj_set_size(g_state.canvas, 64, 32);
  lv_obj_center(g_state.canvas);

  g_state.last_ms = 0;
  // Draw a frame BEFORE loading the screen, to avoid a "garbage flash" from an uninitialized draw buffer.
  RenderFrame(0);
  g_state.first_frame_drawn = true;
  g_state.timer = lv_timer_create(TickCb, g_state.fb_mode ? kTickMsFb : kTickMsText, nullptr);

  lv_screen_load(g_state.scr);
  ESP_LOGI(kTag, "screen shown (%s)", app_dir.c_str());
}

}  // namespace

void LvglShowLuaWeatherAppScreen() { ShowApp("/littlefs/apps/demo_weather"); }

void LvglStopLuaWeatherAppScreen() { StopScreen(); }

void LvglShowLuaStocksAppScreen() { ShowApp("/littlefs/apps/demo_stocks"); }

void LvglStopLuaStocksAppScreen() { StopScreen(); }

void LvglShowLuaStockScreen3AppScreen() { ShowApp("/littlefs/apps/stock_screen3"); }

void LvglStopLuaStockScreen3AppScreen() { StopScreen(); }

void LvglShowLuaFbTestAppScreen() { ShowApp("/littlefs/apps/fb_test"); }

void LvglStopLuaFbTestAppScreen() { StopScreen(); }

void LvglShowLuaOwmWeatherAppScreen() { ShowApp("/littlefs/apps/weather_owm"); }

void LvglStopLuaOwmWeatherAppScreen() { StopScreen(); }

void LvglShowLuaAppDirScreen(const char* app_dir) {
  if (!app_dir || !*app_dir) return;
  ShowApp(std::string(app_dir));
}

void LvglStopLuaAppDirScreen() { StopScreen(); }

bool LvglCaptureLuaAppFrameRgb565(uint16_t* out_pixels, size_t pixel_count, size_t* out_width, size_t* out_height) {
  if (out_width) *out_width = 64;
  if (out_height) *out_height = 32;
  if (!out_pixels || pixel_count < (64u * 32u)) return false;
  if (!g_state.canvas_buf || !g_state.canvas_buf->data) return false;
  if (g_state.canvas_buf->header.w != 64 || g_state.canvas_buf->header.h != 32) return false;
  if (g_state.canvas_buf->header.cf != LV_COLOR_FORMAT_RGB565) return false;
  if (g_state.canvas_buf->header.stride < 64 * 2) return false;

  const uint8_t* src = static_cast<const uint8_t*>(g_state.canvas_buf->data);
  const size_t stride = static_cast<size_t>(g_state.canvas_buf->header.stride);
  for (size_t y = 0; y < 32; y++) {
    const uint16_t* row = reinterpret_cast<const uint16_t*>(src + y * stride);
    memcpy(out_pixels + (y * 64), row, 64 * sizeof(uint16_t));
  }
  return true;
}
