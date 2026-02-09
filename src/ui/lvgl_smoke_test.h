#pragma once

// A tiny LVGL demo suitable for 64x32 panels.
// Creates a moving bar + bouncing box and keeps the UI updating.
void LvglRunSmokeTest();

// Stops the smoke test timer and deletes its screen if it exists.
void LvglStopSmokeTest();
