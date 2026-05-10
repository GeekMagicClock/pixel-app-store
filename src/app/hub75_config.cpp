#include "app/hub75_config.h"

#include "esp_log.h"

#include <cerrno>
#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {
#include "cJSON.h"
}

static constexpr int kPanelWidth = 64;
static constexpr int kPanelHeight = 32;
static constexpr int kChainLength = 1;
static constexpr const char* kHub75CfgDir = "/littlefs/.sys";
static constexpr const char* kHub75CfgPath = "/littlefs/.sys/hub75_config.json";
static const char *kTag = "hub75_cfg";
static bool g_x_compensation_enabled = false;

static bool EnsureDir(const char* path) {
  if (!path || !*path) return false;
  if (mkdir(path, 0755) == 0) return true;
  return errno == EEXIST;
}

static bool SaveTextFileAtomic(const char* path, const std::string& text) {
  if (!path || !*path) return false;
  const std::string tmp = std::string(path) + ".tmp";
  FILE* f = fopen(tmp.c_str(), "wb");
  if (!f) return false;
  const size_t need = text.size();
  const size_t wrote = fwrite(text.data(), 1, need, f);
  const bool ok_close = (fclose(f) == 0);
  if (!ok_close || wrote != need) {
    unlink(tmp.c_str());
    return false;
  }
  if (rename(tmp.c_str(), path) != 0) {
    unlink(tmp.c_str());
    return false;
  }
  return true;
}

static bool ReadSmallFile(const char* path, std::string* out) {
  if (out) out->clear();
  if (!path || !out) return false;
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  std::string text;
  char buf[128];
  while (true) {
    const size_t n = fread(buf, 1, sizeof(buf), f);
    if (n > 0) text.append(buf, n);
    if (n < sizeof(buf)) break;
  }
  fclose(f);
  *out = std::move(text);
  return true;
}

static void LogPanelConfig(const HUB75_I2S_CFG &cfg) {
  ESP_LOGI(kTag,
           "panel=%dx%d chain=%d driver=%d clkphase=%d latch_blank=%d min_refresh=%d i2s_hz=%u x_comp=%d",
           cfg.mx_width, cfg.mx_height, cfg.chain_length, static_cast<int>(cfg.driver),
           static_cast<int>(cfg.clkphase), static_cast<int>(cfg.latch_blanking),
           static_cast<int>(cfg.min_refresh_rate), static_cast<unsigned>(cfg.i2sspeed),
           cfg.x_compensation ? 1 : 0);
  ESP_LOGI(kTag,
           "pins r1=%d g1=%d b1=%d r2=%d g2=%d b2=%d a=%d b=%d c=%d d=%d e=%d lat=%d oe=%d clk=%d",
           cfg.gpio.r1, cfg.gpio.g1, cfg.gpio.b1, cfg.gpio.r2, cfg.gpio.g2, cfg.gpio.b2,
           cfg.gpio.a, cfg.gpio.b, cfg.gpio.c, cfg.gpio.d, cfg.gpio.e, cfg.gpio.lat,
           cfg.gpio.oe, cfg.gpio.clk);
}

HUB75_I2S_CFG MakePanelConfig() {
  HUB75_I2S_CFG cfg(kPanelWidth, kPanelHeight, kChainLength);

  cfg.gpio.r1 = R1_PIN_DEFAULT;
  cfg.gpio.g1 = G1_PIN_DEFAULT;
  cfg.gpio.b1 = B1_PIN_DEFAULT;
  cfg.gpio.r2 = R2_PIN_DEFAULT;
  cfg.gpio.g2 = G2_PIN_DEFAULT;
  cfg.gpio.b2 = B2_PIN_DEFAULT;

  cfg.gpio.a = A_PIN_DEFAULT;
  cfg.gpio.b = B_PIN_DEFAULT;
  cfg.gpio.c = C_PIN_DEFAULT;
  cfg.gpio.d = D_PIN_DEFAULT;
  cfg.gpio.e = E_PIN_DEFAULT;

  cfg.gpio.lat = LAT_PIN_DEFAULT;
  cfg.gpio.oe = OE_PIN_DEFAULT;
  cfg.gpio.clk = CLK_PIN_DEFAULT;

#ifndef HUB75_LATCH_BLANKING
#define HUB75_LATCH_BLANKING 2
#endif
#ifndef HUB75_I2S_SPEED
#define HUB75_I2S_SPEED HUB75_I2S_CFG::HZ_20M
#endif
#ifndef HUB75_MIN_REFRESH_RATE
#define HUB75_MIN_REFRESH_RATE 60
#endif
#ifndef HUB75_CLKPHASE
//#define HUB75_CLKPHASE false
#define HUB75_CLKPHASE true 
#endif
#ifndef HUB75_PANEL_DRIVER
#define HUB75_PANEL_DRIVER HUB75_I2S_CFG::SHIFTREG
#endif

  cfg.latch_blanking = HUB75_LATCH_BLANKING;
  cfg.i2sspeed = HUB75_I2S_SPEED;
  cfg.clkphase = HUB75_CLKPHASE;
  cfg.driver = HUB75_PANEL_DRIVER;
  cfg.min_refresh_rate = HUB75_MIN_REFRESH_RATE;
  cfg.x_compensation = g_x_compensation_enabled;
  LogPanelConfig(cfg);
  return cfg;
}

bool Hub75GetXCompensationEnabled() { return g_x_compensation_enabled; }

void Hub75SetXCompensationEnabled(bool enabled) {
  g_x_compensation_enabled = enabled;
}

bool Hub75LoadPersistentConfig() {
  std::string text;
  if (!ReadSmallFile(kHub75CfgPath, &text) || text.empty()) {
    g_x_compensation_enabled = false;
    return false;
  }

  cJSON* root = cJSON_ParseWithLength(text.c_str(), text.size());
  if (!root) {
    g_x_compensation_enabled = false;
    return false;
  }

  const cJSON* x_comp = cJSON_GetObjectItemCaseSensitive(root, "x_compensation");
  const bool enabled = cJSON_IsBool(x_comp) ? cJSON_IsTrue(x_comp) : false;
  g_x_compensation_enabled = enabled;
  cJSON_Delete(root);
  ESP_LOGI(kTag, "loaded persisted x_compensation=%d", enabled ? 1 : 0);
  return true;
}

bool Hub75SavePersistentConfig() {
  if (!EnsureDir(kHub75CfgDir)) return false;
  cJSON* root = cJSON_CreateObject();
  if (!root) return false;
  cJSON_AddBoolToObject(root, "x_compensation", g_x_compensation_enabled);
  char* rendered = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!rendered) return false;

  const std::string body(rendered);
  cJSON_free(rendered);
  const bool ok = SaveTextFileAtomic(kHub75CfgPath, body + "\n");
  if (ok) {
    ESP_LOGI(kTag, "saved persisted x_compensation=%d", g_x_compensation_enabled ? 1 : 0);
  }
  return ok;
}
