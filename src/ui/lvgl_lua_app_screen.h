#pragma once

#include <cstddef>
#include <cstdint>

// Generic "Lua app" screen (64x32).
// It loads /littlefs/apps/<app_id>/main.lua and calls app.render() periodically.

void LvglShowLuaWeatherAppScreen();
void LvglStopLuaWeatherAppScreen();

void LvglShowLuaStocksAppScreen();
void LvglStopLuaStocksAppScreen();

void LvglShowLuaStockScreen3AppScreen();
void LvglStopLuaStockScreen3AppScreen();

void LvglShowLuaFbTestAppScreen();
void LvglStopLuaFbTestAppScreen();

void LvglShowLuaOwmWeatherAppScreen();
void LvglStopLuaOwmWeatherAppScreen();

// Generic: show any app directory under LittleFS, e.g. "/littlefs/apps/fb_test".
void LvglShowLuaAppDirScreen(const char* app_dir);
void LvglStopLuaAppDirScreen();

// Pre-create the persistent Lua app root screen early, before runtime app switching.
void LvglLuaAppScreenPrewarm();

// Capture current app canvas as RGB565 frame (64x32).
// Returns false if no active Lua app canvas is available.
bool LvglCaptureLuaAppFrameRgb565(uint16_t* out_pixels, size_t pixel_count, size_t* out_width, size_t* out_height);

// Query currently running Lua app info.
// Returns true when there is an active app id.
// Any output pointer may be nullptr; corresponding size must be >0 when pointer is non-null.
bool LvglGetCurrentLuaAppInfo(char* out_app_id, size_t app_id_size, char* out_app_dir, size_t app_dir_size);

// OTA overlay shown on the pixel screen while firmware upload is in progress.
void LvglOtaOverlayBegin(size_t total_bytes);
void LvglOtaOverlayUpdate(size_t written_bytes, size_t total_bytes);
void LvglOtaOverlayFinalizing();
void LvglOtaOverlayFail();
void LvglOtaOverlayClear();
