#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import subprocess
import time
import urllib.request
from pathlib import Path


def jget(url: str) -> dict:
    with urllib.request.urlopen(url, timeout=10) as resp:
        return json.loads(resp.read().decode("utf-8"))


def bget(url: str) -> bytes:
    with urllib.request.urlopen(url, timeout=15) as resp:
        return resp.read()


def run(cmd: list[str], cwd: Path) -> None:
    print("+", " ".join(cmd))
    subprocess.run(cmd, cwd=str(cwd), check=True)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("device")
    ap.add_argument("--app-id", default="frame_rect_analog_clock")
    ap.add_argument("--push", action="store_true")
    ap.add_argument("--wait-ms", type=int, default=5000)
    ap.add_argument("--artifacts-dir", default="artifacts/frame_rect_analog_clock")
    args = ap.parse_args()

    root = Path(__file__).resolve().parents[1]
    art = root / args.artifacts_dir
    art.mkdir(parents=True, exist_ok=True)

    if args.push:
        run(["bash", "scripts/push_app.sh", args.device, args.app_id, "data_littlefs/apps", "--switch"], root)
    else:
        run(["bash", "scripts/switch_app.sh", args.device, args.app_id], root)

    time.sleep(args.wait_ms / 1000.0)
    status = jget(f"http://{args.device}/api/system/status")
    actual = bget(f"http://{args.device}/api/screen/capture.ppm")
    local = status["time"]["local"]
    exp = art / "expected.ppm"
    act = art / "actual.ppm"
    diff = art / "diff.ppm"
    act.write_bytes(actual)
    run(
        [
            "python3",
            "scripts/render_frame_rect_analog_clock.py",
            "render",
            "--time",
            f"{int(local['hour']):02d}:{int(local['min']):02d}:{int(local['sec']):02d}",
            "--date",
            f"{int(local['year']):04d}-{int(local['month']):02d}-{int(local['day']):02d}",
            "--wday",
            str(int(local["wday"])),
            "--output",
            str(exp),
        ],
        root,
    )
    run(
        [
            "python3",
            "scripts/render_frame_rect_analog_clock.py",
            "compare",
            "--expected",
            str(exp),
            "--actual",
            str(act),
            "--diff",
            str(diff),
        ],
        root,
    )
    print(f"artifacts written to {art}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
