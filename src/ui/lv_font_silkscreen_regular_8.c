/*******************************************************************************
 * Size: 8 px
 * Bpp: 1
 * Source: Silkscreen-Regular.ttf
 * Range: 0x20-0x7F (ASCII)
 ******************************************************************************/

#include "lvgl.h"

#if 1

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    0x00, 0x80, 0x80, 0x80, 0x00, 0x80, 0xa0, 0xa0, 0x50, 0xf8, 0x50, 0xf8, 0x50, 0x20, 0x70, 0x80,
    0x60, 0x10, 0xe0, 0x40, 0xd0, 0xd0, 0x20, 0x58, 0x58, 0x20, 0x70, 0x80, 0x60, 0x80, 0x70, 0x20,
    0x80, 0x80, 0x40, 0x80, 0x80, 0x80, 0x40, 0x80, 0x40, 0x40, 0x40, 0x80, 0x20, 0xa8, 0x70, 0xa8,
    0x20, 0x20, 0x20, 0xf8, 0x20, 0x20, 0x40, 0x80, 0xe0, 0x80, 0x20, 0x20, 0x40, 0x80, 0x80, 0x60,
    0x90, 0x90, 0x90, 0x60, 0xc0, 0x40, 0x40, 0x40, 0xe0, 0xe0, 0x10, 0x60, 0x80, 0xf0, 0xe0, 0x10,
    0x60, 0x10, 0xe0, 0xa0, 0xa0, 0xf0, 0x20, 0x20, 0xf0, 0x80, 0xe0, 0x10, 0xe0, 0x60, 0x80, 0xe0,
    0x90, 0x60, 0xf0, 0x10, 0x20, 0x40, 0x40, 0x60, 0x90, 0x60, 0x90, 0x60, 0x60, 0x90, 0x70, 0x10,
    0x60, 0x80, 0x00, 0x80, 0x40, 0x00, 0x40, 0x80, 0x20, 0x40, 0x80, 0x40, 0x20, 0xe0, 0x00, 0xe0,
    0x80, 0x40, 0x20, 0x40, 0x80, 0xe0, 0x10, 0x60, 0x00, 0x40, 0x70, 0xa8, 0xb0, 0x80, 0x70, 0x60,
    0x90, 0xf0, 0x90, 0x90, 0xe0, 0x90, 0xf0, 0x90, 0xe0, 0x60, 0x90, 0x80, 0x90, 0x60, 0xe0, 0x90,
    0x90, 0x90, 0xe0, 0xe0, 0x80, 0xe0, 0x80, 0xe0, 0xe0, 0x80, 0xe0, 0x80, 0x80, 0x70, 0x80, 0xb0,
    0x90, 0x60, 0x90, 0x90, 0xf0, 0x90, 0x90, 0x80, 0x80, 0x80, 0x80, 0x80, 0x10, 0x10, 0x10, 0x90,
    0x60, 0x90, 0xa0, 0xc0, 0xa0, 0x90, 0x80, 0x80, 0x80, 0x80, 0xe0, 0x88, 0xd8, 0xa8, 0x88, 0x88,
    0x88, 0xc8, 0xa8, 0x98, 0x88, 0x60, 0x90, 0x90, 0x90, 0x60, 0xe0, 0x90, 0xe0, 0x80, 0x80, 0x60,
    0x90, 0x90, 0x90, 0x60, 0x10, 0xe0, 0x90, 0xe0, 0xa0, 0x90, 0x70, 0x80, 0x60, 0x10, 0xe0, 0xe0,
    0x40, 0x40, 0x40, 0x40, 0x90, 0x90, 0x90, 0x90, 0x60, 0x88, 0x88, 0x50, 0x50, 0x20, 0x88, 0xa8,
    0xa8, 0xa8, 0x50, 0x88, 0x50, 0x20, 0x50, 0x88, 0x88, 0x50, 0x20, 0x20, 0x20, 0xe0, 0x20, 0x40,
    0x80, 0xe0, 0xc0, 0x80, 0x80, 0x80, 0xc0, 0x80, 0x80, 0x40, 0x20, 0x20, 0xc0, 0x40, 0x40, 0x40,
    0xc0, 0x40, 0xa0, 0xf0, 0x80, 0x40, 0x60, 0x90, 0xf0, 0x90, 0x90, 0xe0, 0x90, 0xf0, 0x90, 0xe0,
    0x60, 0x90, 0x80, 0x90, 0x60, 0xe0, 0x90, 0x90, 0x90, 0xe0, 0xe0, 0x80, 0xe0, 0x80, 0xe0, 0xe0,
    0x80, 0xe0, 0x80, 0x80, 0x70, 0x80, 0xb0, 0x90, 0x60, 0x90, 0x90, 0xf0, 0x90, 0x90, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x10, 0x10, 0x10, 0x90, 0x60, 0x90, 0xa0, 0xc0, 0xa0, 0x90, 0x80, 0x80, 0x80,
    0x80, 0xe0, 0x88, 0xd8, 0xa8, 0x88, 0x88, 0x88, 0xc8, 0xa8, 0x98, 0x88, 0x60, 0x90, 0x90, 0x90,
    0x60, 0xe0, 0x90, 0xe0, 0x80, 0x80, 0x60, 0x90, 0x90, 0x90, 0x60, 0x10, 0xe0, 0x90, 0xe0, 0xa0,
    0x90, 0x70, 0x80, 0x60, 0x10, 0xe0, 0xe0, 0x40, 0x40, 0x40, 0x40, 0x90, 0x90, 0x90, 0x90, 0x60,
    0x88, 0x88, 0x50, 0x50, 0x20, 0x88, 0xa8, 0xa8, 0xa8, 0x50, 0x88, 0x50, 0x20, 0x50, 0x88, 0x88,
    0x50, 0x20, 0x20, 0x20, 0xe0, 0x20, 0x40, 0x80, 0xe0, 0x60, 0x40, 0x80, 0x40, 0x60, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0xc0, 0x40, 0x20, 0x40, 0xc0, 0x50, 0xa0, 0xe0, 0xa0, 0xa0, 0xa0,
    0xe0,
};

/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 0, .adv_w = 64, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1, .adv_w = 32, .box_w = 1, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 6, .adv_w = 64, .box_w = 3, .box_h = 2, .ofs_x = 1, .ofs_y = 3},
    {.bitmap_index = 8, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 13, .adv_w = 80, .box_w = 4, .box_h = 7, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 20, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 25, .adv_w = 80, .box_w = 4, .box_h = 7, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 32, .adv_w = 32, .box_w = 1, .box_h = 2, .ofs_x = 1, .ofs_y = 3},
    {.bitmap_index = 34, .adv_w = 48, .box_w = 2, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 39, .adv_w = 48, .box_w = 2, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 44, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 49, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 54, .adv_w = 48, .box_w = 2, .box_h = 2, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 56, .adv_w = 64, .box_w = 3, .box_h = 1, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 57, .adv_w = 32, .box_w = 1, .box_h = 1, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 58, .adv_w = 64, .box_w = 3, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 63, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 68, .adv_w = 64, .box_w = 3, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 73, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 78, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 83, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 88, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 93, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 98, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 103, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 108, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 113, .adv_w = 32, .box_w = 1, .box_h = 3, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 116, .adv_w = 48, .box_w = 2, .box_h = 4, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 120, .adv_w = 64, .box_w = 3, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 125, .adv_w = 64, .box_w = 3, .box_h = 3, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 128, .adv_w = 64, .box_w = 3, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 133, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 138, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 143, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 148, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 153, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 158, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 163, .adv_w = 64, .box_w = 3, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 168, .adv_w = 64, .box_w = 3, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 173, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 178, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 183, .adv_w = 32, .box_w = 1, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 188, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 193, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 198, .adv_w = 64, .box_w = 3, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 203, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 208, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 213, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 218, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 223, .adv_w = 80, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 229, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 234, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 239, .adv_w = 64, .box_w = 3, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 244, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 249, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 254, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 259, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 264, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 269, .adv_w = 64, .box_w = 3, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 274, .adv_w = 48, .box_w = 2, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 279, .adv_w = 64, .box_w = 3, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 284, .adv_w = 48, .box_w = 2, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 289, .adv_w = 64, .box_w = 3, .box_h = 2, .ofs_x = 1, .ofs_y = 4},
    {.bitmap_index = 291, .adv_w = 80, .box_w = 4, .box_h = 1, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 292, .adv_w = 48, .box_w = 2, .box_h = 2, .ofs_x = 1, .ofs_y = 6},
    {.bitmap_index = 294, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 299, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 304, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 309, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 314, .adv_w = 64, .box_w = 3, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 319, .adv_w = 64, .box_w = 3, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 324, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 329, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 334, .adv_w = 32, .box_w = 1, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 339, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 344, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 349, .adv_w = 64, .box_w = 3, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 354, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 359, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 364, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 369, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 374, .adv_w = 80, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 380, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 385, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 390, .adv_w = 64, .box_w = 3, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 395, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 400, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 405, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 410, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 415, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 420, .adv_w = 64, .box_w = 3, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 425, .adv_w = 64, .box_w = 3, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 430, .adv_w = 32, .box_w = 1, .box_h = 7, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 437, .adv_w = 64, .box_w = 3, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 442, .adv_w = 80, .box_w = 4, .box_h = 2, .ofs_x = 1, .ofs_y = 3},
    {.bitmap_index = 444, .adv_w = 64, .box_w = 3, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const lv_font_fmt_txt_cmap_t cmaps[] = {
    {
        .range_start = 32, .range_length = 96, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
};

/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 1,
    .kern_classes = 0,
    .bitmap_format = 0,
    .stride = 1,
};

/*-----------------
 *  PUBLIC FONT
 *----------------*/

#if LVGL_VERSION_MAJOR >= 8
const lv_font_t lv_font_silkscreen_regular_8 = {
#else
lv_font_t lv_font_silkscreen_regular_8 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
    .line_height = 10,
    .base_line = 2,
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = 0,
    .underline_thickness = 0,
#endif
    .dsc = &font_dsc
};

#endif
