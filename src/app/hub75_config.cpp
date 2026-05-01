#include "app/hub75_config.h"

#include "esp_log.h"

static constexpr int kPanelWidth = 64;
static constexpr int kPanelHeight = 32;
static constexpr int kChainLength = 1;
static const char *kTag = "hub75_cfg";

static void LogPanelConfig(const HUB75_I2S_CFG &cfg) {
  ESP_LOGI(kTag,
           "panel=%dx%d chain=%d driver=%d clkphase=%d latch_blank=%d min_refresh=%d i2s_hz=%u",
           cfg.mx_width, cfg.mx_height, cfg.chain_length, static_cast<int>(cfg.driver),
           static_cast<int>(cfg.clkphase), static_cast<int>(cfg.latch_blanking),
           static_cast<int>(cfg.min_refresh_rate), static_cast<unsigned>(cfg.i2sspeed));
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
  cfg.x_compensation = true;
  LogPanelConfig(cfg);
  return cfg;
}
