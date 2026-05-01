# esp32-pixel

ESP32-S3 + ESP-IDF + HUB75 64x32 LED matrix project.

This repository is optimized for fast iteration on device-side "apps" stored in LittleFS. The current main path is:

- firmware in `src/`
- runtime apps in `data_littlefs/apps/<app_id>/`
- app upload / switch / OTA helpers in `scripts/`

This README is written for the next AI or engineer taking over the repo.

## Current Status

- Main active build target: `esp32-s3-n16r8`
- Framework: ESP-IDF via PlatformIO
- Display stack: `hub75_dma` + LVGL
- App model: Lua apps loaded from LittleFS at runtime
- Filesystem root on device: `/littlefs`
- Default app source directory in repo: `data_littlefs/apps`

The current firmware already supports:

- scanning installed apps under `/littlefs/apps`
- selecting a startup app
- uploading app files over HTTP
- switching apps without reflashing firmware
- OTA firmware update over HTTP

## Build And Run

Build the active firmware:

```bash
pio run
```

Upload firmware over serial:

```bash
pio run -t upload
```

Build LittleFS image from `data_littlefs`:

```bash
pio run -t buildfs
```

Upload LittleFS image over serial:

```bash
pio run -t uploadfs
```

Open monitor:

```bash
pio device monitor
```

If you want reset-then-monitor behavior, use:

```bash
python3 scripts/monitor_reset_then_monitor.py '$PORT' '$BAUD' "$PWD" esp32-s3-n16r8
```

Notes:

- `platformio.ini` sets `default_envs = esp32-s3-n16r8`
- `scripts/pio_data_dir_idf.py` forces PlatformIO to use `data_littlefs/` as the ESP-IDF filesystem source
- `pio run` is the quickest sanity check after any firmware change

## PC LVGL Simulator

For faster LVGL layout iteration, this repo now includes a host-side SDL simulator adapted from `../miner/tools/lvgl_simulator`:

```bash
cd tools/lvgl_simulator
cmake -S . -B build
cmake --build build -j
./build/lvgl_pixel_sim
```

Current simulator scope:

- opens a 64x32 SDL window
- reuses current project LVGL source directly
- currently boots the `Boot WiFi` screen path first
- supports frame export for visual review

Examples:

```bash
LVGL_SIM_ZOOM=10 ./tools/lvgl_simulator/build/lvgl_pixel_sim
LVGL_SIM_SCREEN=boot_success ./tools/lvgl_simulator/build/lvgl_pixel_sim
LVGL_SIM_SCREEN=boot_failed ./tools/lvgl_simulator/build/lvgl_pixel_sim
LVGL_SIM_SCREEN=stock6 ./tools/lvgl_simulator/build/lvgl_pixel_sim
LVGL_SIM_SCREEN=boot_success LVGL_SIM_EXPORT=/tmp/pixel_boot_success.bmp ./tools/lvgl_simulator/build/lvgl_pixel_sim
LVGL_SIM_SCREEN=stock6 LVGL_SIM_EXPORT=/tmp/pixel_stock6.bmp ./tools/lvgl_simulator/build/lvgl_pixel_sim
```

Use this simulator for:

- text budget
- spacing and alignment
- panel geometry
- general LVGL widget iteration

Do not use it as the final source of truth for:

- LED panel brightness
- exact RGB565 color impression
- hardware refresh artifacts
- final readable-on-device judgment

## Love2D Desktop Proto

This repo also includes a desktop-only `Love2D` prototype sandbox:

```bash
love tools/love2d_proto
```

Purpose:

- explore visual ideas before porting to the 64x32 device
- test depth, highlight, motion, material, and composition quickly
- prototype styles that would be too slow to iterate directly on hardware

Important:

- this is not device runtime code
- it is only for desktop exploration
- on this machine, `love` is not currently installed, so the prototype directory is prepared but not locally executed yet

## Repo Layout

Important paths:

- `platformio.ini`: build environments and board config
- `src/main.cpp`: firmware boot flow, LittleFS mount, LVGL task, startup app selection
- `src/app/`: runtime services like Wi-Fi, OTA, app update server, Lua runtime
- `src/ui/`: LVGL screens, app screen host, HUB75 LVGL bridge
- `data_littlefs/apps/`: runtime app source tree copied into LittleFS
- `data_littlefs/fonts/`: runtime fonts loaded from LittleFS
- `dist/store/`: packaged app artifacts and app index output
- `scripts/`: developer tooling for app push, switch, OTA, asset generation, font tools
- `components/`: vendored third-party components such as `lvgl`, `esp_littlefs`, `hub75_dma`, `lua`
- `tools/lvgl_simulator/`: host SDL/LVGL simulator adapted from the sibling `miner` project
- `tools/love2d_proto/`: desktop-only visual prototype sandbox

## Dev Data Proxy

For device-side app debugging, this repo now includes a LAN proxy service:

```bash
python3 scripts/dev_api_proxy.py --host 0.0.0.0 --port 8787
```

Expected debug host in the current setup:

- `http://192.168.3.139:8787`

Built-in preset routes:

- `/coingecko/simple_price`
- `/yahoo/quote`
- `/yahoo/chart`
- `/alternative/fng`
- `/stocks/fear_greed`

Generic extensible route:

- `/proxy/<provider>/<path>?<query>`

Currently supported providers:

- `coingecko`
- `yahoo`
- `alternative`
- `onoff`

Quick checks:

```bash
curl http://192.168.3.139:8787/health
curl 'http://192.168.3.139:8787/coingecko/simple_price?ids=bitcoin&vs_currencies=usd&include_24hr_change=true&precision=full'
curl 'http://192.168.3.139:8787/alternative/fng?limit=7&format=json'
curl http://192.168.3.139:8787/stocks/fear_greed
```

Current debug defaults:

- `coingecko_ticker` defaults to `http://192.168.3.139:8787`
- `stock_fear_index` defaults to `http://192.168.3.139:8787`
- `crypto_fear_index` defaults to `http://192.168.3.139:8787`

Override keys if needed:

- `proxy.market_data_base`
- `proxy.coingecko_base`
- `proxy.stock_base`
- `proxy.crypto_base`

Important release rule:

- `scripts/dev_api_proxy.py` is debug-only tooling and must not be a runtime dependency for published apps.
- During development, finance apps may default to a LAN proxy for fast debugging.
- Finance apps must still work with `proxy.*` keys unset (direct upstream path).
- Yahoo auth/retry strategy (cookie + crumb flow) must be implemented in app/runtime logic, not only in dev proxy.
- For release validation, explicitly test with proxy disabled.

## Firmware Architecture

At a high level:

1. `src/main.cpp` mounts LittleFS at `/littlefs`
2. firmware starts LVGL and display services
3. firmware scans `/littlefs/apps`
4. one app is chosen as startup app
5. `LvglShowLuaAppDirScreen()` loads that app into the Lua app runtime

Relevant firmware pieces:

- `src/app/lua_app_runtime.*`: loads and executes Lua apps
- `src/ui/lvgl_lua_app_screen.*`: hosts a Lua app on the 64x32 display
- `src/ui/lvgl_lua_app_carousel.*`: app list / carousel behavior
- `src/app/app_update_server.*`: HTTP API for uploading, reloading, switching, uninstalling apps, and OTA

## Lua App Model

Each app lives in:

```text
data_littlefs/apps/<app_id>/
```

Typical files:

- `main.lua`: entry file
- `manifest.json`: metadata and layout hints
- `thumbnail.png`: preview asset for app listing / store flows
- optional extra assets: images, fonts, JSON, etc.

Example manifest:

```json
{
  "app_id": "openmeteo_3day",
  "name": "Open-Meteo 3 Day",
  "version": "1.0.3",
  "min_fw": "0.0.0",
  "min_api": "1.0",
  "entry": "main.lua",
  "layout_id": "framebuffer_rgb565",
  "capabilities": ["weather", "net"]
}
```

## App Settings Decoupling Rule

Development rule (must follow):

- `webpages/f.html` must stay generic and must not add per-app hardcoded settings UI.
- Preferred mode: each app owns its own settings UI page at `data_littlefs/apps/<app_id>/settings.html`.
- Firmware exposes app web assets at `/api/apps/web/<app_id>/<file>`.
- If `<file>.gz` exists, firmware serves it with `Content-Encoding: gzip` (recommended for `settings.html`).
- `f.html` only decides whether to show the `Settings` button, then loads the app page; it does not define per-app fields.
- App settings pages should read/write values via `/api/system/lua-data`, then call `/api/apps/reload` (and optional `/api/apps/switch/<app_id>`).

This keeps app development decoupled from frontend changes and allows adding new apps without editing `f.html`.

## Finance App Data Rule

Development rule (must follow):

- Finance apps (`stock1`, `stock_chart`, future stock/crypto apps) must own their data-fetch logic and failure handling.
- App defaults must not hardcode LAN proxy endpoints.
- Any debug proxy usage must come only from explicit `lua-data` config (`proxy.*`) and remain optional.
- For Yahoo endpoints, follow the production flow used by firmware/app logic (cookie + crumb), and keep this path valid without proxy.

Runtime expectations from `src/app/lua_app_runtime.h`:

- app directory is `/littlefs/apps/<app_id>/`
- default entry is `main.lua`
- `manifest.json` may override entry via `"entry"`
- app should return an `app` table

Common optional callbacks:

- `app.init(config_table)`
- `app.tick(ms)`
- `app.render()`
- `app.render_fb(...)`

Two rendering modes are currently supported:

- text/binding mode through `app.render()`
- framebuffer mode through `app.render_fb(...)`

Framebuffer mode is the main path for richer apps on the 64x32 display.

## Lua APIs Exposed By Firmware

The Lua runtime exposes lightweight `sys` and `data` modules.

Currently visible from firmware source:

- `sys.log(...)`
- `sys.now_ms()`
- `sys.local_time()`
- `sys.unix_time()`
- `sys.listdir(path)`
- `data.get(key)`
- `data.set(key, value)`

Persistent app data is stored under:

- `/littlefs/.sys/lua_data.json`

If you are adding new firmware-side capabilities for apps, `src/app/lua_app_runtime.cpp` is the place to extend.

## Fastest App Dev Loop

For app-only work, do not reflash firmware unless the runtime API changed.

Device IP rule:

1. keep active target device in repo root `device_ip.txt` (first non-empty line)
2. app/device scripts read this file by default
3. explicit `<device_ip[:port]>` argument still overrides the file for one-off commands

Recommended loop:

1. create or edit `data_littlefs/apps/<app_id>/main.lua`
2. update `data_littlefs/apps/<app_id>/manifest.json`
3. push the app to device with `scripts/push_app.sh`
4. switch to the app with `scripts/switch_app.sh` or use `--switch`
5. watch logs with serial monitor if needed

Push one app:

```bash
scripts/push_app.sh openmeteo_3day data_littlefs/apps --switch
```

Switch active app:

```bash
scripts/switch_app.sh openmeteo_3day
```

Uninstall app from device:

```bash
scripts/uninstall_app.sh openmeteo_3day
```

Build + package + install + run one app (single command):

```bash
scripts/build_package_install_run_app.sh openmeteo_3day
```

Auto-upload on every file change while developing an app:

```bash
scripts/watch_and_push_app.sh openmeteo_3day
```

Fetch device logs over API:

```bash
scripts/device_logs.sh --scope app --limit 120
```

When to rebuild firmware instead:

- you changed anything under `src/`
- you added new Lua-native APIs
- you changed display / LVGL / filesystem behavior
- you changed HTTP endpoints or OTA flow

## App Development Principles

These are mandatory for new app work in this repository.

1. Design for the real canvas first.
   Every app must be composed for the actual 64x32 framebuffer, not a desktop-first mockup.

2. Prefer deterministic framebuffer rendering.
   If the app has a custom visual identity, use `app.render_fb(...)` and keep the layout geometry explicit.

3. Local simulation is required before device upload.
   A new app must have a local preview or render script that can generate a reference frame and catch obvious geometry mistakes before any OTA or HTTP push.

4. Device verification is required before sign-off.
   After upload, capture the real device frame and compare it against the local reference design. Do not treat "uploaded successfully" as "layout verified".

5. Visual review must focus on spacing, balance, and legibility.
   On 64x32, one-pixel mistakes are visible. Check alignment, clipping, hand lengths, margins, text density, and contrast.

6. Text layout must be validated against worst-case content, not sample content.
   Every text-heavy app must test long labels, long numbers, negative values, and edge-case strings before upload. Do not assume short demo strings represent production data.

7. Ellipsis is not a default layout tool.
   `...` may be used only as a last-resort fallback after the primary layout has already been designed to avoid crowding. If important content is routinely truncated, the layout is wrong and must be redesigned.

8. Minimum spacing is part of correctness.
   Text blocks, labels, charts, and meters must have explicit separation. If two elements feel tight on a 64x32 preview, treat that as a layout bug even if pixels do not literally overlap.

9. Firmware changes must expose the data needed for verification.
   If screenshot comparison needs runtime time or state, add a minimal debug/status field rather than guessing locally.

10. Keep the app self-contained.
   New apps should live under one `data_littlefs/apps/<app_id>/` directory with clear metadata and no hidden external assumptions.

11. Every app must ship with a thumbnail and a local renderer/check path.
   `thumbnail.png` is required. A new app is incomplete if it has no generated thumbnail or no local script capable of rendering and checking its intended layout.

12. Compiled app-store packages must use the repo-pinned compatible `luac`.
   Device runtime Lua bytecode is version-sensitive. Store packages must be compiled with `python/store/tools/luac-esp-compat`, not the system `luac`. If firmware Lua is upgraded, update the repo-pinned compatible compiler first, then publish packages again.

## Required New-App Workflow

For every newly created app, use this sequence:

1. Define the layout and palette for the exact 64x32 target.
2. Implement the app under `data_littlefs/apps/<app_id>/`.
3. Create or update a local render/check script in `scripts/`.
4. Generate `thumbnail.png` from the same intended layout, not as an afterthought.
5. Run the local render/check script with worst-case text and fix all spacing, overlap, and truncation issues first.
6. Upload the app to the device.
7. Switch to the app on device.
8. Capture the real device frame from `/api/screen/capture.ppm`.
9. Compare the captured frame against the local reference render.
10. Only after the comparison passes should the app be considered visually verified.

## Device HTTP API

The firmware exposes a local HTTP API from `src/app/app_update_server.h`.

Main app endpoints:

- `GET /api/apps/ping`
- `PUT /api/apps/<app_id>/main.lua`
- `PUT /api/apps/<app_id>/manifest.json`
- `PUT /api/apps/<app_id>/<file>`
- `POST /api/apps/reload`
- `POST /api/apps/switch/<app_id>`
- `POST /api/apps/switch?app_id=<app_id>`
- `DELETE /api/apps/<app_id>`
- `DELETE /api/apps/<app_id>/<file>`

Firmware endpoints:

- `GET /api/firmware`
- `POST /api/firmware/ota`
- `POST /api/system/reboot`

Web UI:

- `GET /f.html`
- `GET /app.html`

The HTML for `/f.html` is sourced from `webpages/f.html` and compressed into a generated header during build.

## scripts/ Directory

This repo depends heavily on `scripts/`. These are the ones most likely to matter during active development.

### App Workflow Scripts

- `scripts/push_app.sh`
  Uploads all files from `data_littlefs/apps/<app_id>/` to the device via HTTP `PUT`.
  Can optionally switch to the app after upload with `--switch`.
  Upload policy: if `settings.html` exists, `settings.html.gz` is required, and only `.gz` is uploaded.

- `scripts/switch_app.sh`
  Sends a request to switch the active app on device without reflashing firmware.

- `scripts/uninstall_app.sh`
  Deletes an installed app from the device through the firmware API.

- `scripts/build_package_install_run_app.sh`
  One-step flow for a single app: compile/package with `publish_apps.py`, install to device, and switch to run it.
  Install path is always packaged bytecode output (`app.bin`), never source Lua.

- `scripts/publish_apps.py`
  Packaging policy mirrors upload: `settings.html` is excluded from zips; use `settings.html.gz` only.

- `scripts/watch_and_push_app.sh`
  Watches one app directory and auto-pushes to device on every file change.

- `scripts/device_logs.sh`
  Pulls `/api/system/logs` from device and prints concise logs (or raw JSON with `--raw`).

- `scripts/ota_update.py`
  Uploads `firmware.bin` to the device over local HTTP OTA.
  Defaults to the latest firmware in `.pio/build/esp32-s3-n16r8/` or `.pio/build/hub75_idf/`.

- `scripts/render_atelier_analog_clock.py`
  Local reference renderer for the `atelier_analog_clock` app.
  Generates a deterministic PPM design frame and can compare two PPM frames pixel-by-pixel.

- `scripts/verify_atelier_analog_clock.py`
  End-to-end verification helper for `atelier_analog_clock`.
  Can switch or push the app, query device status, fetch `/api/screen/capture.ppm`, render the expected local design, and compare expected vs actual output.

### Build Support Scripts

- `scripts/pio_data_dir_idf.py`
  PlatformIO pre-script.
  Forces `PROJECT_DATA_DIR = data_littlefs` and improves default serial-port selection on macOS.

- `scripts/embed_web_asset.py`
  Compresses a static asset and emits a C header.
  Used in `src/CMakeLists.txt` to embed `webpages/f.html`.

- `scripts/monitor_reset_then_monitor.py`
  Pulses reset before starting monitor, or can dump raw serial logs for a fixed duration.

### Font / Asset Utility Scripts

- `scripts/gen_lvgl_font_silkscreen_8.py`
  Generates a compact LVGL bitmap font C file from a TTF.

- `scripts/download_pixel_fonts.py`
  Helper for fetching pixel-style font assets.

- `scripts/preview_fonts_64x32.py`
  Local preview tool for testing font fit on a 64x32 layout.

- `scripts/font_server.py`
  Simple local HTTP server for previewing fonts from `data_littlefs/fonts`.

- `scripts/font_viewer.html`
- `scripts/font_manager.html`
- `scripts/font_preview_web.html`
  Browser-side tools for font preview / management.

### Publishing / Distribution

- `scripts/publish_apps.py`
  Builds compiled app-store packages, generates `apps-index.json` / `apps-index.json.gz`, and can upload them to the remote app server.
  This publishes bytecode bundles, not source bundles.
  Safety defaults:
  - when publishing selected app ids, index update is incremental merge (keeps other apps)
  - selected-app publish requires version bump vs current index
  - use `--full-index` only for intentional full index rewrite
  - use `--allow-no-bump` only for emergency override

Compiled publish examples:

```bash
python3 scripts/publish_apps.py crypto_fear_index stock_fear_index
python3 scripts/publish_apps.py --upload
python3 scripts/publish_apps.py stock1 --upload            # incremental (safe default)
python3 scripts/publish_apps.py --upload --full-index      # full rewrite
```

One-click beta incremental publish (changed apps only):

```bash
scripts/S2_publish_beta_incremental.sh
scripts/S2_publish_beta_incremental.sh --since-ref origin/main --push
scripts/S2_publish_beta_incremental.sh --dry-run
```

Packaging behavior:

- `main.lua` is compiled to `app.bin`
- any extra `.lua` assets are compiled before packaging
- `manifest.json` in the zip is rewritten to `"entry": "app.bin"`
- output goes to `dist/store/pixel64x32V2/apps/`
- default compiler must be `python/store/tools/luac-esp-compat`
- do not publish compiled packages with the system `luac`, even if it is also Lua 5.4.x
- current pinned compiler version is `Lua 5.4.7`, matching `components/lua/src/lua.h`

## App Packaging Conventions

The repo already contains many app examples under `data_littlefs/apps/`. Use those as templates instead of starting from zero.

Good seed apps to inspect:

- `data_littlefs/apps/fb_test`
- `data_littlefs/apps/openmeteo_3day`
- `data_littlefs/apps/media_gallery`
- `data_littlefs/apps/moon_phase_png`

The analog clock reference app added for this workflow is:

- `data_littlefs/apps/atelier_analog_clock`

Practical rules:

- keep `app_id` filesystem-safe: `[A-Za-z0-9_-]+`
- prefer one self-contained app directory
- keep runtime assets small; LittleFS space is limited
- if app visuals are custom, prefer framebuffer mode
- use `manifest.json` consistently even if the app is simple

## For The Next AI

If asked to "build a new app", follow this order:

1. inspect `data_littlefs/apps/` for the closest existing app
2. clone that app into a new `<app_id>` directory
3. update `manifest.json`
4. implement visuals in `main.lua`
5. push with `scripts/push_app.sh ... --switch`
6. only touch firmware in `src/` if the Lua runtime cannot support the feature

If asked to "fix app behavior", first determine whether the issue is:

- app logic bug in `data_littlefs/apps/...`
- runtime bug in `src/app/lua_app_runtime.cpp`
- LVGL host issue in `src/ui/lvgl_lua_app_screen.cpp`
- device API / OTA issue in `src/app/app_update_server.cpp`

If asked to "add a new app capability", likely touch:

- `src/app/lua_app_runtime.cpp`
- optionally `src/app/lua_app_runtime.h`
- possibly `src/ui/lvgl_lua_app_screen.cpp`

Then rebuild with `pio run`, OTA or serial-flash firmware, and continue app iteration.

## Known Reality Of Dependency Management

Third-party code under `components/` is currently vendored directly into this repository, not managed as active Git submodules.

Important vendored components:

- `components/lvgl`
- `components/esp_littlefs`
- `components/hub75_dma`
- `components/lua`

Treat these as in-repo dependencies. Update them carefully and expect large diffs.

## Minimum Verification Before Handing Off

For firmware changes:

```bash
pio run
```

For app-only changes:

1. push app with `scripts/push_app.sh`
2. switch to app
3. verify on-device render and serial logs
4. default all device operations to `device_ip.txt` unless explicitly overriding target

For new app launches, the minimum bar is higher:

1. run the local reference render/check first
2. upload and switch the app on device
3. capture `/api/screen/capture.ppm`
4. compare captured output against the local design reference

If you change app upload / OTA / filesystem behavior, verify both firmware build and at least one end-to-end device flow.
