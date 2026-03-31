#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  scripts/build_package_install_run_app.sh <device_ip[:port]> <app_id> [--source]

Description:
  Build and package exactly one app, then install it to device and run it.
  Default install mode uses the packaged bytecode bundle (app.bin).

Options:
  --source   Build/package first, but install source files from data_littlefs/apps.

Examples:
  scripts/build_package_install_run_app.sh 192.168.1.88 openmeteo_3day
  scripts/build_package_install_run_app.sh 192.168.1.88 openmeteo_3day --source
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

if ! command -v python3 >/dev/null 2>&1; then
  echo "error: python3 not found" >&2
  exit 1
fi

DEVICE="$1"
APP_ID="$2"
MODE="${3:-}"

if [[ -n "${MODE}" && "${MODE}" != "--source" ]]; then
  echo "error: unknown option '${MODE}', only --source is supported" >&2
  exit 1
fi

if [[ ! "${APP_ID}" =~ ^[A-Za-z0-9_-]+$ ]]; then
  echo "error: invalid app_id '${APP_ID}' (allowed: A-Z a-z 0-9 _ -)" >&2
  exit 1
fi

APP_DIR="data_littlefs/apps/${APP_ID}"
MANIFEST="${APP_DIR}/manifest.json"
if [[ ! -d "${APP_DIR}" ]]; then
  echo "error: app dir not found: ${APP_DIR}" >&2
  exit 1
fi
if [[ ! -f "${MANIFEST}" ]]; then
  echo "error: missing manifest: ${MANIFEST}" >&2
  exit 1
fi

echo "==> Build package for ${APP_ID}"
python3 scripts/publish_apps.py "${APP_ID}"

if [[ "${MODE}" == "--source" ]]; then
  echo "==> Install source app and switch"
  bash scripts/push_app.sh "${DEVICE}" "${APP_ID}" "data_littlefs/apps" --switch
  echo "done: packaged + installed source app '${APP_ID}' on ${DEVICE}"
  exit 0
fi

APP_VERSION="$(
python3 - "${MANIFEST}" <<'PY'
import json
import sys
from pathlib import Path

manifest = Path(sys.argv[1])
obj = json.loads(manifest.read_text(encoding="utf-8"))
version = str(obj.get("version", "")).strip()
if not version:
    raise SystemExit("manifest version is required")
print(version)
PY
)"

ZIP_PATH="dist/store/pixel64x32V2/apps/${APP_ID}-${APP_VERSION}.zip"
if [[ ! -f "${ZIP_PATH}" ]]; then
  echo "error: packaged zip not found: ${ZIP_PATH}" >&2
  exit 1
fi

TMP_DIR="$(mktemp -d "/tmp/${APP_ID}_pkg_XXXXXX")"
cleanup() {
  rm -rf "${TMP_DIR}"
}
trap cleanup EXIT

TMP_APPS_ROOT="${TMP_DIR}/apps"
TMP_APP_DIR="${TMP_APPS_ROOT}/${APP_ID}"
mkdir -p "${TMP_APP_DIR}"

echo "==> Extract package: ${ZIP_PATH}"
if command -v unzip >/dev/null 2>&1; then
  unzip -q "${ZIP_PATH}" -d "${TMP_APP_DIR}"
elif command -v bsdtar >/dev/null 2>&1; then
  bsdtar -xf "${ZIP_PATH}" -C "${TMP_APP_DIR}"
else
  echo "error: neither unzip nor bsdtar found, cannot extract ${ZIP_PATH}" >&2
  exit 1
fi

if [[ ! -f "${TMP_APP_DIR}/manifest.json" ]]; then
  echo "error: invalid package, manifest.json missing in ${ZIP_PATH}" >&2
  exit 1
fi

echo "==> Install packaged app and switch"
bash scripts/push_app.sh "${DEVICE}" "${APP_ID}" "${TMP_APPS_ROOT}" --switch

echo "done: built + packaged + installed + switched '${APP_ID}' on ${DEVICE}"
echo "package: ${ZIP_PATH}"
