#pragma once

extern "C" {
#include "lvgl.h"
}

void LvglRuntimeFontsInit();

const lv_font_t *LvglRuntimeFontSmall();
const lv_font_t *LvglRuntimeFontTime();
const lv_font_t *LvglRuntimeFontSimple();    // 最简单清晰 - VT323
const lv_font_t *LvglRuntimeFontBold();      // 粗体 - PressStart2P
const lv_font_t *LvglRuntimeFontMono();      // 等宽 - ShareTechMono
const lv_font_t *LvglRuntimeFontPixelBold(); // 像素粗体 - Silkscreen

