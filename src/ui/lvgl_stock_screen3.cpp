#include "ui/lvgl_stock_screen3.h"

#include "esp_log.h"
#include <cmath>
#include <cstring>
#include <sys/stat.h>

extern "C" {
#include "lvgl.h"
}

static const char *kTag = "stock_screen3";

// Debug instrumentation for animation issues (black flash / slow slide).
// Set to 1 temporarily when diagnosing.
#ifndef STOCK3_ANIM_DEBUG
#define STOCK3_ANIM_DEBUG 0
#endif

// When enabled, draw a fallback letter in the icon area if PNG decode/draw fails.
// Default off: avoids unexpected characters appearing during transitions.
#ifndef STOCK3_ICON_FALLBACK_LETTER
#define STOCK3_ICON_FALLBACK_LETTER 0
#endif

namespace {

static lv_obj_t *g_scr = nullptr;
static lv_obj_t *g_canvas = nullptr;
// Offscreen canvas used to render cached frames for slide animation.
// Kept hidden so it never hits the display, but uses LVGL's normal canvas
// layer init so draw_image/draw_label work correctly.
static lv_obj_t *g_offscreen_canvas = nullptr;
static lv_font_t *g_font = nullptr;
static lv_font_t *g_icon_font = nullptr;
static lv_draw_buf_t *g_canvas_buf = nullptr;
static lv_timer_t *g_rotate_timer = nullptr;
static lv_timer_t *g_anim_timer = nullptr;
static int g_rotate_index = 0;

struct Transition {
  bool active = false;
  int from_idx = 0;
  int to_idx = 0;
  uint8_t step = 0;
};

static Transition g_tr;

// For slide animation: cache two full 64x32 frames in RGB565 to avoid decoding PNG
// and re-drawing text twice per frame.
static lv_draw_buf_t *g_frame_from = nullptr;
static lv_draw_buf_t *g_frame_to = nullptr;

struct Stock3Item {
  const char *icon_src;  // LVGL image src (e.g. S:/littlefs/...)
  const char *name;
  const char *chg;
  bool up;
  const char *price;
};

static const Stock3Item kItems[] = {
  {"S:/littlefs/icon/apple-24.png", "APPLE", "+1.9%", true, "$323"},
  {"S:/littlefs/icon/tesla-24.png", "TESLA", "-0.8%", false, "$191"},
  {"S:/littlefs/icon/facebook-24.png", "META", "+0.4%", true, "$468"},
};
static constexpr int kItemCount = sizeof(kItems) / sizeof(kItems[0]);

// Fade animation tuning
// Make the transition snappier to match other screens.
// Slide timing: keep it smooth but not too slow.
static constexpr uint32_t kAnimPeriodMs = 16;
// On this target, the effective tick observed is often ~20ms.
// Use fewer steps so the transition doesn't feel slow.
static constexpr uint8_t kAnimSteps = 12;

}  // namespace

void LvglStockScreen3PreloadFont() {
  if (g_font) return;

  ESP_LOGI(kTag, "Preloading Silkscreen-Regular font...");
  g_font = lv_tiny_ttf_create_file_ex(
      "S:/littlefs/fonts/Silkscreen-Regular.ttf",
      8,
      LV_FONT_KERNING_NONE,
      64);

  // icon 中间的字母需要更大字号；单独建一个 12px 字体（只多占一点点内存）
  g_icon_font = lv_tiny_ttf_create_file_ex(
    "S:/littlefs/fonts/Silkscreen-Regular.ttf",
    12,
    LV_FONT_KERNING_NONE,
    64);

  if (!g_font) {
    ESP_LOGE(kTag, "Failed to preload Silkscreen-Regular.ttf");
  } else {
    ESP_LOGI(kTag, "Font preloaded successfully");
  }

  if (!g_icon_font) {
    ESP_LOGW(kTag, "Failed to preload 12px icon font, will fallback to 8px");
  }
}

static void DrawRightText(lv_layer_t *layer,
                          const char *name,
                          const char *chg,
                          bool up,
                          const char *price,
                          lv_opa_t opa) {
  // 右侧 32x32：上/中/下 3 行
  // 为了在 32px 高度里更紧凑，用 4px 步进（会有少量重叠，但可读性更好）
  // 右侧 32x32：画到同一个 64x32 canvas 上，区域是 x=[32..63]
  constexpr int x0 = 32;
  constexpr int x1 = 63;

  auto draw_line = [&](int y, const char *text, lv_color_t color) {
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.font = g_font;
    dsc.color = color;
    dsc.text = text;
    dsc.opa = opa;

    lv_area_t a = {x0, y, x1, y + 8};
    lv_draw_label(layer, &dsc, &a);
  };

  draw_line(0, name, lv_color_white());
  draw_line(12, chg, up ? lv_color_make(0, 255, 0) : lv_color_make(255, 0, 0));
  draw_line(24, price, lv_color_white());
}

static void DrawItemAt(lv_layer_t *layer, int idx, int x_off, lv_opa_t opa) {
  if (idx < 0) idx = 0;
  idx %= kItemCount;
  const Stock3Item &it = kItems[idx];

  // Left icon (24x24 centered in 32x32; apply x offset)
  {
    lv_draw_image_dsc_t imgd;
    lv_draw_image_dsc_init(&imgd);
    imgd.src = it.icon_src;
    imgd.opa = opa;

    lv_area_t icon_area = {x_off + 4, 4, x_off + 27, 27};
    lv_draw_image(layer, &imgd, &icon_area);
  }

  // Right text (apply x offset)
  {
    constexpr int x0 = 32;
    constexpr int x1 = 63;

    auto draw_line = [&](int y, const char *text, lv_color_t color) {
      lv_draw_label_dsc_t dsc;
      lv_draw_label_dsc_init(&dsc);
      dsc.font = g_font;
      dsc.color = color;
      dsc.text = text;
      dsc.opa = opa;

      lv_area_t a = {x_off + x0, y, x_off + x1, y + 8};
      lv_draw_label(layer, &dsc, &a);
    };

    draw_line(0, it.name, lv_color_white());
    draw_line(12, it.chg, it.up ? lv_color_make(0, 255, 0) : lv_color_make(255, 0, 0));
    draw_line(24, it.price, lv_color_white());
  }
}

static void RedrawCanvasForItem(int idx, lv_opa_t opa) {
  if (!g_canvas || !g_canvas_buf) return;

  if (idx < 0) idx = 0;
  idx %= kItemCount;

  const Stock3Item &it = kItems[idx];

  // Clear
  lv_canvas_fill_bg(g_canvas, lv_color_black(), LV_OPA_COVER);

  lv_layer_t layer;
  lv_canvas_init_layer(g_canvas, &layer);

  // Left icon
  {
    lv_draw_image_dsc_t imgd;
    lv_draw_image_dsc_init(&imgd);
    imgd.src = it.icon_src;
    imgd.opa = opa;

    // 24x24 icon centered in a 32x32 area => offset (4,4)
    lv_area_t icon_area = {4, 4, 27, 27};
    lv_draw_image(&layer, &imgd, &icon_area);
  }

  // Right text
  DrawRightText(&layer, it.name, it.chg, it.up, it.price, opa);

  lv_canvas_finish_layer(g_canvas, &layer);
  lv_obj_invalidate(g_canvas);
}

static void FreeSlideFrames() {
  if (g_frame_from) {
    lv_draw_buf_destroy(g_frame_from);
    g_frame_from = nullptr;
  }
  if (g_frame_to) {
    lv_draw_buf_destroy(g_frame_to);
    g_frame_to = nullptr;
  }
}

static void RenderItemToDrawBuf(lv_draw_buf_t *buf, int idx) {
  if (!buf) return;
  if (!g_offscreen_canvas) {
    // Defensive: avoid leaving the buffer uninitialized.
    if (buf->data) std::memset(buf->data, 0, static_cast<size_t>(buf->header.stride) * 32);
    return;
  }
  if (idx < 0) idx = 0;
  idx %= kItemCount;

  const Stock3Item &it = kItems[idx];

  // Render via an offscreen canvas so LVGL initializes the layer correctly.
  // This avoids undefined behavior from partially-initialized lv_layer_t.
  lv_canvas_set_draw_buf(g_offscreen_canvas, buf);
  lv_canvas_fill_bg(g_offscreen_canvas, lv_color_black(), LV_OPA_COVER);

  lv_layer_t layer;
  lv_canvas_init_layer(g_offscreen_canvas, &layer);

  // Left icon (single draw)
  {
    lv_draw_image_dsc_t imgd;
    lv_draw_image_dsc_init(&imgd);
    imgd.src = it.icon_src;
    imgd.opa = LV_OPA_COVER;
    lv_area_t icon_area = {4, 4, 27, 27};
    lv_draw_image(&layer, &imgd, &icon_area);
  }

  DrawRightText(&layer, it.name, it.chg, it.up, it.price, LV_OPA_COVER);
  lv_canvas_finish_layer(g_offscreen_canvas, &layer);
}

static void EnsureSlideFramesRendered(int from_idx, int to_idx) {
  if (!g_canvas || !g_canvas_buf) return;

  if (!g_frame_from) {
    g_frame_from = lv_draw_buf_create(64, 32, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
  }
  if (!g_frame_to) {
    g_frame_to = lv_draw_buf_create(64, 32, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
  }
  if (!g_frame_from || !g_frame_to) {
    FreeSlideFrames();
    return;
  }

  // Render both frames once per transition.
  RenderItemToDrawBuf(g_frame_from, from_idx);
  RenderItemToDrawBuf(g_frame_to, to_idx);

  // If render failed for some reason, drop back to slow path.
  if (!g_frame_from->data || !g_frame_to->data) {
    ESP_LOGW(kTag, "slide cache render failed (data null), fallback to slow path");
    FreeSlideFrames();
  }
}

static void StopAnimTimer() {
  if (g_anim_timer) {
    lv_timer_del(g_anim_timer);
    g_anim_timer = nullptr;
  }
  g_tr.active = false;
}

static void TransitionFinish() {
  StopAnimTimer();
  g_rotate_index = g_tr.to_idx;
  RedrawCanvasForItem(g_rotate_index, LV_OPA_COVER);
}

static void TransitionRenderSlideFrame(uint8_t step) {
  if (!g_canvas || !g_canvas_buf) return;

  // Linear progress for a clear, consistent left-slide.
  const float t01 = (kAnimSteps == 0) ? 1.0f : (static_cast<float>(step) / kAnimSteps);
  const int dx = static_cast<int>(64.0f * t01 + 0.5f);
  const int x_old = -dx;
  const int x_new = 64 - dx;

  // Fast path: blit cached RGB565 frames.
  if (g_frame_from && g_frame_to && g_frame_from->data && g_frame_to->data) {
    const int dst_stride_px = static_cast<int>(g_canvas_buf->header.stride / 2);
    const int from_stride_px = static_cast<int>(g_frame_from->header.stride / 2);
    const int to_stride_px = static_cast<int>(g_frame_to->header.stride / 2);

    auto *dst = reinterpret_cast<lv_color16_t *>(g_canvas_buf->data);
    const auto *src_from = reinterpret_cast<const lv_color16_t *>(g_frame_from->data);
    const auto *src_to = reinterpret_cast<const lv_color16_t *>(g_frame_to->data);

    for (int y = 0; y < 32; y++) {
      // Base = full "from" frame (prevents black gaps).
      std::memcpy(&dst[y * dst_stride_px], &src_from[y * from_stride_px], sizeof(lv_color16_t) * 64);

      // Overlay visible part of "to" frame.
      int sx = 0;
      int dx0 = x_new;
      int w = 64;
      if (dx0 < 0) {
        sx = -dx0;
        w -= sx;
        dx0 = 0;
      }
      if (dx0 + w > 64) w = 64 - dx0;
      if (w > 0) {
        std::memcpy(&dst[y * dst_stride_px + dx0], &src_to[y * to_stride_px + sx], sizeof(lv_color16_t) * w);
      }
    }
    lv_obj_invalidate(g_canvas);

    return;
  }

  // Fallback: draw live into the on-screen canvas.
  // This guarantees a visible slide even if caching fails.
  lv_canvas_fill_bg(g_canvas, lv_color_black(), LV_OPA_COVER);
  lv_layer_t layer;
  lv_canvas_init_layer(g_canvas, &layer);
  DrawItemAt(&layer, g_tr.from_idx, x_old, LV_OPA_COVER);
  DrawItemAt(&layer, g_tr.to_idx, x_new, LV_OPA_COVER);
  lv_canvas_finish_layer(g_canvas, &layer);
  lv_obj_invalidate(g_canvas);
}

static void AnimTimerCb(lv_timer_t *t) {
  (void)t;

  if (!g_tr.active) {
    // Defensive: shouldn't happen, but don't draw into freed objects.
    StopAnimTimer();
    return;
  }

  if (g_tr.step > kAnimSteps) {
    TransitionFinish();
    return;
  }

  // Ensure cached frames exist on first step.
  if (g_tr.step == 0 && (!g_frame_from || !g_frame_to || !g_frame_from->data || !g_frame_to->data)) {
    FreeSlideFrames();
    EnsureSlideFramesRendered(g_tr.from_idx, g_tr.to_idx);
  }

  TransitionRenderSlideFrame(g_tr.step);
  g_tr.step++;
}

static void StartTransitionTo(int next_idx) {
  next_idx %= kItemCount;
  if (next_idx < 0) next_idx = 0;

  // If already animating, restart from current index
  StopAnimTimer();

  // Don't start a transition if we're already on that item.
  if (next_idx == g_rotate_index) {
    return;
  }

  g_tr.active = true;
  g_tr.from_idx = g_rotate_index;
  g_tr.to_idx = next_idx;
  g_tr.step = 0;

  // composite frame immediately. This avoids a perceived "flash" where the
  // canvas jumps to the final frame before the first anim tick.
  EnsureSlideFramesRendered(g_tr.from_idx, g_tr.to_idx);

  // If cache isn't ready, we still animate via live draw fallback.

  // Create timer first so we can schedule an immediate first frame.
  g_anim_timer = lv_timer_create(AnimTimerCb, kAnimPeriodMs, nullptr);
  // Render frame 0 immediately (avoids one-tick black/blank).
  AnimTimerCb(nullptr);
}

static void RotateTimerCb(lv_timer_t *t) {
  (void)t;
  // Don't queue new transitions while animating; it causes overlap/backlog.
  if (g_tr.active) return;
  const int next_idx = (g_rotate_index + 1) % kItemCount;
  StartTransitionTo(next_idx);
}

void LvglShowStockScreen3() {
  ESP_LOGI(kTag, "Creating stock screen3");

  // Just in case we re-enter without a clean stop.
  StopAnimTimer();
  FreeSlideFrames();

  if (!g_font) {
    ESP_LOGW(kTag, "Font not preloaded, loading now...");
    LvglStockScreen3PreloadFont();
    if (!g_font) return;
  }

  g_scr = lv_obj_create(nullptr);
  // screen3 用深绿色背景便于识别
  lv_obj_set_style_bg_color(g_scr, lv_color_hex(0x003000), 0);
  lv_obj_set_style_bg_opa(g_scr, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(g_scr, 0, 0);
  lv_obj_clear_flag(g_scr, LV_OBJ_FLAG_SCROLLABLE);

  // 诊断：确认 decoder/文件/PNG header 都正常
  {
    ESP_LOGI(kTag, "LVGL decoders compiled: LODEPNG=%d LIBPNG=%d TJPGD=%d BMP=%d",
             (int)LV_USE_LODEPNG,
             (int)LV_USE_LIBPNG,
             (int)LV_USE_TJPGD,
             (int)LV_USE_BMP);

#ifdef CONFIG_LV_USE_LODEPNG
    ESP_LOGI(kTag, "Kconfig: CONFIG_LV_USE_LODEPNG=%d", (int)CONFIG_LV_USE_LODEPNG);
#else
    ESP_LOGI(kTag, "Kconfig: CONFIG_LV_USE_LODEPNG is not defined");
#endif

    // 先在 C 层确认文件确实存在（避免“没 uploadfs / 路径错”的假象）
    const char *host_path = "/littlefs/icon/apple-24.png";
    struct stat st;
    if (stat(host_path, &st) == 0) {
      ESP_LOGI(kTag, "icon exists: %s size=%ld", host_path, static_cast<long>(st.st_size));
    } else {
      ESP_LOGW(kTag, "icon NOT found on littlefs: %s (did you run uploadfs?)", host_path);
    }

    // 再让 LVGL decoder 预读一下 header，拿到更具体的失败原因（如果失败会返回非 OK）
    lv_image_header_t header;
    lv_result_t r = lv_image_decoder_get_info("S:/littlefs/icon/apple-24.png", &header);
    if (r == LV_RESULT_OK) {
      ESP_LOGI(kTag, "lvgl image info: %dx%d cf=%d", (int)header.w, (int)header.h, (int)header.cf);
    } else {
      // 注意：lv_result_t 里 INVALID=0, OK=1（不要被 err=0 误导）
      ESP_LOGW(kTag, "lvgl image decoder get_info FAILED (r=%d; OK=%d INVALID=%d)", (int)r, (int)LV_RESULT_OK,
               (int)LV_RESULT_INVALID);
    }

    // 说明：不要在这里调用 lv_image_decoder_open。
    // lv_image_decoder_dsc_t 在 LVGL 公共头里是 opaque/incomplete type，
    // 直接实例化会在 C++ 编译时报错。
  }

  // 一个 64x32 canvas：左侧贴图，右侧画字
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

  // Offscreen canvas for cached-frame rendering.
  // Hidden: it won't be drawn, but can still be used as a rendering target.
  g_offscreen_canvas = lv_canvas_create(g_scr);
  lv_obj_add_flag(g_offscreen_canvas, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_pos(g_offscreen_canvas, 0, 0);
  lv_obj_set_size(g_offscreen_canvas, 64, 32);

  // First draw + start rotation
  g_rotate_index = 0;
  RedrawCanvasForItem(g_rotate_index, LV_OPA_COVER);

  if (g_rotate_timer) {
    lv_timer_delete(g_rotate_timer);
    g_rotate_timer = nullptr;
  }
  g_rotate_timer = lv_timer_create(RotateTimerCb, 5000, nullptr);

  lv_screen_load(g_scr);
  // 强制全屏刷新一次（避免“部分刷新区域没触发/没覆盖”的情况）
  lv_obj_invalidate(g_scr);
  // Note: don't call lv_refr_now() here. Forcing an immediate refresh can block
  // the LVGL task and makes animations feel stuttery when screens are switched
  // by the carousel. Invalidation is enough; LVGL will refresh on next handler.
  ESP_LOGI(kTag, "Stock screen3 displayed");
}

void LvglStopStockScreen3() {
  if (g_rotate_timer) {
    lv_timer_delete(g_rotate_timer);
    g_rotate_timer = nullptr;
  }

  StopAnimTimer();

  FreeSlideFrames();

  if (g_font) {
    lv_tiny_ttf_destroy(g_font);
    g_font = nullptr;
  }

  if (g_icon_font) {
    lv_tiny_ttf_destroy(g_icon_font);
    g_icon_font = nullptr;
  }

  if (g_canvas_buf) {
    lv_draw_buf_destroy(g_canvas_buf);
    g_canvas_buf = nullptr;
  }

  if (g_scr) {
    lv_obj_delete(g_scr);
    g_scr = nullptr;
  }

  // Important: g_canvas is a child of g_scr. Once g_scr is deleted the pointer
  // becomes dangling. Leaving it non-null can cause later redraws (from any
  // leftover timer or async callback) to scribble into freed memory.
  g_canvas = nullptr;
  g_offscreen_canvas = nullptr;

  ESP_LOGI(kTag, "Stock screen3 stopped");
}
