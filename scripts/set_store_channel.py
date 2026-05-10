#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from urllib import error, request


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_IP_FILE = ROOT / "device_ip.txt"


def load_default_host(ip_file: Path) -> str:
    if not ip_file.is_file():
        raise SystemExit(f"device ip file not found: {ip_file}")

    for raw in ip_file.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        return line

    raise SystemExit(f"no valid host found in: {ip_file}")


def http_json(method: str, url: str, payload=None, timeout: int = 8) -> dict:
    data = None
    headers = {"Accept": "application/json"}
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"

    req = request.Request(url, data=data, headers=headers, method=method.upper())
    with request.urlopen(req, timeout=timeout) as resp:
        body = resp.read().decode("utf-8", errors="replace").strip()
        if not body:
            return {}
        try:
            return json.loads(body)
        except json.JSONDecodeError:
            return {"raw": body}


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser(
        description="Set device store channel to beta/stable and verify readback.",
    )
    ap.add_argument(
        "channel",
        choices=["beta", "stable"],
        help="Target channel",
    )
    ap.add_argument(
        "--host",
        default="",
        help="Device host/IP. If omitted, read first valid line from device_ip.txt",
    )
    ap.add_argument(
        "--timeout",
        type=int,
        default=8,
        help="HTTP timeout seconds (default: 8)",
    )
    return ap.parse_args()


def main() -> int:
    args = parse_args()
    host = args.host.strip() or load_default_host(DEFAULT_IP_FILE)

    base = f"http://{host}"
    set_url = f"{base}/api/store/channel"
    get_url = f"{base}/api/store/channel"

    print(f"host: {host}")
    print(f"channel -> {args.channel}")

    try:
        set_resp = http_json("POST", set_url, {"channel": args.channel}, timeout=args.timeout)
        print("SET response:", json.dumps(set_resp, ensure_ascii=False))

        get_resp = http_json("GET", get_url, timeout=args.timeout)
        print("GET response:", json.dumps(get_resp, ensure_ascii=False))

        current = str(get_resp.get("channel", "")).strip().lower()
        if current != args.channel:
            print(
                f"[FAIL] verify mismatch: expected={args.channel}, actual={current or '(empty)'}",
                file=sys.stderr,
            )
            return 2

        print(f"[OK] channel is now: {current}")
        return 0

    except error.HTTPError as e:
        detail = e.read().decode("utf-8", errors="replace")
        print(f"[HTTPError] {e.code} {e.reason}: {detail}", file=sys.stderr)
        return 1
    except Exception as e:  # noqa: BLE001
        print(f"[Error] {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
