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

// Capture current app canvas as RGB565 frame (64x32).
// Returns false if no active Lua app canvas is available.
bool LvglCaptureLuaAppFrameRgb565(uint16_t* out_pixels, size_t pixel_count, size_t* out_width, size_t* out_height);
