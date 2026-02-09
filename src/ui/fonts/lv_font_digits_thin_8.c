/*******************************************************************************
 * Name: digits-thin-8 (custom)
 * Size: 8 px
 * Bpp: 1
 * Range: 0x20-0x5A (space..'Z')
 *
 * Notes:
 * - 5x7 glyphs, mostly monospace (6px). '.' is narrow and space is smaller.
 * - Intended for low-res pixel displays (HUB75 64x32).
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
  #include "lvgl.h"
#else
  #include "lvgl.h"
#endif

#include "ui/fonts/lv_font_digits_thin_8.h"

/*-----------------
 *    BITMAPS
 *----------------*/

/* 59 glyphs (0x20..0x5A), 5 bytes per glyph (5x7 @ 1bpp = 35 bits). */
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */
    0x00, 0x00, 0x00, 0x00, 0x00,
    /* U+0021 "!" */
    0x00, 0x00, 0x00, 0x00, 0x00,
    /* U+0022 "\"" */
    0x00, 0x00, 0x00, 0x00, 0x00,
    /* U+0023 "#" */
    0x00, 0x00, 0x00, 0x00, 0x00,
    /* U+0024 "$" */
    0x00, 0x00, 0x00, 0x00, 0x00,
    /* U+0025 "%" */
    0xc6, 0x44, 0x44, 0x4c, 0x60,
    /* U+0026 "&" */
    0x00, 0x00, 0x00, 0x00, 0x00,
    /* U+0027 "'" */
    0x00, 0x00, 0x00, 0x00, 0x00,
    /* U+0028 "(" */
    0x00, 0x00, 0x00, 0x00, 0x00,
    /* U+0029 ")" */
    0x00, 0x00, 0x00, 0x00, 0x00,
    /* U+002A "*" */
    0x00, 0x00, 0x00, 0x00, 0x00,
    /* U+002B "+" */
    0x01, 0x09, 0xf2, 0x10, 0x00,
    /* U+002C "," */
    0x00, 0x00, 0x00, 0x00, 0x00,
    /* U+002D "-" */
    0x00, 0x01, 0xf0, 0x00, 0x00,
    /* U+002E "." */
    0x00, 0x00, 0x00, 0x02, 0x00,
    /* U+002F "/" */
    0x00, 0x00, 0x00, 0x00, 0x00,
    /* U+0030 "0" */
    0x74, 0x67, 0x5c, 0xc5, 0xc0,
    /* U+0031 "1" */
    0x23, 0x08, 0x42, 0x11, 0xc0,
    /* U+0032 "2" */
    0x74, 0x42, 0x22, 0x23, 0xe0,
    /* U+0033 "3" */
    0xf0, 0x42, 0xe0, 0x87, 0xc0,
    /* U+0034 "4" */
    0x11, 0x95, 0x2f, 0x88, 0x40,
    /* U+0035 "5" */
    0xfc, 0x21, 0xe0, 0x87, 0xc0,
    /* U+0036 "6" */
    0x74, 0x21, 0xe8, 0xc5, 0xc0,
    /* U+0037 "7" */
    0xf8, 0x44, 0x44, 0x21, 0x00,
    /* U+0038 "8" */
    0x74, 0x62, 0xe8, 0xc5, 0xc0,
    /* U+0039 "9" */
    0x74, 0x62, 0xf0, 0x85, 0xc0,
    /* U+003A ":" */
    0x04, 0x00, 0x08, 0x00, 0x00,
    /* U+003B ";" */
    0x00, 0x00, 0x00, 0x00, 0x00,
    /* U+003C "<" */
    0x00, 0x00, 0x00, 0x00, 0x00,
    /* U+003D "=" */
    0x00, 0x00, 0x00, 0x00, 0x00,
    /* U+003E ">" */
    0x00, 0x00, 0x00, 0x00, 0x00,
    /* U+003F "?" */
    0x00, 0x00, 0x00, 0x00, 0x00,
    /* U+0040 "@" */
    0x00, 0x00, 0x00, 0x00, 0x00,
    /* U+0041 "A" */
    0x74, 0x63, 0xf8, 0xc6, 0x20,
    /* U+0042 "B" */
    0xf4, 0x63, 0xe8, 0xc7, 0xc0,
    /* U+0043 "C" */
    0x74, 0x61, 0x08, 0x45, 0xc0,
    /* U+0044 "D" */
    0xf4, 0x63, 0x18, 0xc7, 0xc0,
    /* U+0045 "E" */
    0xfc, 0x21, 0xe8, 0x43, 0xe0,
    /* U+0046 "F" */
    0xfc, 0x21, 0xe8, 0x42, 0x00,
    /* U+0047 "G" */
    0x74, 0x61, 0x78, 0xc5, 0xc0,
    /* U+0048 "H" */
    0x8c, 0x63, 0xf8, 0xc6, 0x20,
    /* U+0049 "I" */
    0x71, 0x08, 0x42, 0x11, 0xc0,
    /* U+004A "J" */
    0x38, 0x84, 0x21, 0x49, 0x80,
    /* U+004B "K" */
    0x8c, 0xa9, 0x8a, 0x4a, 0x20,
    /* U+004C "L" */
    0x84, 0x21, 0x08, 0x43, 0xe0,
    /* U+004D "M" */
    0x8e, 0xeb, 0x58, 0xc6, 0x20,
    /* U+004E "N" */
    0x8e, 0x6b, 0x38, 0xc6, 0x20,
    /* U+004F "O" */
    0x74, 0x63, 0x18, 0xc5, 0xc0,
    /* U+0050 "P" */
    0xf4, 0x63, 0xe8, 0x42, 0x00,
    /* U+0051 "Q" */
    0x74, 0x63, 0x1a, 0xc9, 0xa0,
    /* U+0052 "R" */
    0xf4, 0x63, 0xea, 0x4a, 0x20,
    /* U+0053 "S" */
    0x7c, 0x20, 0xe0, 0x87, 0xc0,
    /* U+0054 "T" */
    0xf9, 0x08, 0x42, 0x10, 0x80,
    /* U+0055 "U" */
    0x8c, 0x63, 0x18, 0xc5, 0xc0,
    /* U+0056 "V" */
    0x8c, 0x63, 0x18, 0xa8, 0x80,
    /* U+0057 "W" */
    0x8c, 0x63, 0x5a, 0xd5, 0x40,
    /* U+0058 "X" */
    0x8c, 0x54, 0x45, 0x46, 0x20,
    /* U+0059 "Y" */
    0x8c, 0x54, 0x42, 0x10, 0x80,
    /* U+005A "Z" */
    0xf8, 0x44, 0x44, 0x43, 0xe0,
};

/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    /* 0x20..0x5A, 5 bytes each */
    {.bitmap_index = 0, .adv_w = 48, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0020 */
    {.bitmap_index = 5, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0021 */
    {.bitmap_index = 10, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0022 */
    {.bitmap_index = 15, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0023 */
    {.bitmap_index = 20, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0024 */
    {.bitmap_index = 25, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0025 */
    {.bitmap_index = 30, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0026 */
    {.bitmap_index = 35, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0027 */
    {.bitmap_index = 40, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0028 */
    {.bitmap_index = 45, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0029 */
    {.bitmap_index = 50, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+002A */
    {.bitmap_index = 55, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+002B */
    {.bitmap_index = 60, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+002C */
    {.bitmap_index = 65, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+002D */
    {.bitmap_index = 70, .adv_w = 32, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+002E */
    {.bitmap_index = 75, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+002F */
    {.bitmap_index = 80, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0030 */
    {.bitmap_index = 85, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0031 */
    {.bitmap_index = 90, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0032 */
    {.bitmap_index = 95, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0033 */
    {.bitmap_index = 100, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0034 */
    {.bitmap_index = 105, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0035 */
    {.bitmap_index = 110, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0036 */
    {.bitmap_index = 115, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0037 */
    {.bitmap_index = 120, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0038 */
    {.bitmap_index = 125, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0039 */
    {.bitmap_index = 130, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+003A */
    {.bitmap_index = 135, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+003B */
    {.bitmap_index = 140, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+003C */
    {.bitmap_index = 145, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+003D */
    {.bitmap_index = 150, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+003E */
    {.bitmap_index = 155, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+003F */
    {.bitmap_index = 160, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0040 */
    {.bitmap_index = 165, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0041 */
    {.bitmap_index = 170, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0042 */
    {.bitmap_index = 175, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0043 */
    {.bitmap_index = 180, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0044 */
    {.bitmap_index = 185, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0045 */
    {.bitmap_index = 190, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0046 */
    {.bitmap_index = 195, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0047 */
    {.bitmap_index = 200, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0048 */
    {.bitmap_index = 205, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0049 */
    {.bitmap_index = 210, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+004A */
    {.bitmap_index = 215, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+004B */
    {.bitmap_index = 220, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+004C */
    {.bitmap_index = 225, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+004D */
    {.bitmap_index = 230, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+004E */
    {.bitmap_index = 235, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+004F */
    {.bitmap_index = 240, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0050 */
    {.bitmap_index = 245, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0051 */
    {.bitmap_index = 250, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0052 */
    {.bitmap_index = 255, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0053 */
    {.bitmap_index = 260, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0054 */
    {.bitmap_index = 265, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0055 */
    {.bitmap_index = 270, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0056 */
    {.bitmap_index = 275, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0057 */
    {.bitmap_index = 280, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0058 */
    {.bitmap_index = 285, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+0059 */
    {.bitmap_index = 290, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 1}, /* U+005A */
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const lv_font_fmt_txt_cmap_t cmaps[] = {
    {
        .range_start = 32,
        .range_length = 59,
        .glyph_id_start = 1,
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
    .cmap_num = 1,
    .bpp = 1,
    .kern_classes = 0,
    .bitmap_format = 0,
};

/*-----------------
 *  PUBLIC FONT
 *----------------*/

#if LVGL_VERSION_MAJOR >= 8
const lv_font_t lv_font_digits_thin_8 = {
#else
lv_font_t lv_font_digits_thin_8 = {
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
    .dsc = &font_dsc,
};
