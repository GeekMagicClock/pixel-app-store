#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
S2: Publish firmware to channel (bin -> GitHub, manifest -> OTA)
Usage:
  scripts/S2_firmware_publish_beta.sh [--version <x.y.z>] [--channel <beta|stable>] [--env <pio_env>] [--github-worktree <path>] [--push]
  scripts/S2_firmware_publish_beta.sh --bin <firmware.bin> [--version <x.y.z>] [--channel <beta|stable>] [--github-worktree <path>] [--push]
Examples:
  scripts/S2_firmware_publish_beta.sh --channel beta --push
  scripts/S2_firmware_publish_beta.sh --env esp32-s3-n16r8 --channel beta
  scripts/S2_firmware_publish_beta.sh --bin dist/fw.bin --channel stable --push
USAGE
}

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BOARD="${PIXEL_STORE_BOARD:-pixel64x32V2}"
CHANNEL="beta"
BIN_PATH=""
VERSION=""
DO_PUSH=0
GITHUB_WORKTREE="${PIXEL_STORE_GITHUB_WORKTREE:-}"
PIO_ENV="${PIXEL_FIRMWARE_PIO_ENV:-esp32-s3-n16r8}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --bin) BIN_PATH="${2:-}"; shift 2 ;;
    --version) VERSION="${2:-}"; shift 2 ;;
    --channel) CHANNEL="${2:-}"; shift 2 ;;
    --env) PIO_ENV="${2:-}"; shift 2 ;;
    --github-worktree) GITHUB_WORKTREE="${2:-}"; shift 2 ;;
    --push) DO_PUSH=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown arg: $1" >&2; usage; exit 1 ;;
  esac
done

if [[ "${CHANNEL}" != "beta" && "${CHANNEL}" != "stable" ]]; then
  echo "error: channel must be beta or stable" >&2
  exit 1
fi

if [[ -z "${GITHUB_WORKTREE}" ]]; then
  for c in "${ROOT}/../pixel-app-store" "${HOME}/develop/project/pixel-app-store" "${HOME}/pixel-app-store"; do
    if [[ -d "${c}/.git" ]]; then GITHUB_WORKTREE="${c}"; break; fi
  done
fi
if [[ -z "${GITHUB_WORKTREE}" || ! -d "${GITHUB_WORKTREE}/.git" ]]; then
  echo "error: github worktree not found" >&2
  exit 1
fi

if [[ -z "${VERSION}" ]]; then
  CUR_VER="$(python3 - "${ROOT}/include/my_debug.h" <<'PY'
import re, sys
p = sys.argv[1]
try:
    txt = open(p, "r", encoding="utf-8").read()
except Exception:
    print("")
    raise SystemExit(0)
m = re.search(r'#define\s+SW_VERSION\s+"([^"]+)"', txt)
if not m:
    print("")
    raise SystemExit(0)
v = m.group(1).strip()
if v[:1].lower() == "v":
    v = v[1:]
print(v)
PY
)"
  if [[ -z "${CUR_VER}" ]]; then
    MANIFEST_URL="https://ota.geekmagic.cc/firmware/${BOARD}/${CHANNEL}/firmware.json"
    CUR_VER="$(curl --silent --show-error --fail "${MANIFEST_URL}" | python3 -c 'import json,sys; print((json.load(sys.stdin).get("version") or "").strip())' 2>/dev/null || true)"
  fi
  if [[ -n "${CUR_VER}" ]]; then
    VERSION="$(python3 - "${CUR_VER}" <<'PY'
import re, sys
v = (sys.argv[1] or "").strip()
m = re.match(r'^\s*(\d+)\.(\d+)\.(\d+)\s*$', v)
if not m:
    print("0.0.1")
    raise SystemExit(0)
major, minor, patch = map(int, m.groups())
print(f"{major}.{minor}.{patch + 1}")
PY
)"
    echo "auto version: ${CUR_VER} -> ${VERSION} (${CHANNEL})"
  else
    VERSION="0.0.1"
    echo "auto version: no existing manifest, init -> ${VERSION} (${CHANNEL})"
  fi
fi

echo "==> Sync firmware code version: SW_VERSION -> V${VERSION}"
python3 - "${ROOT}/include/my_debug.h" "${VERSION}" <<'PY'
import re, sys
path, ver = sys.argv[1], sys.argv[2]
txt = open(path, "r", encoding="utf-8").read()
new_line = f'#define SW_VERSION  "V{ver}"'
if re.search(r'#define\s+SW_VERSION\s+"[^"]+"', txt):
    txt2 = re.sub(r'#define\s+SW_VERSION\s+"[^"]+"', new_line, txt, count=1)
else:
    txt2 = new_line + "\n" + txt
if txt2 != txt:
    with open(path, "w", encoding="utf-8") as f:
        f.write(txt2)
print(f"updated {path} -> {new_line}")
PY

if [[ -z "${BIN_PATH}" ]]; then
  echo "==> Build firmware with PlatformIO env: ${PIO_ENV}"
  (cd "${ROOT}" && pio run -e "${PIO_ENV}")
  BIN_PATH="${ROOT}/.pio/build/${PIO_ENV}/firmware.bin"
fi
if [[ ! -f "${BIN_PATH}" ]]; then
  echo "error: firmware bin not found: ${BIN_PATH}" >&2
  exit 1
fi

FW_NAME="firmware-v${VERSION}.bin"
RAW_URL="https://raw.githubusercontent.com/GeekMagicClock/pixel-app-store/main/firmware/${CHANNEL}/${FW_NAME}"
DIST_DIR="${ROOT}/dist/firmware/${BOARD}/${CHANNEL}"
MANIFEST_PATH="${DIST_DIR}/firmware.json"

mkdir -p "${DIST_DIR}"
mkdir -p "${GITHUB_WORKTREE}/firmware/${CHANNEL}"
cp -f "${BIN_PATH}" "${GITHUB_WORKTREE}/firmware/${CHANNEL}/${FW_NAME}"

SIZE_BYTES="$(wc -c < "${BIN_PATH}" | tr -d ' ')"
SHA256="$(shasum -a 256 "${BIN_PATH}" | awk '{print $1}')"
python3 - "${MANIFEST_PATH}" "${VERSION}" "${RAW_URL}" "${SIZE_BYTES}" "${SHA256}" "${BOARD}" "${CHANNEL}" <<'PY'
import json, sys, time
p, v, u, size, sha, board, ch = sys.argv[1:]
obj = {
  "ok": True,
  "board": board,
  "channel": ch,
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

REMOTE="${PIXEL_FIRMWARE_REMOTE_ROOT:-root@111.229.177.3:/www/wwwroot/ota.geekmagic.cc/firmware}/${BOARD}/${CHANNEL}"
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

echo "==> Upload firmware manifest to OTA"
"${SSH_ARGS[@]}" "${REMOTE_LOGIN}" "mkdir -p '${REMOTE_PATH}'"
"${SCP_ARGS[@]}" "${MANIFEST_PATH}" "${REMOTE_LOGIN}:${REMOTE_PATH}/firmware.json"

if [[ "${DO_PUSH}" == "1" ]]; then
  pushd "${GITHUB_WORKTREE}" >/dev/null
  git add "firmware/${CHANNEL}/${FW_NAME}"
  if git diff --cached --quiet; then
    echo "No github firmware changes to commit."
  else
    git commit -m "Publish ${CHANNEL} firmware v${VERSION} [skip ci]"
    git push
  fi
  popd >/dev/null
else
  echo "github firmware copied; pass --push to commit and push"
fi

echo "done"
