#include "ui/lvgl_lua_app_screen.h"

#include "ui/lvgl_hub75_port.h"
#include "ui/lvgl_mem_utils.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_system.h"

extern "C" {
#include "lvgl.h"
}

LV_FONT_DECLARE(lv_font_silkscreen_regular_8);

#include "cJSON.h"

#include "app/lua_app_runtime.h"

static const char* kTag = "app_screen";

namespace {

static lv_font_t* g_font = nullptr;
static constexpr uint32_t kTickMsText = 200;
static constexpr uint32_t kTickMsFb = 33;
static constexpr uint32_t kAppNameOverlayMs = 2000;

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
  lv_obj_t* root = nullptr;
  lv_obj_t* canvas = nullptr;
  lv_obj_t* ota_overlay = nullptr;
  lv_obj_t* ota_title = nullptr;
  lv_obj_t* ota_percent = nullptr;
  lv_obj_t* ota_bar = nullptr;
  lv_obj_t* app_name_overlay = nullptr;
  uint32_t app_name_overlay_until_ms = 0;
  lv_draw_buf_t* canvas_buf = nullptr;
  lv_timer_t* timer = nullptr;
  TaskHandle_t fb_task = nullptr;
  SemaphoreHandle_t fb_mutex = nullptr;
  std::unique_ptr<LuaAppRuntime> app;
  std::vector<std::string> lines;
  std::string app_dir;
  uint32_t last_ms = 0;
  bool first_frame_drawn = false;
  bool fb_mode = false;
  bool fb_request_pending = false;
  bool fb_buffer_ready = false;
  bool fb_faulted = false;
  uint32_t fb_request_dt_ms = 0;
  std::string fb_error;
  std::vector<WidgetSpec> widgets;

  std::vector<std::string> bind_keys;
  std::vector<std::string> bind_values;
  std::vector<std::string> prev_bind_values;
  uint8_t* fb_front = nullptr;
  uint8_t* fb_back = nullptr;
  size_t fb_bytes = 0;
  uint32_t fb_stride = 0;
};

static ScreenState g_state;
static constexpr BaseType_t kLuaFbTaskCore = 1;
static portMUX_TYPE g_current_app_mux = portMUX_INITIALIZER_UNLOCKED;
static char g_current_app_id[64] = {};
static char g_current_app_dir[96] = {};

static void SetCurrentAppInfo(const char* app_dir) {
  char next_id[sizeof(g_current_app_id)] = {};
  char next_dir[sizeof(g_current_app_dir)] = {};
  if (app_dir && *app_dir) {
    snprintf(next_dir, sizeof(next_dir), "%s", app_dir);
    const char* slash = strrchr(app_dir, '/');
    const char* id = (slash && slash[1]) ? (slash + 1) : app_dir;
    if (id && *id) snprintf(next_id, sizeof(next_id), "%s", id);
  }
  portENTER_CRITICAL(&g_current_app_mux);
  memcpy(g_current_app_id, next_id, sizeof(g_current_app_id));
  memcpy(g_current_app_dir, next_dir, sizeof(g_current_app_dir));
  portEXIT_CRITICAL(&g_current_app_mux);
}

enum class OtaOverlayPhase : uint8_t {
  kIdle = 0,
  kUploading = 1,
  kFinalizing = 2,
  kFailed = 3,
};

static std::atomic<uint8_t> g_ota_phase{static_cast<uint8_t>(OtaOverlayPhase::kIdle)};
static std::atomic<uint32_t> g_ota_written_bytes{0};
static std::atomic<uint32_t> g_ota_total_bytes{0};

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

static std::string ReadAppDisplayName(const std::string& app_dir) {
  std::string json;
  if (!ReadFileToString(app_dir + "/manifest.json", &json) || json.empty()) return {};
  cJSON* root = cJSON_ParseWithLength(json.c_str(), json.size());
  if (!root) return {};
  std::string out;
  cJSON* name = cJSON_GetObjectItem(root, "name");
  if (cJSON_IsString(name) && name->valuestring && name->valuestring[0]) out = name->valuestring;
  cJSON_Delete(root);
  return out;
}

static std::string SplitNameForTwoLines(const std::string& name) {
  if (name.empty()) return name;
  const size_t n = name.size();
  if (n <= 10) return name;
  if (name.find(' ') == std::string::npos) return name;
  size_t split = n / 2;
  size_t best = std::string::npos;
  int best_dist = 1 << 30;
  for (size_t i = 1; i + 1 < n; i++) {
    if (name[i] == ' ') {
      const int dist = std::abs(static_cast<int>(i) - static_cast<int>(split));
      if (dist < best_dist) {
        best_dist = dist;
        best = i;
      }
    }
  }
  if (best == std::string::npos) return name;
  std::string left = name.substr(0, best);
  std::string right = name.substr(best + 1);
  while (!left.empty() && left.back() == ' ') left.pop_back();
  while (!right.empty() && right.front() == ' ') right.erase(right.begin());
  if (left.empty() || right.empty()) return name;
  return left + "\n" + right;
}

static void EnsureFontLoaded() {
  (void)g_font;
}

static void FreeFrameBuffers() {
  if (g_state.fb_front) {
    LvglFreePreferPsram(g_state.fb_front);
    g_state.fb_front = nullptr;
  }
  if (g_state.fb_back) {
    LvglFreePreferPsram(g_state.fb_back);
    g_state.fb_back = nullptr;
  }
  g_state.fb_bytes = 0;
  g_state.fb_stride = 0;
  g_state.fb_buffer_ready = false;
}

static bool EnsureFrameBuffers() {
  constexpr uint32_t kStride = 64 * 2;
  constexpr size_t kBytes = static_cast<size_t>(kStride) * 32u;
  if (g_state.fb_front && g_state.fb_back && g_state.fb_bytes == kBytes && g_state.fb_stride == kStride) return true;

  FreeFrameBuffers();
  g_state.fb_front = static_cast<uint8_t*>(LvglAllocPreferPsram(kBytes));
  g_state.fb_back = static_cast<uint8_t*>(LvglAllocPreferPsram(kBytes));
  if (!g_state.fb_front || !g_state.fb_back) {
    FreeFrameBuffers();
    return false;
  }

  memset(g_state.fb_front, 0, kBytes);
  memset(g_state.fb_back, 0, kBytes);
  g_state.fb_bytes = kBytes;
  g_state.fb_stride = kStride;
  return true;
}

static void StopFbTask() {
  if (g_state.fb_task) {
    TaskHandle_t task = g_state.fb_task;
    g_state.fb_task = nullptr;
    vTaskDelete(task);
  }
  if (g_state.fb_mutex) {
    vSemaphoreDelete(g_state.fb_mutex);
    g_state.fb_mutex = nullptr;
  }
  g_state.fb_request_pending = false;
  g_state.fb_buffer_ready = false;
  g_state.fb_faulted = false;
  g_state.fb_request_dt_ms = 0;
  g_state.fb_error.clear();
  FreeFrameBuffers();
}

static void FbRenderTask(void* arg) {
  (void)arg;
  while (true) {
    if (!g_state.fb_mode || !g_state.app) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    if (!g_state.fb_request_pending) {
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }

    if (!g_state.fb_back || g_state.fb_stride == 0 || g_state.fb_bytes == 0) {
      g_state.fb_faulted = true;
      g_state.fb_error = "fb buffers missing";
      g_state.fb_request_pending = false;
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    const uint32_t dt_ms = g_state.fb_request_dt_ms;
    g_state.fb_request_pending = false;

    const int64_t t0_us = esp_timer_get_time();
    if (g_state.fb_mutex) xSemaphoreTake(g_state.fb_mutex, portMAX_DELAY);
    g_state.app->Tick(dt_ms);
    memset(g_state.fb_back, 0, g_state.fb_bytes);
    const bool ok = g_state.app->RenderFrameBufferTo(64, 32, g_state.fb_back, g_state.fb_stride, nullptr, FontOrFallback());
    g_state.app->ClearFrameBufferTemps();
    if (ok) {
      std::swap(g_state.fb_front, g_state.fb_back);
      g_state.fb_buffer_ready = true;
      g_state.fb_faulted = false;
      g_state.fb_error.clear();
    } else {
      g_state.fb_faulted = true;
      const char* err = g_state.app->last_error();
      g_state.fb_error = err ? err : "render_fb failed";
    }
    if (g_state.fb_mutex) xSemaphoreGive(g_state.fb_mutex);
    const uint32_t render_ms = static_cast<uint32_t>((esp_timer_get_time() - t0_us) / 1000);
    if (render_ms > 100) {
  ESP_LOGW(kTag, "app_fb render slow: %lu ms app=%s", static_cast<unsigned long>(render_ms),
               g_state.app_dir.c_str());
    }
    taskYIELD();
  }
}

static bool StartFbTaskIfNeeded() {
  if (!g_state.fb_mode) return true;
  if (!EnsureFrameBuffers()) {
    g_state.fb_faulted = true;
    g_state.fb_error = "alloc fb buffers failed";
    return false;
  }
  if (!g_state.fb_mutex) {
    g_state.fb_mutex = xSemaphoreCreateMutex();
    if (!g_state.fb_mutex) {
      g_state.fb_faulted = true;
      g_state.fb_error = "fb mutex failed";
      return false;
    }
  }
  if (g_state.fb_task) return true;

  BaseType_t rc = xTaskCreatePinnedToCore(FbRenderTask, "app_fb", 8192, nullptr, 4, &g_state.fb_task, kLuaFbTaskCore);
  if (rc != pdPASS) {
    g_state.fb_task = nullptr;
    g_state.fb_faulted = true;
    g_state.fb_error = "fb task create failed";
    return false;
  }
  ESP_LOGI(kTag, "app_fb task started on core %ld", static_cast<long>(kLuaFbTaskCore));
  return true;
}

static void StopTimer() {
  if (!g_state.timer) return;
  lv_timer_del(g_state.timer);
  g_state.timer = nullptr;
}

static void EnsureRootContainer() {
  g_state.scr = lv_screen_active();
  if (!g_state.scr || !lv_obj_is_valid(g_state.scr)) return;
  if (g_state.root && lv_obj_is_valid(g_state.root)) return;

  g_state.root = lv_obj_create(g_state.scr);
  ESP_LOGI(kTag, "root container created raw=%p parent=%p", static_cast<void*>(g_state.root), static_cast<void*>(g_state.scr));
  lv_obj_remove_style_all(g_state.root);
  lv_obj_clear_flag(g_state.root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(g_state.root, 0, 0);
  lv_obj_set_style_border_width(g_state.root, 0, 0);
  lv_obj_set_style_bg_opa(g_state.root, LV_OPA_TRANSP, 0);
  lv_obj_set_size(g_state.root, 64, 32);
  lv_obj_center(g_state.root);
  ESP_LOGI(kTag, "root container ready");
}

static void ClearRootChildren() {
  if (!g_state.root || !lv_obj_is_valid(g_state.root)) return;

  while (true) {
    lv_obj_t* child = lv_obj_get_child(g_state.root, 0);
    if (!child) break;
    lv_obj_delete(child);
  }
  g_state.ota_overlay = nullptr;
  g_state.ota_title = nullptr;
  g_state.ota_percent = nullptr;
  g_state.ota_bar = nullptr;
  g_state.app_name_overlay = nullptr;
  g_state.app_name_overlay_until_ms = 0;
}

static uint32_t OtaProgressPercent(uint32_t written, uint32_t total) {
  if (total == 0) return 0;
  if (written >= total) return 100;
  return static_cast<uint32_t>((static_cast<uint64_t>(written) * 100u) / total);
}

static void EnsureOtaOverlay() {
  if (!g_state.root || !lv_obj_is_valid(g_state.root)) return;
  if (g_state.ota_overlay && lv_obj_is_valid(g_state.ota_overlay)) return;

  g_state.ota_overlay = lv_obj_create(g_state.root);
  lv_obj_remove_style_all(g_state.ota_overlay);
  lv_obj_clear_flag(g_state.ota_overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(g_state.ota_overlay, 0, 0);
  lv_obj_set_style_radius(g_state.ota_overlay, 0, 0);
  lv_obj_set_style_border_width(g_state.ota_overlay, 0, 0);
  lv_obj_set_style_bg_color(g_state.ota_overlay, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g_state.ota_overlay, LV_OPA_COVER, 0);
  lv_obj_set_size(g_state.ota_overlay, 64, 32);
  lv_obj_center(g_state.ota_overlay);
  lv_obj_add_flag(g_state.ota_overlay, LV_OBJ_FLAG_HIDDEN);

  g_state.ota_title = lv_label_create(g_state.ota_overlay);
  lv_obj_set_style_text_font(g_state.ota_title, FontOrFallback(), 0);
  lv_obj_set_style_text_color(g_state.ota_title, lv_color_white(), 0);
  lv_label_set_text(g_state.ota_title, "UPGRADING");
  lv_obj_align(g_state.ota_title, LV_ALIGN_TOP_MID, 0, 1);

  g_state.ota_percent = lv_label_create(g_state.ota_overlay);
  lv_obj_set_style_text_font(g_state.ota_percent, FontOrFallback(), 0);
  lv_obj_set_style_text_color(g_state.ota_percent, lv_color_white(), 0);
  lv_label_set_text(g_state.ota_percent, "0%");
  lv_obj_align(g_state.ota_percent, LV_ALIGN_CENTER, 0, 1);

  g_state.ota_bar = lv_bar_create(g_state.ota_overlay);
  lv_obj_remove_style_all(g_state.ota_bar);
  lv_obj_set_size(g_state.ota_bar, 56, 4);
  lv_obj_align(g_state.ota_bar, LV_ALIGN_BOTTOM_MID, 0, -3);
  lv_bar_set_range(g_state.ota_bar, 0, 100);
  lv_obj_set_style_bg_color(g_state.ota_bar, lv_color_hex(0x202020), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(g_state.ota_bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(g_state.ota_bar, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_color(g_state.ota_bar, lv_color_white(), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(g_state.ota_bar, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(g_state.ota_bar, 0, LV_PART_INDICATOR);
}

static void SyncOtaOverlay() {
  if (!g_state.root || !lv_obj_is_valid(g_state.root)) return;
  EnsureOtaOverlay();
  if (!g_state.ota_overlay || !lv_obj_is_valid(g_state.ota_overlay)) return;

  const OtaOverlayPhase phase = static_cast<OtaOverlayPhase>(g_ota_phase.load(std::memory_order_relaxed));
  if (phase == OtaOverlayPhase::kIdle) {
    lv_obj_add_flag(g_state.ota_overlay, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  const uint32_t total = g_ota_total_bytes.load(std::memory_order_relaxed);
  const uint32_t written = g_ota_written_bytes.load(std::memory_order_relaxed);
  uint32_t percent = OtaProgressPercent(written, total);
  const char* title = "UPGRADING";
  const char* percent_text = nullptr;
  char percent_buf[16] = {};

  if (phase == OtaOverlayPhase::kFinalizing) {
    title = "UPGRADING";
    percent = 100;
  } else if (phase == OtaOverlayPhase::kFailed) {
    title = "OTA FAILED";
    percent = 100;
    percent_text = "FAILED";
  }

  if (!percent_text) {
    snprintf(percent_buf, sizeof(percent_buf), "%lu%%", static_cast<unsigned long>(percent));
    percent_text = percent_buf;
  }

  lv_obj_clear_flag(g_state.ota_overlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(g_state.ota_overlay);
  lv_label_set_text(g_state.ota_title, title);
  lv_label_set_text(g_state.ota_percent, percent_text);
  lv_bar_set_value(g_state.ota_bar, static_cast<int32_t>(percent), LV_ANIM_OFF);
  lv_obj_invalidate(g_state.ota_overlay);
}

static void StopScreen() {
  StopTimer();
  StopFbTask();

  if (g_state.canvas && lv_obj_is_valid(g_state.canvas)) {
    lv_obj_delete(g_state.canvas);
  }
  g_state.canvas = nullptr;
  g_state.ota_overlay = nullptr;
  g_state.ota_title = nullptr;
  g_state.ota_percent = nullptr;
  g_state.ota_bar = nullptr;

  if (g_state.canvas_buf) {
    lv_draw_buf_destroy(g_state.canvas_buf);
    g_state.canvas_buf = nullptr;
  }

  ClearRootChildren();
  g_state.root = (g_state.root && lv_obj_is_valid(g_state.root)) ? g_state.root : nullptr;
  g_state.scr = lv_screen_active();
  g_state.lines.clear();
  g_state.app_dir.clear();
  g_state.last_ms = 0;
  g_state.first_frame_drawn = false;
  g_state.fb_mode = false;
  g_state.widgets.clear();
  g_state.bind_keys.clear();
  g_state.bind_values.clear();
  g_state.prev_bind_values.clear();
  SetCurrentAppInfo(nullptr);
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
      SetCurrentAppInfo(app_dir.c_str());
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
  SetCurrentAppInfo(nullptr);
  return false;
}

static void DrawLine(lv_layer_t* layer, int row, const char* text, lv_color_t color) {
  // Compact fallback text layout for 64x32 LED error view:
  // 7px glyph box + 1px gap between rows.
  constexpr int kTextH = 7;
  constexpr int kLineGap = 1;
  constexpr int kRowPitch = kTextH + kLineGap;
  const int y0 = row * kRowPitch;
  int y1 = y0 + kTextH - 1;
  if (y0 > 31) return;
  if (y1 > 31) y1 = 31;

  lv_draw_label_dsc_t dsc;
  lv_draw_label_dsc_init(&dsc);
  dsc.font = FontOrFallback();
  dsc.opa = LV_OPA_COVER;
  dsc.color = color;
  dsc.text = text ? text : "";
  dsc.align = LV_TEXT_ALIGN_LEFT;

  lv_area_t a = {0, y0, 63, static_cast<lv_coord_t>(y1)};
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
  g_state.prev_bind_values.clear();

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
    ESP_LOGE(kTag, "app render failed app=%s reason=runtime missing", g_state.app_dir.c_str());
    if (g_state.first_frame_drawn) {
      SyncOtaOverlay();
      return;
    }
    lv_canvas_fill_bg(g_state.canvas, lv_color_black(), LV_OPA_COVER);
    lv_obj_invalidate(g_state.canvas);
    SyncOtaOverlay();
    return;
  } else {
    if (g_state.fb_mode) {
      g_state.prev_bind_values.clear();
      if (!g_state.canvas_buf->data || g_state.canvas_buf->header.w != 64 || g_state.canvas_buf->header.h != 32 ||
          g_state.canvas_buf->header.cf != LV_COLOR_FORMAT_RGB565 || g_state.canvas_buf->header.stride < 64 * 2) {
        ESP_LOGE(kTag, "app render failed app=%s reason=bad draw buf", g_state.app_dir.c_str());
        if (g_state.first_frame_drawn) {
          SyncOtaOverlay();
          return;
        }
        lv_canvas_fill_bg(g_state.canvas, lv_color_black(), LV_OPA_COVER);
        lv_obj_invalidate(g_state.canvas);
        SyncOtaOverlay();
        return;
      } else {
        // Framebuffer apps may call fb.text()/fb.image(), which require a real LVGL layer.
        // Keep this path on the LVGL task so text and PNG rendering remain valid.
        const size_t canvas_bytes =
            static_cast<size_t>(g_state.canvas_buf->header.stride) * static_cast<size_t>(g_state.canvas_buf->header.h);
        memset(g_state.canvas_buf->data, 0, canvas_bytes);
        g_state.app->Tick(dt_ms);

        lv_layer_t layer;
        lv_canvas_init_layer(g_state.canvas, &layer);
        const bool ok = g_state.app->RenderFrameBufferTo(
            64, 32, static_cast<uint8_t*>(g_state.canvas_buf->data), g_state.canvas_buf->header.stride, &layer,
            FontOrFallback());
        lv_canvas_finish_layer(g_state.canvas, &layer);
        g_state.app->ClearFrameBufferTemps();

        if (ok) {
          lv_obj_invalidate(g_state.canvas);
          g_state.first_frame_drawn = true;
          SyncOtaOverlay();
          return;
        }

        const char* err = g_state.app->last_error();
        ESP_LOGE(kTag, "app render failed app=%s err=%s", g_state.app_dir.c_str(), err ? err : "unknown");
        if (g_state.first_frame_drawn) {
          SyncOtaOverlay();
          return;
        }
        lv_canvas_fill_bg(g_state.canvas, lv_color_black(), LV_OPA_COVER);
        lv_obj_invalidate(g_state.canvas);
        SyncOtaOverlay();
        return;
      }
    } else if (!g_state.widgets.empty()) {
      g_state.app->Tick(dt_ms);
      g_state.bind_values.clear();
      if (!g_state.app->RenderBinds(g_state.bind_keys, &g_state.bind_values)) {
        const char* err = g_state.app->last_error();
        ESP_LOGE(kTag, "app render binds failed app=%s err=%s", g_state.app_dir.c_str(), err ? err : "unknown");
        g_state.prev_bind_values.clear();
        if (g_state.first_frame_drawn) {
          SyncOtaOverlay();
          return;
        }
        lv_canvas_fill_bg(g_state.canvas, lv_color_black(), LV_OPA_COVER);
        lv_obj_invalidate(g_state.canvas);
        SyncOtaOverlay();
        return;
      } else {
        const bool binds_changed = !g_state.first_frame_drawn || (g_state.bind_values != g_state.prev_bind_values);
        if (!binds_changed) {
          // Keep OTA overlay responsive but skip unchanged widget redraw to avoid
          // repeated image decode/draw work on every timer tick.
          SyncOtaOverlay();
          return;
        }
        g_state.prev_bind_values = g_state.bind_values;
      }
    } else {
      g_state.prev_bind_values.clear();
      g_state.app->Tick(dt_ms);
      g_state.lines.clear();
      if (!g_state.app->Render(&g_state.lines)) {
        const char* err = g_state.app->last_error();
        ESP_LOGE(kTag, "app render text failed app=%s err=%s", g_state.app_dir.c_str(), err ? err : "unknown");
        if (g_state.first_frame_drawn) {
          SyncOtaOverlay();
          return;
        }
        lv_canvas_fill_bg(g_state.canvas, lv_color_black(), LV_OPA_COVER);
        lv_obj_invalidate(g_state.canvas);
        SyncOtaOverlay();
        return;
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
  SyncOtaOverlay();
}

static void TickCb(lv_timer_t* t) {
  (void)t;
  if (!g_state.scr || !g_state.root || !g_state.canvas || !g_state.canvas_buf) return;
  if (!lv_obj_is_valid(g_state.scr) || !lv_obj_is_valid(g_state.root) || !lv_obj_is_valid(g_state.canvas)) return;
  if (lv_screen_active() != g_state.scr) return;

  const uint32_t now = lv_tick_get();
  const uint32_t dt = (g_state.last_ms == 0) ? 0 : (now - g_state.last_ms);
  g_state.last_ms = now;

  RenderFrame(dt);
  if (g_state.app_name_overlay && lv_obj_is_valid(g_state.app_name_overlay)) {
    if (static_cast<int32_t>(lv_tick_get() - g_state.app_name_overlay_until_ms) >= 0) {
      lv_obj_add_flag(g_state.app_name_overlay, LV_OBJ_FLAG_HIDDEN);
    }
  }
  SyncOtaOverlay();
  if (g_state.fb_mode) vTaskDelay(pdMS_TO_TICKS(1));
}

static void ShowApp(const std::string& app_dir) {
  ESP_LOGI(kTag, "ShowApp begin (%s)", app_dir.c_str());
  StopScreen();
  ESP_LOGI(kTag, "ShowApp after StopScreen");
  if (!EnsureAppLoaded(app_dir)) return;
  ESP_LOGI(kTag, "ShowApp after EnsureAppLoaded fb_mode=%d", g_state.fb_mode ? 1 : 0);
  LvglHub75SetFlushEnabled(false);
  ESP_LOGI(kTag, "ShowApp flush gated");
  EnsureFontLoaded();
  if (!g_state.fb_mode) {
    LoadWidgetsFromUiJson(app_dir);
    PrepareBindKeys();
  } else {
    g_state.widgets.clear();
    g_state.bind_keys.clear();
    g_state.bind_values.clear();
  }

  EnsureRootContainer();
  if (!g_state.scr || !g_state.root) {
    ESP_LOGE(kTag, "ShowApp root container unavailable");
    LvglHub75SetFlushEnabled(true);
    return;
  }
  ESP_LOGI(kTag, "ShowApp using active screen=%p root=%p", static_cast<void*>(g_state.scr), static_cast<void*>(g_state.root));

  g_state.canvas_buf = LvglCreateDrawBufferPreferPsram(64, 32, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
  if (!g_state.canvas_buf) {
    ESP_LOGE(kTag, "Failed to create canvas buffer");
    StopScreen();
    return;
  }
  ESP_LOGI(kTag, "ShowApp canvas buffer ready stride=%u", static_cast<unsigned>(g_state.canvas_buf->header.stride));

  g_state.canvas = lv_canvas_create(g_state.root);
  ESP_LOGI(kTag, "ShowApp canvas object created");
  lv_canvas_set_draw_buf(g_state.canvas, g_state.canvas_buf);
  lv_obj_clear_flag(g_state.canvas, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(g_state.canvas, 0, 0);
  lv_obj_set_size(g_state.canvas, 64, 32);
  lv_obj_center(g_state.canvas);
  EnsureOtaOverlay();
  SyncOtaOverlay();

  g_state.last_ms = 0;
  if (g_state.fb_mode) {
    g_state.fb_request_dt_ms = 0;
    g_state.fb_request_pending = true;
  }
  // Draw a frame BEFORE loading the screen, to avoid a "garbage flash" from an uninitialized draw buffer.
  ESP_LOGI(kTag, "ShowApp before initial RenderFrame");
  RenderFrame(0);
  ESP_LOGI(kTag, "ShowApp after initial RenderFrame");
  g_state.first_frame_drawn = true;
  g_state.timer = lv_timer_create(TickCb, g_state.fb_mode ? kTickMsFb : kTickMsText, nullptr);
  ESP_LOGI(kTag, "ShowApp timer created period=%u", static_cast<unsigned>(g_state.fb_mode ? kTickMsFb : kTickMsText));

  if (g_state.app_name_overlay && lv_obj_is_valid(g_state.app_name_overlay)) {
    lv_obj_delete(g_state.app_name_overlay);
    g_state.app_name_overlay = nullptr;
  }
  g_state.app_name_overlay = lv_obj_create(g_state.root);
  lv_obj_remove_style_all(g_state.app_name_overlay);
  lv_obj_set_size(g_state.app_name_overlay, 64, 32);
  lv_obj_center(g_state.app_name_overlay);
  lv_obj_set_style_bg_color(g_state.app_name_overlay, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g_state.app_name_overlay, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(g_state.app_name_overlay, 0, 0);
  lv_obj_set_style_border_width(g_state.app_name_overlay, 0, 0);
  lv_obj_t* name = lv_label_create(g_state.app_name_overlay);
  std::string display_name = ReadAppDisplayName(app_dir);
  if (display_name.empty()) {
    const char* slash = strrchr(app_dir.c_str(), '/');
    display_name = (slash && slash[1]) ? (slash + 1) : app_dir;
  }
  const std::string multi_line_name = SplitNameForTwoLines(display_name);
  lv_label_set_text(name, multi_line_name.c_str());
  lv_obj_set_style_text_color(name, lv_color_hex(0x00FFFF), 0);
  lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(name, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(name, 62);
  lv_obj_center(name);
  lv_obj_move_foreground(g_state.app_name_overlay);
  g_state.app_name_overlay_until_ms = lv_tick_get() + kAppNameOverlayMs;

  LvglHub75SetFlushEnabled(true);
  ESP_LOGI(kTag, "ShowApp flush ungated");
  lv_obj_invalidate(g_state.root);
  ESP_LOGI(kTag, "ShowApp root container invalidated");
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

void LvglShowAppUpdatingScreen() {
  StopScreen();
  LvglHub75SetFlushEnabled(false);
  EnsureRootContainer();
  if (!g_state.scr || !lv_obj_is_valid(g_state.scr) || !g_state.root || !lv_obj_is_valid(g_state.root)) {
    LvglHub75SetFlushEnabled(true);
    return;
  }
  ClearRootChildren();
  // Force OTA overlay state to idle so no overlay widget can reappear and overlap text.
  g_ota_phase.store(static_cast<uint8_t>(OtaOverlayPhase::kIdle), std::memory_order_relaxed);
  g_ota_total_bytes.store(0, std::memory_order_relaxed);
  g_ota_written_bytes.store(0, std::memory_order_relaxed);
  g_state.ota_overlay = nullptr;
  g_state.ota_title = nullptr;
  g_state.ota_percent = nullptr;
  g_state.ota_bar = nullptr;
  lv_obj_set_style_bg_color(g_state.scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g_state.scr, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(g_state.root, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g_state.root, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(g_state.root, 0, 0);
  lv_obj_set_style_pad_all(g_state.root, 0, 0);

  lv_obj_t* label = lv_label_create(g_state.root);
  lv_label_set_text(label, "APP\nUPDATING");
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(label);
  lv_obj_invalidate(g_state.root);
  LvglHub75SetFlushEnabled(true);
}

void LvglLuaAppScreenPrewarm() {
  g_state.scr = lv_screen_active();
}

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

bool LvglGetCurrentLuaAppInfo(char* out_app_id, size_t app_id_size, char* out_app_dir, size_t app_dir_size) {
  char snap_id[sizeof(g_current_app_id)] = {};
  char snap_dir[sizeof(g_current_app_dir)] = {};
  portENTER_CRITICAL(&g_current_app_mux);
  memcpy(snap_id, g_current_app_id, sizeof(snap_id));
  memcpy(snap_dir, g_current_app_dir, sizeof(snap_dir));
  portEXIT_CRITICAL(&g_current_app_mux);

  if (out_app_id && app_id_size > 0) {
    snprintf(out_app_id, app_id_size, "%s", snap_id);
  }
  if (out_app_dir && app_dir_size > 0) {
    snprintf(out_app_dir, app_dir_size, "%s", snap_dir);
  }
  return snap_id[0] != '\0';
}

void LvglOtaOverlayBegin(size_t total_bytes) {
  g_ota_total_bytes.store(static_cast<uint32_t>(total_bytes > UINT32_MAX ? UINT32_MAX : total_bytes),
                          std::memory_order_relaxed);
  g_ota_written_bytes.store(0, std::memory_order_relaxed);
  g_ota_phase.store(static_cast<uint8_t>(OtaOverlayPhase::kUploading), std::memory_order_relaxed);
}

void LvglOtaOverlayUpdate(size_t written_bytes, size_t total_bytes) {
  g_ota_total_bytes.store(static_cast<uint32_t>(total_bytes > UINT32_MAX ? UINT32_MAX : total_bytes),
                          std::memory_order_relaxed);
  g_ota_written_bytes.store(static_cast<uint32_t>(written_bytes > UINT32_MAX ? UINT32_MAX : written_bytes),
                            std::memory_order_relaxed);
  g_ota_phase.store(static_cast<uint8_t>(OtaOverlayPhase::kUploading), std::memory_order_relaxed);
}

void LvglOtaOverlayFinalizing() {
  const uint32_t total = g_ota_total_bytes.load(std::memory_order_relaxed);
  if (total > 0) g_ota_written_bytes.store(total, std::memory_order_relaxed);
  g_ota_phase.store(static_cast<uint8_t>(OtaOverlayPhase::kFinalizing), std::memory_order_relaxed);
}

void LvglOtaOverlayFail() {
  g_ota_phase.store(static_cast<uint8_t>(OtaOverlayPhase::kFailed), std::memory_order_relaxed);
}

void LvglOtaOverlayClear() {
  g_ota_phase.store(static_cast<uint8_t>(OtaOverlayPhase::kIdle), std::memory_order_relaxed);
  g_ota_written_bytes.store(0, std::memory_order_relaxed);
  g_ota_total_bytes.store(0, std::memory_order_relaxed);
}
