#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  scripts/switch_app.sh <device_ip[:port]> <app_id>

Examples:
  scripts/switch_app.sh 192.168.3.140 weather_card_owm
  scripts/switch_app.sh 192.168.1.88 moon_phase_png
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

if [[ $# -ne 2 ]]; then
  usage
  exit 1
fi

DEVICE="$1"
APP_ID="$2"
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
