#!/usr/bin/env python3
import argparse
import os
import pathlib
import subprocess
import sys
import urllib.request
import urllib.error
import json

# Force direct LAN connection for OTA.
# urllib would otherwise honor HTTP(S)_PROXY from environment.
urllib.request.install_opener(urllib.request.build_opener(urllib.request.ProxyHandler({})))


def find_default_firmware(project_root: pathlib.Path) -> pathlib.Path:
    candidates = [
        project_root / ".pio" / "build" / "esp32-s3-n16r8" / "firmware.bin",
        project_root / ".pio" / "build" / "hub75_idf" / "firmware.bin",
    ]
    for path in candidates:
        if path.exists():
            return path
    raise FileNotFoundError("No firmware.bin found in .pio/build/{esp32-s3-n16r8,hub75_idf}")


def http_json(url: str, timeout_s: float):
    with urllib.request.urlopen(url, timeout=timeout_s) as resp:
        return json.loads(resp.read().decode("utf-8"))


def upload_file(url: str, data: bytes, timeout_s: float):
    req = urllib.request.Request(url, data=data, method="POST")
    req.add_header("Content-Type", "application/octet-stream")
    req.add_header("Content-Length", str(len(data)))
    with urllib.request.urlopen(req, timeout=timeout_s) as resp:
        body = resp.read().decode("utf-8", errors="replace")
        return resp.status, body


def read_default_device_ip(project_root: pathlib.Path) -> str:
    device_file = pathlib.Path(os.environ.get("DEVICE_IP_FILE", str(project_root / "device_ip.txt")))
    if not device_file.exists():
        raise FileNotFoundError(f"device ip not provided and file not found: {device_file}")
    for raw in device_file.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw.split("#", 1)[0].strip()
        if line:
            return line
    raise ValueError(f"no device ip found in {device_file}")


def build_firmware(project_root: pathlib.Path, env: str) -> None:
    cmd = ["pio", "run", "-e", env]
    print("+ " + " ".join(cmd))
    subprocess.run(cmd, cwd=str(project_root), check=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Upload ESP32 firmware over local OTA by device IP")
    parser.add_argument("ip", nargs="?", help="Device IP address, e.g. 192.168.1.88")
    parser.add_argument("--ip", dest="ip_opt", help="Device IP address (overrides positional ip)")
    parser.add_argument("--firmware", help="Path to firmware.bin (default: latest .pio build output)")
    parser.add_argument("--skip-build", action="store_true", help="Skip running pio run before OTA upload")
    parser.add_argument("--env", default="esp32-s3-n16r8", help="PlatformIO environment to build before upload")
    parser.add_argument("--status-timeout", type=float, default=8.0, help="Timeout (seconds) for firmware status query")
    parser.add_argument("--upload-timeout", type=float, default=240.0, help="Timeout (seconds) for OTA upload request")
    args = parser.parse_args()

    project_root = pathlib.Path(__file__).resolve().parents[1]
    ip = (args.ip_opt or args.ip or "").strip()
    if not ip:
        ip = read_default_device_ip(project_root)

    if not args.skip_build:
        build_firmware(project_root, args.env)

    fw_path = pathlib.Path(args.firmware) if args.firmware else find_default_firmware(project_root)
    fw_path = fw_path.resolve()

    if not fw_path.exists():
        print(f"Firmware not found: {fw_path}", file=sys.stderr)
        sys.exit(1)

    base = f"http://{ip}"
    try:
        info = http_json(base + "/api/firmware", timeout_s=max(1.0, float(args.status_timeout)))
        print(f"Target: {info.get('project')} version={info.get('version')} partition={info.get('partition')}")
    except Exception as exc:
        print(f"Warning: could not query firmware status: {exc}", file=sys.stderr)

    data = fw_path.read_bytes()
    print(f"Uploading {fw_path.name} ({len(data)} bytes) to {base}/api/firmware/ota ...")
    try:
        status, body = upload_file(base + "/api/firmware/ota", data, timeout_s=max(10.0, float(args.upload_timeout)))
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace")
        print(f"HTTP {exc.code}: {detail}", file=sys.stderr)
        sys.exit(2)
    except Exception as exc:
        print(f"Upload failed: {exc}", file=sys.stderr)
        sys.exit(3)

    print(f"HTTP {status}: {body}")
    print("If the response says rebooting=true, wait a few seconds for the device to restart.")
