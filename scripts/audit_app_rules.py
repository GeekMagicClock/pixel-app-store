#!/usr/bin/env python3
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
APPS = ROOT / "data_littlefs" / "apps"
SCRIPTS = ROOT / "scripts"

APP_RENDER_CHECKS = {
    "global_news_ticker": ["python3", "scripts/render_global_news_ticker.py", "--check", "--output", "/tmp/global_news_ticker_audit.png"],
    "stock_fear_index": ["python3", "scripts/render_fear_index_apps.py", "render", "stock", "--check", "--output", "/tmp/stock_fear_index_audit.png"],
    "crypto_fear_index": ["python3", "scripts/render_fear_index_apps.py", "render", "crypto", "--check", "--output", "/tmp/crypto_fear_index_audit.png"],
}

APP_SCRIPT_HINTS = {
    "atelier_analog_clock": ["render_atelier_analog_clock.py"],
    "frame_rect_analog_clock": ["render_frame_rect_analog_clock.py"],
    "global_news_ticker": ["render_global_news_ticker.py"],
    "stock_fear_index": ["render_fear_index_apps.py"],
    "crypto_fear_index": ["render_fear_index_apps.py"],
}

FORBIDDEN_SNIPPETS = (
    "ellipsis == true",
    "text_box(x + 1, y + 1, w, 8, text, C_BG, FONT_UI, 8, align or \"left\", true)",
    "text_box(x, y, w, 8, text, color, FONT_UI, 8, align or \"left\", true)",
)


def audit_app(app_id: str) -> list[str]:
    problems: list[str] = []
    app_dir = APPS / app_id
    main_lua = app_dir / "main.lua"
    manifest = app_dir / "manifest.json"
    thumb = app_dir / "thumbnail.png"

    if not app_dir.exists():
        return [f"{app_id}: missing app directory"]
    if not main_lua.exists():
        problems.append(f"{app_id}: missing main.lua")
    if not manifest.exists():
        problems.append(f"{app_id}: missing manifest.json")
    if not thumb.exists():
        problems.append(f"{app_id}: missing thumbnail.png")

    hints = APP_SCRIPT_HINTS.get(app_id, [])
    if hints and not any((SCRIPTS / name).exists() for name in hints):
        problems.append(f"{app_id}: missing local render/check script")

    if main_lua.exists():
        text = main_lua.read_text(encoding="utf-8")
        for snippet in FORBIDDEN_SNIPPETS:
            if snippet in text:
                problems.append(f"{app_id}: forbidden ellipsis fallback remains")
                break

    cmd = APP_RENDER_CHECKS.get(app_id)
    if cmd:
        proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        if proc.returncode != 0:
            detail = (proc.stderr or proc.stdout).strip().splitlines()
            problems.append(f"{app_id}: render/check failed: {detail[-1] if detail else 'unknown error'}")

    return problems


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("apps", nargs="*", default=sorted(APP_SCRIPT_HINTS.keys()))
    args = parser.parse_args()

    failures: list[str] = []
    for app_id in args.apps:
        failures.extend(audit_app(app_id))

    if failures:
        for line in failures:
            print(line)
        return 1

    print("app audit passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
