#!/usr/bin/env python3
from __future__ import annotations

import argparse
import math
from pathlib import Path

W = 64
H = 32

COLORS = {
    "bg": 0x0842,
    "frame": 0xBDF7,
    "frame_inner": 0x5ACB,
    "panel": 0x10A2,
    "grid": 0x18E3,
    "tick": 0x7BEF,
    "text": 0xFFDF,
    "text_dim": 0xBDF7,
    "text_shadow": 0x0000,
    "hour": 0xFEF7,
    "minute": 0x4F7F,
    "second": 0xF9C6,
    "center": 0xFD20,
    "accent": 0xFFE0,
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
    "M": ["101", "111", "111", "101", "101"],
    "N": ["101", "111", "111", "111", "101"],
    "O": ["111", "101", "101", "101", "111"],
    "R": ["110", "101", "110", "101", "101"],
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
        self.p = [[0] * W for _ in range(H)]

    def set_px(self, x: int, y: int, c: int) -> None:
        if 0 <= x < W and 0 <= y < H:
            self.p[y][x] = c

    def rect(self, x: int, y: int, w: int, h: int, c: int) -> None:
        if w <= 0 or h <= 0:
            return
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.set_px(xx, yy, c)

    def line(self, x0: int, y0: int, x1: int, y1: int, c: int) -> None:
        dx = abs(x1 - x0)
        sx = 1 if x0 < x1 else -1
        dy = -abs(y1 - y0)
        sy = 1 if y0 < y1 else -1
        err = dx + dy
        while True:
            self.set_px(x0, y0, c)
            if x0 == x1 and y0 == y1:
                break
            e2 = err * 2
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy

    def glyph(self, ch: str, x: int, y: int, color: int) -> None:
        for row, line in enumerate(FONT.get(ch, FONT[" "])):
            for col, bit in enumerate(line):
                if bit == "1":
                    self.rect(x + col, y + row, 1, 1, color)

    def text(self, text: str, x: int, y: int, color: int, shadow: int, spacing: int = 1) -> None:
        cur = x
        for ch in text:
            self.glyph(ch, cur + 1, y + 1, shadow)
            self.glyph(ch, cur, y, color)
            cur += 3 + spacing

    def save_ppm(self, path: Path) -> None:
        data = bytearray(f"P6\n{W} {H}\n255\n".encode("ascii"))
        for row in self.p:
            for c in row:
                data.extend(rgb565_to_rgb888(c))
        path.write_bytes(data)


def endpoint(cx: int, cy: int, radius: int, angle_deg: float) -> tuple[int, int]:
    rad = math.radians(angle_deg - 90)
    return round(cx + math.cos(rad) * radius), round(cy + math.sin(rad) * radius)


def draw_hand(cv: Canvas, cx: int, cy: int, angle: float, radius: int, color: int, width: int) -> None:
    x1, y1 = endpoint(cx, cy, radius, angle)
    cv.line(cx, cy, x1, y1, color)
    if width > 1:
        ox, oy = endpoint(0, 0, width // 2, angle + 90)
        cv.line(cx + ox, cy + oy, x1 + ox, y1 + oy, color)
        cv.line(cx - ox, cy - oy, x1 - ox, y1 - oy, color)


def draw_face(cv: Canvas) -> None:
    cv.rect(0, 0, 64, 32, COLORS["bg"])
    cv.rect(0, 0, 64, 1, COLORS["frame"])
    cv.rect(0, 31, 64, 1, COLORS["frame"])
    cv.rect(0, 0, 1, 32, COLORS["frame"])
    cv.rect(63, 0, 1, 32, COLORS["frame"])
    cv.rect(1, 1, 62, 1, COLORS["frame_inner"])
    cv.rect(1, 30, 62, 1, COLORS["frame_inner"])
    cv.rect(1, 1, 1, 30, COLORS["frame_inner"])
    cv.rect(62, 1, 1, 30, COLORS["frame_inner"])
    cv.rect(4, 4, 56, 24, COLORS["panel"])
    for x in range(8, 57, 8):
        cv.rect(x, 5, 1, 22, COLORS["grid"])
    for y in range(8, 25, 8):
        cv.rect(5, y, 54, 1, COLORS["grid"])
    for x in range(4, 61, 14):
        cv.rect(x, 3, 4, 1, COLORS["tick"])
        cv.rect(x, 28, 4, 1, COLORS["tick"])
    for y in range(4, 29, 8):
        cv.rect(3, y, 1, 4, COLORS["tick"])
        cv.rect(60, y, 1, 4, COLORS["tick"])


def draw_info(cv: Canvas, hh: int, mm: int, ss: int, wday: int, month: int, day: int) -> None:
    cv.text(WEEKDAYS[wday - 1], 5, 5, COLORS["text_dim"], COLORS["text_shadow"], 1)
    cv.text(f"{hh:02d}:{mm:02d}", 39, 5, COLORS["text"], COLORS["text_shadow"], 1)
    cv.text(MONTHS[month - 1], 5, 23, COLORS["text_dim"], COLORS["text_shadow"], 0)
    cv.text(f"{day:02d}", 53, 23, COLORS["text"], COLORS["text_shadow"], 0)
    cv.rect(26, 26, 12, 1, COLORS["frame_inner"])
    cv.rect(26, 26, round((ss / 59) * 12), 1, COLORS["accent"])


def render(hh: int, mm: int, ss: int, wday: int, month: int, day: int) -> Canvas:
    cv = Canvas()
    draw_face(cv)
    cx, cy = 32, 16
    draw_hand(cv, cx, cy, (mm + ss / 60) * 6, 10, COLORS["minute"], 1)
    draw_hand(cv, cx, cy, ((hh % 12) + mm / 60 + ss / 3600) * 30, 7, COLORS["hour"], 1)
    draw_hand(cv, cx, cy, ss * 6, 11, COLORS["second"], 1)
    cv.rect(cx - 1, cy - 1, 3, 3, COLORS["center"])
    draw_info(cv, hh, mm, ss, wday, month, day)
    return cv


def compare(expected: Path, actual: Path, diff: Path | None) -> tuple[int, int]:
    def load(path: Path) -> bytes:
        b = path.read_bytes()
        return b[b.find(b"\n255\n") + 5 :]
    e = load(expected)
    a = load(actual)
    bad = 0
    out = bytearray(f"P6\n{W} {H}\n255\n".encode("ascii")) if diff else bytearray()
    for i in range(0, len(e), 3):
        delta = abs(e[i] - a[i]) + abs(e[i + 1] - a[i + 1]) + abs(e[i + 2] - a[i + 2])
        if delta > 30:
            bad += 1
            if diff:
                out.extend((255, 48, 48))
        elif diff:
            out.extend((32, 32, 32))
    if diff:
        diff.write_bytes(out)
    return bad, W * H


def main() -> int:
    ap = argparse.ArgumentParser()
    sp = ap.add_subparsers(dest="cmd", required=True)
    r = sp.add_parser("render")
    r.add_argument("--time", required=True)
    r.add_argument("--date", required=True)
    r.add_argument("--wday", required=True, type=int)
    r.add_argument("--output", required=True)
    c = sp.add_parser("compare")
    c.add_argument("--expected", required=True)
    c.add_argument("--actual", required=True)
    c.add_argument("--diff")
    args = ap.parse_args()
    if args.cmd == "render":
        hh, mm, ss = [int(x) for x in args.time.split(":")]
        _, mon, day = [int(x) for x in args.date.split("-")]
        out = Path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        render(hh, mm, ss, args.wday, mon, day).save_ppm(out)
        return 0
    bad, total = compare(Path(args.expected), Path(args.actual), Path(args.diff) if args.diff else None)
    pct = bad * 100.0 / total
    print(f"diff_pixels={bad} total_pixels={total} diff_pct={pct:.3f}")
    return 0 if pct <= 8.0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
