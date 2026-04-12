#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  scripts/set_app_mock.sh <app_id> <mock_json_file> [--switch]
  scripts/set_app_mock.sh <app_id> --disable
  scripts/set_app_mock.sh <device_ip[:port]> <app_id> <mock_json_file> [--switch]
  scripts/set_app_mock.sh <device_ip[:port]> <app_id> --disable

Examples:
  scripts/set_app_mock.sh 192.168.3.162 sports_scoreboard data_littlefs/apps/sports_scoreboard/mock.layout.json --switch
  scripts/set_app_mock.sh 192.168.3.162 sports_scoreboard --disable
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

post_lua_data_with_retry() {
  local payload_file="$1"
  local attempt=1
  local max_attempts=3
  while true; do
    if curl --silent --show-error --fail \
      -H 'Content-Type: application/json' \
      -X POST "${BASE_URL}/api/system/lua-data" \
      --data-binary @"${payload_file}" >/dev/null; then
      return 0
    fi
    if [[ "${attempt}" -ge "${max_attempts}" ]]; then
      echo "error: failed to update lua-data after ${max_attempts} attempts" >&2
      return 1
    fi
    attempt=$((attempt + 1))
    sleep 1
  done
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

if [[ $# -lt 2 || $# -gt 4 ]]; then
  usage
  exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
  echo "error: python3 not found" >&2
  exit 1
fi

if [[ $# -ge 3 ]] && looks_like_device "$1"; then
  DEVICE="$1"
  shift
else
  DEVICE="$(read_default_device)"
fi

APP_ID="$1"
THIRD="${2:-}"
FOURTH="${3:-}"
BASE_URL="http://${DEVICE}"

if [[ ! "${APP_ID}" =~ ^[A-Za-z0-9_-]+$ ]]; then
  echo "error: invalid app_id '${APP_ID}'" >&2
  exit 1
fi

echo "==> Checking device API: ${BASE_URL}/api/apps/ping"
curl --silent --show-error --fail "${BASE_URL}/api/apps/ping" >/dev/null

if [[ "${THIRD}" == "--disable" ]]; then
  echo "==> Disable mock for ${APP_ID}"
  PAYLOAD_FILE="$(mktemp /tmp/set_app_mock_disable_XXXXXX.json)"
  trap 'rm -f "${PAYLOAD_FILE}"' EXIT
  python3 - "${APP_ID}" >"${PAYLOAD_FILE}" <<'PY'
import json
import sys

app_id = sys.argv[1]
payload = {
    "items": {
        f"mock.{app_id}.enabled": False,
        f"mock.{app_id}.data": None,
    }
}
print(json.dumps(payload, ensure_ascii=False))
PY
  post_lua_data_with_retry "${PAYLOAD_FILE}"
  echo "done: mock disabled for ${APP_ID}"
  exit 0
fi

MOCK_JSON="${THIRD}"
SWITCH_AFTER=0
if [[ "${FOURTH}" == "--switch" ]]; then
  SWITCH_AFTER=1
elif [[ -n "${FOURTH}" ]]; then
  echo "error: unknown option '${FOURTH}'" >&2
  exit 1
fi

if [[ ! -f "${MOCK_JSON}" ]]; then
  echo "error: mock json not found: ${MOCK_JSON}" >&2
  exit 1
fi

echo "==> Enable mock for ${APP_ID} from ${MOCK_JSON}"
PAYLOAD_FILE="$(mktemp /tmp/set_app_mock_enable_XXXXXX.json)"
trap 'rm -f "${PAYLOAD_FILE}"' EXIT
python3 - "${APP_ID}" "${MOCK_JSON}" >"${PAYLOAD_FILE}" <<'PY'
import json
import sys
from pathlib import Path

app_id = sys.argv[1]
mock_path = Path(sys.argv[2])
mock_data = json.loads(mock_path.read_text(encoding="utf-8"))
payload = {
    "items": {
        f"mock.{app_id}.enabled": True,
        f"mock.{app_id}.data": mock_data,
    }
}
print(json.dumps(payload, ensure_ascii=False))
PY
post_lua_data_with_retry "${PAYLOAD_FILE}"

if [[ "${SWITCH_AFTER}" == "1" ]]; then
  echo "==> Switch to ${APP_ID}"
  curl --silent --show-error --fail -X POST "${BASE_URL}/api/apps/switch/${APP_ID}" >/dev/null
fi

echo "done: mock enabled for ${APP_ID}"
