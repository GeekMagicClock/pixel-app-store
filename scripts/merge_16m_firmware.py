#!/usr/bin/env python3

from __future__ import annotations

import argparse
import csv
import json
import os
import re
import shlex
import subprocess
import sys
from pathlib import Path


DEFAULT_ENV = "esp32-s3-n16r8"
DEFAULT_PARTITION_CSV = "16m12m_littlefs_idf.csv"
EXPECTED_FLASH_SIZE = "16MB"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Merge the 16MB ESP32-S3 firmware image, including bootloader, "
            "partition table, OTA data, app firmware, and LittleFS."
        )
    )
    parser.add_argument(
        "--env",
        default=DEFAULT_ENV,
        help=f"PlatformIO environment to use (default: {DEFAULT_ENV})",
    )
    parser.add_argument(
        "--partition-csv",
        default=DEFAULT_PARTITION_CSV,
        help=f"Partition table CSV to read offsets from (default: {DEFAULT_PARTITION_CSV})",
    )
    parser.add_argument(
        "--output",
        help="Output path for the merged firmware image (default: .pio/build/<env>/...)",
    )
    parser.add_argument(
        "--no-build",
        action="store_true",
        help="Fail if required artifacts are missing instead of running PlatformIO builds",
    )
    return parser.parse_args()


def run_command(command: list[str], *, cwd: Path, env: dict[str, str] | None = None) -> None:
    print(f"$ {shlex.join(command)}")
    subprocess.run(command, cwd=cwd, env=env, check=True)


def read_define(header_path: Path, name: str) -> str | None:
    pattern = re.compile(rf'^\s*#define\s+{re.escape(name)}\s+"([^"]+)"')
    for line in header_path.read_text(encoding="utf-8").splitlines():
        match = pattern.match(line)
        if match:
            return match.group(1)
    return None


def sanitize_fragment(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9._-]+", "_", value).strip("._-") or "image"


def read_partition_offsets(csv_path: Path) -> dict[str, str]:
    offsets: dict[str, str] = {}
    with csv_path.open(newline="", encoding="utf-8") as handle:
        reader = csv.reader(handle)
        for row in reader:
            if not row:
                continue
            first = row[0].strip()
            if not first or first.startswith("#"):
                continue
            if len(row) < 4:
                continue
            offsets[first] = row[3].strip()
    return offsets


def read_flasher_args(flasher_args_path: Path) -> dict[str, object]:
    with flasher_args_path.open(encoding="utf-8") as handle:
        return json.load(handle)


def ensure_artifacts(
    *,
    project_root: Path,
    env_name: str,
    build_dir: Path,
    no_build: bool,
) -> None:
    firmware_artifacts = [
        build_dir / "bootloader.bin",
        build_dir / "partitions.bin",
        build_dir / "ota_data_initial.bin",
        build_dir / "firmware.bin",
        build_dir / "flasher_args.json",
    ]
    littlefs_bin = build_dir / "littlefs.bin"

    missing_firmware = [path for path in firmware_artifacts if not path.is_file()]
    if missing_firmware:
        if no_build:
            missing = "\n".join(f"- {path}" for path in missing_firmware)
            raise SystemExit(f"Missing firmware build artifacts:\n{missing}")
        print(f"Building firmware artifacts for {env_name} ...")
        run_command(["pio", "run", "-e", env_name], cwd=project_root)

    if not littlefs_bin.is_file():
        if no_build:
            raise SystemExit(f"Missing LittleFS image: {littlefs_bin}")
        print(f"Building LittleFS image for {env_name} ...")
        run_command(["pio", "run", "-e", env_name, "-t", "buildfs"], cwd=project_root)


def resolve_esptool() -> tuple[list[str], dict[str, str]]:
    penv_python = Path.home() / ".platformio" / "penv" / "bin" / "python"
    esptool_py = Path.home() / ".platformio" / "packages" / "tool-esptoolpy" / "esptool.py"
    if not penv_python.is_file():
        raise SystemExit(f"PlatformIO Python not found: {penv_python}")
    if not esptool_py.is_file():
        raise SystemExit(f"PlatformIO esptool.py not found: {esptool_py}")

    env = os.environ.copy()
    current_pythonpath = env.get("PYTHONPATH")
    tool_path = str(esptool_py.parent)
    env["PYTHONPATH"] = (
        f"{tool_path}{os.pathsep}{current_pythonpath}" if current_pythonpath else tool_path
    )
    return [str(penv_python), str(esptool_py)], env


def main() -> int:
    args = parse_args()
    project_root = Path(__file__).resolve().parent.parent
    build_dir = project_root / ".pio" / "build" / args.env
    partition_csv = (project_root / args.partition_csv).resolve()
    header_path = project_root / "include" / "my_debug.h"

    ensure_artifacts(
        project_root=project_root,
        env_name=args.env,
        build_dir=build_dir,
        no_build=args.no_build,
    )

    if not partition_csv.is_file():
        raise SystemExit(f"Partition CSV not found: {partition_csv}")

    flasher_args = read_flasher_args(build_dir / "flasher_args.json")
    flash_settings = flasher_args.get("flash_settings", {})
    flash_mode = str(flash_settings.get("flash_mode", "dio"))
    flash_freq = str(flash_settings.get("flash_freq", "80m"))
    flash_size = str(flash_settings.get("flash_size", EXPECTED_FLASH_SIZE))
    chip = str(flasher_args.get("extra_esptool_args", {}).get("chip", "esp32s3"))

    if flash_size != EXPECTED_FLASH_SIZE:
        raise SystemExit(
            f"This script is intended for 16MB targets, but build output reports {flash_size}."
        )

    offsets = read_partition_offsets(partition_csv)
    otadata_offset = offsets.get("otadata")
    littlefs_offset = offsets.get("littlefs")
    if not otadata_offset:
        raise SystemExit(f"Could not find otadata offset in {partition_csv}")
    if not littlefs_offset:
        raise SystemExit(f"Could not find littlefs offset in {partition_csv}")

    bootloader_offset = str(flasher_args.get("bootloader", {}).get("offset", "0x0"))
    partition_offset = str(flasher_args.get("partition-table", {}).get("offset", "0x8000"))
    app_offset = str(flasher_args.get("app", {}).get("offset", "0x10000"))

    product_model = read_define(header_path, "PRODUCT_MODEL") or "Pixel"
    version = read_define(header_path, "SW_VERSION") or "dev"
    default_name = (
        f"{sanitize_fragment(product_model)}_{sanitize_fragment(version)}_full_{flash_size}.bin"
    )
    output_path = Path(args.output).expanduser() if args.output else build_dir / default_name
    if not output_path.is_absolute():
        output_path = (project_root / output_path).resolve()

    bootloader_bin = build_dir / "bootloader.bin"
    partitions_bin = build_dir / "partitions.bin"
    ota_data_bin = build_dir / "ota_data_initial.bin"
    firmware_bin = build_dir / "firmware.bin"
    littlefs_bin = build_dir / "littlefs.bin"

    required_files = [
        bootloader_bin,
        partitions_bin,
        ota_data_bin,
        firmware_bin,
        littlefs_bin,
    ]
    missing_files = [path for path in required_files if not path.is_file()]
    if missing_files:
        missing = "\n".join(f"- {path}" for path in missing_files)
        raise SystemExit(f"Missing merge inputs:\n{missing}")

    output_path.parent.mkdir(parents=True, exist_ok=True)

    print("Preparing merged firmware image:")
    print(f"  env: {args.env}")
    print(f"  chip: {chip}")
    print(f"  flash size: {flash_size}")
    print(f"  bootloader @ {bootloader_offset}: {bootloader_bin}")
    print(f"  partitions @ {partition_offset}: {partitions_bin}")
    print(f"  otadata @ {otadata_offset}: {ota_data_bin}")
    print(f"  app @ {app_offset}: {firmware_bin}")
    print(f"  littlefs @ {littlefs_offset}: {littlefs_bin}")
    print(f"  output: {output_path}")

    esptool_cmd, esptool_env = resolve_esptool()
    merge_command = [
        *esptool_cmd,
        "--chip",
        chip,
        "merge_bin",
        "--output",
        str(output_path),
        "--flash_mode",
        flash_mode,
        "--flash_freq",
        flash_freq,
        "--flash_size",
        flash_size,
        "--fill-flash-size",
        flash_size,
        bootloader_offset,
        str(bootloader_bin),
        partition_offset,
        str(partitions_bin),
        otadata_offset,
        str(ota_data_bin),
        app_offset,
        str(firmware_bin),
        littlefs_offset,
        str(littlefs_bin),
    ]
    run_command(merge_command, cwd=project_root, env=esptool_env)

    size_bytes = output_path.stat().st_size
    print(f"Merged image ready: {output_path}")
    print(f"Image size: {size_bytes} bytes")
    print(f"Burn at offset 0x0 when flashing the merged file.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
