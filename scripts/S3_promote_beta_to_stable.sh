#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
S3: Promote beta -> stable without recompiling
Usage:
  scripts/S3_promote_beta_to_stable.sh [app_id ...] [--upload]
Examples:
  scripts/S3_promote_beta_to_stable.sh campfire_scene
  scripts/S3_promote_beta_to_stable.sh --upload
USAGE
}

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BOARD="${PIXEL_STORE_BOARD:-pixel64x32V2}"
DIST_ROOT="${PIXEL_STORE_DIST_ROOT:-${ROOT}/dist/store}"
BETA_DIR="${DIST_ROOT}/${BOARD}/beta"
STABLE_DIR="${DIST_ROOT}/${BOARD}/stable"
DO_UPLOAD=0
APPS=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --upload)
      DO_UPLOAD=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      APPS+=("$1")
      shift
      ;;
  esac
done

if [[ ! -d "${BETA_DIR}" ]]; then
  echo "error: beta dir not found: ${BETA_DIR}" >&2
  exit 1
fi
mkdir -p "${STABLE_DIR}"

if [[ ${#APPS[@]} -eq 0 ]]; then
  echo "==> Promote full beta channel to stable (no compile)"
  rsync -a --delete "${BETA_DIR}/" "${STABLE_DIR}/"
else
  echo "==> Promote selected app(s): ${APPS[*]}"
  python3 - "${BETA_DIR}" "${STABLE_DIR}" "${APPS[@]}" <<'PY'
import json
import shutil
import sys
from pathlib import Path

beta = Path(sys.argv[1])
stable = Path(sys.argv[2])
apps = [a.strip() for a in sys.argv[3:] if a.strip()]

beta_idx = beta / "apps-index.json"
stable_idx = stable / "apps-index.json"
if not beta_idx.exists():
    raise SystemExit(f"missing beta index: {beta_idx}")
if not stable_idx.exists():
    raise SystemExit(f"missing stable index: {stable_idx}")

beta_payload = json.loads(beta_idx.read_text(encoding="utf-8"))
stable_payload = json.loads(stable_idx.read_text(encoding="utf-8"))
beta_apps = beta_payload.get("apps") or []
stable_apps = stable_payload.get("apps") or []

beta_map = {str(x.get("id") or ""): x for x in beta_apps}
stable_map = {str(x.get("id") or ""): x for x in stable_apps}

for app_id in apps:
    if app_id not in beta_map:
        raise SystemExit(f"{app_id}: not found in beta index")
    ent = beta_map[app_id]
    zip_url = str(ent.get("zip_url") or "")
    zip_name = zip_url.rsplit("/", 1)[-1]
    if not zip_name:
        raise SystemExit(f"{app_id}: invalid zip_url")
    src_zip = beta / zip_name
    if not src_zip.exists():
        raise SystemExit(f"{app_id}: missing beta zip {src_zip}")
    dst_zip = stable / zip_name
    shutil.copy2(src_zip, dst_zip)
    thumb_url = str(ent.get("thumbnail_url") or "")
    thumb_name = thumb_url.split("?", 1)[0].rsplit("/", 1)[-1]
    if thumb_name:
        src_thumb = beta / "thumbs" / thumb_name
        dst_thumb = stable / "thumbs" / thumb_name
        if src_thumb.exists():
            dst_thumb.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src_thumb, dst_thumb)

    promoted = dict(ent)
    if isinstance(promoted.get("zip_url"), str):
        promoted["zip_url"] = promoted["zip_url"].replace("/beta/", "/stable/")
    if isinstance(promoted.get("thumbnail_url"), str):
        promoted["thumbnail_url"] = promoted["thumbnail_url"].replace("/beta/", "/stable/")
    stable_map[app_id] = promoted

merged = list(stable_map.values())
merged.sort(key=lambda e: (str(e.get("category") or ""), str(e.get("name") or "").lower(), str(e.get("id") or "")))
stable_payload["apps"] = merged
if "generated_at" in beta_payload:
    stable_payload["generated_at"] = beta_payload["generated_at"]
stable_payload["channel"] = "stable"

stable_idx.write_text(json.dumps(stable_payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print(f"updated stable index: {stable_idx}")
PY

  python3 - "${STABLE_DIR}/apps-index.json" "${STABLE_DIR}/apps-index.json.gz" <<'PY'
import gzip
import sys
from pathlib import Path

src = Path(sys.argv[1])
dst = Path(sys.argv[2])
with dst.open("wb") as raw:
    with gzip.GzipFile(filename="", mode="wb", fileobj=raw, compresslevel=9, mtime=0) as gz:
        gz.write(src.read_bytes())
print(f"wrote {dst}")
PY
fi

if [[ "${DO_UPLOAD}" == "1" ]]; then
  echo "==> Upload stable index only to OTA"
  REMOTE="${PIXEL_STORE_REMOTE:-root@111.229.177.3:/www/wwwroot/ota.geekmagic.cc/store_data/pixel64x32V2/stable}"
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
  "${SSH_ARGS[@]}" "${REMOTE_LOGIN}" "mkdir -p '${REMOTE_PATH}'"
  "${SCP_ARGS[@]}" \
    "${STABLE_DIR}/apps-index.json" \
    "${STABLE_DIR}/apps-index.json.gz" \
    "${REMOTE_LOGIN}:${REMOTE_PATH}/"
  echo "uploaded stable index files to ${REMOTE}"
fi

echo "done: S3 promotion completed"
