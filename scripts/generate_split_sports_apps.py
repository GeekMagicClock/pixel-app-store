#!/usr/bin/env python3
import json
import re
import shutil
import gzip
import struct
import zlib
import binascii
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
APPS = ROOT / "data_littlefs" / "apps"

SPORTS = {
    "soccer": {"label": "Soccer", "league": "eng.1"},
    "basketball": {"label": "Basketball", "league": "nba"},
    "baseball": {"label": "Baseball", "league": "mlb"},
    "football": {"label": "Football", "league": "nfl"},
    "hockey": {"label": "Hockey", "league": "nhl"},
}

KINDS = {
    "scoreboard": {
        "template": "sports_scoreboard",
        "old_prefix": "sports_scoreboard",
        "name_suffix": "Scoreboard",
    },
    "standings": {
        "template": "sports_standings",
        "old_prefix": "sports_standings",
        "name_suffix": "Standings",
    },
    "schedule": {
        "template": "team_schedule",
        "old_prefix": "team_schedule",
        "name_suffix": "Schedule",
    },
}


def reg_sub(text: str, pattern: str, repl: str) -> str:
    out, n = re.subn(pattern, repl, text, flags=re.MULTILINE)
    if n == 0:
        raise RuntimeError(f"pattern not found: {pattern}")
    return out


def update_main_lua(lua_path: Path, new_prefix: str, app_name: str, sport_code: str, league_code: str):
    text = lua_path.read_text(encoding="utf-8")

    # Prefix isolation per split app.
    text = text.replace('"sports_scoreboard.', f'"{new_prefix}.')
    text = text.replace('"sports_standings.', f'"{new_prefix}.')
    text = text.replace('"team_schedule.', f'"{new_prefix}.')

    # App splash title.
    text = re.sub(
        r'local APP_NAME = tostring\(data\.get\("[^"]+\.app_name"\) or "[^"]+"\)',
        f'local APP_NAME = tostring(data.get("{new_prefix}.app_name") or "{app_name}")',
        text,
        count=1,
    )

    # Lock sport and default league per split app.
    text = reg_sub(text, r'^local SPORT = .*$', f'local SPORT = "{sport_code}"')
    text = reg_sub(
        text,
        r'^local LEAGUE = .*$',
        f'local LEAGUE = tostring(data.get("{new_prefix}.league") or "{league_code}")',
    )

    lua_path.write_text(text, encoding="utf-8")


def update_settings_html(html_path: Path, new_app_id: str, old_prefix: str, sport_code: str):
    text = html_path.read_text(encoding="utf-8")

    text = text.replace(f'const APP_ID = "{old_prefix}";', f'const APP_ID = "{new_app_id}";\n    const FORCED_SPORT = "{sport_code}";')
    text = text.replace(f'"{old_prefix}.', f'"{new_app_id}.')
    text = text.replace('const TEAM_CACHE_PREFIX = APP_ID + ":teams:v1:";', 'const TEAM_CACHE_PREFIX = "sports:teams:v2:";')

    # Keep sport fixed to this app type.
    text = text.replace('el.sport.value || "soccer"', 'FORCED_SPORT')
    text = text.replace(f'String(items["{new_app_id}.sport"] || "soccer")', 'FORCED_SPORT')

    # Hide and lock sport selector.
    marker = 'function sportDef() {'
    alt_marker = 'function selectedSportDef() {'
    inject = (
        '    function lockSportField() {\n'
        '      if (!el.sport) return;\n'
        '      el.sport.value = FORCED_SPORT;\n'
        '      el.sport.disabled = true;\n'
        '      const row = el.sport.closest ? el.sport.closest(".row") : null;\n'
        '      if (row) row.style.display = "none";\n'
        '    }\n\n'
    )
    if 'function lockSportField()' not in text:
        if marker in text:
            text = text.replace(marker, inject + marker)
        elif alt_marker in text:
            text = text.replace(alt_marker, inject + alt_marker)

    # Make sure lock executes after initial option build.
    for pattern in [
        'setOptions(el.sport, sportOptions, FORCED_SPORT);',
        'setSelectOptions(el.sport, sportOptions, FORCED_SPORT);',
    ]:
        text = text.replace(pattern, pattern + '\n      lockSportField();')

    # Ensure save path uses forced sport even if field is absent.
    text = text.replace('function collect() {', 'function collect() {\n      lockSportField();')
    text = text.replace('function readForm() {', 'function readForm() {\n      lockSportField();')

    html_path.write_text(text, encoding="utf-8")

    gz_path = html_path.with_suffix(html_path.suffix + '.gz')
    with open(gz_path, 'wb') as f:
        with gzip.GzipFile(filename='', mode='wb', fileobj=f, compresslevel=9, mtime=0) as gz:
            gz.write(text.encode('utf-8'))


def update_manifest(manifest_path: Path, app_id: str, app_name: str):
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest['app_id'] = app_id
    manifest['name'] = app_name
    manifest['version'] = '1.0.0'
    manifest_path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')


def ensure_no_plain_settings_html(dst: Path):
    # Keep source html in repo for editing, but app push/package scripts will ship only .gz.
    # No action needed here beyond ensuring .gz exists.
    if (dst / 'settings.html').exists() and not (dst / 'settings.html.gz').exists():
        raise RuntimeError(f"missing settings.html.gz for {dst.name}")


def _write_rgb_png(path: Path, pixels):
    w = len(pixels[0])
    h = len(pixels)

    def chunk(t: bytes, data: bytes) -> bytes:
        return (
            struct.pack("!I", len(data))
            + t
            + data
            + struct.pack("!I", binascii.crc32(t + data) & 0xFFFFFFFF)
        )

    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            r, g, b = pixels[y][x]
            raw.extend((r, g, b))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack("!IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    path.write_bytes(png)


def generate_scoreboard_thumbnail(dst: Path, sport_code: str):
    w, h = 64, 32
    palettes = {
        "baseball": ((8, 20, 42), (24, 96, 156), (245, 98, 84)),
        "basketball": ((44, 20, 8), (176, 88, 22), (255, 156, 70)),
        "hockey": ((6, 30, 38), (18, 120, 138), (132, 248, 255)),
        "football": ((18, 32, 10), (46, 108, 40), (255, 220, 120)),
        "soccer": ((10, 28, 14), (24, 128, 70), (140, 255, 170)),
    }
    bg_top, bg_bot, accent = palettes.get(sport_code, ((16, 16, 16), (72, 72, 72), (200, 200, 200)))

    pixels = []
    for y in range(h):
        t = y / (h - 1)
        r = int(bg_top[0] * (1 - t) + bg_bot[0] * t)
        g = int(bg_top[1] * (1 - t) + bg_bot[1] * t)
        b = int(bg_top[2] * (1 - t) + bg_bot[2] * t)
        pixels.append([(r, g, b) for _ in range(w)])

    for y in range(0, 8):
        for x in range(w):
            pixels[y][x] = (8, 8, 8)
    for x in range(w):
        pixels[9][x] = accent
    for y in range(14, 18):
        for x in range(3, 26):
            pixels[y][x] = (240, 240, 240)
    for y in range(22, 26):
        for x in range(3, 19):
            pixels[y][x] = accent
    for y in range(22, 26):
        for x in range(44, 61):
            pixels[y][x] = (235, 235, 235)

    _write_rgb_png(dst / "thumbnail.png", pixels)


def main():
    generated = []
    for sport_code, sport_info in SPORTS.items():
        for kind, kind_info in KINDS.items():
            template = APPS / kind_info['template']
            app_id = f"{sport_code}_{kind}"
            app_name = f"{sport_info['label']} {kind_info['name_suffix']}"
            dst = APPS / app_id

            if dst.exists():
                shutil.rmtree(dst)
            shutil.copytree(template, dst)

            update_manifest(dst / 'manifest.json', app_id, app_name)
            update_main_lua(dst / 'main.lua', app_id, app_name, sport_code, sport_info['league'])
            update_settings_html(dst / 'settings.html', app_id, kind_info['old_prefix'], sport_code)
            if kind == "scoreboard":
                generate_scoreboard_thumbnail(dst, sport_code)
            ensure_no_plain_settings_html(dst)
            generated.append(app_id)

    print('generated:', ', '.join(generated))


if __name__ == '__main__':
    main()
