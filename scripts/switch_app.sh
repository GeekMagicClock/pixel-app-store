#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  scripts/switch_app.sh <app_id>
  scripts/switch_app.sh <device_ip[:port]> <app_id>

Examples:
  scripts/switch_app.sh weather_card_owm
  scripts/switch_app.sh 192.168.3.140 weather_card_owm
  scripts/switch_app.sh 192.168.1.88 moon_phase_png
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

if [[ $# -lt 1 || $# -gt 2 ]]; then
  usage
  exit 1
fi

if [[ $# -eq 2 ]] && looks_like_device "$1"; then
  DEVICE="$1"
  APP_ID="$2"
else
  DEVICE="$(read_default_device)"
  APP_ID="$1"
fi
BASE_URL="http://${DEVICE}"

if [[ ! "${APP_ID}" =~ ^[A-Za-z0-9_-]+$ ]]; then
  echo "error: invalid app_id '${APP_ID}' (allowed: A-Z a-z 0-9 _ -)" >&2
  exit 1
fi

echo "==> ping ${BASE_URL}/api/apps/ping"
curl --silent --show-error --fail "${BASE_URL}/api/apps/ping" >/dev/null

echo "==> switch ${APP_ID}"
curl --silent --show-error --fail -X POST \
  "${BASE_URL}/api/apps/switch?app_id=${APP_ID}" >/dev/null

echo "done: switched to ${APP_ID} on ${DEVICE}"
