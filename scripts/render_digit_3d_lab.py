#!/usr/bin/env python3
from __future__ import annotations

import argparse
import math
import subprocess
import tempfile
from pathlib import Path

W = 64
H = 32

BG = (0, 0, 8)
PANEL = (8, 16, 33)
GRID = (24, 24, 57)
TEXT = (198, 198, 198)
TEXT_DIM = (115, 117, 115)
SHADOW = (0, 0, 0)
PALETTE_A = {"front": (255, 202, 198), "top": (255, 243, 231), "side": (139, 76, 49), "edge": (255, 255, 255)}
PALETTE_B = {"front": (132, 223, 255), "top": (198, 255, 255), "side": (33, 84, 107), "edge": (239, 255, 255)}

FONT = {
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
    "B": ("110", "101", "110", "101", "110"),
    "D": ("110", "101", "101", "101", "110"),
    "E": ("111", "100", "110", "100", "111"),
    "G": ("011", "100", "101", "101", "011"),
    "I": ("111", "010", "010", "010", "111"),
    "L": ("100", "100", "100", "100", "111"),
    "M": ("101", "111", "111", "101", "101"),
    "O": ("111", "101", "101", "101", "111"),
    "P": ("110", "101", "110", "100", "100"),
    "R": ("110", "101", "110", "101", "101"),
    "S": ("011", "100", "111", "001", "110"),
    "T": ("111", "010", "010", "010", "010"),
    "V": ("101", "101", "101", "101", "010"),
    "X": ("101", "101", "010", "101", "101"),
    " ": ("000", "000", "000", "000", "000"),
}

SEGMENTS = {
    "0": (True, True, True, True, True, True, False),
    "1": (False, True, True, False, False, False, False),
    "2": (True, True, False, True, True, False, True),
    "3": (True, True, True, True, False, False, True),
    "4": (False, True, True, False, False, True, True),
    "5": (True, False, True, True, False, True, True),
    "6": (True, False, True, True, True, True, True),
    "7": (True, True, True, False, False, False, False),
    "8": (True, True, True, True, True, True, True),
    "9": (True, True, True, True, False, True, True),
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


def line(canvas, x0: int, y0: int, x1: int, y1: int, color, scale: int) -> None:
    dx = abs(x1 - x0)
    sx = 1 if x0 < x1 else -1
    dy = -abs(y1 - y0)
    sy = 1 if y0 < y1 else -1
    err = dx + dy
    while True:
        set_px(canvas, x0, y0, color, scale)
        if x0 == x1 and y0 == y1:
            break
        e2 = err * 2
        if e2 >= dy:
            err += dy
            x0 += sx
        if e2 <= dx:
            err += dx
            y0 += sy


def draw_micro_text(canvas, text: str, x: int, y: int, color, scale: int, shadow=None) -> None:
    cur = x
    for ch in text.upper():
        glyph = FONT.get(ch, FONT[" "])
        if shadow:
            for gy, row in enumerate(glyph):
                for gx, bit in enumerate(row):
                    if bit == "1":
                        set_px(canvas, cur + gx + 1, y + gy + 1, shadow, scale)
        for gy, row in enumerate(glyph):
            for gx, bit in enumerate(row):
                if bit == "1":
                    set_px(canvas, cur + gx, y + gy, color, scale)
        cur += 4


def rotate_y(x: float, y: float, z: float, a: float):
    ca = math.cos(a)
    sa = math.sin(a)
    return x * ca + z * sa, y, -x * sa + z * ca


def rotate_x(x: float, y: float, z: float, a: float):
    ca = math.cos(a)
    sa = math.sin(a)
    return x, y * ca - z * sa, y * sa + z * ca


def project(x: float, y: float, z: float, cx: float, cy: float, scale: float, cam: float):
    denom = max(0.5, cam - z)
    k = scale / denom
    return cx + x * k, cy + y * k, z


def lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * t


def draw_quad_fill(canvas, quad, color, scale: int) -> None:
    for i in range(5):
        t = i / 4.0
        ax = lerp(quad[0][0], quad[3][0], t)
        ay = lerp(quad[0][1], quad[3][1], t)
        bx = lerp(quad[1][0], quad[2][0], t)
        by = lerp(quad[1][1], quad[2][1], t)
        line(canvas, round(ax), round(ay), round(bx), round(by), color, scale)


def draw_quad_edges(canvas, quad, color, scale: int) -> None:
    for i in range(4):
        a = quad[i]
        b = quad[(i + 1) % 4]
        line(canvas, round(a[0]), round(a[1]), round(b[0]), round(b[1]), color, scale)


def make_prism(cx: float, cy: float, horizontal: bool):
    hx = 1.9 if horizontal else 0.45
    hy = 0.45 if horizontal else 1.85
    hz = 0.55
    return [
        (-hx + cx, -hy + cy, hz),
        (hx + cx, -hy + cy, hz),
        (hx + cx, hy + cy, hz),
        (-hx + cx, hy + cy, hz),
        (-hx + cx, -hy + cy, -hz),
        (hx + cx, -hy + cy, -hz),
        (hx + cx, hy + cy, -hz),
        (-hx + cx, hy + cy, -hz),
    ]


def transform_prism(prism, ay: float, ax: float, ox: float, oy: float):
    out = []
    for x, y, z in prism:
        x1, y1, z1 = rotate_y(x, y, z, ay)
        x2, y2, z2 = rotate_x(x1, y1, z1, ax)
        sx, sy, zf = project(x2, y2, z2, ox, oy, 22.0, 11.0)
        out.append((sx, sy, zf))
    return out


def segment_faces(pts, ay: float, ax: float):
    faces = [([pts[0], pts[1], pts[2], pts[3]], "front")]
    if ax < 0:
        faces.append(([pts[4], pts[5], pts[1], pts[0]], "top"))
    else:
        faces.append(([pts[3], pts[2], pts[6], pts[7]], "top"))
    if ay >= 0:
        faces.append(([pts[1], pts[5], pts[6], pts[2]], "side"))
    else:
        faces.append(([pts[4], pts[0], pts[3], pts[7]], "side"))
    return faces


def render_digit(canvas, ch: str, ox: int, oy: int, ay: float, ax: float, palette, scale: int) -> None:
    specs = [
        (0.0, -4.2, True),
        (2.4, -2.1, False),
        (2.4, 2.1, False),
        (0.0, 4.2, True),
        (-2.4, 2.1, False),
        (-2.4, -2.1, False),
        (0.0, 0.0, True),
    ]
    faces = []
    for active, spec in zip(SEGMENTS[ch], specs):
        if not active:
            continue
        prism = make_prism(*spec)
        pts = transform_prism(prism, ay, ax, ox, oy)
        for quad, kind in segment_faces(pts, ay, ax):
            z = sum(p[2] for p in quad) / 4.0
            faces.append((z, quad, kind))
    faces.sort(key=lambda item: item[0])
    for _, quad, kind in faces:
        color = palette["front"] if kind == "front" else palette["top"] if kind == "top" else palette["side"]
        draw_quad_fill(canvas, quad, color, scale)
        draw_quad_edges(canvas, quad, palette["edge"], scale)
    rect(canvas, ox - 6, oy + 9, 12, 1, SHADOW, scale)


def draw_floor(canvas, scale: int) -> None:
    for i in range(5):
        y = 21 + i * 2
        line(canvas, 9 - i * 2, y, 55 + i * 2, y, GRID, scale)
    for i in range(6):
        x = 12 + i * 8
        line(canvas, x, 21, 32 + (x - 32) * 2, 31, PANEL, scale)


def render(scale: int, t: float):
    canvas = new_canvas(scale)
    rect(canvas, 0, 0, W, H, BG, scale)
    rect(canvas, 0, 0, W, 8, PANEL, scale)
    draw_floor(canvas, scale)

    left_y = math.sin(t * 1.15) * 0.8
    right_y = -math.sin(t * 0.95 + 1.1) * 0.8
    left_x = -0.34 + math.sin(t * 0.7) * 0.05
    right_x = -0.28 + math.cos(t * 0.85 + 0.4) * 0.06

    render_digit(canvas, "2", 18, 14, left_y, left_x, PALETTE_A, scale)
    render_digit(canvas, "8", 46, 14, right_y, right_x, PALETTE_B, scale)
    draw_micro_text(canvas, "PRISM 3D LAB", 3, 2, TEXT, scale, shadow=SHADOW)
    draw_micro_text(canvas, "7SEG ROTATE", 3, 26, TEXT_DIM, scale)
    return canvas


def check_layout() -> None:
    assert 3 + len("PRISM 3D LAB") * 4 <= 64, "title too wide"
    assert 3 + len("7SEG ROTATE") * 4 <= 64, "footer too wide"
    assert 18 - 7 >= 0 and 46 + 7 <= 63, "digits out of bounds"


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
    parser.add_argument("--time", type=float, default=1.2)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if args.check:
        check_layout()
    canvas = render(args.scale, args.time)
    save_image(canvas, Path(args.output))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
