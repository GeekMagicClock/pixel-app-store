/*******************************************************************************
 * Name: time-big-10 (custom)
 * Size: 10 px (7x9 glyphs)
 * Bpp: 1
 * Range: 0x30-0x3A ('0'..'9', ':')
 *
 * Notes:
 * - Digit advance is 8px; ':' is 4px.
 * - Intended for 64x32 pixel displays to render HH:MM:SS prominently.
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
  #include "lvgl.h"
#else
  #include "lvgl.h"
#endif

#include "ui/fonts/lv_font_time_big_10.h"

/*-----------------
 *    BITMAPS
 *----------------*/

static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " (same width as ':', blank) */
    0x00, 0x00, 0x00, 0x00,
    /* U+0030 "0" */
    0x7d, 0x06, 0x1c, 0x59, 0x34, 0x70, 0xc1, 0x7c,
    /* U+0031 "1" */
    0x30, 0xa0, 0x40, 0x81, 0x02, 0x04, 0x08, 0x7c,
    /* U+0032 "2" */
    0x7d, 0x04, 0x08, 0x21, 0x8c, 0x20, 0x40, 0xfe,
    /* U+0033 "3" */
    0xfc, 0x04, 0x09, 0xe0, 0x20, 0x40, 0xc1, 0x7c,
    /* U+0034 "4" */
    0x0c, 0x28, 0x92, 0x28, 0x5f, 0xc1, 0x02, 0x04,
    /* U+0035 "5" */
    0xff, 0x02, 0x07, 0xe0, 0x20, 0x40, 0xc1, 0x7c,
    /* U+0036 "6" */
    0x3c, 0x82, 0x07, 0xe8, 0x30, 0x60, 0xc1, 0x7c,
    /* U+0037 "7" */
    0xfe, 0x04, 0x10, 0x41, 0x04, 0x08, 0x10, 0x20,
    /* U+0038 "8" */
    0x7d, 0x06, 0x0b, 0xe8, 0x30, 0x60, 0xc1, 0x7c,
    /* U+0039 "9" */
    0x7d, 0x06, 0x0c, 0x17, 0xe0, 0x40, 0x82, 0xf8,
    /* U+003A ":" */
    0x09, 0x00, 0x90, 0x00,
};

/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 64, .box_w = 3, .box_h = 9, .ofs_x = 0, .ofs_y = 0},   /* ' ' */
    {.bitmap_index = 4, .adv_w = 128, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0},  /* '0' */
    {.bitmap_index = 12, .adv_w = 128, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0}, /* '1' */
    {.bitmap_index = 20, .adv_w = 128, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0}, /* '2' */
    {.bitmap_index = 28, .adv_w = 128, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0}, /* '3' */
    {.bitmap_index = 36, .adv_w = 128, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0}, /* '4' */
    {.bitmap_index = 44, .adv_w = 128, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0}, /* '5' */
    {.bitmap_index = 52, .adv_w = 128, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0}, /* '6' */
    {.bitmap_index = 60, .adv_w = 128, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0}, /* '7' */
    {.bitmap_index = 68, .adv_w = 128, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0}, /* '8' */
    {.bitmap_index = 76, .adv_w = 128, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0}, /* '9' */
    {.bitmap_index = 84, .adv_w = 64, .box_w = 3, .box_h = 9, .ofs_x = 0, .ofs_y = 0},  /* ':' */
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const lv_font_fmt_txt_cmap_t cmaps[] = {
    {
        .range_start = 32, /* ' ' */
        .range_length = 1,
        .glyph_id_start = 1,
        .unicode_list = NULL,
        .glyph_id_ofs_list = NULL,
        .list_length = 0,
        .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY,
    },
    {
        .range_start = 48,  /* '0' */
        .range_length = 11, /* '0'..':' */
        .glyph_id_start = 2,
        .unicode_list = NULL,
        .glyph_id_ofs_list = NULL,
        .list_length = 0,
        .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY,
    },
};

/*--------------------
 *  ALL CUSTOM DATA
 *-------------------*/

static const lv_font_fmt_txt_dsc_t font_dsc = {
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 2,
    .bpp = 1,
    .kern_classes = 0,
    .bitmap_format = 0,
};

/*-----------------
 *  PUBLIC FONT
 *----------------*/

#if LVGL_VERSION_MAJOR >= 8
const lv_font_t lv_font_time_big_10 = {
#else
lv_font_t lv_font_time_big_10 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
    .line_height = 10,
    .base_line = 0,
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = 0,
    .underline_thickness = 0,
#endif
    .dsc = &font_dsc,
};
