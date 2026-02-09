#!/usr/bin/env python3

import json
import os
import subprocess
import sys
import time
from typing import List, Optional


def _detect_first_serial_port() -> Optional[str]:
    try:
        out = subprocess.check_output(
            ["pio", "device", "list", "--serial", "--json-output"],
            stderr=subprocess.DEVNULL,
            text=True,
        )
        ports = json.loads(out) if out.strip() else []
        for p in ports:
            port = p.get("port")
            if port:
                return port
    except Exception:
        return None
    return None


def _pulse_reset(port: str, baud: int) -> None:
    try:
        import serial  # type: ignore
    except Exception as e:
        print(f"[monitor_reset] pyserial not available: {e}", file=sys.stderr)
        return

    # Open briefly and toggle control lines to reset ESP32 (stay out of ROM bootloader).
    # Typical wiring: RTS -> EN (inverted), DTR -> IO0 (inverted).
    try:
        ser = serial.Serial(port=port, baudrate=baud, timeout=0.1)
    except Exception as e:
        print(f"[monitor_reset] failed to open {port}: {e}", file=sys.stderr)
        return

    try:
        # Keep IO0 deasserted.
        ser.setDTR(False)
        time.sleep(0.02)
        # Pulse EN low then high.
        ser.setRTS(True)
        time.sleep(0.10)
        ser.setRTS(False)
        time.sleep(0.10)
    finally:
        try:
            ser.close()
        except Exception:
            pass


def _dump_serial(port: str, baud: int, seconds: float) -> int:
    try:
        import serial  # type: ignore
    except Exception as e:
        print(f"[monitor_reset] pyserial not available: {e}", file=sys.stderr)
        return 2

    end_t = time.time() + seconds
    try:
        ser = serial.Serial(port=port, baudrate=baud, timeout=0.2)
    except Exception as e:
        print(f"[monitor_reset] failed to open {port}: {e}", file=sys.stderr)
        return 2

    try:
        buf = b""
        while time.time() < end_t:
            chunk = ser.read(1024)
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                try:
                    print(line.decode("utf-8", errors="replace"))
                except Exception:
                    print(repr(line))
        if buf:
            try:
                print(buf.decode("utf-8", errors="replace"), end="")
            except Exception:
                print(repr(buf))
    finally:
        try:
            ser.close()
        except Exception:
            pass
    return 0


def main(argv: List[str]) -> int:
    port = argv[1] if len(argv) > 1 else ""
    baud_s = argv[2] if len(argv) > 2 else ""
    project_dir = argv[3] if len(argv) > 3 else ""
    env_name = argv[4] if len(argv) > 4 else ""
    dump_seconds_s = argv[5] if len(argv) > 5 else ""

    if not project_dir or project_dir.startswith("$"):
        project_dir = os.getcwd()

    try:
        baud = int(baud_s) if baud_s else 115200
    except Exception:
        baud = 115200

    if not port or port.startswith("$"):
        port = _detect_first_serial_port() or port

    if not port or port.startswith("$"):
        print("[monitor_reset] no serial port detected; starting monitor without reset", file=sys.stderr)
    else:
        _pulse_reset(port, baud)

    # If a duration is provided, dump raw serial output for that many seconds.
    if dump_seconds_s:
        try:
            dump_seconds = float(dump_seconds_s)
        except Exception:
            dump_seconds = 10.0
        if not port or port.startswith("$"):
            print("[monitor_reset] can't dump without a valid serial port", file=sys.stderr)
            return 2
        return _dump_serial(port, baud, dump_seconds)

    # Otherwise, hand off to PlatformIO's monitor (interactive).
    cmd = ["pio", "device", "monitor", "--baud", str(baud)]
    if port and not port.startswith("$"):
        cmd += ["--port", port]
    if project_dir and env_name:
        cmd += ["--project-dir", project_dir, "--environment", env_name]

    os.execvp(cmd[0], cmd)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
