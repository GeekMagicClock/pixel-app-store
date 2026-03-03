#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  scripts/uninstall_app.sh <device_ip[:port]> <app_id> [--no-reload]

Examples:
  scripts/uninstall_app.sh 192.168.3.140 weather_card_owm
  scripts/uninstall_app.sh 192.168.1.88 moon_phase_png --no-reload
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

if [[ $# -lt 2 || $# -gt 3 ]]; then
  usage
  exit 1
fi

if ! command -v curl >/dev/null 2>&1; then
  echo "error: curl not found" >&2
  exit 1
fi

DEVICE="$1"
APP_ID="$2"
OPT="${3:-}"
BASE_URL="http://${DEVICE}"

if [[ ! "${APP_ID}" =~ ^[A-Za-z0-9_-]+$ ]]; then
  echo "error: invalid app_id '${APP_ID}' (allowed: A-Z a-z 0-9 _ -)" >&2
  exit 1
fi

if [[ -n "${OPT}" && "${OPT}" != "--no-reload" ]]; then
  echo "error: unknown option '${OPT}', only --no-reload is supported" >&2
  exit 1
fi

echo "==> ping ${BASE_URL}/api/apps/ping"
curl --silent --show-error --fail "${BASE_URL}/api/apps/ping" >/dev/null

echo "==> uninstall ${APP_ID}"
set +e
UNINSTALL_OUT="$(curl --silent --show-error -w $'\nHTTP_STATUS:%{http_code}' -X DELETE \
  "${BASE_URL}/api/apps/${APP_ID}" 2>&1)"
RC=$?
set -e

if [[ ${RC} -ne 0 ]]; then
  echo "error: uninstall request failed"
  echo "${UNINSTALL_OUT}" >&2
  exit 1
fi

HTTP_STATUS="${UNINSTALL_OUT##*HTTP_STATUS:}"
BODY="${UNINSTALL_OUT%$'\n'HTTP_STATUS:*}"

if [[ "${HTTP_STATUS}" != "200" ]]; then
  echo "error: uninstall failed (HTTP ${HTTP_STATUS})"
  echo "response: ${BODY}" >&2
  if [[ "${HTTP_STATUS}" == "404" || "${HTTP_STATUS}" == "400" ]]; then
    echo "hint: firmware may not include DELETE /api/apps/<app_id> yet" >&2
  fi
  exit 1
fi

echo "==> uninstalled ${APP_ID}"

if [[ "${OPT}" != "--no-reload" ]]; then
  echo "==> reload app carousel"
  curl --silent --show-error --fail -X POST "${BASE_URL}/api/apps/reload" >/dev/null
fi

echo "done: uninstalled ${APP_ID} on ${DEVICE}"
