# LVGL PC Simulator

This is a minimal host-side LVGL simulator for the current project, adapted from `../miner/tools/lvgl_simulator`.

Current scope:
- Runs a 64x32 SDL window
- Reuses current project LVGL code directly
- Starts with the Boot WiFi screen because it is pure LVGL and has limited runtime dependencies
- Supports exporting the active frame to BMP for visual review

## Build

```bash
cd tools/lvgl_simulator
cmake -S . -B build
cmake --build build -j
```

## Run

```bash
./build/lvgl_pixel_sim
```

Useful environment variables:

```bash
LVGL_SIM_ZOOM=10 ./build/lvgl_pixel_sim
LVGL_SIM_SCREEN=boot_success ./build/lvgl_pixel_sim
LVGL_SIM_SCREEN=boot_failed ./build/lvgl_pixel_sim
LVGL_SIM_SCREEN=stock6 ./build/lvgl_pixel_sim
LVGL_SIM_EXPORT=/tmp/pixel_sim.bmp ./build/lvgl_pixel_sim
```

## Notes

- This is intentionally minimal. It proves out the host SDL/LVGL loop and lets us iterate on LVGL layout faster.
- Next expansion target should be additional pure-LVGL screens, then selective runtime-backed screens.
