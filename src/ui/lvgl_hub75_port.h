#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class MatrixPanel_I2S_DMA;

struct LvglHub75PortConfig {
  MatrixPanel_I2S_DMA *display;
  SemaphoreHandle_t display_mutex;
  int hor_res;
  int ver_res;
};

// Initializes LVGL, registers HUB75 display driver, and starts the LVGL task.
void LvglHub75Start(const LvglHub75PortConfig &cfg);

// Runs LVGL's benchmark demo (requires CONFIG_LV_USE_DEMO_BENCHMARK=y).
void LvglRunBenchmarkDemo();

