#include <chrono>
#include <cstdlib>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "ui/lvgl_hub75_port.h"

extern "C" void *heap_caps_malloc(size_t size, unsigned caps) {
  (void)caps;
  return std::malloc(size);
}

extern "C" void *heap_caps_calloc(size_t n, size_t size, unsigned caps) {
  (void)caps;
  return std::calloc(n, size);
}

extern "C" void heap_caps_free(void *ptr) {
  std::free(ptr);
}

extern "C" int64_t esp_timer_get_time(void) {
  using namespace std::chrono;
  static const steady_clock::time_point start = steady_clock::now();
  return duration_cast<microseconds>(steady_clock::now() - start).count();
}

void LvglHub75Start(const LvglHub75PortConfig &cfg) {
  (void)cfg;
}

void LvglHub75SetFlushEnabled(bool enabled) {
  (void)enabled;
}

void LvglRunBenchmarkDemo() {
}
