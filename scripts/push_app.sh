#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  scripts/push_app.sh <device_ip[:port]> <app_id> [apps_root] [--switch]

Examples:
  scripts/push_app.sh 192.168.1.88 binance_ticker
  scripts/push_app.sh 192.168.1.88:80 weather_owm data_littlefs/apps
  scripts/push_app.sh 192.168.1.88 sunrise_sunset_owm data_littlefs/apps --switch
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

if [[ $# -lt 2 || $# -gt 4 ]]; then
  usage
  exit 1
fi

if ! command -v curl >/dev/null 2>&1; then
  echo "error: curl not found" >&2
  exit 1
fi

DEVICE="$1"
APP_ID="$2"
APPS_ROOT="${3:-data_littlefs/apps}"
SWITCH_AFTER_PUSH="${4:-}"
APP_DIR="${APPS_ROOT}/${APP_ID}"
BASE_URL="http://${DEVICE}"

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
curl --silent --show-error --fail "${BASE_URL}/api/apps/ping" >/dev/null

uploaded=0
while IFS= read -r -d '' file_path; do
  rel="${file_path#${APP_DIR}/}"
  if [[ -z "${rel}" ]]; then
    continue
  fi
  if [[ "${rel}" == .* ]]; then
    continue
  fi
  echo "==> Uploading ${file_path}"
  curl --silent --show-error --fail \
    -X PUT \
    --data-binary @"${file_path}" \
    "${BASE_URL}/api/apps/${APP_ID}/${rel}" >/dev/null
  uploaded=$((uploaded + 1))
done < <(find "${APP_DIR}" -type f -print0 | sort -z)

if [[ ${uploaded} -eq 0 ]]; then
  echo "error: no files found in ${APP_DIR}" >&2
  exit 1
fi

echo "==> Reloading app carousel"
curl --silent --show-error --fail -X POST "${BASE_URL}/api/apps/reload" >/dev/null

if [[ "${SWITCH_AFTER_PUSH}" == "--switch" ]]; then
  echo "==> Switching to ${APP_ID}"
  curl --silent --show-error --fail -X POST "${BASE_URL}/api/apps/switch/${APP_ID}" >/dev/null
fi

echo "done: pushed ${APP_ID} (${uploaded} file(s)) to ${DEVICE}"
