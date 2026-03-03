#!/usr/bin/env python3

import argparse
from pathlib import Path

import freetype


def iter_pixels_mono(bitmap, x, y):
    pitch = bitmap.pitch
    row = bitmap.buffer[y * pitch : (y + 1) * pitch]
    byte = row[x >> 3]
    bit = 7 - (x & 7)
    return (byte >> bit) & 1


def pack_bpp1(bits, w, h):
    out = bytearray()
    for y in range(h):
        byte = 0
        n = 0
        for x in range(w):
            byte = (byte << 1) | (1 if bits[y][x] else 0)
            n += 1
            if n == 8:
                out.append(byte)
                byte = 0
                n = 0
        if n != 0:
            byte <<= 8 - n
            out.append(byte)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ttf", required=True)
    ap.add_argument("--size", type=int, default=8)
    ap.add_argument("--out", required=True)
    ap.add_argument("--name", default="lv_font_silkscreen_regular_8")
    ap.add_argument("--range", default="0x20-0x7F")
    args = ap.parse_args()

    ttf = Path(args.ttf)
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)

    r0, r1 = args.range.split("-")
    start = int(r0, 16)
    end = int(r1, 16)
    if end < start:
        raise SystemExit("bad range")

    face = freetype.Face(str(ttf))
    face.set_pixel_sizes(0, args.size)

    glyph_bitmap = bytearray()
    glyph_dsc = []

    # id=0 reserved
    glyph_dsc.append(
        dict(bitmap_index=0, adv_w=0, box_w=0, box_h=0, ofs_x=0, ofs_y=0)
    )

    for cp in range(start, end + 1):
        ch = chr(cp)
        face.load_char(
            ch,
            freetype.FT_LOAD_RENDER
            | freetype.FT_LOAD_TARGET_MONO
            | freetype.FT_LOAD_MONOCHROME,
        )
        g = face.glyph
        bm = g.bitmap
        box_w = bm.width
        box_h = bm.rows
        ofs_x = g.bitmap_left
        ofs_y = g.bitmap_top - box_h
        adv_px = g.advance.x >> 6
        adv_w = adv_px * 16

        if box_w == 0 or box_h == 0:
            bitmap_index = len(glyph_bitmap)
            glyph_bitmap.append(0x00)
            glyph_dsc.append(
                dict(
                    bitmap_index=bitmap_index,
                    adv_w=adv_w,
                    box_w=0,
                    box_h=0,
                    ofs_x=0,
                    ofs_y=0,
                )
            )
            continue

        bits = [[0] * box_w for _ in range(box_h)]
        for y in range(box_h):
            for x in range(box_w):
                bits[y][x] = iter_pixels_mono(bm, x, y)

        packed = pack_bpp1(bits, box_w, box_h)
        bitmap_index = len(glyph_bitmap)
        glyph_bitmap.extend(packed)
        glyph_dsc.append(
            dict(
                bitmap_index=bitmap_index,
                adv_w=adv_w,
                box_w=box_w,
                box_h=box_h,
                ofs_x=ofs_x,
                ofs_y=ofs_y,
            )
        )

    # Metrics
    line_height = face.size.height >> 6
    base_line = (-face.size.descender) >> 6

    def c_array_bytes(name, data, indent="    "):
        lines = [f"static LV_ATTRIBUTE_LARGE_CONST const uint8_t {name}[] = {{"]
        for i in range(0, len(data), 16):
            chunk = data[i : i + 16]
            lines.append(indent + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
        lines.append("};")
        return "\n".join(lines)

    def c_array_glyph_dsc(name, dscs):
        lines = [f"static const lv_font_fmt_txt_glyph_dsc_t {name}[] = {{"]
        for d in dscs:
            lines.append(
                "    {.bitmap_index = %d, .adv_w = %d, .box_w = %d, .box_h = %d, .ofs_x = %d, .ofs_y = %d},"
                % (
                    d["bitmap_index"],
                    d["adv_w"],
                    d["box_w"],
                    d["box_h"],
                    d["ofs_x"],
                    d["ofs_y"],
                )
            )
        lines.append("};")
        return "\n".join(lines)

    cmap_len = (end - start + 1)

    c = []
    c.append("/*******************************************************************************")
    c.append(f" * Size: {args.size} px")
    c.append(" * Bpp: 1")
    c.append(f" * Source: {ttf.name}")
    c.append(f" * Range: 0x{start:02X}-0x{end:02X} (ASCII)")
    c.append(" ******************************************************************************/")
    c.append("")
    c.append('#include "lvgl.h"')
    c.append("")
    c.append("#if 1")
    c.append("")
    c.append("/*-----------------")
    c.append(" *    BITMAPS")
    c.append(" *----------------*/")
    c.append("")
    c.append("/*Store the image of the glyphs*/")
    c.append(c_array_bytes("glyph_bitmap", glyph_bitmap))
    c.append("")
    c.append("/*---------------------")
    c.append(" *  GLYPH DESCRIPTION")
    c.append(" *--------------------*/")
    c.append("")
    c.append(c_array_glyph_dsc("glyph_dsc", glyph_dsc))
    c.append("")
    c.append("/*---------------------")
    c.append(" *  CHARACTER MAPPING")
    c.append(" *--------------------*/")
    c.append("")
    c.append("static const lv_font_fmt_txt_cmap_t cmaps[] = {")
    c.append("    {")
    c.append(
        f"        .range_start = {start}, .range_length = {cmap_len}, .glyph_id_start = 1,"
    )
    c.append(
        "        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY"
    )
    c.append("    }")
    c.append("};")
    c.append("")
    c.append("/*--------------------")
    c.append(" *  ALL CUSTOM DATA")
    c.append(" *--------------------*/")
    c.append("")
    c.append("#if LVGL_VERSION_MAJOR >= 8")
    c.append("static const lv_font_fmt_txt_dsc_t font_dsc = {")
    c.append("#else")
    c.append("static lv_font_fmt_txt_dsc_t font_dsc = {")
    c.append("#endif")
    c.append("    .glyph_bitmap = glyph_bitmap,")
    c.append("    .glyph_dsc = glyph_dsc,")
    c.append("    .cmaps = cmaps,")
    c.append("    .kern_dsc = NULL,")
    c.append("    .kern_scale = 0,")
    c.append("    .cmap_num = 1,")
    c.append("    .bpp = 1,")
    c.append("    .kern_classes = 0,")
    c.append("    .bitmap_format = 0,")
    c.append("    .stride = 1,")
    c.append("};")
    c.append("")
    c.append("/*-----------------")
    c.append(" *  PUBLIC FONT")
    c.append(" *----------------*/")
    c.append("")
    c.append("#if LVGL_VERSION_MAJOR >= 8")
    c.append(f"const lv_font_t {args.name} = {{")
    c.append("#else")
    c.append(f"lv_font_t {args.name} = {{")
    c.append("#endif")
    c.append("    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,")
    c.append("    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,")
    c.append(f"    .line_height = {line_height},")
    c.append(f"    .base_line = {base_line},")
    c.append("#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)")
    c.append("    .subpx = LV_FONT_SUBPX_NONE,")
    c.append("#endif")
    c.append("#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8")
    c.append("    .underline_position = 0,")
    c.append("    .underline_thickness = 0,")
    c.append("#endif")
    c.append("    .dsc = &font_dsc")
    c.append("};")
    c.append("")
    c.append("#endif")

    out.write_text("\n".join(c) + "\n", encoding="utf-8")
    print(f"Wrote {out} ({len(glyph_bitmap)} bytes bitmap, {len(glyph_dsc)} glyphs)")


if __name__ == "__main__":
    main()
