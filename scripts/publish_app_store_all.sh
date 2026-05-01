#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  scripts/publish_app_store_all.sh [--ota-only] [--github-worktree <path>] [--push]

Description:
  Build stable/beta app store assets once, sync zip/png/index to GitHub worktree,
  and upload only apps-index.json(.gz) to ota.geekmagic.cc.

Examples:
  scripts/publish_app_store_all.sh --github-worktree ../pixel-app-store --push
  scripts/publish_app_store_all.sh --ota-only
EOF
}

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BOARD="${PIXEL_STORE_BOARD:-pixel64x32V2}"
GITHUB_WORKTREE="${PIXEL_STORE_GITHUB_WORKTREE:-}"
SSH_IDENTITY="${PIXEL_STORE_SSH_IDENTITY:-$HOME/.ssh/t20260309.pem}"
DO_PUSH=0
OTA_ONLY=0

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
    --ota-only)
      OTA_ONLY=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "error: unknown option '$1'" >&2
      usage
      exit 1
      ;;
  esac
done

GITHUB_RAW_BASE="https://raw.githubusercontent.com/GeekMagicClock/pixel-app-store/main/dist/store/{board}/{channel}"
OTA_PUBLIC_BASE="https://ota.geekmagic.cc/apps/{board}/{channel}"

echo "==> Building stable store (local)"
python3 "${ROOT}/scripts/publish_apps.py" \
  --channel stable \
  --base-url "${OTA_PUBLIC_BASE}" \
  --thumbnail-base-url "${GITHUB_RAW_BASE}" \
  --zip-base-url "${GITHUB_RAW_BASE}" \
  --zip-url-mode direct \
  --full-index

echo "==> Building beta store (local)"
python3 "${ROOT}/scripts/publish_apps.py" \
  --channel beta \
  --base-url "${OTA_PUBLIC_BASE}" \
  --thumbnail-base-url "${GITHUB_RAW_BASE}" \
  --zip-base-url "${GITHUB_RAW_BASE}" \
  --zip-url-mode direct \
  --full-index

if [[ "${OTA_ONLY}" == "1" ]]; then
  REMOTE_ROOT="${PIXEL_STORE_REMOTE_ROOT:-root@111.229.177.3:/www/wwwroot/ota.geekmagic.cc/store_data/${BOARD}}"
  SSH_PORT="${PIXEL_STORE_SSH_PORT:-22}"
  REMOTE_LOGIN="${REMOTE_ROOT%%:*}"
  REMOTE_PATH="${REMOTE_ROOT#*:}"
  SCP_ARGS=(scp -P "${SSH_PORT}")
  SSH_ARGS=(ssh -p "${SSH_PORT}")
  if [[ -n "${SSH_IDENTITY}" ]]; then
    SCP_ARGS+=( -i "${SSH_IDENTITY}" )
    SSH_ARGS+=( -i "${SSH_IDENTITY}" )
  fi
  for CH in stable beta; do
    "${SSH_ARGS[@]}" "${REMOTE_LOGIN}" "mkdir -p '${REMOTE_PATH}/${CH}'"
    "${SCP_ARGS[@]}" \
      "${ROOT}/dist/store/${BOARD}/${CH}/apps-index.json" \
      "${ROOT}/dist/store/${BOARD}/${CH}/apps-index.json.gz" \
      "${REMOTE_LOGIN}:${REMOTE_PATH}/${CH}/"
  done
  exit 0
fi

if [[ -z "${GITHUB_WORKTREE}" ]]; then
  GITHUB_WORKTREE="$(detect_github_worktree || true)"
fi

if [[ -z "${GITHUB_WORKTREE}" ]]; then
  echo "warning: no github worktree found, skipping git sync" >&2
  exit 0
fi

if [[ ! -d "${GITHUB_WORKTREE}/.git" ]]; then
  echo "error: github worktree not found: ${GITHUB_WORKTREE}" >&2
  exit 1
fi

echo "==> Syncing dist/store to github worktree"
rsync -a --delete "${ROOT}/dist/store/" "${GITHUB_WORKTREE}/dist/store/"

if [[ "${DO_PUSH}" == "1" ]]; then
  pushd "${GITHUB_WORKTREE}" >/dev/null
  if git diff --quiet -- dist/store; then
    echo "No github changes to commit."
  else
    git add dist/store
    git commit -m "Update app store assets [skip ci]"
    git push
  fi
  popd >/dev/null
else
  echo "github worktree synced; pass --push to commit and push"
fi

REMOTE_ROOT="${PIXEL_STORE_REMOTE_ROOT:-root@111.229.177.3:/www/wwwroot/ota.geekmagic.cc/store_data/${BOARD}}"
SSH_PORT="${PIXEL_STORE_SSH_PORT:-22}"
REMOTE_LOGIN="${REMOTE_ROOT%%:*}"
REMOTE_PATH="${REMOTE_ROOT#*:}"
SCP_ARGS=(scp -P "${SSH_PORT}")
SSH_ARGS=(ssh -p "${SSH_PORT}")
if [[ -n "${SSH_IDENTITY}" ]]; then
  SCP_ARGS+=( -i "${SSH_IDENTITY}" )
  SSH_ARGS+=( -i "${SSH_IDENTITY}" )
fi
echo "==> Uploading stable/beta index only to OTA"
for CH in stable beta; do
  "${SSH_ARGS[@]}" "${REMOTE_LOGIN}" "mkdir -p '${REMOTE_PATH}/${CH}'"
  "${SCP_ARGS[@]}" \
    "${ROOT}/dist/store/${BOARD}/${CH}/apps-index.json" \
    "${ROOT}/dist/store/${BOARD}/${CH}/apps-index.json.gz" \
    "${REMOTE_LOGIN}:${REMOTE_PATH}/${CH}/"
done
