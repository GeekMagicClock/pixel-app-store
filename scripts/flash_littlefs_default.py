#!/usr/bin/env python3
from __future__ import annotations

import configparser
import csv
import json
import os
import re
import shlex
import subprocess
import sys
from pathlib import Path

from prepare_default_installed_apps import prepare_default_installed_apps


PROJECT_ROOT = Path(__file__).resolve().parents[1]
PLATFORMIO_INI = PROJECT_ROOT / "platformio.ini"


def run(cmd: list[str], *, cwd: Path, env: dict[str, str] | None = None) -> None:
    print(f"$ {shlex.join(cmd)}")
    subprocess.run(cmd, cwd=cwd, env=env, check=True)


def read_platformio_defaults() -> tuple[str, str | None, str | None, str | None]:
    if not PLATFORMIO_INI.is_file():
        raise SystemExit(f"platformio.ini not found: {PLATFORMIO_INI}")

    cfg = configparser.ConfigParser(inline_comment_prefixes=(";", "#"))
    cfg.optionxform = str
    cfg.read(PLATFORMIO_INI, encoding="utf-8")

    if "platformio" not in cfg:
        raise SystemExit("Missing [platformio] section in platformio.ini")

    default_envs = cfg.get("platformio", "default_envs", fallback="").strip()
    if not default_envs:
        raise SystemExit("default_envs is empty in [platformio]")

    env_name = default_envs.split(",")[0].strip()
    env_section = f"env:{env_name}"
    if env_section not in cfg:
        raise SystemExit(f"Missing [{env_section}] in platformio.ini")

    partitions = cfg.get(env_section, "board_build.partitions", fallback="").strip() or None
    upload_port = cfg.get(env_section, "upload_port", fallback="").strip() or None
    upload_speed = cfg.get(env_section, "upload_speed", fallback="").strip() or None
    flash_size = cfg.get(env_section, "board_upload.flash_size", fallback="").strip() or None

    return env_name, partitions, upload_port, upload_speed, flash_size


def read_littlefs_offset(partition_csv_name: str) -> str:
    csv_path = (PROJECT_ROOT / partition_csv_name).resolve()
    if not csv_path.is_file():
        raise SystemExit(f"Partition CSV not found: {csv_path}")

    with csv_path.open(newline="", encoding="utf-8") as f:
        reader = csv.reader(f)
        for row in reader:
            if not row:
                continue
            name = row[0].strip()
            if not name or name.startswith("#"):
                continue
            if len(row) < 4:
                continue
            if name == "littlefs":
                offset = row[3].strip()
                if not offset:
                    break
                return offset

    raise SystemExit(f"Could not find littlefs offset in {csv_path}")


def auto_pick_port() -> str | None:
    patterns = [
        "/dev/cu.wchusbserial*",
        "/dev/cu.usbserial*",
        "/dev/cu.SLAB_USBtoUART*",
        "/dev/cu.usbmodem*",
    ]

    matches: list[str] = []
    for pattern in patterns:
        matches.extend(str(p) for p in Path("/dev").glob(pattern.replace("/dev/", "")))

    if not matches:
        return None
    return sorted(set(matches))[0]


def resolve_esptool() -> tuple[list[str], dict[str, str]]:
    penv_python = Path.home() / ".platformio" / "penv" / "bin" / "python"
    esptool_py = Path.home() / ".platformio" / "packages" / "tool-esptoolpy" / "esptool.py"

    if not penv_python.is_file():
        raise SystemExit(f"PlatformIO Python not found: {penv_python}")
    if not esptool_py.is_file():
        raise SystemExit(f"PlatformIO esptool.py not found: {esptool_py}")

    env = os.environ.copy()
    tool_path = str(esptool_py.parent)
    current = env.get("PYTHONPATH")
    env["PYTHONPATH"] = f"{tool_path}{os.pathsep}{current}" if current else tool_path
    return [str(penv_python), str(esptool_py)], env


def read_flasher_defaults(build_dir: Path) -> tuple[str, str, str, str]:
    # chip, flash_mode, flash_freq, flash_size
    flasher_args = build_dir / "flasher_args.json"
    if flasher_args.is_file():
        data = json.loads(flasher_args.read_text(encoding="utf-8"))
        chip = str(data.get("extra_esptool_args", {}).get("chip", "esp32s3"))
        settings = data.get("flash_settings", {})
        mode = str(settings.get("flash_mode", "dio"))
        freq = str(settings.get("flash_freq", "80m"))
        size = str(settings.get("flash_size", "16MB"))
        return chip, mode, freq, size

    return "esp32s3", "dio", "80m", "16MB"


def normalize_flash_size(value: str | None, fallback: str) -> str:
    if not value:
        return fallback
    v = value.strip()
    # keep esptool/PlatformIO style, e.g. 16MB
    m = re.fullmatch(r"(\d+)\s*([mM])([bB])?", v)
    if m:
        return f"{m.group(1)}MB"
    return v


def main() -> int:
    env_name, partitions, upload_port, upload_speed, flash_size_cfg = read_platformio_defaults()
    if not partitions:
        raise SystemExit(f"board_build.partitions is missing for env: {env_name}")

    print(f"Using default env: {env_name}")
    print(f"Using partitions: {partitions}")

    # Prepare compiled default apps into data_littlefs/apps, then build littlefs.
    prepared = prepare_default_installed_apps()
    print(f"Prepared {len(prepared)} default apps into data_littlefs/apps")
    run(["pio", "run", "-e", env_name, "-t", "buildfs"], cwd=PROJECT_ROOT)

    build_dir = PROJECT_ROOT / ".pio" / "build" / env_name
    littlefs_bin = build_dir / "littlefs.bin"
    if not littlefs_bin.is_file():
        raise SystemExit(f"littlefs.bin not found: {littlefs_bin}")

    offset = read_littlefs_offset(partitions)

    port = upload_port or auto_pick_port()
    if not port:
        raise SystemExit("No serial port detected. Set upload_port in platformio.ini or plug in device.")

    baud = upload_speed or "315200"

    chip, flash_mode, flash_freq, flash_size = read_flasher_defaults(build_dir)
    flash_size = normalize_flash_size(flash_size_cfg, flash_size)

    esptool_cmd, esptool_env = resolve_esptool()
    cmd = [
        *esptool_cmd,
        "--chip",
        chip,
        "--port",
        port,
        "--baud",
        baud,
        "write_flash",
        "--flash_mode",
        flash_mode,
        "--flash_freq",
        flash_freq,
        "--flash_size",
        flash_size,
        offset,
        str(littlefs_bin),
    ]

    print("\nFlashing LittleFS only:")
    print(f"  port: {port}")
    print(f"  baud: {baud}")
    print(f"  offset: {offset}")
    print(f"  image: {littlefs_bin}")

    run(cmd, cwd=PROJECT_ROOT, env=esptool_env)

    print("\nDone. littlefs.bin flashed successfully.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
