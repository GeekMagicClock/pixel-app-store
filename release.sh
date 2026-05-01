#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

OTA_HUB_DIR="$ROOT_DIR/../ota_hub"

if [[ ! -f "$OTA_HUB_DIR/manage_projects.py" ]]; then
  echo "ota_hub not found: $OTA_HUB_DIR/manage_projects.py" >&2
  exit 1
fi

if [[ $# -eq 0 ]]; then
  python3 "$OTA_HUB_DIR/manage_projects.py" release pixel64x32V2 "auto release"
elif [[ "$1" == -* ]]; then
  python3 "$OTA_HUB_DIR/manage_projects.py" release pixel64x32V2 "auto release" "$@"
else
  python3 "$OTA_HUB_DIR/manage_projects.py" release pixel64x32V2 "$@"
fi
