#include "ui/lvgl_stock_screen2.h"

#include "esp_log.h"

extern "C" {
#include "lvgl.h"
}

static const char *kTag = "stock_screen2";

namespace {

static lv_obj_t *g_scr = nullptr;
static lv_obj_t *g_canvas = nullptr;
static lv_font_t *g_font = nullptr;
static lv_draw_buf_t *g_canvas_buf = nullptr;

}  // namespace

void LvglStockScreen2PreloadFont() {
  if (g_font) return;  // 已经加载
  
  ESP_LOGI(kTag, "Preloading Silkscreen-Regular font...");
  g_font = lv_tiny_ttf_create_file_ex(
      "S:/littlefs/fonts/Silkscreen-Regular.ttf",
      8,
      LV_FONT_KERNING_NONE,
      64
  );

  if (!g_font) {
    ESP_LOGE(kTag, "Failed to preload Silkscreen-Regular.ttf");
  } else {
    ESP_LOGI(kTag, "Font preloaded successfully");
  }
}

void LvglShowStockScreen2() {
  ESP_LOGI(kTag, "Creating stock screen2 with Silkscreen-Regular");

  // 确保字体已加载
  if (!g_font) {
    ESP_LOGW(kTag, "Font not preloaded, loading now...");
    LvglStockScreen2PreloadFont();
    if (!g_font) return;
  }

  // 创建屏幕
  g_scr = lv_obj_create(nullptr);
  // DEBUG: screen2 用深蓝色背景，肉眼可见是否切换
  lv_obj_set_style_bg_color(g_scr, lv_color_hex(0x000040), 0);

  // 创建 64x32 canvas
  g_canvas_buf = lv_draw_buf_create(64, 32, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
  if (!g_canvas_buf) {
    ESP_LOGE(kTag, "Failed to create canvas buffer");
    return;
  }

  g_canvas = lv_canvas_create(g_scr);
  lv_canvas_set_draw_buf(g_canvas, g_canvas_buf);
  lv_obj_clear_flag(g_canvas, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(g_canvas, 0, 0);

  // 清空画布为黑色
  lv_canvas_fill_bg(g_canvas, lv_color_black(), LV_OPA_COVER);

  // 准备绘制标签
  lv_layer_t layer;
  lv_canvas_init_layer(g_canvas, &layer);

  // screen2：4 只股票，每行显示“名称 + 价格”，不显示涨跌幅。
  // 64x32：使用标准 4 行布局（行高 8px），内容更清晰。
  constexpr int kRowH = 8;

  auto draw_row = [&](int row, const char *name, const char *price) {
    const int y0 = row * kRowH;
    lv_area_t area;

    lv_draw_label_dsc_t dsc_name;
    lv_draw_label_dsc_init(&dsc_name);
    dsc_name.font = g_font;
    dsc_name.color = lv_color_white();
    dsc_name.text = name;
    area = (lv_area_t){0, y0, 31, y0 + kRowH};
    lv_draw_label(&layer, &dsc_name, &area);

    lv_draw_label_dsc_t dsc_price;
    lv_draw_label_dsc_init(&dsc_price);
    dsc_price.font = g_font;
    dsc_price.color = lv_color_white();
    dsc_price.text = price;
    area = (lv_area_t){32, y0, 63, y0 + kRowH};
    lv_draw_label(&layer, &dsc_price, &area);
  };

  draw_row(0, "AAPL", "$323");
  draw_row(1, "TSLA", "$489");
  draw_row(2, "NVDA", "$702");
  draw_row(3, "AMZN", "$172");

  lv_canvas_finish_layer(g_canvas, &layer);

  // DEBUG: 左上角画一个“2”
  lv_draw_label_dsc_t dsc_dbg;
  lv_draw_label_dsc_init(&dsc_dbg);
  dsc_dbg.color = lv_color_white();
  dsc_dbg.font = g_font;
  dsc_dbg.text = "2";
  lv_area_t dbg_area = {0, 0, 8, 8};
  lv_draw_label(&layer, &dsc_dbg, &dbg_area);

  // 加载屏幕
  lv_screen_load(g_scr);

  ESP_LOGI(kTag, "Stock screen displayed");
}

void LvglStopStockScreen2() {
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

  // g_canvas is a child of g_scr; once g_scr is deleted this pointer is invalid.
  g_canvas = nullptr;

  ESP_LOGI(kTag, "Stock screen2 stopped");
}
