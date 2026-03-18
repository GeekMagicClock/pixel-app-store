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

typedef struct {
  char mode[8];
  char saved_ssid[33];
  char sta_ssid[33];
  char sta_ip[16];
  char ap_ssid[33];
  char ap_ip[16];
  bool sta_connected;
  bool ap_active;
} WifiStatusInfo;

typedef struct {
  char ssid[33];
  char auth[16];
  int8_t rssi;
  uint8_t strength;
} WifiScanResult;

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

// Returns current Wi-Fi mode, saved SSID, connected SSID/IP, and AP SSID/IP.
bool WifiManagerGetStatus(WifiStatusInfo *out_status);

// Scans nearby APs. Returns number of results written to out_results.
int WifiManagerScanNetworks(WifiScanResult *out_results, int max_results, uint32_t timeout_ms);

// Saves STA credentials to NVS-backed Wi-Fi config for future boots.
bool WifiManagerSaveStaCredentials(const char *ssid, const char *password);
