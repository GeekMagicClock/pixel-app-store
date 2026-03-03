# App Store Packaging

Build zip packages and `apps-index.json` from local apps:

```bash
# 1) Capture real thumbnails from device (recommended, matches actual screen)
python3 python/store/capture_real_thumbnails.py \
  --device 192.168.3.140 \
  --apps-root data_littlefs/apps

# 2) (optional fallback) generate synthetic previews if no device available
python3 python/store/generate_app_thumbnails.py --apps-root data_littlefs/apps

python3 python/store/build_store_index.py \
  --apps-root data_littlefs/apps \
  --out-dir dist/store \
  --base-url https://raw.githubusercontent.com/<org>/<repo>/main/dist/store

# Optional: package Lua bytecode instead of plain-text main.lua
# (requires luac 5.4.x to match device runtime Lua 5.4)
# Build an ESP-compatible luac first (avoids "lua_Integer size mismatch"):
./python/store/build_luac_esp_compat.sh

python3 python/store/build_store_index.py \
  --apps-root data_littlefs/apps \
  --out-dir dist/store \
  --base-url https://raw.githubusercontent.com/<org>/<repo>/main/dist/store \
  --lua-bytecode \
  --luac-bin python/store/tools/luac-esp-compat \
  --entry-name app.bin
```

When `--lua-bytecode` is enabled:
- package entry file can be renamed (for example `app.bin`)
- script injects `manifest.json` field: `"entry": "app.bin"`
- device runtime will load this `entry` file instead of fixed `main.lua`

Then commit/push `dist/store/*` to GitHub.

Optional thumbnail:
- Put one of these files in each app folder: `thumbnail.png` / `thumbnail.jpg` / `thumbnail.jpeg` / `thumb.png` / `preview.png`
- Script will copy it to `dist/store/thumbs/` and write `thumbnail_url` into `apps-index.json`.

On device, open:

- `http://<device-ip>/api/store/ui`

The page loads `apps-index.json`, downloads a zip, unpacks in browser, and uploads files to the device via `/api/apps/<app_id>/<file>`.

Capture API:
- `GET /api/screen/capture.ppm` returns current 64x32 frame as PPM(P6), used by `capture_real_thumbnails.py`.
