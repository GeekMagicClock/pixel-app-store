#pragma once

#include <stdint.h>

// Boot WiFi UI:
// - Connecting screen with 20s progress bar
// - Success screen (STA SSID + IP)
// - Fail screen (AP SSID + IP + hint)
//
// All functions must be called on the LVGL thread.

void LvglBootWifiScreenPreloadFont();

void LvglShowBootWifiConnecting(const char *try_ssid, uint32_t timeout_ms);
void LvglShowBootWifiSuccess(const char *sta_ssid, const char *sta_ip);
void LvglShowBootWifiFailed(const char *ap_ssid, const char *ap_ip);

void LvglStopBootWifiScreen();

