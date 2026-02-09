#include "app/hub75_config.h"

static constexpr int kPanelWidth = 64;
static constexpr int kPanelHeight = 32;
static constexpr int kChainLength = 1;

HUB75_I2S_CFG MakePanelConfig() {
  HUB75_I2S_CFG cfg(kPanelWidth, kPanelHeight, kChainLength);

  cfg.gpio.r1 = 25;
  cfg.gpio.g1 = 26;
  cfg.gpio.b1 = 27;
  cfg.gpio.r2 = 14;
  cfg.gpio.g2 = 12;
  cfg.gpio.b2 = 13;

  cfg.gpio.a = 23;
  cfg.gpio.b = 19;
  cfg.gpio.c = 5;
  cfg.gpio.d = 17;

  // Only required for 64x64 (1/32 scan) panels.
  cfg.gpio.e = -1;

  cfg.gpio.lat = 4;
  cfg.gpio.oe = 15;
  cfg.gpio.clk = 16;

#ifndef HUB75_LATCH_BLANKING
#define HUB75_LATCH_BLANKING 2
#endif
#ifndef HUB75_I2S_SPEED
#define HUB75_I2S_SPEED HUB75_I2S_CFG::HZ_10M
#endif
#ifndef HUB75_MIN_REFRESH_RATE
#define HUB75_MIN_REFRESH_RATE 60
#endif

  cfg.latch_blanking = HUB75_LATCH_BLANKING;
  cfg.i2sspeed = HUB75_I2S_SPEED;
  cfg.clkphase = false;
  cfg.min_refresh_rate = HUB75_MIN_REFRESH_RATE;

  return cfg;
}

