#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
S1: Debug push app to device
Usage:
  scripts/S1_debug_push.sh <app_id> [--switch]
  scripts/S1_debug_push.sh <device_ip[:port]> <app_id> [--switch]
USAGE
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

exec bash scripts/push_app.sh "$@"
