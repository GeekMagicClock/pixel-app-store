#pragma once

#include <stdbool.h>
#include <stdint.h>

// Minimal WiFi bring-up helper for boot sequence.
// Intended for ESP-IDF framework.

typedef struct {
  char ssid[33];
  char ip[16];
} WifiStaInfo;

typedef struct {
  char ssid[33];
  char ip[16];
} WifiApInfo;

// Initializes NVS, esp-netif, event loop, and esp_wifi once.
// Safe to call multiple times.
bool WifiManagerInit();

// Returns whether a saved STA SSID exists (from esp_wifi_get_config).
bool WifiManagerGetSavedStaSsid(char *out_ssid, uint32_t out_ssid_sz);

// Starts STA, attempts to connect, and waits for IP.
// On success, fills sta_out and returns true.
// On failure/timeout, returns false.
bool WifiManagerConnectSta(uint32_t timeout_ms, WifiStaInfo *sta_out);

// Starts AP for configuration assistance.
// Fills ap_out and returns true if AP started (IP may still be "--" if not ready yet).
bool WifiManagerStartSetupAp(WifiApInfo *ap_out);

