#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Set/Get device store channel (stable|beta)
Usage:
  scripts/S_channel.sh [device_ip[:port]] get
  scripts/S_channel.sh [device_ip[:port]] set <stable|beta>
Examples:
  scripts/S_channel.sh get
  scripts/S_channel.sh 192.168.1.88 set beta
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

if [[ $# -lt 1 ]]; then
  usage
  exit 1
fi

ACTION="$1"
shift
BASE="http://${DEVICE}"

case "${ACTION}" in
  get)
    curl --silent --show-error --fail --noproxy "*" "${BASE}/api/store/channel"
    echo
    ;;
  set)
    CH="${1:-}"
    if [[ "${CH}" != "stable" && "${CH}" != "beta" ]]; then
      echo "error: channel must be stable or beta" >&2
      exit 1
    fi
    curl --silent --show-error --fail --noproxy "*" \
      -H 'Content-Type: application/json' \
      -X POST \
      -d "{\"channel\":\"${CH}\"}" \
      "${BASE}/api/store/channel"
    echo
    ;;
  *)
    usage
    exit 1
    ;;
esac
