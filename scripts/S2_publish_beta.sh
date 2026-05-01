#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
S2: Publish beta (zip/png to GitHub, index(.gz) to OTA)
Usage:
  scripts/S2_publish_beta.sh [app_id ...] [--github-worktree <path>] [--push] [--no-ota-index] [--allow-no-bump]
Examples:
  scripts/S2_publish_beta.sh campfire_scene
  scripts/S2_publish_beta.sh campfire_scene moon_phase_png --push
USAGE
}

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BOARD="${PIXEL_STORE_BOARD:-pixel64x32V2}"
GITHUB_WORKTREE="${PIXEL_STORE_GITHUB_WORKTREE:-}"
DO_PUSH=0
UPLOAD_OTA_INDEX=1
ALLOW_NO_BUMP=0

detect_github_worktree() {
  local candidates=(
    "${GITHUB_WORKTREE}"
    "${ROOT}/../pixel-app-store"
    "${HOME}/develop/project/pixel-app-store"
    "${HOME}/pixel-app-store"
  )
  local c
  for c in "${candidates[@]}"; do
    if [[ -n "${c}" && -d "${c}/.git" ]]; then
      printf '%s\n' "${c}"
      return 0
    fi
  done
  return 1
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

APPS=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --github-worktree)
      GITHUB_WORKTREE="${2:-}"
      shift 2
      ;;
    --push)
      DO_PUSH=1
      shift
      ;;
    --no-ota-index)
      UPLOAD_OTA_INDEX=0
      shift
      ;;
    --allow-no-bump)
      ALLOW_NO_BUMP=1
      shift
      ;;
    *)
      APPS+=("$1")
      shift
      ;;
  esac
done

GITHUB_RAW_BASE="https://raw.githubusercontent.com/GeekMagicClock/pixel-app-store/main/dist/store/{board}/{channel}"
OTA_PUBLIC_BASE="https://ota.geekmagic.cc/apps/{board}/{channel}"

echo "==> Build beta artifacts locally (no zip/png upload to OTA)"
CMD=(python3 "${ROOT}/scripts/publish_apps.py"
  --channel beta
  --base-url "${OTA_PUBLIC_BASE}"
  --thumbnail-base-url "${GITHUB_RAW_BASE}"
  --zip-base-url "${GITHUB_RAW_BASE}"
  --zip-url-mode direct
)
if [[ "${ALLOW_NO_BUMP}" == "1" ]]; then
  CMD+=(--allow-no-bump)
fi
CMD+=("${APPS[@]}")
"${CMD[@]}"

if [[ -z "${GITHUB_WORKTREE}" ]]; then
  GITHUB_WORKTREE="$(detect_github_worktree || true)"
fi
if [[ -z "${GITHUB_WORKTREE}" || ! -d "${GITHUB_WORKTREE}/.git" ]]; then
  echo "error: github worktree not found" >&2
  exit 1
fi

echo "==> Sync beta artifacts to github worktree"
mkdir -p "${GITHUB_WORKTREE}/dist/store/${BOARD}/beta"
rsync -a --delete "${ROOT}/dist/store/${BOARD}/beta/" "${GITHUB_WORKTREE}/dist/store/${BOARD}/beta/"

if [[ "${DO_PUSH}" == "1" ]]; then
  pushd "${GITHUB_WORKTREE}" >/dev/null
  if git diff --quiet -- "dist/store/${BOARD}/beta"; then
    echo "No github beta changes to commit."
  else
    git add "dist/store/${BOARD}/beta"
    git commit -m "Update beta app assets [skip ci]"
    git push
  fi
  popd >/dev/null
else
  echo "github worktree synced; pass --push to commit and push"
fi

if [[ "${UPLOAD_OTA_INDEX}" == "1" ]]; then
  REMOTE="${PIXEL_STORE_REMOTE:-root@111.229.177.3:/www/wwwroot/ota.geekmagic.cc/store_data/${BOARD}/beta}"
  SSH_PORT="${PIXEL_STORE_SSH_PORT:-22}"
  IDENTITY="${PIXEL_STORE_SSH_IDENTITY:-$HOME/.ssh/t20260309.pem}"
  REMOTE_LOGIN="${REMOTE%%:*}"
  REMOTE_PATH="${REMOTE#*:}"
  SCP_ARGS=(scp -P "${SSH_PORT}")
  SSH_ARGS=(ssh -p "${SSH_PORT}")
  if [[ -n "${IDENTITY}" ]]; then
    SCP_ARGS+=( -i "${IDENTITY}" )
    SSH_ARGS+=( -i "${IDENTITY}" )
  fi
  echo "==> Upload beta index only to OTA"
  "${SSH_ARGS[@]}" "${REMOTE_LOGIN}" "mkdir -p '${REMOTE_PATH}'"
  "${SCP_ARGS[@]}" \
    "${ROOT}/dist/store/${BOARD}/beta/apps-index.json" \
    "${ROOT}/dist/store/${BOARD}/beta/apps-index.json.gz" \
    "${REMOTE_LOGIN}:${REMOTE_PATH}/"
fi
