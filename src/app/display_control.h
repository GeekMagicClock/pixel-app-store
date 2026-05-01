#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class MatrixPanel_I2S_DMA;

void DisplayControlBind(MatrixPanel_I2S_DMA* display, SemaphoreHandle_t display_mutex, uint8_t initial_brightness);
bool DisplayControlSetBrightness(uint8_t brightness);
uint8_t DisplayControlGetBrightness();
bool DisplayControlIsReady();
bool DisplayControlShowSleepCountdown(int seconds_left);
