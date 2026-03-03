/*******************************************************************************
 * Size: 8 px
 * Bpp: 1
 * Source: PressStart2P-Regular.ttf
 * Range: 0x20-0x7F (ASCII)
 ******************************************************************************/

#include "lvgl.h"

#if 1

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    0x00, 0xe0, 0xe0, 0xe0, 0xc0, 0xc0, 0x00, 0xc0, 0xd8, 0xd8, 0xd8, 0x6c, 0xfe, 0x6c, 0x6c, 0x6c,
    0xfe, 0x6c, 0x10, 0x7c, 0xd0, 0x7c, 0x16, 0xfc, 0x10, 0x62, 0xa4, 0xc8, 0x10, 0x26, 0x4a, 0x8c,
    0x70, 0xd8, 0xd8, 0x70, 0xda, 0xcc, 0x7e, 0xc0, 0xc0, 0xc0, 0x30, 0x60, 0xc0, 0xc0, 0xc0, 0x60,
    0x30, 0xc0, 0x60, 0x30, 0x30, 0x30, 0x60, 0xc0, 0x6c, 0x38, 0xfe, 0x38, 0x6c, 0x30, 0x30, 0xfc,
    0x30, 0x30, 0x60, 0x60, 0xc0, 0xfc, 0xc0, 0xc0, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x38,
    0x4c, 0xc6, 0xc6, 0xc6, 0x64, 0x38, 0x30, 0x70, 0x30, 0x30, 0x30, 0x30, 0xfc, 0x7c, 0xc6, 0x0e,
    0x3c, 0x78, 0xe0, 0xfe, 0x7e, 0x0c, 0x18, 0x3c, 0x06, 0xc6, 0x7c, 0x1c, 0x3c, 0x6c, 0xcc, 0xfe,
    0x0c, 0x0c, 0xfc, 0xc0, 0xfc, 0x06, 0x06, 0xc6, 0x7c, 0x3c, 0x60, 0xc0, 0xfc, 0xc6, 0xc6, 0x7c,
    0xfe, 0xc6, 0x0c, 0x18, 0x30, 0x30, 0x30, 0x78, 0xc4, 0xe4, 0x78, 0x9e, 0x86, 0x7c, 0x7c, 0xc6,
    0xc6, 0x7e, 0x06, 0x0c, 0x78, 0xc0, 0xc0, 0x00, 0xc0, 0xc0, 0x60, 0x60, 0x00, 0x60, 0x60, 0xc0,
    0x18, 0x30, 0x60, 0xc0, 0x60, 0x30, 0x18, 0xfe, 0x00, 0xfe, 0xc0, 0x60, 0x30, 0x18, 0x30, 0x60,
    0xc0, 0x7c, 0xfe, 0xc6, 0x0c, 0x38, 0x00, 0x38, 0x7c, 0x82, 0xba, 0xaa, 0xbe, 0x80, 0x7c, 0x38,
    0x6c, 0xc6, 0xc6, 0xfe, 0xc6, 0xc6, 0xfc, 0xc6, 0xc6, 0xfc, 0xc6, 0xc6, 0xfc, 0x3c, 0x66, 0xc0,
    0xc0, 0xc0, 0x66, 0x3c, 0xf8, 0xcc, 0xc6, 0xc6, 0xc6, 0xcc, 0xf8, 0xfe, 0xc0, 0xc0, 0xfc, 0xc0,
    0xc0, 0xfe, 0xfe, 0xc0, 0xc0, 0xfc, 0xc0, 0xc0, 0xc0, 0x3e, 0x60, 0xc0, 0xce, 0xc6, 0x66, 0x3e,
    0xc6, 0xc6, 0xc6, 0xfe, 0xc6, 0xc6, 0xc6, 0xfc, 0x30, 0x30, 0x30, 0x30, 0x30, 0xfc, 0x06, 0x06,
    0x06, 0x06, 0x06, 0xc6, 0x7c, 0xc6, 0xcc, 0xd8, 0xf0, 0xf8, 0xdc, 0xce, 0xc0, 0xc0, 0xc0, 0xc0,
    0xc0, 0xc0, 0xfc, 0xc6, 0xee, 0xfe, 0xd6, 0xd6, 0xc6, 0xc6, 0xc6, 0xe6, 0xf6, 0xde, 0xce, 0xc6,
    0xc6, 0x7c, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0x7c, 0xfc, 0xc6, 0xc6, 0xc6, 0xfc, 0xc0, 0xc0, 0x7c,
    0xc6, 0xc6, 0xc6, 0xde, 0xcc, 0x7a, 0xfc, 0xc6, 0xc6, 0xce, 0xf8, 0xdc, 0xce, 0x7c, 0xc6, 0xc0,
    0x7c, 0x06, 0xc6, 0x7c, 0xfc, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6,
    0xc6, 0x7c, 0xc6, 0xc6, 0xc6, 0xee, 0x7c, 0x38, 0x10, 0xd6, 0xd6, 0xd6, 0xd6, 0xfe, 0xee, 0x44,
    0xc6, 0xc6, 0x6c, 0x38, 0x6c, 0xc6, 0xc6, 0xcc, 0xcc, 0xcc, 0x78, 0x30, 0x30, 0x30, 0xfe, 0x0e,
    0x1c, 0x38, 0x70, 0xe0, 0xfe, 0xf0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xf0, 0x80, 0x40, 0x20, 0x10,
    0x08, 0x04, 0x02, 0xf0, 0x30, 0x30, 0x30, 0x30, 0x30, 0xf0, 0x70, 0xd8, 0xfe, 0x80, 0x40, 0x7c,
    0x06, 0x7e, 0xc6, 0x7e, 0xc0, 0xc0, 0xfc, 0xc6, 0xc6, 0xc6, 0x7c, 0x7e, 0xc0, 0xc0, 0xc0, 0x7e,
    0x06, 0x06, 0x7e, 0xc6, 0xc6, 0xc6, 0x7e, 0x7c, 0xc6, 0xfe, 0xc0, 0x7c, 0x1c, 0x30, 0xfc, 0x30,
    0x30, 0x30, 0x30, 0x7e, 0xc6, 0xc6, 0x7e, 0x06, 0x7c, 0xc0, 0xc0, 0xfc, 0xc6, 0xc6, 0xc6, 0xc6,
    0x30, 0x00, 0x70, 0x30, 0x30, 0x30, 0xfc, 0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x18, 0xf0, 0xc0,
    0xc0, 0xc6, 0xcc, 0xf8, 0xcc, 0xc6, 0x70, 0x30, 0x30, 0x30, 0x30, 0x30, 0xfc, 0xfc, 0xb6, 0xb6,
    0xb6, 0xb6, 0xfc, 0xc6, 0xc6, 0xc6, 0xc6, 0x7c, 0xc6, 0xc6, 0xc6, 0x7c, 0xfc, 0xc6, 0xc6, 0xfc,
    0xc0, 0xc0, 0x7e, 0xc6, 0xc6, 0x7e, 0x06, 0x06, 0xdc, 0xe0, 0xc0, 0xc0, 0xc0, 0x7c, 0xc0, 0x7c,
    0x06, 0xfc, 0x30, 0x30, 0xfc, 0x30, 0x30, 0x30, 0x30, 0xc6, 0xc6, 0xc6, 0xc6, 0x7e, 0xcc, 0xcc,
    0xcc, 0x78, 0x30, 0xd6, 0xd6, 0xd6, 0xd6, 0x6c, 0xc6, 0x6c, 0x38, 0x6c, 0xc6, 0xc6, 0xc6, 0xc6,
    0x7e, 0x06, 0x7c, 0xfe, 0x1c, 0x38, 0x70, 0xfe, 0x30, 0x60, 0x60, 0xc0, 0x60, 0x60, 0x30, 0xc0,
    0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0x60, 0x60, 0x30, 0x60, 0x60, 0xc0, 0x70, 0xba, 0x1c,
    0xd8, 0xd8,
};

/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 0, .adv_w = 128, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1, .adv_w = 128, .box_w = 3, .box_h = 7, .ofs_x = 2, .ofs_y = 1},
    {.bitmap_index = 8, .adv_w = 128, .box_w = 5, .box_h = 3, .ofs_x = 1, .ofs_y = 5},
    {.bitmap_index = 11, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 18, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 25, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 32, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 39, .adv_w = 128, .box_w = 2, .box_h = 3, .ofs_x = 2, .ofs_y = 5},
    {.bitmap_index = 42, .adv_w = 128, .box_w = 4, .box_h = 7, .ofs_x = 2, .ofs_y = 1},
    {.bitmap_index = 49, .adv_w = 128, .box_w = 4, .box_h = 7, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 56, .adv_w = 128, .box_w = 7, .box_h = 5, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 61, .adv_w = 128, .box_w = 6, .box_h = 5, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 66, .adv_w = 128, .box_w = 3, .box_h = 3, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 69, .adv_w = 128, .box_w = 6, .box_h = 1, .ofs_x = 1, .ofs_y = 4},
    {.bitmap_index = 70, .adv_w = 128, .box_w = 2, .box_h = 2, .ofs_x = 2, .ofs_y = 1},
    {.bitmap_index = 72, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 79, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 86, .adv_w = 128, .box_w = 6, .box_h = 7, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 93, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 100, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 107, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 114, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 121, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 128, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 135, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 142, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 149, .adv_w = 128, .box_w = 2, .box_h = 5, .ofs_x = 2, .ofs_y = 2},
    {.bitmap_index = 154, .adv_w = 128, .box_w = 3, .box_h = 6, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 160, .adv_w = 128, .box_w = 5, .box_h = 7, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 167, .adv_w = 128, .box_w = 7, .box_h = 3, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 170, .adv_w = 128, .box_w = 5, .box_h = 7, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 177, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 184, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 191, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 198, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 205, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 212, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 219, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 226, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 233, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 240, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 247, .adv_w = 128, .box_w = 6, .box_h = 7, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 254, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 261, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 268, .adv_w = 128, .box_w = 6, .box_h = 7, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 275, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 282, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 289, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 296, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 303, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 310, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 317, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 324, .adv_w = 128, .box_w = 6, .box_h = 7, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 331, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 338, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 345, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 352, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 359, .adv_w = 128, .box_w = 6, .box_h = 7, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 366, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 373, .adv_w = 128, .box_w = 4, .box_h = 7, .ofs_x = 2, .ofs_y = 1},
    {.bitmap_index = 380, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 387, .adv_w = 128, .box_w = 4, .box_h = 7, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 394, .adv_w = 128, .box_w = 5, .box_h = 2, .ofs_x = 1, .ofs_y = 6},
    {.bitmap_index = 396, .adv_w = 128, .box_w = 7, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 397, .adv_w = 128, .box_w = 2, .box_h = 2, .ofs_x = 3, .ofs_y = 6},
    {.bitmap_index = 399, .adv_w = 128, .box_w = 7, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 404, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 411, .adv_w = 128, .box_w = 7, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 416, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 423, .adv_w = 128, .box_w = 7, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 428, .adv_w = 128, .box_w = 6, .box_h = 7, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 435, .adv_w = 128, .box_w = 7, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 441, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 448, .adv_w = 128, .box_w = 6, .box_h = 7, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 455, .adv_w = 128, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 463, .adv_w = 128, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 470, .adv_w = 128, .box_w = 6, .box_h = 7, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 477, .adv_w = 128, .box_w = 7, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 482, .adv_w = 128, .box_w = 7, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 487, .adv_w = 128, .box_w = 7, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 492, .adv_w = 128, .box_w = 7, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 498, .adv_w = 128, .box_w = 7, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 504, .adv_w = 128, .box_w = 6, .box_h = 5, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 509, .adv_w = 128, .box_w = 7, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 514, .adv_w = 128, .box_w = 6, .box_h = 7, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 521, .adv_w = 128, .box_w = 7, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 526, .adv_w = 128, .box_w = 6, .box_h = 5, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 531, .adv_w = 128, .box_w = 7, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 536, .adv_w = 128, .box_w = 7, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 541, .adv_w = 128, .box_w = 7, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 547, .adv_w = 128, .box_w = 7, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 552, .adv_w = 128, .box_w = 4, .box_h = 7, .ofs_x = 2, .ofs_y = 1},
    {.bitmap_index = 559, .adv_w = 128, .box_w = 2, .box_h = 7, .ofs_x = 3, .ofs_y = 1},
    {.bitmap_index = 566, .adv_w = 128, .box_w = 4, .box_h = 7, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 573, .adv_w = 128, .box_w = 7, .box_h = 3, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 576, .adv_w = 128, .box_w = 5, .box_h = 2, .ofs_x = 1, .ofs_y = 1},
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
const lv_font_t lv_font_pressstart2p_regular_8 = {
#else
lv_font_t lv_font_pressstart2p_regular_8 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
    .line_height = 8,
    .base_line = 0,
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
