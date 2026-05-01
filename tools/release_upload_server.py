#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
S2_SCRIPT = ROOT / "scripts" / "S2_firmware_publish_beta.sh"
SW_FILE = ROOT / "include" / "my_debug.h"


def read_sw_version() -> str:
    text = SW_FILE.read_text(encoding="utf-8")
    m = re.search(r'#define\s+SW_VERSION\s+"([^"]+)"', text)
    if not m:
        raise SystemExit(f"SW_VERSION not found in {SW_FILE}")
    v = m.group(1).strip()
    if v[:1].lower() == "v":
        v = v[1:]
    if not re.fullmatch(r"\d+\.\d+\.\d+", v):
        raise SystemExit(f"unsupported SW_VERSION format: {m.group(1)}")
    return v


def bump(ver: str, part: str) -> str:
    major, minor, patch = [int(x) for x in ver.split(".")]
    if part == "major":
        major += 1
        minor = 0
        patch = 0
    elif part == "minor":
        minor += 1
        patch = 0
    else:
        patch += 1
    return f"{major}.{minor}.{patch}"


def run(cmd: list[str]) -> None:
    print("+", " ".join(cmd))
    subprocess.run(cmd, cwd=str(ROOT), check=True)


def main() -> int:
    ap = argparse.ArgumentParser(description="Release firmware via unified OTA hub entry")
    ap.add_argument("notes", help="release notes")
    ap.add_argument("--product", default="pixel64x32V2")
    ap.add_argument("--model", default="pixel64x32V2")
    ap.add_argument("--channel", default="stable")
    ap.add_argument("--part", choices=["patch", "minor", "major"], default="patch")
    ap.add_argument("--skip-build", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--no-sync-server", action="store_true")
    ap.add_argument("--no-remote-exec", action="store_true")
    ap.add_argument("--env", default="esp32-s3-n16r8")
    args = ap.parse_args()

    if not S2_SCRIPT.is_file():
        raise SystemExit(f"missing script: {S2_SCRIPT}")

    ch = str(args.channel or "stable").strip().lower()
    if ch == "release":
        ch = "stable"
    if ch not in ("beta", "stable"):
        raise SystemExit(f"unsupported channel for pixel release: {args.channel}")

    current = read_sw_version()
    target = bump(current, args.part)

    cmd = ["bash", str(S2_SCRIPT), "--channel", ch, "--env", args.env, "--version", target]

    if args.skip_build:
        bin_path = ROOT / ".pio" / "build" / args.env / "firmware.bin"
        if not bin_path.is_file():
            raise SystemExit(f"--skip-build set but firmware not found: {bin_path}")
        cmd += ["--bin", str(bin_path)]

    # Release path should publish remotely by default.
    if not args.no_remote_exec:
        cmd.append("--push")

    if args.force:
        print("note: --force accepted (no extra guard in this project)")
    if args.no_sync_server:
        print("note: --no-sync-server accepted (no OTA hub sync step in this project)")

    print(f"notes: {args.notes}")
    print(f"product/model: {args.product}/{args.model}")
    print(f"channel: {ch}")
    print(f"version: {current} -> {target} (part={args.part})")

    if args.dry_run:
        print("dry-run only, command not executed")
        print(" ".join(cmd))
        return 0

    run(cmd)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
