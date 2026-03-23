#!/usr/bin/env python3
import argparse
import pathlib
import struct
import zlib

W, H = 64, 32
THUMBNAIL_NAMES = ("thumbnail.png", "thumbnail.jpg", "thumbnail.jpeg", "thumb.png", "preview.png")

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
        "A": ["010", "101", "111", "101", "101"],
        "B": ["110", "101", "110", "101", "110"],
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
        "D": ["110", "101", "101", "101", "110"],
        "E": ["111", "100", "110", "100", "111"],
        "F": ["111", "100", "110", "100", "100"],
        "G": ["111", "100", "101", "101", "111"],
        "H": ["101", "101", "111", "101", "101"],
        "I": ["111", "010", "010", "010", "111"],
        "J": ["111", "001", "001", "101", "111"],
        "K": ["101", "101", "110", "101", "101"],
        "L": ["100", "100", "100", "100", "111"],
        "M": ["101", "111", "111", "101", "101"],
        "N": ["101", "111", "111", "111", "101"],
        "O": ["111", "101", "101", "101", "111"],
        "P": ["110", "101", "110", "100", "100"],
        "Q": ["111", "101", "101", "111", "001"],
        "R": ["110", "101", "110", "101", "101"],
        "S": ["111", "100", "111", "001", "111"],
        "T": ["111", "010", "010", "010", "010"],
        "U": ["101", "101", "101", "101", "111"],
        "V": ["101", "101", "101", "101", "010"],
        "W": ["101", "101", "111", "111", "101"],
        "X": ["101", "101", "010", "101", "101"],
        "Y": ["101", "101", "010", "010", "010"],
        "Z": ["111", "001", "010", "100", "111"],
    }
    cx = x
    for ch in txt:
        p = g.get(ch, g[" "])
        for ry, row in enumerate(p):
            for rx, bit in enumerate(row):
                if bit == "1":
                    px(pix, cx + rx, y + ry, c)
        cx += 4


def block_digit(pix, digit, x, y, scale, c):
    glyphs = {
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
    }
    rows = glyphs.get(digit)
    if not rows:
        return
    for yy, row in enumerate(rows):
        for xx, bit in enumerate(row):
            if bit == "1":
                rect(pix, x + xx * scale, y + yy * scale, scale, scale, c)


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


def preview_pixel_clock(pix):
    bg = (2, 8, 26)
    panel = (10, 28, 56)
    text = (248, 252, 248)
    muted = (160, 194, 214)
    accent = (80, 220, 255)
    star_dim = (92, 112, 154)
    star_mid = (178, 204, 234)
    star_blue = (92, 182, 255)

    rect(pix, 0, 0, W, H, bg)
    stars = [
        (4, 3, star_mid), (12, 7, star_blue), (30, 4, star_mid), (58, 3, star_mid),
        (61, 9, star_blue), (6, 13, star_mid), (56, 15, star_mid), (3, 21, star_blue),
        (60, 22, star_mid), (8, 26, star_dim), (20, 24, star_blue), (34, 26, star_dim),
        (48, 24, star_blue), (56, 27, star_mid),
    ]
    for x, y, c in stars:
        px(pix, x, y, c)
    for x, y in [(30, 4), (6, 13), (60, 22)]:
        px(pix, x - 1, y, star_dim)
        px(pix, x + 1, y, star_dim)
        px(pix, x, y - 1, star_dim)
        px(pix, x, y + 1, star_dim)
    rect(pix, 0, 0, W, 1, panel)
    rect(pix, 0, H - 1, W, 1, panel)

    dot_text(pix, 3, 1, "TIME", muted)
    dot_text(pix, 49, 1, "SAT", muted)

    digits = "1234"
    scale = 3
    digit_w = 3 * scale
    gap = 2
    colon_gap = 4
    total_w = digit_w * 4 + gap * 2 + colon_gap
    x = (W - total_w) // 2
    y = 8

    block_digit(pix, digits[0], x, y, scale, text)
    x += digit_w + gap
    block_digit(pix, digits[1], x, y, scale, text)
    x += digit_w + 1
    rect(pix, x, y + 4, 2, 2, accent)
    rect(pix, x, y + 10, 2, 2, accent)
    x += colon_gap - 1
    block_digit(pix, digits[2], x, y, scale, text)
    x += digit_w + gap
    block_digit(pix, digits[3], x, y, scale, text)

    rect(pix, 2, 29, 60, 2, panel)
    rect(pix, 3, 29, 52, 2, accent)


def preview_auto_snake(pix):
    bg = (2, 10, 18)
    hud = (12, 34, 58)
    board_a = (6, 44, 72)
    board_b = (8, 52, 84)
    grid = (18, 78, 112)
    snake_head = (82, 255, 140)
    snake_body = (36, 190, 116)
    snake_tail = (18, 134, 92)
    food = (255, 204, 72)
    text = (232, 242, 255)
    warn = (255, 122, 122)

    rect(pix, 0, 0, W, H, bg)
    rect(pix, 0, 0, W, 8, hud)
    dot_text(pix, 2, 1, "AUTO", text)
    dot_text(pix, 50, 1, "12S", text)

    cell = 4
    top = 8
    for row in range(6):
        for col in range(16):
            x = col * cell
            y = top + row * cell
            rect(pix, x, y, cell, cell, board_a if (row + col) % 2 == 0 else board_b)
            px(pix, x, y, grid)

    snake = [(9, 3), (8, 3), (7, 3), (6, 3), (6, 2)]
    for idx, (col, row) in enumerate(reversed(snake)):
        x = col * cell
        y = top + row * cell
        color = snake_tail
        if idx == len(snake) - 1:
            color = snake_head
        elif idx > 0:
            color = snake_body
        rect(pix, x, y, cell, cell, color)
        rect(pix, x + 1, y + 1, cell - 2, cell - 2, color)

    fx = 12 * cell
    fy = top + 2 * cell
    rect(pix, fx + 1, fy + 1, 2, 2, food)
    px(pix, fx + 1, fy, food)
    px(pix, fx + 2, fy, food)
    px(pix, fx + 1, fy + 3, food)
    px(pix, fx + 2, fy + 3, food)
    px(pix, fx, fy + 1, food)
    px(pix, fx + 3, fy + 1, food)
    px(pix, fx, fy + 2, food)
    px(pix, fx + 3, fy + 2, food)

    rect(pix, 0, 18, W, 6, (66, 12, 18))
    dot_text(pix, 22, 19, "RETRY", warn)


def preview_vfd_clock(pix):
    bg = (0, 4, 10)
    panel = (10, 22, 38)
    glass = (0, 56, 72)
    glass_alt = (0, 46, 62)
    frame = (26, 44, 66)
    on = (142, 255, 240)
    bright = (220, 255, 252)
    dim = (86, 132, 146)
    ghost = (22, 56, 62)

    def seg_h(x, y, active=True):
        rect(pix, x, y, 8, 2, on if active else ghost)
        rect(pix, x + 1, y, 6, 1, bright if active else dim)

    def seg_v(x, y, active=True):
        rect(pix, x, y, 2, 6, on if active else ghost)
        rect(pix, x, y + 1, 1, 4, bright if active else dim)

    def digit(ch, x, y):
        m = {
            "0": [1, 1, 1, 1, 1, 1, 0],
            "1": [0, 1, 1, 0, 0, 0, 0],
            "2": [1, 1, 0, 1, 1, 0, 1],
            "3": [1, 1, 1, 1, 0, 0, 1],
            "4": [0, 1, 1, 0, 0, 1, 1],
            "5": [1, 0, 1, 1, 0, 1, 1],
            "6": [1, 0, 1, 1, 1, 1, 1],
            "7": [1, 1, 1, 0, 0, 0, 0],
            "8": [1, 1, 1, 1, 1, 1, 1],
            "9": [1, 1, 1, 1, 0, 1, 1],
        }[ch]
        seg_h(x + 2, y, m[0])
        seg_v(x + 10, y + 2, m[1])
        seg_v(x + 10, y + 10, m[2])
        seg_h(x + 2, y + 16, m[3])
        seg_v(x, y + 10, m[4])
        seg_v(x, y + 2, m[5])
        seg_h(x + 2, y + 8, m[6])

    rect(pix, 0, 0, W, H, bg)
    rect(pix, 1, 1, 62, 30, panel)
    rect(pix, 2, 2, 60, 28, glass)
    for y in range(3, 30):
        if y % 3 == 0:
            rect(pix, 2, y, 60, 1, glass_alt)
    rect(pix, 0, 0, W, 1, frame)
    rect(pix, 0, H - 1, W, 1, frame)
    rect(pix, 0, 0, 1, H, frame)
    rect(pix, W - 1, 0, 1, H, frame)

    dot_text(pix, 4, 2, "VFD", on)
    rect(pix, 47, 1, 13, 7, panel)
    rect(pix, 47, 1, 13, 1, frame)
    dot_text(pix, 49, 2, "SAT", ghost)
    dot_text(pix, 49, 1, "SAT", bright)
    rect(pix, 19, 3, 2, 2, bright)

    x = 5
    y = 7
    for ch in "1234":
        digit(ch, x, y)
        x += 13
        if ch == "2":
            rect(pix, x + 1, y + 5, 2, 2, on)
            rect(pix, x + 1, y + 11, 2, 2, on)
            px(pix, x + 1, y + 5, bright)
            px(pix, x + 1, y + 11, bright)
            x += 4

    dot_text(pix, 4, 24, "MAR 21", on)
    rect(pix, 42, 28, 18, 2, ghost)
    rect(pix, 43, 28, 11, 2, on)


def preview_arcade_clock(pix):
    bg = (0, 0, 0)
    frame = (26, 34, 54)
    white = (244, 248, 252)
    yellow = (255, 220, 64)
    red = (255, 94, 84)
    blue = (88, 186, 255)
    ghost = (90, 96, 112)

    rect(pix, 0, 0, W, H, bg)
    rect(pix, 0, 0, W, 1, frame)
    rect(pix, 0, H - 1, W, 1, frame)
    for x, y in [(3, 4), (11, 6), (19, 3), (52, 4), (60, 7), (6, 18), (57, 17), (4, 27), (49, 28)]:
        px(pix, x, y, ghost)

    dot_text(pix, 2, 1, "1UP", red)
    dot_text(pix, 28, 1, "HI", yellow)
    dot_text(pix, 49, 1, "SAT", blue)

    x = 10
    y = 8
    scale = 3
    for ch in "1234":
        block_digit(pix, ch, x, y, scale, yellow)
        x += 11
        if ch == "2":
          rect(pix, 33, 12, 2, 2, red)
          rect(pix, 33, 18, 2, 2, red)
          x += 6

    dot_text(pix, 2, 24, "CREDIT 1", red)
    dot_text(pix, 42, 24, "MAR21", blue)


def preview_flip_clock(pix):
    bg = (12, 16, 24)
    top = (70, 74, 84)
    bot = (42, 46, 54)
    frame = (18, 22, 28)
    text = (248, 248, 244)
    dim = (200, 188, 164)
    accent = (255, 224, 88)
    ghost = (90, 90, 90)

    rect(pix, 0, 0, W, H, bg)
    rect(pix, 0, 0, W, 1, frame)
    rect(pix, 0, H - 1, W, 1, frame)
    dot_text(pix, 2, 1, "PLT 2", dim)
    dot_text(pix, 49, 1, "SAT", accent)

    tiles = [(3, "1"), (18, "2"), (33, "3"), (48, "4")]
    for x, ch in tiles:
        rect(pix, x, 7, 13, 19, top)
        rect(pix, x, 16, 13, 10, bot)
        rect(pix, x, 16, 13, 1, frame)
        rect(pix, x, 7, 13, 1, frame)
        rect(pix, x, 25, 13, 1, frame)
        rect(pix, x, 7, 1, 19, frame)
        rect(pix, x + 12, 7, 1, 19, frame)
        block_digit(pix, ch, x + 2, 9, 3, text)

    rect(pix, 30, 12, 3, 3, accent)
    rect(pix, 30, 18, 3, 3, accent)
    dot_text(pix, 2, 24, "MAR21", dim)
    rect(pix, 40, 27, 20, 2, ghost)
    rect(pix, 41, 27, 10, 2, accent)


def preview_crt_clock(pix):
    bg = (0, 0, 0)
    scan = (0, 18, 0)
    glow = (48, 255, 132)
    bright = (188, 255, 216)
    ghost = (0, 82, 48)
    dim = (22, 146, 80)

    rect(pix, 0, 0, W, H, bg)
    for y in range(1, H, 2):
        rect(pix, 0, y, W, 1, scan)
    rect(pix, 1, 15, 62, 1, ghost)
    dot_text(pix, 2, 1, "SYS> TIME", glow)

    block_digit(pix, "1", 10, 8, 3, glow)
    block_digit(pix, "2", 21, 8, 3, glow)
    rect(pix, 33, 12, 2, 2, bright)
    rect(pix, 33, 18, 2, 2, bright)
    block_digit(pix, "3", 38, 8, 3, glow)
    block_digit(pix, "4", 49, 8, 3, glow)
    dot_text(pix, 2, 24, "03-21 READY_", dim)


def preview_walkman_clock(pix):
    bg = (24, 40, 52)
    body = (42, 68, 82)
    window = (176, 140, 102)
    window_dark = (128, 96, 70)
    tape = (220, 214, 206)
    reel = (64, 64, 68)
    reel_hi = (244, 244, 244)
    text = (248, 248, 244)
    accent = (255, 122, 72)
    prog = (86, 238, 255)
    text_dark = (44, 44, 40)

    rect(pix, 0, 0, W, H, bg)
    rect(pix, 1, 1, 62, 30, body)
    rect(pix, 8, 4, 48, 10, window)
    rect(pix, 9, 5, 46, 8, window_dark)
    rect(pix, 3, 15, 58, 14, tape)
    rect(pix, 30, 17, 4, 8, reel)
    dot_text(pix, 2, 1, "PLAY", accent)
    dot_text(pix, 41, 1, "SIDE A", text)

    block_digit(pix, "1", 13, 6, 2, text)
    block_digit(pix, "2", 21, 6, 2, text)
    rect(pix, 30, 10, 1, 1, text)
    rect(pix, 30, 14, 1, 1, text)
    block_digit(pix, "3", 34, 6, 2, text)
    block_digit(pix, "4", 42, 6, 2, text)

    for cx in [17, 47]:
        rect(pix, cx - 6, 22 - 6, 13, 13, reel)
        rect(pix, cx - 5, 22 - 5, 11, 11, tape)
        rect(pix, cx - 1, 22 - 1, 3, 3, reel)
        rect(pix, cx - 1, 17, 3, 1, reel_hi)
        rect(pix, cx - 1, 26, 3, 1, reel_hi)
    rect(pix, 23, 25, 18, 2, reel)
    rect(pix, 24, 25, 10, 2, prog)
    dot_text(pix, 21, 16, "03-21", text_dark)


def preview_motion_clock(pix):
    bg = (12, 18, 24)
    panel = (34, 44, 58)
    cyan = (80, 220, 255)
    magenta = (255, 92, 220)
    yellow = (255, 226, 92)
    text = (248, 248, 255)
    dim = (188, 188, 204)
    ghost = (96, 96, 110)

    rect(pix, 0, 0, W, H, bg)
    rect(pix, 0, 0, W, 1, panel)
    rect(pix, 0, H - 1, W, 1, panel)
    rect(pix, -2, 5, 18, 3, cyan)
    rect(pix, 18, 10, 18, 3, magenta)
    rect(pix, 39, 15, 18, 3, yellow)
    rect(pix, 10, 20, 18, 3, cyan)
    rect(pix, 33, 25, 18, 3, magenta)
    dot_text(pix, 2, 1, "MOTION", text)
    dot_text(pix, 49, 1, "SAT", dim)

    block_digit(pix, "1", 10, 8, 3, text)
    block_digit(pix, "2", 21, 8, 3, text)
    rect(pix, 33, 12, 2, 2, yellow)
    rect(pix, 33, 18, 2, 2, yellow)
    block_digit(pix, "3", 38, 8, 3, text)
    block_digit(pix, "4", 49, 8, 3, text)
    dot_text(pix, 2, 23, "MAR21", dim)
    dot_text(pix, 50, 23, "37S", dim)
    for i in range(8):
        rect(pix, 4 + i * 7, 27 - (i % 4), 4, 1 + (i % 4), cyan if i % 2 == 0 else magenta)


def preview_default(pix):
    rect(pix, 0, 0, W, H, BLACK)
    rect(pix, 2, 2, 60, 28, (18, 22, 32))
    rect(pix, 4, 4, 56, 24, (8, 10, 16))
    for x in range(6, 58, 4):
        px(pix, x, 8, CYAN)
        px(pix, x + 1, 16, WHITE)
        px(pix, x + 2, 24, BLUE)


def has_thumbnail(app_dir: pathlib.Path) -> bool:
    return any((app_dir / name).is_file() for name in THUMBNAIL_NAMES)


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
        "pixel_clock": preview_pixel_clock,
        "auto_snake": preview_auto_snake,
        "vfd_clock": preview_vfd_clock,
        "arcade_clock": preview_arcade_clock,
        "flip_clock": preview_flip_clock,
        "crt_clock": preview_crt_clock,
        "walkman_clock": preview_walkman_clock,
        "motion_clock": preview_motion_clock,
    }
    fn = m.get(app_id, preview_default)
    fn(pix)


def main():
    ap = argparse.ArgumentParser(description="Generate 64x32 app preview thumbnails")
    ap.add_argument("--apps-root", default="data_littlefs/apps")
    ap.add_argument("--app-id")
    ap.add_argument("--apps", action="append", default=[], help="Only generate specified app ids. Supports comma-separated values.")
    ap.add_argument("--force", action="store_true", help="Overwrite existing thumbnails")
    args = ap.parse_args()
    root = pathlib.Path(args.apps_root)
    selected = set()
    if args.app_id:
        selected.add(str(args.app_id).strip())
    for raw in args.apps:
        for item in str(raw).split(","):
            app_id = item.strip()
            if app_id:
                selected.add(app_id)
    count = 0
    for app_dir in sorted([p for p in root.iterdir() if p.is_dir()]):
        if selected and app_dir.name not in selected:
            continue
        if not (app_dir / "main.lua").exists():
            continue
        if not args.force and has_thumbnail(app_dir):
            print(f"[skip] {app_dir.name}: thumbnail already exists")
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
