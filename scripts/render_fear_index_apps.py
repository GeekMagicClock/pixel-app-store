#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import subprocess
import tempfile
from pathlib import Path
from typing import Sequence

W = 64
H = 32
BG = (0, 0, 0)
LINE = (24, 24, 24)
TEXT = (255, 250, 255)
TEXT_DIM = (156, 153, 156)
OK = (131, 255, 131)
WARN = (255, 165, 0)
ERR = (255, 0, 0)
NEUTRAL = (255, 255, 0)
GREED = (0, 255, 255)

SAMPLES = {
    "stock": {"score": 42, "label": "EXTREME FEAR", "delta": -9.8, "history": [42, 41, 43, 45, 47, 44, 42], "title": "STOCK"},
    "crypto": {"score": 57, "label": "EXTREME FEAR", "delta": 12.0, "history": [26, 23, 11, 12, 10, 8, 11], "title": "CRYPTO"},
}

DIGITS = {
    "0": ("111", "101", "101", "101", "111"),
    "1": ("010", "110", "010", "010", "111"),
    "2": ("111", "001", "111", "100", "111"),
    "3": ("111", "001", "111", "001", "111"),
    "4": ("101", "101", "111", "001", "001"),
    "5": ("111", "100", "111", "001", "111"),
    "6": ("111", "100", "111", "101", "111"),
    "7": ("111", "001", "010", "010", "010"),
    "8": ("111", "101", "111", "101", "111"),
    "9": ("111", "101", "111", "001", "111"),
}

FONT_C = Path(__file__).resolve().parents[1] / "src" / "ui" / "lv_font_silkscreen_regular_8.c"
_GLYPHS: dict[str, dict] | None = None


def new_canvas(scale: int) -> list[list[tuple[int, int, int]]]:
    return [[BG for _ in range(W * scale)] for _ in range(H * scale)]


def set_px(canvas, x: int, y: int, color, scale: int) -> None:
    max_w = len(canvas[0])
    max_h = len(canvas)
    for yy in range(y * scale, y * scale + scale):
        for xx in range(x * scale, x * scale + scale):
            if 0 <= xx < max_w and 0 <= yy < max_h:
                canvas[yy][xx] = color


def rect(canvas, x: int, y: int, w: int, h: int, color, scale: int) -> None:
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            set_px(canvas, xx, yy, color, scale)


def load_glyphs() -> dict[str, dict]:
    global _GLYPHS
    if _GLYPHS is not None:
        return _GLYPHS

    text = FONT_C.read_text(encoding="utf-8")
    bitmap_match = re.search(r"glyph_bitmap\[\] = \{(.*?)\};", text, re.S)
    dsc_match = re.search(r"glyph_dsc\[\] = \{(.*?)\};", text, re.S)
    if not bitmap_match or not dsc_match:
        raise SystemExit("failed to parse lv_font_silkscreen_regular_8.c")

    bitmap = [int(part.strip(), 16) for part in bitmap_match.group(1).replace("\n", " ").split(",") if part.strip()]
    entries = re.findall(
        r"\{\.bitmap_index = (\d+), \.adv_w = (\d+), \.box_w = (\d+), \.box_h = (\d+), \.ofs_x = (-?\d+), \.ofs_y = (-?\d+)\}",
        dsc_match.group(1),
    )

    glyphs: dict[str, dict] = {}
    for codepoint in range(32, 128):
        idx = codepoint - 31
        bitmap_index, adv_w, box_w, box_h, ofs_x, ofs_y = [int(x) for x in entries[idx]]
        rows: list[int] = []
        if box_w and box_h:
            row_bytes = (box_w + 7) // 8
            count = row_bytes * box_h
            rows = bitmap[bitmap_index:bitmap_index + count]
        glyphs[chr(codepoint)] = {
            "adv": adv_w // 16,
            "box_w": box_w,
            "box_h": box_h,
            "ofs_x": ofs_x,
            "ofs_y": ofs_y,
            "rows": rows,
        }
    _GLYPHS = glyphs
    return glyphs


def draw_glyph(canvas, ch: str, x: int, y: int, color, scale: int) -> int:
    glyphs = load_glyphs()
    g = glyphs.get(ch) or glyphs[" "]
    row_bytes = (g["box_w"] + 7) // 8 if g["box_w"] else 0
    for row in range(g["box_h"]):
        row_data = g["rows"][row * row_bytes:(row + 1) * row_bytes]
        for col in range(g["box_w"]):
            byte = row_data[col // 8]
            if byte & (0x80 >> (col % 8)):
                set_px(canvas, x + g["ofs_x"] + col, y - g["ofs_y"] + row + 1, color, scale)
    return g["adv"]


def draw_text(canvas, text: str, x: int, y: int, color, scale: int, shadow: bool = False) -> None:
    cur = x
    if shadow:
        draw_text(canvas, text, x + 1, y + 1, BG, scale, shadow=False)
    for ch in text:
        cur += draw_glyph(canvas, ch, cur, y, color, scale)


def text_width(text: str) -> int:
    glyphs = load_glyphs()
    return sum((glyphs.get(ch) or glyphs[" "])["adv"] for ch in text)


def text_bounds_width(text: str) -> int:
    glyphs = load_glyphs()
    cur = 0
    right = 0
    for ch in text:
        g = glyphs.get(ch) or glyphs[" "]
        if g["box_w"] > 0:
            right = max(right, cur + g["ofs_x"] + g["box_w"])
        cur += g["adv"]
    return right


def draw_big_digit(canvas, digit: str, x: int, y: int, color, scale: int) -> None:
    pat = DIGITS[digit]
    mul = 3
    for row, line in enumerate(pat):
        for col, bit in enumerate(line):
            if bit == "1":
                rect(canvas, x + col * mul, y + row * mul, mul, mul, color, scale)


def draw_number(canvas, text: str, x: int, y: int, color, scale: int) -> None:
    cur = x
    for ch in text:
        draw_big_digit(canvas, ch, cur, y, color, scale)
        cur += 12


def band_color(score: float) -> tuple[int, int, int]:
    if score < 25:
        return ERR
    if score < 45:
        return WARN
    if score < 56:
        return NEUTRAL
    if score < 75:
        return OK
    return GREED


def split_label(label: str) -> tuple[str, str | None]:
    text = label.upper()
    if text == "EXTREME FEAR":
        return "EXTREME", "FEAR"
    if text == "EXTREME GREED":
        return "EXTREME", "GREED"
    return text, None


def render_card(kind: str, scale: int) -> list[list[tuple[int, int, int]]]:
    data = SAMPLES[kind]
    canvas = new_canvas(scale)
    rect(canvas, 0, 0, W, H, BG, scale)
    rect(canvas, 0, 0, W, 1, LINE, scale)
    rect(canvas, 0, H - 1, W, 1, LINE, scale)

    draw_text(canvas, data["title"], 2, 0, TEXT, scale, shadow=True)

    hist: Sequence[float] = data["history"]
    base_x = 61
    for i, value in enumerate(hist[:7]):
        height = 1 + int(value / 17)
        x = base_x - i * 3
        rect(canvas, x, 7 - height, 2, height, band_color(value), scale)

    accent = band_color(data["score"])
    draw_number(canvas, f"{data['score']:02d}", 2, 8, accent, scale)
    label_1, label_2 = split_label(data["label"])
    if label_2:
        draw_text(canvas, label_1, 29, 6, accent, scale, shadow=True)
        draw_text(canvas, label_2, 29, 12, accent, scale, shadow=True)
    else:
        draw_text(canvas, label_1, 29, 9, accent, scale, shadow=True)

    delta = data["delta"]
    delta_text = f"{delta:+.1f} 1D" if kind == "stock" else f"{delta:+.0f} 1D"
    draw_text(canvas, delta_text, 29, 19, OK if delta >= 0 else ERR, scale, shadow=True)

    y = 28
    for i in range(20):
        score = (i / 19) * 100
        x = 2 + i * 3
        rect(canvas, x, y, 2, 2, band_color(score), scale)
    px = 2 + int((data["score"] / 100) * 57)
    rect(canvas, px, y - 1, 3, 4, TEXT, scale)
    return canvas


def render_from_values(
    kind: str,
    *,
    score: int,
    label: str,
    delta: float,
    history: list[float],
    title: str,
    scale: int,
) -> list[list[tuple[int, int, int]]]:
    prev = SAMPLES[kind]
    SAMPLES[kind] = {
        "score": score,
        "label": label,
        "delta": delta,
        "history": history,
        "title": title,
    }
    try:
        return render_card(kind, scale)
    finally:
        SAMPLES[kind] = prev


def save_ppm(canvas, path: Path) -> None:
    h = len(canvas)
    w = len(canvas[0])
    with path.open("wb") as f:
        f.write(f"P6\n{w} {h}\n255\n".encode("ascii"))
        for row in canvas:
            for r, g, b in row:
                f.write(bytes((r, g, b)))


def save_image(canvas, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.suffix.lower() == ".ppm":
        save_ppm(canvas, output)
        return
    with tempfile.TemporaryDirectory() as tmpdir:
        ppm = Path(tmpdir) / "frame.ppm"
        save_ppm(canvas, ppm)
        subprocess.run(["/usr/bin/sips", "-s", "format", "png", str(ppm), "--out", str(output)], check=True, capture_output=True)


def compare_ppm(expected: Path, actual: Path, diff: Path | None) -> tuple[int, int]:
    e = expected.read_bytes()
    a = actual.read_bytes()
    e_head_end = e.find(b"\n255\n") + 5
    a_head_end = a.find(b"\n255\n") + 5
    e_pixels = e[e_head_end:]
    a_pixels = a[a_head_end:]
    if len(e_pixels) != len(a_pixels):
        raise SystemExit("ppm size mismatch")
    bad = 0
    total = W * H
    diff_rows = bytearray()
    if diff is not None:
        diff_rows.extend(f"P6\n{W} {H}\n255\n".encode("ascii"))
    for i in range(0, len(e_pixels), 3):
        er, eg, eb = e_pixels[i:i + 3]
        ar, ag, ab = a_pixels[i:i + 3]
        delta = abs(er - ar) + abs(eg - ag) + abs(eb - ab)
        if delta > 30:
            bad += 1
            if diff is not None:
                diff_rows.extend((255, 48, 48))
        elif diff is not None:
            diff_rows.extend((32, 32, 32))
    if diff is not None:
        diff.write_bytes(diff_rows)
    return bad, total


def check_layout() -> None:
    score_box = (2, 8, 26, 23)
    label_top_box = (29, 6, 62, 10)
    label_bottom_box = (29, 12, 62, 16)
    single_label_box = (29, 9, 62, 13)
    delta_box = (29, 19, 61, 23)
    history_box = (43, 0, 62, 7)
    meter_box = (2, 27, 61, 31)
    assert score_box[2] < label_top_box[0], "score overlaps label"
    assert history_box[3] <= score_box[1], "history overlaps score"
    assert label_top_box[3] < label_bottom_box[1], "label lines overlap"
    assert label_bottom_box[3] < delta_box[1], "label overlaps delta"
    assert delta_box[3] <= meter_box[1], "delta overlaps meter"
    label_top, label_bottom = split_label(SAMPLES["stock"]["label"])
    assert text_bounds_width(label_top) <= (label_top_box[2] - label_top_box[0]), "stock label line 1 too wide"
    assert text_bounds_width(label_bottom or "") <= (label_bottom_box[2] - label_bottom_box[0]), "stock label line 2 too wide"
    label_top, label_bottom = split_label(SAMPLES["crypto"]["label"])
    assert text_bounds_width(label_top) <= (label_top_box[2] - label_top_box[0]), "crypto label line 1 too wide"
    assert text_bounds_width(label_bottom or "") <= (label_bottom_box[2] - label_bottom_box[0]), "crypto label line 2 too wide"
    assert text_bounds_width("NEUTRAL") <= (single_label_box[2] - single_label_box[0]), "single-line label too wide"
    assert text_bounds_width(f"{SAMPLES['stock']['delta']:+.1f} 1D") <= (delta_box[2] - delta_box[0]), "stock delta too wide"
    assert text_bounds_width(f"{SAMPLES['crypto']['delta']:+.0f} 1D") <= (delta_box[2] - delta_box[0]), "crypto delta too wide"


def main() -> int:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="cmd", required=True)

    render = sub.add_parser("render")
    render.add_argument("kind", choices=sorted(SAMPLES.keys()))
    render.add_argument("--output", required=True)
    render.add_argument("--scale", type=int, default=1)
    render.add_argument("--check", action="store_true")
    render.add_argument("--score", type=int)
    render.add_argument("--label")
    render.add_argument("--delta", type=float)
    render.add_argument("--history")
    render.add_argument("--title")

    compare = sub.add_parser("compare")
    compare.add_argument("--expected", required=True)
    compare.add_argument("--actual", required=True)
    compare.add_argument("--diff")

    args = parser.parse_args()
    if args.cmd == "render":
        if args.check:
            check_layout()
        if args.score is None:
            canvas = render_card(args.kind, args.scale)
        else:
            if args.label is None or args.delta is None or args.history is None:
                raise SystemExit("--score requires --label --delta and --history")
            hist = [float(x) for x in args.history.split(",") if x.strip()]
            canvas = render_from_values(
                args.kind,
                score=int(args.score),
                label=args.label,
                delta=float(args.delta),
                history=hist,
                title=args.title or SAMPLES[args.kind]["title"],
                scale=args.scale,
            )
        save_image(canvas, Path(args.output))
        return 0

    diff_path = Path(args.diff) if args.diff else None
    bad, total = compare_ppm(Path(args.expected), Path(args.actual), diff_path)
    pct = (bad * 100.0) / total
    print(f"diff_pixels={bad} total_pixels={total} diff_pct={pct:.3f}")
    return 0 if pct <= 8.0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
