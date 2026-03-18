#pragma once

#include <cstddef>

extern "C" {
#include "lvgl.h"
}

// Create an LVGL draw buffer whose backing storage prefers PSRAM and falls
// back to internal RAM when external memory is unavailable.
lv_draw_buf_t *LvglCreateDrawBufferPreferPsram(uint32_t width,
                                               uint32_t height,
                                               lv_color_format_t color_format,
                                               uint32_t stride = LV_STRIDE_AUTO);

// Free a draw buffer created via LvglCreateDrawBufferPreferPsram().
void LvglDestroyDrawBufferPreferPsram(lv_draw_buf_t *buf);

// Allocate a raw pixel buffer that prefers PSRAM, then internal RAM fallback.
void *LvglAllocPreferPsram(size_t size_bytes);

// Free memory allocated by LvglAllocPreferPsram().
void LvglFreePreferPsram(void *ptr);