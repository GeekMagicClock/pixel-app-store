#pragma once

// Demo carousel: weather_owm -> fb_test -> ...
void LvglStartLuaAppCarousel(unsigned interval_ms);
void LvglStopLuaAppCarousel();
void LvglLuaAppCarouselNext();
void LvglReloadLuaAppCarousel();

// Thread-safe request API:
// - Call RequestNext from any thread.
// - Call PumpRequests from LVGL thread (e.g. in LvglTask loop).
void LvglLuaAppCarouselRequestNext();
void LvglLuaAppCarouselPumpRequests();
