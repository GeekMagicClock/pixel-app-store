#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path


def http_get_json(url: str) -> dict:
    with urllib.request.urlopen(url, timeout=10) as resp:
        return json.loads(resp.read().decode("utf-8"))


def http_get_bytes(url: str) -> bytes:
    with urllib.request.urlopen(url, timeout=15) as resp:
        return resp.read()


def run(cmd: list[str], cwd: Path) -> None:
    print("+", " ".join(cmd))
    subprocess.run(cmd, cwd=str(cwd), check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description="Push, switch, capture, and compare the atelier analog clock app.")
    parser.add_argument("device", help="device ip[:port], for example 192.168.1.88")
    parser.add_argument("--app-id", default="atelier_analog_clock")
    parser.add_argument("--artifacts-dir", default="artifacts/atelier_analog_clock")
    parser.add_argument("--push", action="store_true")
    parser.add_argument("--wait-ms", type=int, default=5000)
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    artifacts = (root / args.artifacts_dir).resolve()
    artifacts.mkdir(parents=True, exist_ok=True)
    base = f"http://{args.device}"

    if args.push:
        run(["bash", "scripts/push_app.sh", args.device, args.app_id, "data_littlefs/apps", "--switch"], root)
    else:
        run(["bash", "scripts/switch_app.sh", args.device, args.app_id], root)

    time.sleep(args.wait_ms / 1000.0)

    try:
        status = http_get_json(base + "/api/system/status")
        capture = http_get_bytes(base + "/api/screen/capture.ppm")
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace")
        print(f"HTTP {exc.code}: {detail}", file=sys.stderr)
        return 2

    time_info = status.get("time", {})
    local_time = time_info.get("local", {})
    hour = int(local_time.get("hour", 10))
    minute = int(local_time.get("min", 10))
    second = int(local_time.get("sec", 30))
    year = int(local_time.get("year", 2026))
    month = int(local_time.get("month", 3))
    day = int(local_time.get("day", 24))
    wday = int(local_time.get("wday", 3))

    actual_ppm = artifacts / "actual.ppm"
    expected_ppm = artifacts / "expected.ppm"
    diff_ppm = artifacts / "diff.ppm"
    actual_ppm.write_bytes(capture)

    run(
        [
            "python3",
            "scripts/render_atelier_analog_clock.py",
            "render",
            "--time",
            f"{hour:02d}:{minute:02d}:{second:02d}",
            "--date",
            f"{year:04d}-{month:02d}-{day:02d}",
            "--wday",
            str(wday),
            "--output",
            str(expected_ppm),
        ],
        root,
    )

    run(
        [
            "python3",
            "scripts/render_atelier_analog_clock.py",
            "compare",
            "--expected",
            str(expected_ppm),
            "--actual",
            str(actual_ppm),
            "--diff",
            str(diff_ppm),
        ],
        root,
    )

    print(f"artifacts written to {artifacts}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
