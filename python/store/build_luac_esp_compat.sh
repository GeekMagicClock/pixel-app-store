#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/python/store/tools"
OUT_BIN="$OUT_DIR/luac-esp-compat"
LUA_VER="${LUA_VER:-5.4.7}"
WORK_DIR="${TMPDIR:-/tmp}/lua-${LUA_VER}-build-$$"
TARBALL="$WORK_DIR/lua-${LUA_VER}.tar.gz"
SRC_DIR="$WORK_DIR/lua-${LUA_VER}/src"

mkdir -p "$OUT_DIR"
mkdir -p "$WORK_DIR"

cleanup() {
  rm -rf "$WORK_DIR"
}
trap cleanup EXIT

echo "[1/3] downloading Lua ${LUA_VER} source..."
curl -fsSL "https://www.lua.org/ftp/lua-${LUA_VER}.tar.gz" -o "$TARBALL"
tar -xzf "$TARBALL" -C "$WORK_DIR"

echo "[2/3] building luac with ESP-compatible numeric ABI..."
# Force chunk ABI compatible with ESP build:
# - 32-bit lua_Integer
# - 64-bit lua_Number (double)
perl -0777 -i -pe 's/#define LUA_32BITS\s+0/#define LUA_32BITS\t1/g' "$SRC_DIR/luaconf.h"
perl -0777 -i -pe 's/#define LUA_FLOAT_TYPE\s+LUA_FLOAT_FLOAT/#define LUA_FLOAT_TYPE\tLUA_FLOAT_DOUBLE/g' "$SRC_DIR/luaconf.h"

make -C "$SRC_DIR" clean >/dev/null
make -C "$SRC_DIR" luac \
  MYCFLAGS="-DLUA_USE_C89" \
  CC="${CC:-cc} -std=gnu99" >/dev/null

cp "$SRC_DIR/luac" "$OUT_BIN"
chmod +x "$OUT_BIN"

echo "[3/3] output: $OUT_BIN"
"$OUT_BIN" -v
