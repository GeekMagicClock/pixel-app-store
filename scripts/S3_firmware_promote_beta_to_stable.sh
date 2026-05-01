#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
S3: Promote firmware beta -> stable without recompiling
Usage:
  scripts/S3_firmware_promote_beta_to_stable.sh [--github-worktree <path>] [--push]
USAGE
}

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BOARD="${PIXEL_STORE_BOARD:-pixel64x32V2}"
GITHUB_WORKTREE="${PIXEL_STORE_GITHUB_WORKTREE:-}"
DO_PUSH=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --github-worktree) GITHUB_WORKTREE="${2:-}"; shift 2 ;;
    --push) DO_PUSH=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown arg: $1" >&2; usage; exit 1 ;;
  esac
done

if [[ -z "${GITHUB_WORKTREE}" ]]; then
  for c in "${ROOT}/../pixel-app-store" "${HOME}/develop/project/pixel-app-store" "${HOME}/pixel-app-store"; do
    if [[ -d "${c}/.git" ]]; then GITHUB_WORKTREE="${c}"; break; fi
  done
fi
if [[ -z "${GITHUB_WORKTREE}" || ! -d "${GITHUB_WORKTREE}/.git" ]]; then
  echo "error: github worktree not found" >&2
  exit 1
fi

BETA_MANIFEST_URL="https://ota.geekmagic.cc/firmware/${BOARD}/beta/firmware.json"
TMP_JSON="$(mktemp)"
trap 'rm -f "${TMP_JSON}"' EXIT
curl --silent --show-error --fail "${BETA_MANIFEST_URL}" > "${TMP_JSON}"

FW_URL="$(python3 - "${TMP_JSON}" <<'PY'
import json,sys
obj=json.load(open(sys.argv[1],encoding='utf-8'))
print(obj.get('url',''))
PY
)"
VERSION="$(python3 - "${TMP_JSON}" <<'PY'
import json,sys
obj=json.load(open(sys.argv[1],encoding='utf-8'))
print(obj.get('version',''))
PY
)"

if [[ -z "${FW_URL}" || -z "${VERSION}" ]]; then
  echo "error: invalid beta firmware manifest" >&2
  exit 1
fi
FW_NAME="${FW_URL##*/}"
SRC_BIN="${GITHUB_WORKTREE}/firmware/beta/${FW_NAME}"
DST_BIN="${GITHUB_WORKTREE}/firmware/stable/${FW_NAME}"
if [[ ! -f "${SRC_BIN}" ]]; then
  echo "error: missing beta firmware bin in github worktree: ${SRC_BIN}" >&2
  exit 1
fi
mkdir -p "${GITHUB_WORKTREE}/firmware/stable"
cp -f "${SRC_BIN}" "${DST_BIN}"

STABLE_URL="https://raw.githubusercontent.com/GeekMagicClock/pixel-app-store/main/firmware/stable/${FW_NAME}"
SIZE_BYTES="$(wc -c < "${DST_BIN}" | tr -d ' ')"
SHA256="$(shasum -a 256 "${DST_BIN}" | awk '{print $1}')"
DIST_DIR="${ROOT}/dist/firmware/${BOARD}/stable"
MANIFEST_PATH="${DIST_DIR}/firmware.json"
mkdir -p "${DIST_DIR}"
python3 - "${MANIFEST_PATH}" "${VERSION}" "${STABLE_URL}" "${SIZE_BYTES}" "${SHA256}" "${BOARD}" <<'PY'
import json, sys, time
p, v, u, size, sha, board = sys.argv[1:]
obj = {
  "ok": True,
  "board": board,
  "channel": "stable",
  "version": v,
  "url": u,
  "size": int(size),
  "sha256": sha,
  "generated_at": int(time.time()),
}
with open(p, "w", encoding="utf-8") as f:
  json.dump(obj, f, ensure_ascii=False, indent=2)
  f.write("\n")
print(f"wrote {p}")
PY

REMOTE="${PIXEL_FIRMWARE_REMOTE_ROOT:-root@111.229.177.3:/www/wwwroot/ota.geekmagic.cc/firmware}/${BOARD}/stable"
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

echo "==> Upload stable firmware manifest to OTA"
"${SSH_ARGS[@]}" "${REMOTE_LOGIN}" "mkdir -p '${REMOTE_PATH}'"
"${SCP_ARGS[@]}" "${MANIFEST_PATH}" "${REMOTE_LOGIN}:${REMOTE_PATH}/firmware.json"

if [[ "${DO_PUSH}" == "1" ]]; then
  pushd "${GITHUB_WORKTREE}" >/dev/null
  git add "firmware/stable/${FW_NAME}"
  if git diff --cached --quiet; then
    echo "No github firmware changes to commit."
  else
    git commit -m "Promote firmware ${VERSION} beta -> stable [skip ci]"
    git push
  fi
  popd >/dev/null
else
  echo "github stable firmware copied; pass --push to commit and push"
fi

echo "done"
