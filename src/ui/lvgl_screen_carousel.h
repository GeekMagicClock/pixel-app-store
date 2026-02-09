#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Starts an LVGL-timer-driven carousel that cycles through screens (currently 1-7).
// Safe to call from the LVGL thread. If already running, it restarts.
void LvglStartScreenCarousel(unsigned interval_ms);

// Stops the carousel timer (doesn't stop the currently displayed screen).
void LvglStopScreenCarousel();

// Advances to the next screen. Safe to call from the LVGL thread.
void LvglScreenCarouselNext();

// Requests advancing to the next screen via lv_async_call. Safe to call from any task.
void LvglRequestScreenCarouselNext();

#ifdef __cplusplus
}
#endif
