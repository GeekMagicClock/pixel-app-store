#!/usr/bin/env python3
import argparse
import pathlib
import struct
import zlib

W, H = 64, 32

BLACK = (0, 0, 0)
WHITE = (235, 240, 255)
YELLOW = (255, 210, 60)
CYAN = (80, 220, 255)
GREEN = (80, 235, 120)
RED = (255, 95, 95)
BLUE = (95, 130, 255)
GRAY = (90, 95, 110)


def png_pack(tag: bytes, data: bytes) -> bytes:
    return struct.pack("!I", len(data)) + tag + data + struct.pack("!I", zlib.crc32(tag + data) & 0xFFFFFFFF)


def write_png_rgb(path: pathlib.Path, pixels):
    raw = bytearray()
    for y in range(H):
        raw.append(0)
        for x in range(W):
            r, g, b = pixels[y][x]
            raw.extend((r, g, b))
    comp = zlib.compress(bytes(raw), level=9)
    with path.open("wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(png_pack(b"IHDR", struct.pack("!IIBBBBB", W, H, 8, 2, 0, 0, 0)))
        f.write(png_pack(b"IDAT", comp))
        f.write(png_pack(b"IEND", b""))


def canvas(c=BLACK):
    return [[c for _ in range(W)] for _ in range(H)]


def px(pix, x, y, c):
    if 0 <= x < W and 0 <= y < H:
        pix[y][x] = c


def rect(pix, x, y, w, h, c):
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            px(pix, xx, yy, c)


def line(pix, x0, y0, x1, y1, c):
    dx = abs(x1 - x0)
    dy = -abs(y1 - y0)
    sx = 1 if x0 < x1 else -1
    sy = 1 if y0 < y1 else -1
    err = dx + dy
    x, y = x0, y0
    while True:
        px(pix, x, y, c)
        if x == x1 and y == y1:
            break
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x += sx
        if e2 <= dx:
            err += dx
            y += sy


def circle_fill(pix, cx, cy, r, c):
    rr = r * r
    for y in range(cy - r, cy + r + 1):
        dy = y - cy
        for x in range(cx - r, cx + r + 1):
            dx = x - cx
            if dx * dx + dy * dy <= rr:
                px(pix, x, y, c)


def dot_text(pix, x, y, txt, c=WHITE):
    # 3x5 dotted numeric-ish font, enough for preview look.
    g = {
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
        ".": ["000", "000", "000", "000", "010"],
        "%": ["101", "001", "010", "100", "101"],
        "$": ["010", "111", "010", "111", "010"],
        "-": ["000", "000", "111", "000", "000"],
        "/": ["001", "001", "010", "100", "100"],
        " ": ["000", "000", "000", "000", "000"],
        "C": ["111", "100", "100", "100", "111"],
    }
    cx = x
    for ch in txt:
        p = g.get(ch, g[" "])
        for ry, row in enumerate(p):
            for rx, bit in enumerate(row):
                if bit == "1":
                    px(pix, cx + rx, y + ry, c)
        cx += 4


def draw_sun_icon(pix, cx, cy, r=6):
    circle_fill(pix, cx, cy, r, YELLOW)
    for i in range(8):
        if i % 2 == 0:
            line(pix, cx + (r + 1), cy, cx + (r + 3), cy, YELLOW)
            line(pix, cx - (r + 1), cy, cx - (r + 3), cy, YELLOW)
            line(pix, cx, cy + (r + 1), cx, cy + (r + 3), YELLOW)
            line(pix, cx, cy - (r + 1), cx, cy - (r + 3), YELLOW)
        break
    px(pix, cx + r + 2, cy - r - 1, YELLOW)
    px(pix, cx - r - 2, cy + r + 1, YELLOW)


def draw_cloud(pix, x, y, c=WHITE):
    circle_fill(pix, x + 6, y + 6, 5, c)
    circle_fill(pix, x + 13, y + 5, 6, c)
    circle_fill(pix, x + 20, y + 7, 5, c)
    rect(pix, x + 4, y + 7, 19, 6, c)


def preview_binance_ticker(pix):
    rect(pix, 0, 0, W, H, BLACK)
    circle_fill(pix, 8, 8, 6, YELLOW)
    dot_text(pix, 20, 3, "BTC", WHITE)
    dot_text(pix, 34, 12, "62333.21", WHITE)
    dot_text(pix, 44, 22, "+2.4%", GREEN)


def preview_coingecko_ticker(pix):
    rect(pix, 0, 0, W, H, BLACK)
    circle_fill(pix, 8, 8, 6, CYAN)
    dot_text(pix, 20, 3, "ETH", WHITE)
    dot_text(pix, 34, 12, "2333.45", WHITE)
    dot_text(pix, 44, 22, "-1.1%", RED)


def preview_binance_chart(pix):
    rect(pix, 0, 0, W, H, BLACK)
    dot_text(pix, 1, 1, "BTC", WHITE)
    dot_text(pix, 45, 1, "+1.7%", GREEN)
    dot_text(pix, 1, 8, "62333.21", WHITE)
    points = [(1, 30), (8, 26), (16, 22), (24, 18), (31, 20), (39, 16), (48, 12), (57, 8), (62, 10)]
    base = 31
    for i in range(len(points) - 1):
        x0, y0 = points[i]
        x1, y1 = points[i + 1]
        line(pix, x0, y0, x1, y1, CYAN)
    for x, y in points:
        for yy in range(y, base + 1):
            px(pix, x, yy, BLUE)


def preview_sunrise_sunset(pix):
    rect(pix, 0, 0, W, H, BLACK)
    line(pix, 0, 15, W - 1, 15, (255, 70, 70))
    draw_sun_icon(pix, 10, 8, 5)
    draw_sun_icon(pix, 10, 23, 5)
    dot_text(pix, 26, 6, "6:44", WHITE)
    dot_text(pix, 26, 20, "18:36", WHITE)


def preview_openmeteo_3day(pix):
    rect(pix, 0, 0, W, H, BLACK)
    dot_text(pix, 2, 2, "NOW", WHITE)
    dot_text(pix, 24, 2, "SAT", WHITE)
    dot_text(pix, 46, 2, "SUN", WHITE)
    draw_sun_icon(pix, 10, 14, 4)
    draw_cloud(pix, 22, 9, WHITE)
    draw_cloud(pix, 44, 9, GRAY)
    dot_text(pix, 4, 25, "22C", WHITE)
    dot_text(pix, 26, 25, "19C", CYAN)
    dot_text(pix, 48, 25, "27C", RED)


def preview_weather_card_owm(pix):
    rect(pix, 0, 0, W, H, BLACK)
    circle_fill(pix, 14, 8, 12, YELLOW)  # big corner sun
    rect(pix, 24, 0, 40, 18, BLACK)      # mask for partial sun effect
    for k in range(5):
        px(pix, 26 + k * 2, 10 + (k % 2), YELLOW)
    dot_text(pix, 2, 24, "THU", WHITE)
    dot_text(pix, 39, 12, "31/23", WHITE)
    dot_text(pix, 43, 23, "63%", CYAN)


def preview_moon_phase(pix):
    rect(pix, 0, 0, W, H, BLACK)
    circle_fill(pix, 16, 16, 13, WHITE)
    circle_fill(pix, 10, 16, 13, (30, 30, 30))
    dot_text(pix, 36, 10, "MOON", WHITE)
    dot_text(pix, 34, 20, "WAXING", WHITE)


def preview_media_gallery(pix):
    rect(pix, 0, 0, W, H, BLACK)
    rect(pix, 2, 2, 60, 28, (20, 22, 30))
    rect(pix, 4, 4, 56, 24, (5, 5, 8))
    rect(pix, 7, 8, 14, 10, (120, 180, 255))
    circle_fill(pix, 35, 14, 6, YELLOW)
    line(pix, 42, 20, 54, 10, GREEN)
    line(pix, 54, 10, 58, 16, GREEN)
    dot_text(pix, 22, 22, "PLAY", WHITE)


def preview_fb_test(pix):
    rect(pix, 0, 0, W, H, BLACK)
    dot_text(pix, 2, 3, "AAPL", WHITE)
    dot_text(pix, 2, 12, "188.7", WHITE)
    dot_text(pix, 2, 22, "+0.7%", GREEN)
    points = [(36, 24), (40, 20), (45, 19), (49, 16), (54, 13), (58, 14), (62, 11)]
    for i in range(len(points) - 1):
        line(pix, points[i][0], points[i][1], points[i + 1][0], points[i + 1][1], CYAN)


def preview_default(pix):
    rect(pix, 0, 0, W, H, BLACK)
    rect(pix, 2, 2, 60, 28, (18, 22, 32))
    rect(pix, 4, 4, 56, 24, (8, 10, 16))
    for x in range(6, 58, 4):
        px(pix, x, 8, CYAN)
        px(pix, x + 1, 16, WHITE)
        px(pix, x + 2, 24, BLUE)


def render_by_app(app_id: str, pix):
    m = {
        "binance_ticker": preview_binance_ticker,
        "coingecko_ticker": preview_coingecko_ticker,
        "binance_chart": preview_binance_chart,
        "sunrise_sunset_owm": preview_sunrise_sunset,
        "openmeteo_3day": preview_openmeteo_3day,
        "weather_card_owm": preview_weather_card_owm,
        "moon_phase_png": preview_moon_phase,
        "media_gallery": preview_media_gallery,
        "fb_test": preview_fb_test,
    }
    fn = m.get(app_id, preview_default)
    fn(pix)


def main():
    ap = argparse.ArgumentParser(description="Generate 64x32 app preview thumbnails")
    ap.add_argument("--apps-root", default="data_littlefs/apps")
    args = ap.parse_args()
    root = pathlib.Path(args.apps_root)
    count = 0
    for app_dir in sorted([p for p in root.iterdir() if p.is_dir()]):
        if not (app_dir / "main.lua").exists():
            continue
        pix = canvas()
        render_by_app(app_dir.name, pix)
        out = app_dir / "thumbnail.png"
        write_png_rgb(out, pix)
        print(out.as_posix())
        count += 1
    print(f"generated {count} preview thumbnail(s)")


if __name__ == "__main__":
    main()
