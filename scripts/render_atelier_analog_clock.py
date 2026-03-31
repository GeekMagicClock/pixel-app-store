#!/usr/bin/env python3
from __future__ import annotations

import argparse
import math
from pathlib import Path

W = 64
H = 32

COLORS = {
    "bg": 0x1146,
    "panel": 0x1062,
    "panel_edge": 0x4A49,
    "shadow": 0x0000,
    "ring_outer": 0xA514,
    "ring_inner": 0x3186,
    "face": 0x18E3,
    "face_core": 0x10A2,
    "tick_dim": 0x39C7,
    "tick_bold": 0xC638,
    "hour": 0xFEF7,
    "minute": 0x5F7F,
    "second": 0xF9A6,
    "center": 0xFD20,
    "text": 0xFFDF,
    "text_dim": 0xBDD7,
    "accent": 0x07FF,
    "text_shadow": 0x0000,
}

WEEKDAYS = ["SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"]
MONTHS = ["JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"]
FONT = {
    "0": ["111", "101", "101", "101", "111"],
    "1": ["010", "110", "010", "010", "111"],
    "2": ["111", "001", "111", "100", "111"],
    "3": ["111", "001", "111", "001", "111"],
    "4": ["101", "101", "111", "001", "001"],
    "5": ["111", "100", "111", "001", "111"],
    "6": ["111", "100", "111", "101", "111"],
    "7": ["111", "001", "010", "010", "010"],
    "8": ["111", "101", "111", "101", "111"],
    "9": ["111", "101", "111", "001", "111"],
    "A": ["010", "101", "111", "101", "101"],
    "C": ["011", "100", "100", "100", "011"],
    "D": ["110", "101", "101", "101", "110"],
    "E": ["111", "100", "110", "100", "111"],
    "F": ["111", "100", "110", "100", "100"],
    "H": ["101", "101", "111", "101", "101"],
    "I": ["111", "010", "010", "010", "111"],
    "J": ["001", "001", "001", "101", "010"],
    "M": ["101", "111", "111", "101", "101"],
    "N": ["101", "111", "111", "111", "101"],
    "O": ["111", "101", "101", "101", "111"],
    "R": ["110", "101", "110", "101", "101"],
    "S": ["011", "100", "111", "001", "110"],
    "T": ["111", "010", "010", "010", "010"],
    "U": ["101", "101", "101", "101", "111"],
    "W": ["101", "101", "111", "111", "101"],
    ":": ["000", "010", "000", "010", "000"],
    " ": ["000", "000", "000", "000", "000"],
}


def rgb565_to_rgb888(c: int) -> tuple[int, int, int]:
    r = (c >> 11) & 0x1F
    g = (c >> 5) & 0x3F
    b = c & 0x1F
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


class Canvas:
    def __init__(self) -> None:
        self.pixels = [[0 for _ in range(W)] for _ in range(H)]

    def set_px(self, x: int, y: int, color: int) -> None:
        if 0 <= x < W and 0 <= y < H:
            self.pixels[y][x] = color

    def rect(self, x: int, y: int, w: int, h: int, color: int) -> None:
        if w <= 0 or h <= 0:
            return
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.set_px(xx, yy, color)

    def line(self, x0: int, y0: int, x1: int, y1: int, color: int) -> None:
        dx = abs(x1 - x0)
        sx = 1 if x0 < x1 else -1
        dy = -abs(y1 - y0)
        sy = 1 if y0 < y1 else -1
        err = dx + dy
        while True:
            self.set_px(x0, y0, color)
            if x0 == x1 and y0 == y1:
                break
            e2 = err * 2
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy

    def fill_circle(self, cx: int, cy: int, r: int, color: int) -> None:
        for y in range(-r, r + 1):
            for x in range(-r, r + 1):
                if x * x + y * y <= r * r:
                    self.set_px(cx + x, cy + y, color)

    def draw_circle_outline(self, cx: int, cy: int, r: int, color: int) -> None:
        x = r
        y = 0
        err = 1 - x
        while x >= y:
            self._circle_points(cx, cy, x, y, color)
            y += 1
            if err < 0:
                err += 2 * y + 1
            else:
                x -= 1
                err += 2 * (y - x + 1)

    def _circle_points(self, cx: int, cy: int, x: int, y: int, color: int) -> None:
        pts = [
            (cx + x, cy + y), (cx - x, cy + y), (cx + x, cy - y), (cx - x, cy - y),
            (cx + y, cy + x), (cx - y, cy + x), (cx + y, cy - x), (cx - y, cy - x),
        ]
        for px, py in pts:
            self.set_px(px, py, color)

    def draw_glyph(self, ch: str, x: int, y: int, color: int, scale: int = 1) -> None:
        pat = FONT.get(ch, FONT[" "])
        for row, line in enumerate(pat):
            for col, bit in enumerate(line):
                if bit == "1":
                    self.rect(x + col * scale, y + row * scale, scale, scale, color)

    def draw_text(self, text: str, x: int, y: int, color: int, spacing: int = 1, scale: int = 1) -> None:
        cursor = x
        for ch in text:
            self.draw_glyph(ch, cursor, y, color, scale)
            cursor += 3 * scale + spacing

    def draw_text_shadow(self, text: str, x: int, y: int, color: int, shadow: int, spacing: int = 1, scale: int = 1) -> None:
        self.draw_text(text, x + 1, y + 1, shadow, spacing, scale)
        self.draw_text(text, x, y, color, spacing, scale)

    def save_ppm(self, path: Path) -> None:
        header = f"P6\n{W} {H}\n255\n".encode("ascii")
        data = bytearray()
        for row in self.pixels:
            for c in row:
                data.extend(rgb565_to_rgb888(c))
        path.write_bytes(header + data)


def endpoint(cx: int, cy: int, radius: int, angle_deg: float) -> tuple[int, int]:
    rad = math.radians(angle_deg - 90)
    return (round(cx + math.cos(rad) * radius), round(cy + math.sin(rad) * radius))


def draw_hand(canvas: Canvas, cx: int, cy: int, angle_deg: float, radius: int, color: int, width: int) -> None:
    x1, y1 = endpoint(cx, cy, radius, angle_deg)
    canvas.line(cx, cy, x1, y1, color)
    if width > 1:
        ox, oy = endpoint(0, 0, width // 2, angle_deg + 90)
        canvas.line(cx + ox, cy + oy, x1 + ox, y1 + oy, color)
        canvas.line(cx - ox, cy - oy, x1 - ox, y1 - oy, color)


def draw_ticks(canvas: Canvas, cx: int, cy: int, r: int) -> None:
    for i in range(12):
        angle = i * 30
        inner = r - (4 if i % 3 == 0 else 3)
        outer = r - 1
        x0, y0 = endpoint(cx, cy, inner, angle)
        x1, y1 = endpoint(cx, cy, outer, angle)
        canvas.line(x0, y0, x1, y1, COLORS["tick_bold"] if i % 3 == 0 else COLORS["tick_dim"])


def draw_sidebar(canvas: Canvas, hour: int, minute: int, second: int, wday: int, month: int, day: int) -> None:
    canvas.rect(41, 3, 21, 26, COLORS["panel"])
    canvas.rect(41, 3, 21, 1, COLORS["panel_edge"])
    canvas.rect(41, 28, 21, 1, COLORS["panel_edge"])
    canvas.rect(41, 3, 1, 26, COLORS["panel_edge"])
    canvas.draw_text_shadow(WEEKDAYS[wday - 1], 45, 6, COLORS["text"], COLORS["text_shadow"])
    canvas.draw_text_shadow(f"{hour:02d}:{minute:02d}", 43, 14, COLORS["text"], COLORS["text_shadow"])
    canvas.draw_text_shadow(MONTHS[month - 1], 43, 21, COLORS["text_dim"], COLORS["text_shadow"], 0)
    canvas.draw_text_shadow(f"{day:02d}", 55, 21, COLORS["text"], COLORS["text_shadow"], 0)
    canvas.rect(45, 27, 12, 1, COLORS["ring_inner"])
    canvas.rect(45, 27, round((second / 59) * 12), 1, COLORS["accent"])


def render_clock(hour: int, minute: int, second: int, wday: int, month: int, day: int) -> Canvas:
    canvas = Canvas()
    canvas.rect(0, 0, W, H, COLORS["bg"])
    cx, cy, r = 20, 16, 15
    canvas.fill_circle(cx + 1, cy + 1, r, COLORS["shadow"])
    canvas.fill_circle(cx, cy, r, COLORS["face"])
    canvas.fill_circle(cx, cy, r - 2, COLORS["face_core"])
    canvas.draw_circle_outline(cx, cy, r, COLORS["ring_outer"])
    canvas.draw_circle_outline(cx, cy, r - 1, COLORS["ring_inner"])
    draw_ticks(canvas, cx, cy, r)
    hour_angle = ((hour % 12) + minute / 60 + second / 3600) * 30
    minute_angle = (minute + second / 60) * 6
    second_angle = second * 6
    draw_hand(canvas, cx, cy, minute_angle, 11, COLORS["minute"], 1)
    draw_hand(canvas, cx, cy, hour_angle, 8, COLORS["hour"], 1)
    draw_hand(canvas, cx, cy, second_angle, 12, COLORS["second"], 1)
    canvas.fill_circle(cx, cy, 1, COLORS["center"])
    draw_sidebar(canvas, hour, minute, second, wday, month, day)
    return canvas


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
        er, eg, eb = e_pixels[i:i+3]
        ar, ag, ab = a_pixels[i:i+3]
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


def main() -> int:
    parser = argparse.ArgumentParser(description="Render or compare the atelier analog clock app.")
    sub = parser.add_subparsers(dest="cmd", required=True)

    render = sub.add_parser("render")
    render.add_argument("--time", default="10:10:30")
    render.add_argument("--date", default="2026-03-24")
    render.add_argument("--wday", type=int, default=2, help="1=SUN .. 7=SAT")
    render.add_argument("--output", required=True)

    compare = sub.add_parser("compare")
    compare.add_argument("--expected", required=True)
    compare.add_argument("--actual", required=True)
    compare.add_argument("--diff")

    args = parser.parse_args()
    if args.cmd == "render":
        hh, mm, ss = [int(x) for x in args.time.split(":")]
        _, mon, day = [int(x) for x in args.date.split("-")]
        canvas = render_clock(hh, mm, ss, args.wday, mon, day)
        out = Path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        canvas.save_ppm(out)
        return 0

    diff_path = Path(args.diff) if args.diff else None
    bad, total = compare_ppm(Path(args.expected), Path(args.actual), diff_path)
    pct = (bad * 100.0) / total
    print(f"diff_pixels={bad} total_pixels={total} diff_pct={pct:.3f}")
    return 0 if pct <= 8.0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
