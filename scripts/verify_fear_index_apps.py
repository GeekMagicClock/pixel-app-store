#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import subprocess
import time
import urllib.request
from pathlib import Path


APP_META = {
    "crypto": {
        "app_id": "crypto_fear_index",
        "title": "CRYPTO",
        "url": "http://192.168.3.139:8787/alternative/fng?limit=7&format=json",
        "artifacts_dir": "artifacts/crypto_fear_index",
    },
    "stock": {
        "app_id": "stock_fear_index",
        "title": "STOCK",
        "url": "http://192.168.3.139:8787/stocks/fear_greed",
        "artifacts_dir": "artifacts/stock_fear_index",
    },
}


def jget(url: str) -> dict:
    with urllib.request.urlopen(url, timeout=15) as resp:
        return json.loads(resp.read().decode("utf-8"))


def bget(url: str) -> bytes:
    with urllib.request.urlopen(url, timeout=15) as resp:
        return resp.read()


def run(cmd: list[str], cwd: Path) -> None:
    print("+", " ".join(cmd))
    subprocess.run(cmd, cwd=str(cwd), check=True)


def run_capture(cmd: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    print("+", " ".join(cmd))
    return subprocess.run(cmd, cwd=str(cwd), check=False, capture_output=True, text=True)


def build_render_args(kind: str, payload: dict) -> list[str]:
    if kind == "crypto":
        rows = payload.get("data") or []
        cur = rows[0]
        prev = rows[1] if len(rows) > 1 else None
        score = int(round(float(cur["value"])))
        label = str(cur["value_classification"])
        delta = float(cur["value"]) - float(prev["value"]) if prev else 0.0
        history = ",".join(str(float(row["value"])) for row in rows[:7])
    else:
        score = int(round(float(payload["score"])))
        label = str(payload["label"])
        hist_rows = payload.get("history") or []
        prev = hist_rows[1] if len(hist_rows) > 1 else None
        delta = float(payload["score"]) - float(prev["score"]) if prev else 0.0
        history = ",".join(str(float(row["score"])) for row in hist_rows[:7])
    return [
        "--score",
        str(score),
        "--label",
        label,
        "--delta",
        str(delta),
        "--history",
        history,
        "--title",
        APP_META[kind]["title"],
    ]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("kind", choices=sorted(APP_META.keys()))
    ap.add_argument("device")
    ap.add_argument("--push", action="store_true")
    ap.add_argument("--wait-ms", type=int, default=5000)
    args = ap.parse_args()

    root = Path(__file__).resolve().parents[1]
    meta = APP_META[args.kind]
    art = root / meta["artifacts_dir"]
    art.mkdir(parents=True, exist_ok=True)

    if args.push:
        run(["bash", "scripts/push_app.sh", args.device, meta["app_id"], "data_littlefs/apps", "--switch"], root)
    else:
        run(["bash", "scripts/switch_app.sh", args.device, meta["app_id"]], root)

    time.sleep(args.wait_ms / 1000.0)
    payload = jget(meta["url"])
    actual = bget(f"http://{args.device}/api/screen/capture.ppm")
    exp = art / "expected.ppm"
    act = art / "actual.ppm"
    diff = art / "diff.ppm"
    act.write_bytes(actual)
    run(
        [
            "python3",
            "scripts/render_fear_index_apps.py",
            "render",
            args.kind,
            "--output",
            str(exp),
            *build_render_args(args.kind, payload),
        ],
        root,
    )
    proc = run_capture(
        [
            "python3",
            "scripts/render_fear_index_apps.py",
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
    if proc.stdout.strip():
        print(proc.stdout.strip())
    if proc.stderr.strip():
        print(proc.stderr.strip())
    print(f"artifacts written to {art}")
    return proc.returncode


if __name__ == "__main__":
    raise SystemExit(main())
