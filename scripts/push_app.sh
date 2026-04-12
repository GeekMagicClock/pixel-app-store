#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  scripts/push_app.sh <app_id> [apps_root] [--switch]
  scripts/push_app.sh <device_ip[:port]> <app_id> [apps_root] [--switch]

Examples:
  scripts/push_app.sh binance_ticker
  scripts/push_app.sh sunrise_sunset_owm data_littlefs/apps --switch
  scripts/push_app.sh 192.168.1.88 binance_ticker
  scripts/push_app.sh 192.168.1.88:80 weather_owm data_littlefs/apps
  scripts/push_app.sh 192.168.1.88 sunrise_sunset_owm data_littlefs/apps --switch
EOF
}

read_default_device() {
  local repo_root device_file line trimmed
  repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
  device_file="${DEVICE_IP_FILE:-${repo_root}/device_ip.txt}"
  if [[ ! -f "${device_file}" ]]; then
    echo "error: device ip not provided and file not found: ${device_file}" >&2
    return 1
  fi
  while IFS= read -r line || [[ -n "${line}" ]]; do
    trimmed="$(printf '%s' "${line}" | sed -e 's/#.*$//' -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')"
    if [[ -n "${trimmed}" ]]; then
      printf '%s\n' "${trimmed}"
      return 0
    fi
  done < "${device_file}"
  echo "error: no device ip found in ${device_file}" >&2
  return 1
}

looks_like_device() {
  local v="$1"
  [[ "${v}" == *.* || "${v}" == *:* || "${v}" == "localhost" ]]
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

if [[ $# -lt 1 || $# -gt 4 ]]; then
  usage
  exit 1
fi

if ! command -v curl >/dev/null 2>&1; then
  echo "error: curl not found" >&2
  exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
  echo "error: python3 not found" >&2
  exit 1
fi

if [[ $# -ge 2 ]] && looks_like_device "$1"; then
  DEVICE="$1"
  shift
else
  DEVICE="$(read_default_device)"
fi

APP_ID="$1"
shift

APPS_ROOT="data_littlefs/apps"
SWITCH_AFTER_PUSH=""
if [[ $# -ge 1 ]]; then
  if [[ "$1" == "--switch" ]]; then
    SWITCH_AFTER_PUSH="--switch"
    shift
  else
    APPS_ROOT="$1"
    shift
  fi
fi
if [[ $# -ge 1 ]]; then
  if [[ "$1" == "--switch" ]]; then
    SWITCH_AFTER_PUSH="--switch"
    shift
  else
    echo "error: unknown option '$1'" >&2
    exit 1
  fi
fi
if [[ $# -ne 0 ]]; then
  usage
  exit 1
fi

APP_DIR="${APPS_ROOT}/${APP_ID}"
BASE_URL="http://${DEVICE}"
CURL_ARGS=(--silent --show-error --fail --noproxy "*")
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEFAULT_LUAC="${REPO_ROOT}/python/store/tools/luac-esp-compat"
LUA_COMPILER="${LUA_COMPILER:-${DEFAULT_LUAC}}"

if [[ "${SWITCH_AFTER_PUSH}" != "" && "${SWITCH_AFTER_PUSH}" != "--switch" ]]; then
  echo "error: unknown option '${SWITCH_AFTER_PUSH}', only --switch is supported" >&2
  exit 1
fi

if [[ ! -d "${APP_DIR}" ]]; then
  echo "error: app dir not found: ${APP_DIR}" >&2
  exit 1
fi

if [[ ! "${APP_ID}" =~ ^[A-Za-z0-9_-]+$ ]]; then
  echo "error: invalid app_id '${APP_ID}' (allowed: A-Z a-z 0-9 _ -)" >&2
  exit 1
fi

echo "==> Checking device API: ${BASE_URL}/api/apps/ping"
curl "${CURL_ARGS[@]}" "${BASE_URL}/api/apps/ping" >/dev/null

if [[ ! -x "${LUA_COMPILER}" ]]; then
  if command -v luac >/dev/null 2>&1; then
    LUA_COMPILER="$(command -v luac)"
  else
    echo "error: luac compiler not found. expected ${DEFAULT_LUAC} or luac in PATH" >&2
    exit 1
  fi
fi

STAGE_ROOT="$(mktemp -d "/tmp/push_app_stage_${APP_ID}_XXXXXX")"
trap 'rm -rf "${STAGE_ROOT}"' EXIT
STAGE_APP_DIR="${STAGE_ROOT}/${APP_ID}"
mkdir -p "${STAGE_APP_DIR}"

if [[ -f "${APP_DIR}/app.bin" && ! -f "${APP_DIR}/main.lua" ]]; then
  echo "==> Staging prebuilt bytecode app"
  python3 - "${APP_DIR}" "${STAGE_APP_DIR}" <<'PY'
import shutil
import sys
from pathlib import Path

src = Path(sys.argv[1])
dst = Path(sys.argv[2])

for path in sorted(src.rglob("*")):
    if path.is_dir():
        continue
    rel = path.relative_to(src)
    if any(part.startswith(".") for part in rel.parts):
        continue
    if rel.name == "settings.html":
        gz = path.with_name(path.name + ".gz")
        if not gz.exists():
            raise SystemExit(f"{src.name}: settings.html.gz is required when settings.html exists")
        # Upload only compressed settings page.
        continue
    out = dst / rel
    out.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(path, out)
PY
else
  echo "==> Staging bytecode app with compiler: ${LUA_COMPILER}"
  python3 - "${APP_DIR}" "${STAGE_APP_DIR}" "${LUA_COMPILER}" <<'PY'
import json
import shutil
import subprocess
import sys
from pathlib import Path

src = Path(sys.argv[1])
dst = Path(sys.argv[2])
luac = sys.argv[3]

manifest_path = src / "manifest.json"
main_lua = src / "main.lua"
if not manifest_path.exists():
    raise SystemExit(f"missing manifest.json: {manifest_path}")
if not main_lua.exists():
    raise SystemExit(f"missing main.lua: {main_lua}")

def is_hidden(rel: Path) -> bool:
    return any(part.startswith(".") for part in rel.parts)

for path in sorted(src.rglob("*")):
    if path.is_dir():
        continue
    rel = path.relative_to(src)
    if is_hidden(rel):
        continue
    if rel.name == "settings.html":
        gz = path.with_name(path.name + ".gz")
        if not gz.exists():
            raise SystemExit(f"{src.name}: settings.html.gz is required when settings.html exists")
        # Upload only compressed settings page.
        continue
    out = dst / rel
    out.parent.mkdir(parents=True, exist_ok=True)
    if rel.as_posix() == "manifest.json":
        manifest = json.loads(path.read_text(encoding="utf-8"))
        manifest["entry"] = "app.bin"
        manifest["lua_bytecode"] = True
        out.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    elif rel.as_posix() == "main.lua":
        out = dst / "app.bin"
        out.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run([luac, "-o", str(out), str(path)], check=True)
    elif path.suffix == ".lua":
        # Keep helper path/name stable, but content is bytecode (no source upload).
        subprocess.run([luac, "-o", str(out), str(path)], check=True)
    else:
        shutil.copy2(path, out)
PY
fi

uploaded=0
while IFS= read -r -d '' file_path; do
  rel="${file_path#${STAGE_APP_DIR}/}"
  if [[ -z "${rel}" ]]; then
    continue
  fi
  if [[ "${rel}" == .* ]]; then
    continue
  fi
  echo "==> Uploading ${file_path}"
  curl "${CURL_ARGS[@]}" \
    -X PUT \
    --data-binary @"${file_path}" \
    "${BASE_URL}/api/apps/${APP_ID}/${rel}" >/dev/null
  uploaded=$((uploaded + 1))
done < <(find "${STAGE_APP_DIR}" -type f -print0 | sort -z)

if [[ ${uploaded} -eq 0 ]]; then
  echo "error: no files found in staged dir ${STAGE_APP_DIR}" >&2
  exit 1
fi

# Clean stale plain HTML files on device when compressed variant is uploaded.
while IFS= read -r -d '' gz_path; do
  rel_gz="${gz_path#${STAGE_APP_DIR}/}"
  rel_html="${rel_gz%.gz}"
  if [[ "${rel_html}" == "${rel_gz}" ]]; then
    continue
  fi
  if [[ "${rel_html}" == *.html ]]; then
    curl --silent --show-error --noproxy "*" -X DELETE \
      "${BASE_URL}/api/apps/${APP_ID}/${rel_html}" >/dev/null || true
  fi
done < <(find "${STAGE_APP_DIR}" -type f -name "*.html.gz" -print0 | sort -z)

echo "==> Reloading app carousel"
curl "${CURL_ARGS[@]}" -X POST "${BASE_URL}/api/apps/reload" >/dev/null

if [[ "${SWITCH_AFTER_PUSH}" == "--switch" ]]; then
  echo "==> Switching to ${APP_ID}"
  curl "${CURL_ARGS[@]}" -X POST "${BASE_URL}/api/apps/switch/${APP_ID}" >/dev/null
fi

echo "done: pushed ${APP_ID} (${uploaded} file(s)) to ${DEVICE}"
