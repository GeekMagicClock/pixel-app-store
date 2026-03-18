#!/usr/bin/env python3
import argparse
import pathlib
import sys
import urllib.request
import urllib.error
import json


def find_default_firmware(project_root: pathlib.Path) -> pathlib.Path:
    candidates = [
        project_root / ".pio" / "build" / "esp32-s3-n16r8" / "firmware.bin",
        project_root / ".pio" / "build" / "hub75_idf" / "firmware.bin",
    ]
    for path in candidates:
        if path.exists():
            return path
    raise FileNotFoundError("No firmware.bin found in .pio/build/{esp32-s3-n16r8,hub75_idf}")


def http_json(url: str):
    with urllib.request.urlopen(url, timeout=5) as resp:
        return json.loads(resp.read().decode("utf-8"))


def upload_file(url: str, data: bytes):
    req = urllib.request.Request(url, data=data, method="POST")
    req.add_header("Content-Type", "application/octet-stream")
    req.add_header("Content-Length", str(len(data)))
    with urllib.request.urlopen(req, timeout=60) as resp:
        body = resp.read().decode("utf-8", errors="replace")
        return resp.status, body


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Upload ESP32 firmware over local OTA by device IP")
    parser.add_argument("ip", help="Device IP address, e.g. 192.168.1.88")
    parser.add_argument("--firmware", help="Path to firmware.bin (default: latest .pio build output)")
    args = parser.parse_args()

    project_root = pathlib.Path(__file__).resolve().parents[1]
    fw_path = pathlib.Path(args.firmware) if args.firmware else find_default_firmware(project_root)
    fw_path = fw_path.resolve()

    if not fw_path.exists():
        print(f"Firmware not found: {fw_path}", file=sys.stderr)
        sys.exit(1)

    base = f"http://{args.ip}"
    try:
        info = http_json(base + "/api/firmware")
        print(f"Target: {info.get('project')} version={info.get('version')} partition={info.get('partition')}")
    except Exception as exc:
        print(f"Warning: could not query firmware status: {exc}", file=sys.stderr)

    data = fw_path.read_bytes()
    print(f"Uploading {fw_path.name} ({len(data)} bytes) to {base}/api/firmware/ota ...")
    try:
        status, body = upload_file(base + "/api/firmware/ota", data)
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace")
        print(f"HTTP {exc.code}: {detail}", file=sys.stderr)
        sys.exit(2)
    except Exception as exc:
        print(f"Upload failed: {exc}", file=sys.stderr)
        sys.exit(3)

    print(f"HTTP {status}: {body}")
    print("If the response says rebooting=true, wait a few seconds for the device to restart.")
