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

One-click SSH publish to the current app store host:

```bash
python3 python/publish_store.py
```

Default publish target:
- SSH: `root@111.229.177.3:/root/fw/pixel64x32V2/apps`
- SSH key: `~/.ssh/t20260309.pem`
- Store index: `http://111.229.177.3:8001/fw/pixel64x32V2/apps/apps-index.json`
- Package entry: `app.bin` by default

Useful options:

```bash
# Capture real thumbnails before publish
python3 python/publish_store.py --thumbnail-mode capture --device 192.168.3.152

# Generate synthetic thumbnails before publish
python3 python/publish_store.py --thumbnail-mode generate

# Build only, do not upload or start the remote server
python3 python/publish_store.py --skip-upload --skip-serve --skip-verify

# If you explicitly want plain-text main.lua packages
python3 python/publish_store.py --plain-lua
```

Optional thumbnail:
- Put one of these files in each app folder: `thumbnail.png` / `thumbnail.jpg` / `thumbnail.jpeg` / `thumb.png` / `preview.png`
- Script will copy it to `dist/store/thumbs/` and write `thumbnail_url` into `apps-index.json`.

On device, open:

- `http://<device-ip>/api/store/ui`

The page loads `apps-index.json`, downloads a zip, unpacks in browser, and uploads files to the device via `/api/apps/<app_id>/<file>`.

Capture API:
- `GET /api/screen/capture.ppm` returns current 64x32 frame as PPM(P6), used by `capture_real_thumbnails.py`.
