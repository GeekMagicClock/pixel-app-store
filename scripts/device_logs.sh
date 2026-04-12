#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  scripts/device_logs.sh [--scope <app|all|sys>] [--limit <n>] [--after <seq>] [--raw]
  scripts/device_logs.sh <device_ip[:port]> [--scope <app|all|sys>] [--limit <n>] [--after <seq>] [--raw]

Examples:
  scripts/device_logs.sh
  scripts/device_logs.sh --scope app --limit 120
  scripts/device_logs.sh 192.168.3.162 --scope all --after 0
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

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

DEVICE=""
if [[ $# -ge 1 ]] && looks_like_device "$1"; then
  DEVICE="$1"
  shift
else
  DEVICE="$(read_default_device)"
fi

SCOPE="app"
LIMIT="120"
AFTER="0"
RAW=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --scope)
      SCOPE="${2:-}"
      shift 2
      ;;
    --limit)
      LIMIT="${2:-}"
      shift 2
      ;;
    --after)
      AFTER="${2:-}"
      shift 2
      ;;
    --raw)
      RAW=1
      shift
      ;;
    *)
      echo "error: unknown option '$1'" >&2
      usage
      exit 1
      ;;
  esac
done

BASE_URL="http://${DEVICE}"
URL="${BASE_URL}/api/system/logs?scope=${SCOPE}&after=${AFTER}&limit=${LIMIT}"

if [[ "${RAW}" == "1" ]]; then
  curl --silent --show-error --fail --noproxy "*" "${URL}"
  exit 0
fi

if command -v jq >/dev/null 2>&1; then
  curl --silent --show-error --fail --noproxy "*" "${URL}" \
    | jq -r '.logs[]? | "[\(.seq)] \(.text)"'
else
  curl --silent --show-error --fail --noproxy "*" "${URL}"
fi
