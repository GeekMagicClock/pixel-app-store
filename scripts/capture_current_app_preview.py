#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import re
import struct
import time
import urllib.error
import urllib.parse
import urllib.request
import zlib
from pathlib import Path


APP_RE_1 = re.compile(r"screen shown \(/littlefs/apps/([A-Za-z0-9_-]+)\)")
APP_RE_2 = re.compile(r"switch app requested:\s*([A-Za-z0-9_-]+)")
APP_RE_3 = re.compile(r"/littlefs/apps/([A-Za-z0-9_-]+)(?:/|\)|\\b)")
_OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def http_json(url: str) -> dict:
  last_err: Exception | None = None
  for _ in range(4):
    try:
      req = urllib.request.Request(url, method="GET")
      with _OPENER.open(req, timeout=10) as resp:
        data = resp.read()
      return json.loads(data.decode("utf-8", errors="replace"))
    except Exception as e:
      last_err = e
      time.sleep(0.25)
  if last_err:
    raise last_err
  raise RuntimeError("http_json failed")


def http_get_bytes(url: str) -> bytes:
  last_err: Exception | None = None
  for attempt in range(8):
    try:
      req = urllib.request.Request(url, method="GET")
      with _OPENER.open(req, timeout=10) as resp:
        return resp.read()
    except Exception as e:
      last_err = e
      time.sleep(0.2 + attempt * 0.1)
  if last_err:
    raise last_err
  raise RuntimeError("http_get_bytes failed")


def http_put_bytes(url: str, body: bytes, content_type: str = "application/octet-stream") -> dict:
  last_err: Exception | None = None
  for _ in range(4):
    try:
      req = urllib.request.Request(url, data=body, method="PUT")
      req.add_header("Content-Type", content_type)
      with _OPENER.open(req, timeout=15) as resp:
        data = resp.read()
      try:
        return json.loads(data.decode("utf-8", errors="replace"))
      except Exception:
        return {"ok": False, "raw": data.decode("utf-8", errors="replace")}
    except Exception as e:
      last_err = e
      time.sleep(0.25)
  if last_err:
    raise last_err
  raise RuntimeError("http_put_bytes failed")


def http_post_json(url: str) -> dict:
  last_err: Exception | None = None
  for _ in range(4):
    try:
      req = urllib.request.Request(url, data=b"{}", method="POST")
      req.add_header("Content-Type", "application/json")
      with _OPENER.open(req, timeout=15) as resp:
        data = resp.read()
      try:
        return json.loads(data.decode("utf-8", errors="replace"))
      except Exception:
        return {"ok": False, "raw": data.decode("utf-8", errors="replace")}
    except Exception as e:
      last_err = e
      time.sleep(0.25)
  if last_err:
    raise last_err
  raise RuntimeError("http_post_json failed")


def parse_ppm_p6(buf: bytes) -> tuple[int, int, bytes]:
  if not buf.startswith(b"P6"):
    raise ValueError("not a P6 PPM")

  i = 2
  n = len(buf)
  tokens: list[bytes] = []

  def skip_ws_and_comments(pos: int) -> int:
    while pos < n:
      ch = buf[pos]
      if ch in b" \t\r\n":
        pos += 1
        continue
      if ch == ord("#"):
        while pos < n and buf[pos] not in b"\r\n":
          pos += 1
        continue
      break
    return pos

  while len(tokens) < 3:
    i = skip_ws_and_comments(i)
    if i >= n:
      raise ValueError("bad PPM header")
    j = i
    while j < n and buf[j] not in b" \t\r\n#":
      j += 1
    tokens.append(buf[i:j])
    i = j

  w = int(tokens[0])
  h = int(tokens[1])
  maxv = int(tokens[2])
  if maxv != 255:
    raise ValueError(f"unsupported maxval {maxv}, only 255 is supported")

  i = skip_ws_and_comments(i)
  rgb = buf[i:]
  expected = w * h * 3
  if len(rgb) < expected:
    raise ValueError(f"PPM payload too short: got {len(rgb)}, need {expected}")
  if len(rgb) > expected:
    rgb = rgb[:expected]
  return w, h, rgb


def write_png_rgb8(path: Path, w: int, h: int, rgb: bytes) -> None:
  if len(rgb) != w * h * 3:
    raise ValueError("bad rgb length for PNG")

  def chunk(kind: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

  raw = bytearray()
  row_bytes = w * 3
  for y in range(h):
    raw.append(0)  # filter type: None
    s = y * row_bytes
    raw.extend(rgb[s:s + row_bytes])

  ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)  # RGB, 8-bit
  idat = zlib.compress(bytes(raw), level=9)
  png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", idat) + chunk(b"IEND", b"")
  path.parent.mkdir(parents=True, exist_ok=True)
  path.write_bytes(png)


def is_mostly_black(rgb: bytes, threshold: int = 2, min_non_black: int = 24) -> bool:
  if not rgb:
    return True
  non_black = 0
  # Treat very dark pixels as black to avoid false positives from sensor noise.
  for i in range(0, len(rgb), 3):
    r = rgb[i]
    g = rgb[i + 1]
    b = rgb[i + 2]
    if r > threshold or g > threshold or b > threshold:
      non_black += 1
      if non_black >= min_non_black:
        return False
  return True


def detect_current_app(host: str) -> str:
  base = f"http://{host}"
  try:
    current = http_json(f"{base}/api/apps/current")
    if bool(current.get("running")):
      app_id = str(current.get("app_id", "")).strip()
      if re.fullmatch(r"[A-Za-z0-9_-]{1,48}", app_id):
        return app_id
  except Exception:
    pass

  base = f"http://{host}"
  for scope in ("all", "app"):
    try:
      j = http_json(f"{base}/api/system/logs?scope={scope}&after=0&limit=120")
    except Exception:
      continue
    logs = j.get("logs")
    if not isinstance(logs, list):
      continue
    for entry in reversed(logs):
      text = str(entry.get("text", ""))
      m1 = APP_RE_1.search(text)
      if m1:
        return m1.group(1)
      m2 = APP_RE_2.search(text)
      if m2:
        return m2.group(1)
      m3 = APP_RE_3.search(text)
      if m3:
        return m3.group(1)
  raise RuntimeError("cannot detect current app from /api/apps/current or /api/system/logs; please pass --app")


def verify_thumbnail_flag(host: str, app_id: str) -> bool:
  j = http_json(f"http://{host}/api/apps/list")
  apps = j.get("apps")
  if not isinstance(apps, list):
    return False
  for item in apps:
    if str(item.get("id", "")) == app_id:
      return bool(item.get("has_thumbnail"))
  return False


def read_default_host(repo_root: Path) -> str:
  device_file = Path(os.environ.get("DEVICE_IP_FILE", str(repo_root / "device_ip.txt")))
  if not device_file.exists():
    raise RuntimeError(f"device ip not provided and file not found: {device_file}")
  for raw in device_file.read_text(encoding="utf-8", errors="replace").splitlines():
    line = raw.split("#", 1)[0].strip()
    if line:
      return line
  raise RuntimeError(f"no device ip found in {device_file}")


def main() -> int:
  parser = argparse.ArgumentParser(description="Capture current running app preview from device and store/upload it.")
  parser.add_argument("--host", default="", help="device host or IP (default: read from device_ip.txt)")
  parser.add_argument("--app", default="", help="override app id; if omitted script auto-detects from /api/apps/current")
  parser.add_argument("--repo-root", default=".", help="repo root containing data_littlefs/apps")
  parser.add_argument("--no-upload", action="store_true", help="only save local preview.png, do not PUT to device")
  parser.add_argument("--retries", type=int, default=6, help="capture retries when frame is black, default: 6")
  parser.add_argument("--retry-delay-ms", type=int, default=250, help="delay between retries in ms, default: 250")
  args = parser.parse_args()

  repo_root = Path(args.repo_root).resolve()
  host = args.host.strip() if args.host else ""
  if not host:
    host = read_default_host(repo_root)
  if not host:
    raise SystemExit("host is required")

  try:
    app_id = args.app.strip() or detect_current_app(host)
  except Exception as e:
    raise SystemExit(f"[ERROR] {e}")

  print(f"[INFO] app_id={app_id}")

  retries = max(1, int(args.retries))
  delay_s = max(0, int(args.retry_delay_ms)) / 1000.0
  last_err: Exception | None = None
  w = h = 0
  rgb = b""
  for attempt in range(1, retries + 1):
    try:
      ppm = http_get_bytes(f"http://{host}/api/screen/capture.ppm")
      w, h, rgb = parse_ppm_p6(ppm)
      if is_mostly_black(rgb):
        if attempt < retries:
          print(f"[WARN] capture frame is black, retry {attempt}/{retries}")
          time.sleep(delay_s)
          continue
      break
    except urllib.error.HTTPError as e:
      last_err = e
      if attempt < retries:
        print(f"[WARN] capture failed HTTP {e.code}, retry {attempt}/{retries}")
        time.sleep(delay_s)
        continue
      raise SystemExit(f"[ERROR] capture failed: HTTP {e.code}")
    except Exception as e:
      last_err = e
      if attempt < retries:
        print(f"[WARN] capture failed ({e}), retry {attempt}/{retries}")
        time.sleep(delay_s)
        continue
      raise SystemExit(f"[ERROR] capture failed: {e}")
  if w <= 0 or h <= 0 or not rgb:
    if last_err:
      raise SystemExit(f"[ERROR] capture failed: {last_err}")
    raise SystemExit("[ERROR] capture failed: empty frame")
  print(f"[INFO] capture size={w}x{h}")

  local_png = repo_root / "data_littlefs" / "apps" / app_id / "thumbnail.png"
  write_png_rgb8(local_png, w, h, rgb)
  print(f"[INFO] wrote local preview: {local_png}")

  if args.no_upload:
    print("[INFO] skip upload (--no-upload)")
    return 0

  app_id_enc = urllib.parse.quote(app_id, safe="")
  begin_url = f"http://{host}/api/apps/install/begin?app_id={app_id_enc}"
  commit_url = f"http://{host}/api/apps/install/commit"
  abort_url = f"http://{host}/api/apps/install/abort"
  upload_url = f"http://{host}/api/apps/{app_id_enc}/thumbnail.png"
  committed = False
  begin_resp = http_post_json(begin_url)
  if not bool(begin_resp.get("ok")):
    raise SystemExit(f"[ERROR] install begin failed: {begin_resp}")
  try:
    put_resp = http_put_bytes(upload_url, local_png.read_bytes(), content_type="image/png")
    if not bool(put_resp.get("ok")):
      raise SystemExit(f"[ERROR] upload failed: {put_resp}")
    commit_resp = http_post_json(commit_url)
    if not bool(commit_resp.get("ok")):
      raise SystemExit(f"[ERROR] install commit failed: {commit_resp}")
    committed = True
    print("[INFO] uploaded thumbnail.png to device")
  finally:
    if not committed:
      try:
        http_post_json(abort_url)
      except Exception:
        pass

  try:
    ok = verify_thumbnail_flag(host, app_id)
    print(f"[INFO] has_thumbnail={str(ok).lower()}")
  except Exception as e:
    print(f"[WARN] verify has_thumbnail failed: {e}")

  return 0


if __name__ == "__main__":
  raise SystemExit(main())
