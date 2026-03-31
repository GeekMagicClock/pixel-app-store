#!/usr/bin/env python3
from __future__ import annotations

import argparse
import subprocess
import tempfile
from pathlib import Path

W = 64
H = 32
BG = (0, 0, 0)
PANEL = (8, 33, 33)
PANEL_2 = (16, 66, 66)
TEXT = (245, 245, 240)
TEXT_DIM = (150, 160, 165)
ACCENT = (0, 255, 220)
WARN = (255, 190, 80)
OK = (120, 240, 160)

FONT = {
    " ": ("000", "000", "000", "000", "000"),
    "/": ("001", "001", "010", "100", "100"),
    "&": ("010", "101", "010", "101", "011"),
    "'": ("010", "010", "000", "000", "000"),
    ",": ("000", "000", "000", "010", "100"),
    ".": ("000", "000", "000", "010", "010"),
    ":": ("000", "010", "000", "010", "000"),
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
    "A": ("010", "101", "111", "101", "101"),
    "C": ("011", "100", "100", "100", "011"),
    "D": ("110", "101", "101", "101", "110"),
    "E": ("111", "100", "110", "100", "111"),
    "F": ("111", "100", "110", "100", "100"),
    "G": ("011", "100", "101", "101", "011"),
    "H": ("101", "101", "111", "101", "101"),
    "I": ("111", "010", "010", "010", "111"),
    "K": ("101", "101", "110", "101", "101"),
    "L": ("100", "100", "100", "100", "111"),
    "M": ("101", "111", "111", "101", "101"),
    "N": ("101", "111", "111", "111", "101"),
    "O": ("111", "101", "101", "101", "111"),
    "P": ("110", "101", "110", "100", "100"),
    "R": ("110", "101", "110", "101", "101"),
    "S": ("011", "100", "111", "001", "110"),
    "T": ("111", "010", "010", "010", "010"),
    "U": ("101", "101", "101", "101", "111"),
    "V": ("101", "101", "101", "101", "010"),
    "W": ("101", "101", "111", "111", "101"),
    "Y": ("101", "101", "010", "010", "010"),
}

SAMPLE = {
    "source": "NPR",
    "healthy": "3/3",
    "status": "LIVE",
    "headline": "FED HOLDS RATES AS MARKETS WATCH INFLATION DATA",
}


def new_canvas(scale: int):
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


def draw_glyph(canvas, ch: str, x: int, y: int, color, scale: int) -> None:
    pat = FONT.get(ch, FONT[" "])
    for row, line in enumerate(pat):
        for col, bit in enumerate(line):
            if bit == "1":
                set_px(canvas, x + col, y + row, color, scale)


def draw_text(canvas, text: str, x: int, y: int, color, scale: int, shadow: bool = False) -> None:
    cur = x
    if shadow:
        draw_text(canvas, text, x + 1, y + 1, BG, scale, shadow=False)
    for ch in text:
        draw_glyph(canvas, ch, cur, y, color, scale)
        cur += 4


def text_width(text: str) -> int:
    return max(0, len(text) * 4 - 1)


def render(scale: int, scroll_x: int) -> list[list[tuple[int, int, int]]]:
    headline = SAMPLE["headline"]
    canvas = new_canvas(scale)
    rect(canvas, 0, 0, W, H, BG, scale)
    rect(canvas, 0, 0, W, 1, PANEL_2, scale)
    rect(canvas, 0, 24, W, 1, PANEL_2, scale)
    rect(canvas, 0, 25, W, 7, PANEL, scale)

    draw_text(canvas, headline, scroll_x, 8, TEXT, scale, shadow=True)
    draw_text(canvas, SAMPLE["source"], 2, 26, ACCENT, scale, shadow=True)
    draw_text(canvas, SAMPLE["healthy"], 23, 26, TEXT_DIM, scale, shadow=True)
    draw_text(canvas, SAMPLE["status"], 46, 26, OK, scale, shadow=True)
    return canvas


def check_layout() -> None:
    headline_y = (8, 12)
    footer_y = (25, 31)
    assert headline_y[1] < footer_y[0], "headline overlaps footer"
    source_box = (2, 26, 14, 30)
    health_box = (23, 26, 36, 30)
    status_box = (46, 26, 61, 30)
    assert source_box[2] < health_box[0], "source too close to health"
    assert health_box[2] < status_box[0], "health too close to status"
    assert text_width(SAMPLE["source"]) <= 12, "source too wide"
    assert text_width(SAMPLE["healthy"]) <= 13, "health text too wide"
    assert text_width(SAMPLE["status"]) <= 15, "status too wide"


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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    parser.add_argument("--scale", type=int, default=1)
    parser.add_argument("--scroll-x", type=int, default=2)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if args.check:
        check_layout()
    canvas = render(args.scale, args.scroll_x)
    save_image(canvas, Path(args.output))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
