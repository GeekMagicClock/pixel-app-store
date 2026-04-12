#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  scripts/watch_and_push_app.sh <app_id> [apps_root] [--no-switch]
  scripts/watch_and_push_app.sh <device_ip[:port]> <app_id> [apps_root] [--no-switch]

Description:
  Watch app files and auto-push to device on every change.
  Device IP defaults to device_ip.txt when omitted.
USAGE
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

fingerprint_dir() {
  local dir="$1"
  find "${dir}" -type f ! -name '.*' -print0 \
    | sort -z \
    | xargs -0 shasum \
    | shasum \
    | awk '{print $1}'
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

if [[ $# -lt 1 || $# -gt 4 ]]; then
  usage
  exit 1
fi

DEVICE=""
if [[ $# -ge 2 ]] && looks_like_device "$1"; then
  DEVICE="$1"
  shift
else
  DEVICE="$(read_default_device)"
fi

APP_ID="$1"
shift
APPS_ROOT="data_littlefs/apps"
SWITCH_FLAG="--switch"

if [[ $# -ge 1 ]]; then
  if [[ "$1" == "--no-switch" ]]; then
    SWITCH_FLAG=""
    shift
  else
    APPS_ROOT="$1"
    shift
  fi
fi

if [[ $# -ge 1 ]]; then
  if [[ "$1" == "--no-switch" ]]; then
    SWITCH_FLAG=""
    shift
  else
    echo "error: unknown option '$1'" >&2
    usage
    exit 1
  fi
fi

if [[ $# -ne 0 ]]; then
  usage
  exit 1
fi

APP_DIR="${APPS_ROOT}/${APP_ID}"
if [[ ! -d "${APP_DIR}" ]]; then
  echo "error: app dir not found: ${APP_DIR}" >&2
  exit 1
fi

push_once() {
  if [[ -n "${SWITCH_FLAG}" ]]; then
    bash scripts/push_app.sh "${DEVICE}" "${APP_ID}" "${APPS_ROOT}" --switch
  else
    bash scripts/push_app.sh "${DEVICE}" "${APP_ID}" "${APPS_ROOT}"
  fi
}

echo "==> watch ${APP_DIR} -> ${DEVICE}"
echo "==> initial push"
push_once

last_fp="$(fingerprint_dir "${APP_DIR}")"
while true; do
  sleep 1
  cur_fp="$(fingerprint_dir "${APP_DIR}")"
  if [[ "${cur_fp}" != "${last_fp}" ]]; then
    echo "==> change detected, pushing ${APP_ID}"
    push_once
    last_fp="${cur_fp}"
  fi
done
