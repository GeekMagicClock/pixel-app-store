#pragma once

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class MatrixPanel_I2S_DMA;

struct ButtonEventFlags {
  volatile bool *request_aapl_ticker;
  volatile bool *request_raw_benchmark;
};

void StartUserButtonTask(MatrixPanel_I2S_DMA &display,
                         SemaphoreHandle_t display_mutex,
                         const ButtonEventFlags &flags);

// Reads button state (active-low). Safe to call during early boot.
// Returns true when the button is currently pressed.
bool IsUserButtonPressed();
