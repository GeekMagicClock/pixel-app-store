#include "ui/lvgl_lua_app_carousel.h"

#include <algorithm>
#include <atomic>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "esp_log.h"

extern "C" {
#include "lvgl.h"
}

#include "ui/lvgl_lua_app_screen.h"

static const char* kTag = "app_carousel";

namespace {

static lv_timer_t* g_timer = nullptr;
static int g_index = -1;
static unsigned g_interval_ms = 10000;
static std::vector<std::string> g_app_dirs;
static std::atomic_uint g_pending_next_requests{0};
static lv_obj_t* g_empty_hint = nullptr;

static bool IsDir(const std::string& path) {
  struct stat st;
  if (stat(path.c_str(), &st) != 0) return false;
  return S_ISDIR(st.st_mode);
}

static bool IsFile(const std::string& path) {
  struct stat st;
  if (stat(path.c_str(), &st) != 0) return false;
  return S_ISREG(st.st_mode);
}

static void ScanLittlefsApps() {
  g_app_dirs.clear();

  const char* root = "/littlefs/apps";
  DIR* d = opendir(root);
  if (!d) {
    ESP_LOGW(kTag, "opendir failed: %s", root);
    // Fallback: keep a known app if present.
    return;
  }

  while (true) {
    struct dirent* e = readdir(d);
    if (!e) break;
    const char* name = e->d_name;
    if (!name || !*name) continue;
    if (name[0] == '.') continue;

    const std::string dir = std::string(root) + "/" + name;
    if (!IsDir(dir)) continue;

    const std::string app_bin = dir + "/app.bin";
    if (!IsFile(app_bin)) continue;

    g_app_dirs.push_back(dir);
  }

  closedir(d);

  std::sort(g_app_dirs.begin(), g_app_dirs.end());
  if (g_app_dirs.empty()) {
    ESP_LOGW(kTag, "no apps found under %s", root);
  } else {
    ESP_LOGI(kTag, "found %u apps", static_cast<unsigned>(g_app_dirs.size()));
    for (size_t i = 0; i < g_app_dirs.size(); i++) {
      ESP_LOGI(kTag, "  app[%u]=%s", static_cast<unsigned>(i), g_app_dirs[i].c_str());
    }
  }
}

static void StopCurrent(int idx) {
  (void)idx;
  LvglStopLuaAppDirScreen();
}

static void Show(int idx) {
  if (g_app_dirs.empty()) return;
  if (idx < 0) idx = 0;
  idx %= static_cast<int>(g_app_dirs.size());
  LvglShowLuaAppDirScreen(g_app_dirs[idx].c_str());
}

static void TimerCb(lv_timer_t* t) {
  (void)t;
  if (g_app_dirs.empty()) return;
  const int prev = g_index;
  g_index = (g_index + 1) % static_cast<int>(g_app_dirs.size());
  ESP_LOGI(kTag, "Switch %d -> %d", prev, g_index);
  StopCurrent(prev);
  Show(g_index);
}

static void ShowNoAppsHint() {
  lv_obj_t* scr = lv_scr_act();
  if (!scr) return;
  if (g_empty_hint && lv_obj_is_valid(g_empty_hint)) {
    lv_obj_del(g_empty_hint);
    g_empty_hint = nullptr;
  }
  g_empty_hint = lv_label_create(scr);
  lv_label_set_long_mode(g_empty_hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(g_empty_hint, 62);
  lv_obj_align(g_empty_hint, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_text_align(g_empty_hint, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(g_empty_hint, lv_color_white(), 0);
  lv_label_set_text(g_empty_hint, "No apps installed.\nUse Web UI to install.");
}

}  // namespace

void LvglStartLuaAppCarousel(unsigned interval_ms) {
  if (interval_ms < 1000) interval_ms = 1000;
  g_interval_ms = interval_ms;

  if (g_timer) {
    lv_timer_del(g_timer);
    g_timer = nullptr;
  }

  ScanLittlefsApps();
  if (g_app_dirs.empty()) {
    ESP_LOGW(kTag, "carousel not started: no apps");
    ShowNoAppsHint();
    return;
  }

  g_index = 0;
  ESP_LOGI(kTag, "Start app carousel interval=%ums", g_interval_ms);
  Show(g_index);

  g_timer = lv_timer_create(TimerCb, g_interval_ms, nullptr);
}

void LvglStopLuaAppCarousel() {
  if (g_timer) {
    lv_timer_del(g_timer);
    g_timer = nullptr;
  ESP_LOGI(kTag, "app carousel stopped");
  }
}

void LvglLuaAppCarouselNext() {
  if (g_app_dirs.empty()) {
    ScanLittlefsApps();
    if (g_app_dirs.empty()) return;
  }

  const int n = static_cast<int>(g_app_dirs.size());
  int prev = g_index;
  if (prev < 0 || prev >= n) prev = -1;
  const int next = (prev + 1) % n;

  if (prev >= 0) {
    StopCurrent(prev);
  } else {
    // Ensure current app screen is closed on first manual switch.
    LvglStopLuaAppDirScreen();
  }

  g_index = next;
  Show(g_index);
  if (g_timer) lv_timer_reset(g_timer);
}

void LvglReloadLuaAppCarousel() {
  if (g_timer) {
    lv_timer_del(g_timer);
    g_timer = nullptr;
  }

  const unsigned keep_interval = g_interval_ms < 1000 ? 10000 : g_interval_ms;
  ScanLittlefsApps();
  if (g_app_dirs.empty()) {
    ESP_LOGW(kTag, "reload skipped: no apps");
    ShowNoAppsHint();
    return;
  }

  g_index = 0;
  ESP_LOGI(kTag, "reload app carousel interval=%ums", keep_interval);
  Show(g_index);
  g_timer = lv_timer_create(TimerCb, keep_interval, nullptr);
}

void LvglLuaAppCarouselRequestNext() {
  unsigned prev = g_pending_next_requests.load(std::memory_order_relaxed);
  while (true) {
    if (prev >= 8) return;
    if (g_pending_next_requests.compare_exchange_weak(prev, prev + 1, std::memory_order_relaxed)) {
      return;
    }
  }
}

void LvglLuaAppCarouselPumpRequests() {
  const unsigned pending = g_pending_next_requests.exchange(0, std::memory_order_relaxed);
  for (unsigned i = 0; i < pending; ++i) {
    LvglLuaAppCarouselNext();
  }
}
