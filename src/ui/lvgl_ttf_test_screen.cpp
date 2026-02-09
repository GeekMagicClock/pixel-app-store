#include "ui/lvgl_ttf_test_screen.h"

#include "esp_log.h"
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <vector>
#include <string>

extern "C" {
#include "lvgl.h"
}

static const char *kTag = "font_test";

namespace {

static lv_obj_t *g_scr = nullptr;
static lv_obj_t *g_label = nullptr;
static lv_timer_t *g_timer = nullptr;
static uint32_t g_idx = 0;

static std::vector<std::string> g_font_files;
static lv_font_t *g_current_font = nullptr;

// 要测试的字体大小数组
static const uint8_t kFontSizes[] = {8, 10, 12};
static const uint8_t kNumSizes = sizeof(kFontSizes) / sizeof(kFontSizes[0]);
static uint8_t g_size_idx = 0;

// 扫描 /littlefs/fonts 目录，获取所有 .ttf 文件
static void ScanFontFiles() {
  g_font_files.clear();
  
  DIR *d = opendir("/littlefs/fonts");
  if (!d) {
    ESP_LOGE(kTag, "Failed to open /littlefs/fonts directory");
    return;
  }

  struct dirent *entry;
  while ((entry = readdir(d)) != nullptr) {
    const char *name = entry->d_name;
    size_t len = strlen(name);
    
    // 检查是否是 .ttf 文件
    if (len > 4 && strcmp(name + len - 4, ".ttf") == 0) {
      // 检查文件大小，跳过过大的字体（>150KB可能导致内存不足）
      std::string realPath = std::string("/littlefs/fonts/") + name;
      struct stat st;
      if (stat(realPath.c_str(), &st) == 0) {
        if (st.st_size > 150 * 1024) {
          ESP_LOGW(kTag, "Skipping large font (>150KB): %s (%ld bytes)", name, st.st_size);
          continue;
        }
      }
      
      // 使用 LVGL 文件系统路径格式: S:/littlefs/fonts/xxx.ttf
      std::string fullPath = std::string("S:/littlefs/fonts/") + name;
      g_font_files.push_back(fullPath);
      ESP_LOGI(kTag, "Found font: %s (%ld bytes)", fullPath.c_str(), st.st_size);
    }
  }
  
  closedir(d);
  ESP_LOGI(kTag, "Total fonts found: %d", (int)g_font_files.size());
}

// 显示指定索引的字体和指定的尺寸
static void ShowFont(uint32_t idx, uint8_t size_idx) {
  if (idx >= g_font_files.size()) {
    ESP_LOGW(kTag, "Font index %lu out of range", (unsigned long)idx);
    return;
  }
  
  if (size_idx >= kNumSizes) {
    ESP_LOGW(kTag, "Size index %u out of range", size_idx);
    return;
  }

  // 销毁之前的字体
  if (g_current_font) {
    lv_tiny_ttf_destroy(g_current_font);
    g_current_font = nullptr;
  }

  const std::string &fontPath = g_font_files[idx];
  uint8_t fontSize = kFontSizes[size_idx];
  ESP_LOGI(kTag, "Loading font: %s @ %upx", fontPath.c_str(), fontSize);

  // 使用 LVGL 文件系统直接加载，cache 64
  g_current_font = lv_tiny_ttf_create_file_ex(
      fontPath.c_str(), 
      fontSize, 
      LV_FONT_KERNING_NONE, 
      64
  );

  if (!g_current_font) {
    ESP_LOGE(kTag, "Failed to load font: %s @ %upx", fontPath.c_str(), fontSize);
    lv_label_set_text(g_label, "Font load failed");
    return;
  }

  // 设置标签使用新字体
  lv_obj_set_style_text_font(g_label, g_current_font, 0);

  // 提取文件名（去掉路径）
  const char *fileName = strrchr(fontPath.c_str(), '/');
  if (fileName) {
    fileName++; // 跳过 '/'
  } else {
    fileName = fontPath.c_str();
  }

  // 设置显示内容：字体名 + 尺寸 + 测试文本
  char displayText[256];
  snprintf(displayText, sizeof(displayText),
    "%upx\n"
    "ABCDEFG\n"
    "abcdefg\n"
    "0123456789",
    fontSize
  );
  lv_label_set_text(g_label, displayText);

  ESP_LOGI(kTag, "Font displayed successfully: %s @ %upx", fileName, fontSize);
}

// 定时器回调，切换字体和尺寸
static void TickCb(lv_timer_t *t) {
  // 先切换尺寸
  g_size_idx++;
  if (g_size_idx >= kNumSizes) {
    g_size_idx = 0;
    // 尺寸循环完一轮后，切换到下一个字体
    g_idx = (g_idx + 1) % g_font_files.size();
  }
  
  ShowFont(g_idx, g_size_idx);
}

}  // namespace

void LvglStopTtfTestScreen() {
  if (g_timer) {
    lv_timer_delete(g_timer);
    g_timer = nullptr;
  }

  if (g_current_font) {
    lv_tiny_ttf_destroy(g_current_font);
    g_current_font = nullptr;
  }

  if (g_scr) {
    lv_obj_delete(g_scr);
    g_scr = nullptr;
  }

  g_label = nullptr;
  g_font_files.clear();
}

void LvglStartTtfTestScreen() {
  LvglStopTtfTestScreen();

  // 扫描字体文件
  ScanFontFiles();
  
  if (g_font_files.empty()) {
    ESP_LOGE(kTag, "No TTF fonts found in /littlefs/fonts");
    return;
  }

  // 创建全屏黑色背景
  g_scr = lv_screen_active();
  lv_obj_set_style_bg_color(g_scr, lv_color_black(), 0);
  
  // 禁用屏幕的滚动功能和滚动条
  lv_obj_clear_flag(g_scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(g_scr, LV_SCROLLBAR_MODE_OFF);

  // 创建居中标签
  g_label = lv_label_create(g_scr);
  lv_obj_set_style_text_color(g_label, lv_color_white(), 0);
  
  // 清除所有可能的装饰元素
  lv_obj_set_style_bg_opa(g_label, LV_OPA_TRANSP, 0);  // 透明背景
  lv_obj_set_style_border_width(g_label, 0, 0);         // 无边框
  lv_obj_set_style_outline_width(g_label, 0, 0);        // 无轮廓
  lv_obj_set_style_shadow_width(g_label, 0, 0);         // 无阴影
  lv_obj_set_style_pad_all(g_label, 0, 0);              // 无内边距
  
  // 禁用滚动功能
  lv_obj_clear_flag(g_label, LV_OBJ_FLAG_SCROLLABLE);
  
  // 隐藏滚动条 - 针对所有部分
  lv_obj_set_scrollbar_mode(g_label, LV_SCROLLBAR_MODE_OFF);
  
  lv_obj_center(g_label);

  // 显示第一个字体的第一个尺寸
  g_idx = 0;
  g_size_idx = 0;
  ShowFont(g_idx, g_size_idx);

  // 创建定时器，5秒切换一次
  g_timer = lv_timer_create(TickCb, 5000, nullptr);

  ESP_LOGI(kTag, "Font test screen started");
}
