#pragma once

#include "ESP32-HUB75-MatrixPanel-I2S-DMA.h"

HUB75_I2S_CFG MakePanelConfig();

bool Hub75GetXCompensationEnabled();
void Hub75SetXCompensationEnabled(bool enabled);

bool Hub75GetCyanGreenCompensationEnabled();
void Hub75SetCyanGreenCompensationEnabled(bool enabled);

int Hub75GetColorRemap();
void Hub75SetColorRemap(int remap);

// Load/store persisted panel config under LittleFS.
// Returns true when load/save operation succeeds.
bool Hub75LoadPersistentConfig();
bool Hub75SavePersistentConfig();
