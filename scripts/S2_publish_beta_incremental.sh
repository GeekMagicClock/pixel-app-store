#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
S2-INC: One-click incremental publish changed apps to beta (safe)
Usage:
  scripts/S2_publish_beta_incremental.sh [--since-ref <git_ref>] [--push] [--allow-no-bump] [--no-ota-index] [--github-worktree <path>] [--dry-run]

Examples:
  scripts/S2_publish_beta_incremental.sh
  scripts/S2_publish_beta_incremental.sh --since-ref origin/main --push
  scripts/S2_publish_beta_incremental.sh --since-ref HEAD~1 --dry-run
USAGE
}

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BOARD="${PIXEL_STORE_BOARD:-pixel64x32V2}"
SINCE_REF="${PIXEL_STORE_INCREMENTAL_SINCE_REF:-origin/main}"
DO_PUSH=0
ALLOW_NO_BUMP=0
UPLOAD_OTA_INDEX=1
DRY_RUN=0
GITHUB_WORKTREE=""

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

while [[ $# -gt 0 ]]; do
  case "$1" in
    --since-ref)
      SINCE_REF="${2:-}"
      shift 2
      ;;
    --push)
      DO_PUSH=1
      shift
      ;;
    --allow-no-bump)
      ALLOW_NO_BUMP=1
      shift
      ;;
    --no-ota-index)
      UPLOAD_OTA_INDEX=0
      shift
      ;;
    --github-worktree)
      GITHUB_WORKTREE="${2:-}"
      shift 2
      ;;
    --dry-run)
      DRY_RUN=1
      shift
      ;;
    *)
      echo "error: unknown option '$1'" >&2
      usage
      exit 1
      ;;
  esac
done

cd "${ROOT}"

if ! git rev-parse --verify "${SINCE_REF}^{commit}" >/dev/null 2>&1; then
  echo "error: invalid --since-ref: ${SINCE_REF}" >&2
  exit 1
fi

CHANGED_APPS=()
while IFS= read -r app; do
  [[ -z "${app}" ]] && continue
  CHANGED_APPS+=("${app}")
done < <(
  git diff --name-only "${SINCE_REF}"...HEAD -- apps \
    | awk -F/ 'NF>=2{print $2}' \
    | sort -u \
    | while read -r app; do
        [[ -z "${app}" ]] && continue
        [[ -f "apps/${app}/manifest.json" && -f "apps/${app}/main.lua" ]] || continue
        printf '%s\n' "${app}"
      done
)

if [[ "${#CHANGED_APPS[@]}" -eq 0 ]]; then
  echo "No changed apps found since ${SINCE_REF}. Nothing to publish."
  exit 0
fi

echo "Changed apps since ${SINCE_REF}:"
printf '  - %s\n' "${CHANGED_APPS[@]}"

if [[ "${DRY_RUN}" == "1" ]]; then
  echo "Dry-run only. No publish executed."
  exit 0
fi

CMD=("${ROOT}/scripts/S2_publish_beta.sh")
if [[ -n "${GITHUB_WORKTREE}" ]]; then
  CMD+=(--github-worktree "${GITHUB_WORKTREE}")
fi
if [[ "${DO_PUSH}" == "1" ]]; then
  CMD+=(--push)
fi
if [[ "${ALLOW_NO_BUMP}" == "1" ]]; then
  CMD+=(--allow-no-bump)
fi
if [[ "${UPLOAD_OTA_INDEX}" == "0" ]]; then
  CMD+=(--no-ota-index)
fi
CMD+=("${CHANGED_APPS[@]}")

echo "==> Run incremental beta publish"
"${CMD[@]}"

INDEX_PATH="${ROOT}/dist/store/${BOARD}/beta/apps-index.json"
if [[ ! -f "${INDEX_PATH}" ]]; then
  echo "error: missing generated index: ${INDEX_PATH}" >&2
  exit 1
fi

python3 - "${INDEX_PATH}" <<'PY'
import json
import sys
from pathlib import Path

idx = Path(sys.argv[1])
payload = json.loads(idx.read_text(encoding='utf-8'))
apps = payload.get('apps') or []
for app in apps:
    aid = str(app.get('id') or 'unknown')
    z = str(app.get('zip_url') or '')
    t = str(app.get('thumbnail_url') or '')
    if not z.startswith('https://raw.githubusercontent.com/'):
        raise SystemExit(f"policy check failed: {aid} zip_url must be GitHub raw URL: {z}")
    if not t.startswith('https://raw.githubusercontent.com/'):
        raise SystemExit(f"policy check failed: {aid} thumbnail_url must be GitHub raw URL: {t}")
print(f"policy check ok: validated {len(apps)} app index entries")
PY

echo "Done. Incremental beta publish completed safely."
