#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from packaging_policy import is_forbidden_doc_asset

DEFAULT_ENV = "esp32-s3-n16r8"
DEFAULT_LUAC_TOOL = "python/store/tools/luac-esp-compat"
DEFAULT_APPS = [
    "openmeteo_3day",          # 3-Day Forecast
    "weather_card_owm",        # Weather Card
    "sunrise_sunset_owm",      # Sunrise Sunset
    "tetris_clock",            # Tetris Clock
    "crt_clock",               # CRT Terminal Clock
    "basketball_scoreboard",   # Basketball Scoreboard
    "basketball_schedule",     # Basketball Schedule
    "media_gallery",           # Media Gallery
    "binance_chart",           # Binance Chart
    "binance_ticker",          # Binance Ticker
    "stock_chart",             # Stock Chart
    "stock_fear_index",        # Stock Fear
    "moon_phase_png",          # Moon Phase
]


def run(cmd: list[str], cwd: Path) -> None:
    print("$", " ".join(cmd))
    subprocess.run(cmd, cwd=cwd, check=True)


def compile_lua(src: Path, dst: Path, luac: str, cwd: Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    run([luac, "-o", str(dst), str(src)], cwd=cwd)


def stage_release_app(src_app_dir: Path, dst_app_dir: Path, luac: str, cwd: Path) -> None:
    manifest_path = src_app_dir / "manifest.json"
    main_lua = src_app_dir / "main.lua"
    thumb = src_app_dir / "thumbnail.png"

    if not manifest_path.is_file():
        raise SystemExit(f"missing manifest.json: {manifest_path}")
    if not main_lua.is_file():
        raise SystemExit(f"missing main.lua: {main_lua}")
    if not thumb.is_file():
        raise SystemExit(f"missing thumbnail.png: {thumb}")

    dst_app_dir.mkdir(parents=True, exist_ok=True)

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["entry"] = "app.bin"
    (dst_app_dir / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )

    # main.lua -> app.bin (release payload)
    compile_lua(main_lua, dst_app_dir / "app.bin", luac, cwd)

    for path in sorted(src_app_dir.rglob("*")):
        if path.is_dir():
            continue
        rel = path.relative_to(src_app_dir)
        if any(part.startswith(".") for part in rel.parts):
            continue
        rel_posix = rel.as_posix()

        # Do not ship docs in device app payload.
        if is_forbidden_doc_asset(rel_posix):
            continue

        # Already handled / or intentionally excluded.
        if rel_posix in {"manifest.json", "main.lua"}:
            continue

        # Delivery policy: include settings.html.gz only, never raw settings.html.
        if rel.name == "settings.html":
            gz = path.with_name(path.name + ".gz")
            if not gz.exists():
                raise SystemExit(f"{src_app_dir.name}: settings.html.gz is required when settings.html exists")
            continue

        out = dst_app_dir / rel
        out.parent.mkdir(parents=True, exist_ok=True)

        if path.suffix == ".lua":
            # Keep helper path/name for compatibility, content is bytecode (no source).
            compile_lua(path, out, luac, cwd)
        else:
            shutil.copy2(path, out)


def read_define(header: Path, name: str) -> str | None:
    pat = re.compile(rf'^\s*#define\s+{re.escape(name)}\s+"([^"]+)"')
    for line in header.read_text(encoding="utf-8").splitlines():
        m = pat.match(line)
        if m:
            return m.group(1)
    return None


def sanitize(name: str) -> str:
    return re.sub(r"[^A-Za-z0-9._-]+", "_", str(name)).strip("._-") or "unknown"


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description=(
            "Build a filtered littlefs.bin containing only selected apps, then merge a 16MB full flash image."
        )
    )
    p.add_argument("--env", default=DEFAULT_ENV, help=f"PlatformIO env (default: {DEFAULT_ENV})")
    p.add_argument(
        "--version",
        default="",
        help="Package version for output folder name (default: SW_VERSION from include/my_debug.h)",
    )
    p.add_argument(
        "--apps",
        nargs="*",
        default=None,
        help="Optional app IDs to include (default: built-in requested list)",
    )
    p.add_argument(
        "--output-root",
        default="",
        help="Optional root dir for output folder (default: project root)",
    )
    p.add_argument(
        "--skip-firmware-build",
        action="store_true",
        help="Skip pio firmware build step if artifacts already exist",
    )
    p.add_argument(
        "--luac",
        default=DEFAULT_LUAC_TOOL,
        help=f"Lua compiler path (default: {DEFAULT_LUAC_TOOL})",
    )
    return p.parse_args()


def main() -> int:
    args = parse_args()

    project_root = Path(__file__).resolve().parent.parent
    header = project_root / "include" / "my_debug.h"
    data_root = project_root / "data_littlefs"
    apps_root = data_root / "apps"
    source_apps_root = project_root / "apps_src"
    build_dir = project_root / ".pio" / "build" / args.env
    luac_path = Path(args.luac)
    if not luac_path.is_absolute():
        luac_path = (project_root / luac_path).resolve()

    if not luac_path.exists():
        raise SystemExit(f"luac tool not found: {luac_path}")

    if not source_apps_root.is_dir():
        raise SystemExit(f"apps source dir not found: {source_apps_root}")

    apps_root.mkdir(parents=True, exist_ok=True)

    version = args.version.strip() or read_define(header, "SW_VERSION") or "dev"
    version_safe = sanitize(version)
    folder_name = f"pixel_v2_{version_safe}"

    output_root = Path(args.output_root).expanduser() if args.output_root else project_root
    if not output_root.is_absolute():
        output_root = (project_root / output_root).resolve()

    output_dir = output_root / folder_name
    output_dir.mkdir(parents=True, exist_ok=True)

    selected_apps = args.apps if args.apps else list(DEFAULT_APPS)
    selected_apps = [a.strip() for a in selected_apps if str(a).strip()]

    if not selected_apps:
        raise SystemExit("No apps selected.")

    print("Selected apps:")
    for app in selected_apps:
        print(" -", app)

    missing = [app for app in selected_apps if not (source_apps_root / app).is_dir()]
    if missing:
        raise SystemExit("Missing app directories:\n" + "\n".join(f"- {m}" for m in missing))

    # Ensure settings.html.gz policy for selected apps.
    for app in selected_apps:
        app_dir = source_apps_root / app
        if (app_dir / "settings.html").is_file() and not (app_dir / "settings.html.gz").is_file():
            raise SystemExit(f"{app}: settings.html.gz is required when settings.html exists")

    # Build firmware artifacts first (needed for merge).
    if not args.skip_firmware_build:
        run(["pio", "run", "-e", args.env], cwd=project_root)

    with tempfile.TemporaryDirectory(prefix="apps_backup_") as tmp:
        tmp_path = Path(tmp)
        backup_apps_dir = tmp_path / "apps_backup"

        print("Preparing filtered data_littlefs/apps (compiled from apps_src) ...")
        shutil.move(str(apps_root), str(backup_apps_dir))
        apps_root.mkdir(parents=True, exist_ok=True)

        try:
            for app in selected_apps:
                src = source_apps_root / app
                dst = apps_root / app
                stage_release_app(src, dst, str(luac_path), project_root)

            # Build littlefs with filtered app set.
            run(["pio", "run", "-e", args.env, "-t", "buildfs"], cwd=project_root)
        finally:
            # Always restore original apps tree.
            if apps_root.exists():
                shutil.rmtree(apps_root)
            shutil.move(str(backup_apps_dir), str(apps_root))

    littlefs_bin = build_dir / "littlefs.bin"
    if not littlefs_bin.is_file():
        raise SystemExit(f"littlefs.bin not found after buildfs: {littlefs_bin}")
    firmware_bin = build_dir / "firmware.bin"
    if not firmware_bin.is_file():
        raise SystemExit(f"firmware.bin not found: {firmware_bin}")

    littlefs_out = output_dir / "littlefs.bin"
    shutil.copy2(littlefs_bin, littlefs_out)
    firmware_name = f"pixel_v2_{version_safe}_firmware.bin"
    firmware_out = output_dir / firmware_name
    shutil.copy2(firmware_bin, firmware_out)

    merged_name = f"pixel_v2_{version_safe}_16MB_merged.bin"
    merged_out = output_dir / merged_name

    # Merge full 16MB image using existing canonical script.
    run(
        [
            sys.executable,
            "scripts/merge_16m_firmware.py",
            "--env",
            args.env,
            "--output",
            str(merged_out),
            "--no-build",
        ],
        cwd=project_root,
    )

    app_list_txt = output_dir / "apps_included.txt"
    app_list_txt.write_text("\n".join(selected_apps) + "\n", encoding="utf-8")

    print("\nPackage ready:")
    print(f"  folder: {output_dir}")
    print(f"  firmware: {firmware_out}")
    print(f"  littlefs: {littlefs_out}")
    print(f"  merged:   {merged_out}")
    print(f"  apps:     {app_list_txt}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
