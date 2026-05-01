#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
S1: Debug OTA firmware to device
Usage:
  scripts/S1_firmware_debug_ota.sh <firmware.bin>
  scripts/S1_firmware_debug_ota.sh <device_ip[:port]> <firmware.bin>
Examples:
  scripts/S1_firmware_debug_ota.sh dist/firmware/pixel64x32V2/beta/firmware-v1.2.3.bin
  scripts/S1_firmware_debug_ota.sh 192.168.3.162 dist/firmware/pixel64x32V2/beta/firmware-v1.2.3.bin
USAGE
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

BASE="${PIXEL_DEVICE_BASE:-http://192.168.3.162}"
BIN=""

if [[ $# -ge 2 && "$1" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+(:[0-9]+)?$ ]]; then
  DEV="$1"
  shift
  BASE="http://${DEV}"
fi

BIN="${1:-}"
if [[ -z "${BIN}" || ! -f "${BIN}" ]]; then
  echo "error: firmware bin not found: ${BIN}" >&2
  usage
  exit 1
fi

echo "==> Upload firmware: ${BIN}"
curl --silent --show-error --fail --noproxy "*" \
  -X POST \
  -H "Content-Type: application/octet-stream" \
  --data-binary "@${BIN}" \
  "${BASE}/api/firmware/ota"

echo
echo "done"
