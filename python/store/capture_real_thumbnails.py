#!/usr/bin/env python3
import argparse
import pathlib
import struct
import time
import urllib.error
import urllib.request
import zlib


def png_pack(tag: bytes, data: bytes) -> bytes:
    return struct.pack("!I", len(data)) + tag + data + struct.pack("!I", zlib.crc32(tag + data) & 0xFFFFFFFF)


def write_png_rgb(path: pathlib.Path, w: int, h: int, pixels):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        row = pixels[y]
        for x in range(w):
            r, g, b = row[x]
            raw.extend((r, g, b))
    comp = zlib.compress(bytes(raw), level=9)
    with path.open("wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(png_pack(b"IHDR", struct.pack("!IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(png_pack(b"IDAT", comp))
        f.write(png_pack(b"IEND", b""))


def http_post(url: str, timeout: float):
    req = urllib.request.Request(url, data=b"", method="POST")
    with urllib.request.urlopen(req, timeout=timeout) as _:
        return


def http_get(url: str, timeout: float) -> bytes:
    with urllib.request.urlopen(url, timeout=timeout) as r:
        return r.read()


def parse_ppm_p6(buf: bytes):
    # P6\nW H\n255\n<rgb...>
    if not buf.startswith(b"P6"):
        raise ValueError("not a P6 ppm")
    i = 2

    def skip_ws():
        nonlocal i
        while i < len(buf) and buf[i] in b" \t\r\n":
            i += 1

    def read_token():
        nonlocal i
        skip_ws()
        j = i
        while i < len(buf) and buf[i] not in b" \t\r\n":
            i += 1
        if j == i:
            raise ValueError("ppm parse token failed")
        return buf[j:i]

    w = int(read_token())
    h = int(read_token())
    maxv = int(read_token())
    if maxv != 255:
        raise ValueError("unsupported ppm max value")
    if i < len(buf) and buf[i] in b"\r\n":
        i += 1
        if i < len(buf) and buf[i - 1] == ord('\r') and buf[i] == ord('\n'):
            i += 1
    need = w * h * 3
    if len(buf) - i < need:
        raise ValueError("ppm body too short")
    body = memoryview(buf)[i:i + need]
    pixels = []
    p = 0
    for _y in range(h):
        row = []
        for _x in range(w):
            row.append((body[p], body[p + 1], body[p + 2]))
            p += 3
        pixels.append(row)
    return w, h, pixels


def score_frame(pixels):
    # Prefer frames that are not "startup blank":
    # 1) more non-black pixels
    # 2) more color diversity
    non_black = 0
    colors = set()
    for row in pixels:
        for r, g, b in row:
            if (r | g | b) != 0:
                non_black += 1
            colors.add((r, g, b))
    return (non_black * 10) + len(colors)


def main():
    ap = argparse.ArgumentParser(description="Capture real 64x32 app thumbnails from device screen")
    ap.add_argument("--device", default="192.168.3.140", help="Device host or host:port")
    ap.add_argument("--apps-root", default="data_littlefs/apps")
    ap.add_argument("--settle-ms", type=int, default=5000, help="Wait after switch before capture")
    ap.add_argument("--timeout", type=float, default=8.0)
    ap.add_argument("--retries", type=int, default=3)
    ap.add_argument("--samples", type=int, default=5, help="Number of frames sampled per app")
    ap.add_argument("--sample-gap-ms", type=int, default=900, help="Gap between sampled frames")
    ap.add_argument(
        "--apps",
        action="append",
        default=[],
        help="Only capture specified app ids. Supports comma-separated list and repeated flag.",
    )
    args = ap.parse_args()

    base = f"http://{args.device}"
    apps_root = pathlib.Path(args.apps_root)
    app_dirs = sorted([p for p in apps_root.iterdir() if p.is_dir() and (p / "main.lua").exists()])
    if not app_dirs:
        raise SystemExit("no apps found")

    selected = set()
    for part in args.apps:
        for item in str(part).split(","):
            item = item.strip()
            if item:
                selected.add(item)
    if selected:
        app_dirs = [p for p in app_dirs if p.name in selected]
        if not app_dirs:
            raise SystemExit(f"no matching apps found for --apps: {sorted(selected)}")

    # Apps with network/data warm-up need longer delay before first capture.
    settle_override_ms = {
        "moon_phase_png": 6500,
        "binance_ticker": 9000,
        "binance_chart": 9500,
        "weather_card_owm": 9000,
        "openmeteo_3day": 9000,
    }

    ok = 0
    for app_dir in app_dirs:
        app_id = app_dir.name
        print(f"[capture] switch -> {app_id}")
        try:
            http_post(f"{base}/api/apps/switch/{app_id}", args.timeout)
        except urllib.error.URLError as e:
            print(f"  switch failed: {e}")
            continue
        settle_ms = max(args.settle_ms, settle_override_ms.get(app_id, 0))
        print(f"  settle {settle_ms}ms")
        time.sleep(max(0.0, settle_ms / 1000.0))

        best = None
        best_score = -1
        for i in range(max(1, args.samples)):
            last_err = None
            ppm = None
            for _ in range(max(1, args.retries)):
                try:
                    ppm = http_get(f"{base}/api/screen/capture.ppm", args.timeout)
                    break
                except Exception as e:
                    last_err = e
                    time.sleep(0.25)
            if ppm is None:
                print(f"  sample {i + 1}: capture failed: {last_err}")
            else:
                try:
                    w, h, pixels = parse_ppm_p6(ppm)
                    sc = score_frame(pixels)
                    if sc > best_score:
                        best_score = sc
                        best = (w, h, pixels)
                    print(f"  sample {i + 1}/{max(1, args.samples)} score={sc}")
                except Exception as e:
                    print(f"  sample {i + 1}: parse failed: {e}")
            if i + 1 < max(1, args.samples):
                time.sleep(max(0.0, args.sample_gap_ms / 1000.0))
        if best is None:
            print("  no valid frame captured")
            continue

        try:
            w, h, pixels = best
            out = app_dir / "thumbnail.png"
            write_png_rgb(out, w, h, pixels)
            print(f"  saved {out.as_posix()} ({w}x{h}) best_score={best_score}")
            ok += 1
        except Exception as e:
            print(f"  parse/write failed: {e}")

    print(f"done: {ok}/{len(app_dirs)}")


if __name__ == "__main__":
    main()
