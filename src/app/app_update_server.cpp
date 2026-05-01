#include "app/app_update_server.h"

#include <cerrno>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <ctime>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <algorithm>

#include "cJSON.h"
#include "app/display_control.h"
#include "app/wifi_manager.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_littlefs.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#if defined(CONFIG_MBEDTLS_CERTIFICATE_BUNDLE)
#include "esp_crt_bundle.h"
#endif
#include "f_html_gz.h"
#include "favicon_ico_gz.h"
#include "my_debug.h"
#include "portal_html_gz.h"
#include "freertos/semphr.h"
#include "ui/lvgl_lua_app_screen.h"

static const char* kTag = "app_update";

namespace {

static httpd_handle_t g_httpd = nullptr;
static AppUpdateReloadCallback g_reload_cb = nullptr;
static AppUpdateSwitchCallback g_switch_cb = nullptr;
static std::string g_updating_session_app_id;
static std::string g_install_session_app_id;
static std::string g_install_session_stage_dir;
static bool g_install_session_switched_to_updating = false;
static const char* kStoreStableIndexUrl =
    "https://ota.geekmagic.cc/apps/pixel64x32V2/stable/apps-index.json";
static const char* kStoreBetaIndexUrl =
    "https://ota.geekmagic.cc/apps/pixel64x32V2/beta/apps-index.json";
static const char* kFirmwareStableManifestUrl =
    "https://ota.geekmagic.cc/firmware/pixel64x32V2/stable/firmware.json";
static const char* kFirmwareBetaManifestUrl =
    "https://ota.geekmagic.cc/firmware/pixel64x32V2/beta/firmware.json";
static const char* kStoreChannelKey = "store.channel";
static const char* kFirmwareAutoOtaKey = "firmware.auto_ota";
static const char* kLuaDataDir = "/littlefs/.sys";
static const char* kAppDataPath = "/littlefs/.sys/app_data.json";
static const char* kInstalledAppsIndexPath = "/littlefs/.sys/installed_apps.json";
static const char* kSchedulerConfigPath = "/littlefs/.sys/scheduler_config.json";
static constexpr size_t kLogRingSize = 160;
static constexpr size_t kLogTextBytes = 256;

struct CapturedLogEntry {
  uint32_t seq = 0;
  int64_t ms = 0;
  bool is_app = false;
  uint8_t level = 1;  // 0=debug,1=info,2=warn,3=error
  char text[kLogTextBytes] = {};
};

static portMUX_TYPE g_log_mux = portMUX_INITIALIZER_UNLOCKED;
static CapturedLogEntry g_log_ring[kLogRingSize] = {};
static size_t g_log_ring_head = 0;
static size_t g_log_ring_count = 0;
static uint32_t g_log_last_seq = 0;
static bool g_log_hook_installed = false;
static vprintf_like_t g_prev_log_vprintf = nullptr;
static SemaphoreHandle_t g_installed_apps_cache_mu = nullptr;
static bool g_installed_apps_cache_valid = false;
static std::string g_installed_apps_cache_body;
static portMUX_TYPE g_capture_mux = portMUX_INITIALIZER_UNLOCKED;
static bool g_capture_busy = false;
static QueueHandle_t g_trash_queue = nullptr;
static TaskHandle_t g_trash_task = nullptr;
static uint8_t g_logs_min_level = 1;  // info
static bool g_logs_detailed_default = false;
static TaskHandle_t g_scheduler_task = nullptr;
static TaskHandle_t g_firmware_ota_task = nullptr;
static constexpr int64_t kPowerSaverBootGraceSeconds = 60;
static constexpr int64_t kPowerSaverCountdownSeconds = 10;
static constexpr int kPowerSaverFadeSteps = 8;
static constexpr int64_t kPowerSaverFadeStepMs = 90;

struct FirmwareOtaState {
  bool auto_enabled = false;
  bool check_done = false;
  bool update_available = false;
  bool update_attempted = false;
  bool update_success = false;
  int64_t last_check_ms = 0;
  std::string remote_version;
  std::string remote_url;
  std::string last_error;
};
static SemaphoreHandle_t g_firmware_ota_mu = nullptr;
static FirmwareOtaState g_firmware_ota_state;

struct SchedulerState {
  bool enabled = false;
  bool running = false;
  std::string mode = "time";
  cJSON* config = nullptr;  // owned
  std::string last_app_id;
  int loop_index = 0;
  int64_t next_switch_unix = 0;
  bool screen_forced_off = false;
  uint8_t brightness_before_off = 64;
  int64_t manual_hold_until_unix = 0;
  bool power_saver_pending_off = false;
  int64_t power_saver_off_at_unix = 0;
  int last_power_saver_countdown_sec = -1;
};
static SemaphoreHandle_t g_scheduler_mu = nullptr;
static SchedulerState g_scheduler;

static bool EnsureDir(const char* path);
static bool ReadSmallFile(const std::string& path, size_t max_bytes, std::string* out);
static bool RemoveTree(const std::string& path);

static bool LooksLikeAppLog(const char* text) {
  if (!text || !*text) return false;
  return strstr(text, " app_runtime:") != nullptr || strstr(text, " app_screen:") != nullptr ||
         strstr(text, " app_carousel:") != nullptr || strstr(text, "[app]") != nullptr;
}

static void SanitizeLuaTokenInPlace(char* text) {
  if (!text || !*text) return;
  for (size_t i = 0; text[i]; ++i) {
    const unsigned char c0 = static_cast<unsigned char>(text[i]);
    const unsigned char c1 = static_cast<unsigned char>(text[i + 1]);
    if (!c1) continue;
    const unsigned char c2 = static_cast<unsigned char>(text[i + 2]);
    if (!c2) continue;
    if (tolower(c0) == 'l' && tolower(c1) == 'u' && tolower(c2) == 'a') {
      text[i] = '*';
      text[i + 1] = '*';
      text[i + 2] = '*';
      i += 2;
    }
  }
}

static uint8_t ParseLogLevelFromLine(const char* text) {
  if (!text || !text[0]) return 1;
  const char c0 = static_cast<char>(toupper(static_cast<unsigned char>(text[0])));
  if (c0 == 'E') return 3;
  if (c0 == 'W') return 2;
  if (c0 == 'I') return 1;
  if (c0 == 'D' || c0 == 'V') return 0;
  if (strstr(text, " E (")) return 3;
  if (strstr(text, " W (")) return 2;
  if (strstr(text, " I (")) return 1;
  if (strstr(text, " D (")) return 0;
  return 1;
}

static const char* LogLevelName(uint8_t level) {
  switch (level) {
    case 0:
      return "debug";
    case 1:
      return "info";
    case 2:
      return "warn";
    case 3:
      return "error";
    default:
      return "info";
  }
}

static bool ParseLogLevelParam(const std::string& value, uint8_t* out_level) {
  if (!out_level) return false;
  const std::string s = value;
  if (s == "debug" || s == "0") {
    *out_level = 0;
    return true;
  }
  if (s == "info" || s == "1") {
    *out_level = 1;
    return true;
  }
  if (s == "warn" || s == "warning" || s == "2") {
    *out_level = 2;
    return true;
  }
  if (s == "error" || s == "err" || s == "3") {
    *out_level = 3;
    return true;
  }
  return false;
}

static void CaptureLogLine(const char* text) {
  if (!text || !*text) return;

  char line[kLogTextBytes] = {};
  size_t len = strnlen(text, sizeof(line) - 1);
  while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r')) len--;
  if (len == 0) return;
  memcpy(line, text, len);
  line[len] = '\0';
  const bool is_app = LooksLikeAppLog(line);
  SanitizeLuaTokenInPlace(line);

  portENTER_CRITICAL(&g_log_mux);
  CapturedLogEntry& entry = g_log_ring[g_log_ring_head];
  entry.seq = ++g_log_last_seq;
  entry.ms = esp_timer_get_time() / 1000;
  entry.is_app = is_app;
  entry.level = ParseLogLevelFromLine(line);
  memcpy(entry.text, line, len + 1);
  g_log_ring_head = (g_log_ring_head + 1) % kLogRingSize;
  if (g_log_ring_count < kLogRingSize) g_log_ring_count++;
  portEXIT_CRITICAL(&g_log_mux);
}

static int CapturingVprintf(const char* fmt, va_list args) {
  va_list capture_args;
  va_copy(capture_args, args);
  char line[320] = {};
  (void)vsnprintf(line, sizeof(line), fmt, capture_args);
  va_end(capture_args);
  CaptureLogLine(line);
  return g_prev_log_vprintf ? g_prev_log_vprintf(fmt, args) : vprintf(fmt, args);
}

static void EnsureLogCaptureInstalled() {
  if (g_log_hook_installed) return;
  g_prev_log_vprintf = esp_log_set_vprintf(CapturingVprintf);
  g_log_hook_installed = true;
  CaptureLogLine("I (0) app_update: runtime log capture installed");
}

struct CapturedLogSnapshot {
  uint32_t seq = 0;
  int64_t ms = 0;
  bool is_app = false;
  uint8_t level = 1;
  std::string text;
};

static std::vector<CapturedLogSnapshot> SnapshotCapturedLogs(uint32_t after_seq, size_t limit, bool app_only,
                                                             uint8_t min_level) {
  std::vector<CapturedLogSnapshot> out;
  if (limit == 0) return out;

  // Keep large log copies off the HTTP handler stack; that stack is only 8 KB.
  std::vector<CapturedLogEntry> copied(kLogRingSize);
  size_t copied_count = 0;
  portENTER_CRITICAL(&g_log_mux);
  const size_t count = g_log_ring_count;
  const size_t start = (g_log_ring_head + kLogRingSize - count) % kLogRingSize;
  for (size_t i = 0; i < count; ++i) {
    const CapturedLogEntry& entry = g_log_ring[(start + i) % kLogRingSize];
    if (entry.seq <= after_seq) continue;
    if (app_only && !entry.is_app) continue;
    if (entry.level < min_level) continue;
    copied[copied_count++] = entry;
  }
  portEXIT_CRITICAL(&g_log_mux);

  out.reserve(copied_count);
  for (size_t i = 0; i < copied_count; ++i) {
    CapturedLogSnapshot snap = {};
    snap.seq = copied[i].seq;
    snap.ms = copied[i].ms;
    snap.is_app = copied[i].is_app;
    snap.level = copied[i].level;
    snap.text = copied[i].text;
    out.push_back(std::move(snap));
  }

  if (out.size() > limit) {
    out.erase(out.begin(), out.begin() + (out.size() - limit));
  }
  return out;
}

static uint32_t LatestCapturedLogSeq() {
  portENTER_CRITICAL(&g_log_mux);
  const uint32_t seq = g_log_last_seq;
  portEXIT_CRITICAL(&g_log_mux);
  return seq;
}

static void LockInstalledAppsCache() {
  if (g_installed_apps_cache_mu) (void)xSemaphoreTake(g_installed_apps_cache_mu, portMAX_DELAY);
}

static void UnlockInstalledAppsCache() {
  if (g_installed_apps_cache_mu) (void)xSemaphoreGive(g_installed_apps_cache_mu);
}

static void InvalidateInstalledAppsCache() {
  LockInstalledAppsCache();
  g_installed_apps_cache_valid = false;
  g_installed_apps_cache_body.clear();
  UnlockInstalledAppsCache();
}

static bool ReadInstalledAppsCache(std::string* out_body) {
  if (out_body) out_body->clear();
  if (!out_body) return false;
  LockInstalledAppsCache();
  const bool ok = g_installed_apps_cache_valid;
  if (ok) *out_body = g_installed_apps_cache_body;
  UnlockInstalledAppsCache();
  return ok;
}

static void WriteInstalledAppsCache(std::string body) {
  LockInstalledAppsCache();
  g_installed_apps_cache_body = std::move(body);
  g_installed_apps_cache_valid = true;
  UnlockInstalledAppsCache();
}

static bool SaveInstalledAppsIndexBody(const std::string& body) {
  if (body.empty()) return false;
  if (!EnsureDir(kLuaDataDir)) return false;
  const std::string tmp_path = std::string(kInstalledAppsIndexPath) + ".tmp";
  FILE* f = fopen(tmp_path.c_str(), "wb");
  if (!f) return false;
  const size_t need = body.size();
  const size_t wrote = fwrite(body.data(), 1, need, f);
  if (fclose(f) != 0 || wrote != need) {
    unlink(tmp_path.c_str());
    return false;
  }
  if (rename(tmp_path.c_str(), kInstalledAppsIndexPath) != 0) {
    unlink(tmp_path.c_str());
    return false;
  }
  return true;
}

static bool LoadInstalledAppsIndexBody(std::string* out_body) {
  if (out_body) out_body->clear();
  if (!out_body) return false;
  std::string body;
  if (!ReadSmallFile(kInstalledAppsIndexPath, 64 * 1024, &body) || body.empty()) return false;
  cJSON* root = cJSON_ParseWithLength(body.c_str(), body.size());
  if (!root) return false;
  const cJSON* apps = cJSON_GetObjectItemCaseSensitive(root, "apps");
  const bool ok = cJSON_IsArray(apps);
  cJSON_Delete(root);
  if (!ok) return false;
  *out_body = std::move(body);
  return true;
}

static bool EnsureDir(const char* path) {
  if (!path || !*path) return false;
  if (mkdir(path, 0755) == 0) return true;
  if (errno == EEXIST) return true;
  ESP_LOGE(kTag, "mkdir failed: %s errno=%d", path, errno);
  return false;
}

static bool IsValidAppId(const std::string& app_id) {
  if (app_id.empty() || app_id.size() > 48) return false;
  for (char ch : app_id) {
    const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                    ch == '_' || ch == '-';
    if (!ok) return false;
  }
  return true;
}

static bool IsAllowedFilename(const std::string& filename) {
  if (filename.empty() || filename.size() > 96) return false;
  if (filename == "." || filename == "..") return false;

  for (char ch : filename) {
    const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                    ch == '_' || ch == '-' || ch == '.' || ch == '/';
    if (!ok) return false;
  }
  if (filename.find("..") != std::string::npos) return false;
  if (filename.front() == '/' || filename.back() == '/') return false;

  return true;
}

static std::string AppInstallStagingDir(const std::string& app_id) {
  return std::string("/littlefs/.staging/apps/") + app_id;
}

static bool EnsureParentDirsForFile(const std::string& base_dir, const std::string& filename) {
  size_t pos = 0;
  while (true) {
    pos = filename.find('/', pos);
    if (pos == std::string::npos) break;
    const std::string sub = filename.substr(0, pos);
    if (!sub.empty()) {
      const std::string sub_dir = base_dir + "/" + sub;
      if (!EnsureDir(sub_dir.c_str())) return false;
    }
    pos++;
  }
  return true;
}

static bool ValidateStagedApp(const std::string& stage_dir, const std::string& app_id, std::string* out_err) {
  if (out_err) out_err->clear();
  std::string manifest;
  if (!ReadSmallFile(stage_dir + "/manifest.json", 8 * 1024, &manifest)) {
    if (out_err) *out_err = "missing manifest.json";
    return false;
  }
  cJSON* root = cJSON_ParseWithLength(manifest.c_str(), manifest.size());
  if (!root || !cJSON_IsObject(root)) {
    if (root) cJSON_Delete(root);
    if (out_err) *out_err = "invalid manifest.json";
    return false;
  }
  const cJSON* app_id_node = cJSON_GetObjectItemCaseSensitive(root, "app_id");
  const cJSON* entry_node = cJSON_GetObjectItemCaseSensitive(root, "entry");
  const bool ok_app_id = cJSON_IsString(app_id_node) && app_id_node->valuestring &&
                         app_id == std::string(app_id_node->valuestring);
  const bool ok_entry = cJSON_IsString(entry_node) && entry_node->valuestring && entry_node->valuestring[0] != '\0';
  std::string entry = ok_entry ? entry_node->valuestring : "";
  cJSON_Delete(root);
  if (!ok_app_id) {
    if (out_err) *out_err = "manifest app_id mismatch";
    return false;
  }
  if (!ok_entry || !IsAllowedFilename(entry) || entry.find("..") != std::string::npos || entry.front() == '/') {
    if (out_err) *out_err = "invalid manifest entry";
    return false;
  }
  struct stat st = {};
  if (stat((stage_dir + "/" + entry).c_str(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) {
    if (out_err) *out_err = "missing or empty entry file";
    return false;
  }
  return true;
}

static void SendJson(httpd_req_t* req, const char* status, const char* json) {
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_status(req, status);
  httpd_resp_set_hdr(req, "Connection", "close");
  httpd_resp_sendstr(req, json);
}

static cJSON* LoadLuaDataRootForHttp() {
  std::string text;
  if (!ReadSmallFile(kAppDataPath, 16 * 1024, &text) || text.empty()) {
    return cJSON_CreateObject();
  }
  cJSON* root = cJSON_ParseWithLength(text.c_str(), text.size());
  if (!root) return cJSON_CreateObject();
  if (!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    return cJSON_CreateObject();
  }
  return root;
}

static bool SaveLuaDataRootForHttp(cJSON* root, std::string* out_err) {
  if (out_err) out_err->clear();
  if (!root || !cJSON_IsObject(root)) {
    if (out_err) *out_err = "invalid root";
    return false;
  }
  if (!EnsureDir(kLuaDataDir)) {
    if (out_err) *out_err = "mkdir failed";
    return false;
  }
  char* rendered = cJSON_PrintUnformatted(root);
  if (!rendered) {
    if (out_err) *out_err = "json render failed";
    return false;
  }
  const std::string text(rendered);
  cJSON_free(rendered);
  const std::string tmp_path = std::string(kAppDataPath) + ".tmp";
  FILE* f = fopen(tmp_path.c_str(), "wb");
  if (!f) {
    if (out_err) *out_err = "open tmp failed";
    return false;
  }
  const size_t need = text.size();
  const size_t wrote = fwrite(text.data(), 1, need, f);
  if (fclose(f) != 0 || wrote != need) {
    unlink(tmp_path.c_str());
    if (out_err) *out_err = "write tmp failed";
    return false;
  }

  if (rename(tmp_path.c_str(), kAppDataPath) == 0) return true;
  const int rename_errno_1 = errno;

  bool replaced = false;
  if (unlink(kAppDataPath) == 0 || errno == ENOENT) {
    if (rename(tmp_path.c_str(), kAppDataPath) == 0) {
      replaced = true;
    }
  }
  const int replace_errno = errno;
  if (replaced) return true;

  // Fallback: rewrite in place when LittleFS blocks unlink/rename due open FD.
  FILE* wf = fopen(kAppDataPath, "wb");
  if (wf) {
    const size_t direct_wrote = fwrite(text.data(), 1, text.size(), wf);
    const bool direct_ok = (fclose(wf) == 0) && (direct_wrote == text.size());
    unlink(tmp_path.c_str());
    if (direct_ok) return true;
    if (out_err) *out_err = "direct write failed";
    return false;
  }

  unlink(tmp_path.c_str());
  if (out_err) {
    *out_err = std::string("rename failed errno=") + std::to_string(rename_errno_1) +
               ", replace errno=" + std::to_string(replace_errno) +
               ", open target failed errno=" + std::to_string(errno);
  }
  return false;
}

static bool IsValidLuaDataKey(const std::string& key) {
  if (key.empty() || key.size() > 96) return false;
  for (char ch : key) {
    const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                    ch == '_' || ch == '-' || ch == '.';
    if (!ok) return false;
  }
  return true;
}

static bool AppInstalled(const std::string& app_id) {
  if (!IsValidAppId(app_id)) return false;
  struct stat st = {};
  const std::string path = std::string("/littlefs/apps/") + app_id;
  return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static bool SaveTextFileAtomic(const char* path, const std::string& text) {
  const std::string tmp = std::string(path) + ".tmp";
  FILE* f = fopen(tmp.c_str(), "wb");
  if (!f) return false;
  const size_t wrote = fwrite(text.data(), 1, text.size(), f);
  if (fclose(f) != 0 || wrote != text.size()) {
    unlink(tmp.c_str());
    return false;
  }
  if (rename(tmp.c_str(), path) == 0) return true;
  (void)unlink(path);
  if (rename(tmp.c_str(), path) == 0) return true;
  unlink(tmp.c_str());
  return false;
}

static std::string NormalizeStoreChannel(const std::string& value) {
  std::string s;
  s.reserve(value.size());
  for (char ch : value) {
    if (ch == '\r' || ch == '\n' || ch == '\t') continue;
    s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  }
  size_t start = 0;
  while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
  size_t end = s.size();
  while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
  const std::string trimmed = s.substr(start, end - start);
  return trimmed == "beta" ? "beta" : "stable";
}

static std::string GetStoreChannel() {
  cJSON* root = LoadLuaDataRootForHttp();
  if (!root) return "stable";
  const cJSON* v = cJSON_GetObjectItemCaseSensitive(root, kStoreChannelKey);
  const char* s = cJSON_IsString(v) ? v->valuestring : nullptr;
  const std::string channel = NormalizeStoreChannel(s ? s : "stable");
  cJSON_Delete(root);
  return channel;
}

static bool SetStoreChannel(const std::string& channel, std::string* out_err) {
  const std::string normalized = NormalizeStoreChannel(channel);
  cJSON* root = LoadLuaDataRootForHttp();
  if (!root) {
    if (out_err) *out_err = "load app data failed";
    return false;
  }
  cJSON_DeleteItemFromObjectCaseSensitive(root, kStoreChannelKey);
  cJSON_AddStringToObject(root, kStoreChannelKey, normalized.c_str());
  const bool ok = SaveLuaDataRootForHttp(root, out_err);
  cJSON_Delete(root);
  return ok;
}

static bool GetFirmwareAutoOtaEnabled() {
  cJSON* root = LoadLuaDataRootForHttp();
  if (!root) return false;
  const cJSON* v = cJSON_GetObjectItemCaseSensitive(root, kFirmwareAutoOtaKey);
  const bool enabled = cJSON_IsBool(v) && cJSON_IsTrue(v);
  cJSON_Delete(root);
  return enabled;
}

static int CompareVersionLoose(const std::string& lhs, const std::string& rhs) {
  auto parse = [](const std::string& s) -> std::vector<int> {
    std::vector<int> out;
    std::string token;
    for (char ch : s) {
      if (ch >= '0' && ch <= '9') {
        token.push_back(ch);
      } else {
        if (!token.empty()) {
          out.push_back(atoi(token.c_str()));
          token.clear();
        }
      }
    }
    if (!token.empty()) out.push_back(atoi(token.c_str()));
    return out;
  };
  const std::vector<int> a = parse(lhs);
  const std::vector<int> b = parse(rhs);
  const size_t n = a.size() > b.size() ? a.size() : b.size();
  for (size_t i = 0; i < n; ++i) {
    const int av = i < a.size() ? a[i] : 0;
    const int bv = i < b.size() ? b[i] : 0;
    if (av > bv) return 1;
    if (av < bv) return -1;
  }
  return 0;
}

static void SetFirmwareOtaState(const FirmwareOtaState& next) {
  if (!g_firmware_ota_mu) return;
  xSemaphoreTake(g_firmware_ota_mu, portMAX_DELAY);
  g_firmware_ota_state = next;
  xSemaphoreGive(g_firmware_ota_mu);
}

static FirmwareOtaState GetFirmwareOtaState() {
  FirmwareOtaState out;
  if (!g_firmware_ota_mu) return out;
  xSemaphoreTake(g_firmware_ota_mu, portMAX_DELAY);
  out = g_firmware_ota_state;
  xSemaphoreGive(g_firmware_ota_mu);
  return out;
}

static std::string GetFirmwareManifestUrlByChannel(const std::string& channel) {
  return channel == "beta" ? kFirmwareBetaManifestUrl : kFirmwareStableManifestUrl;
}

static void ApplyTlsDefaults(esp_http_client_config_t* cfg) {
  if (!cfg) return;
#if defined(CONFIG_MBEDTLS_CERTIFICATE_BUNDLE)
  cfg->crt_bundle_attach = esp_crt_bundle_attach;
#endif
}

static bool HttpGetSmallBody(const std::string& url, size_t max_bytes, std::string* out_body, std::string* out_err) {
  if (out_body) out_body->clear();
  if (out_err) out_err->clear();
  if (!out_body) return false;

  esp_http_client_config_t cfg = {};
  cfg.url = url.c_str();
  cfg.method = HTTP_METHOD_GET;
  cfg.timeout_ms = 10000;
  cfg.disable_auto_redirect = false;
  ApplyTlsDefaults(&cfg);
  esp_http_client_handle_t cli = esp_http_client_init(&cfg);
  if (!cli) {
    if (out_err) *out_err = "http init failed";
    return false;
  }
  bool ok = false;
  do {
    esp_err_t err = esp_http_client_open(cli, 0);
    if (err != ESP_OK) {
      if (out_err) *out_err = "http open failed";
      break;
    }
    const int status = esp_http_client_fetch_headers(cli);
    if (status < 0) {
      if (out_err) *out_err = "http fetch headers failed";
      break;
    }
    const int code = esp_http_client_get_status_code(cli);
    if (code < 200 || code >= 300) {
      if (out_err) *out_err = "http status " + std::to_string(code);
      break;
    }
    char buf[768];
    std::string body;
    while (true) {
      const int r = esp_http_client_read(cli, buf, sizeof(buf));
      if (r < 0) {
        if (out_err) *out_err = "http read failed";
        body.clear();
        break;
      }
      if (r == 0) break;
      if (body.size() + static_cast<size_t>(r) > max_bytes) {
        if (out_err) *out_err = "response too large";
        body.clear();
        break;
      }
      body.append(buf, static_cast<size_t>(r));
    }
    if (body.empty()) {
      if (out_err && out_err->empty()) *out_err = "empty body";
      break;
    }
    *out_body = std::move(body);
    ok = true;
  } while (false);
  esp_http_client_close(cli);
  esp_http_client_cleanup(cli);
  return ok;
}

static bool ParseFirmwareManifest(const std::string& body, std::string* out_version, std::string* out_url,
                                  size_t* out_size, std::string* out_err) {
  if (out_version) out_version->clear();
  if (out_url) out_url->clear();
  if (out_size) *out_size = 0;
  if (out_err) out_err->clear();
  cJSON* root = cJSON_ParseWithLength(body.c_str(), body.size());
  if (!root || !cJSON_IsObject(root)) {
    if (root) cJSON_Delete(root);
    if (out_err) *out_err = "invalid manifest json";
    return false;
  }
  const cJSON* v = cJSON_GetObjectItemCaseSensitive(root, "version");
  const cJSON* u = cJSON_GetObjectItemCaseSensitive(root, "url");
  const cJSON* s = cJSON_GetObjectItemCaseSensitive(root, "size");
  if (!cJSON_IsString(v) || !v->valuestring || !v->valuestring[0] ||
      !cJSON_IsString(u) || !u->valuestring || !u->valuestring[0]) {
    cJSON_Delete(root);
    if (out_err) *out_err = "manifest missing version/url";
    return false;
  }
  if (out_version) *out_version = v->valuestring;
  if (out_url) *out_url = u->valuestring;
  if (out_size && cJSON_IsNumber(s) && s->valuedouble > 0) {
    *out_size = static_cast<size_t>(s->valuedouble);
  }
  cJSON_Delete(root);
  return true;
}

static void FirmwareAutoOtaTask(void* arg) {
  (void)arg;
  vTaskDelay(pdMS_TO_TICKS(3500));

  FirmwareOtaState st = GetFirmwareOtaState();
  st.auto_enabled = GetFirmwareAutoOtaEnabled();
  st.check_done = false;
  st.update_available = false;
  st.update_attempted = false;
  st.update_success = false;
  st.last_error.clear();
  st.remote_url.clear();
  st.remote_version.clear();
  SetFirmwareOtaState(st);

  WifiStatusInfo wifi = {};
  bool connected = false;
  for (int i = 0; i < 45; ++i) {
    if (WifiManagerGetStatus(&wifi) && wifi.sta_connected) {
      connected = true;
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
  st = GetFirmwareOtaState();
  st.last_check_ms = esp_timer_get_time() / 1000;
  if (!connected) {
    st.check_done = true;
    st.last_error = "wifi not connected";
    SetFirmwareOtaState(st);
    g_firmware_ota_task = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  const std::string channel = GetStoreChannel();
  const std::string manifest_url = GetFirmwareManifestUrlByChannel(channel);
  std::string manifest_body;
  std::string err;
  if (!HttpGetSmallBody(manifest_url, 8 * 1024, &manifest_body, &err)) {
    st.check_done = true;
    st.last_error = err.empty() ? "manifest fetch failed" : err;
    SetFirmwareOtaState(st);
    g_firmware_ota_task = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  std::string remote_version;
  std::string remote_url;
  size_t remote_size = 0;
  if (!ParseFirmwareManifest(manifest_body, &remote_version, &remote_url, &remote_size, &err)) {
    st.check_done = true;
    st.last_error = err.empty() ? "manifest parse failed" : err;
    SetFirmwareOtaState(st);
    g_firmware_ota_task = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  const esp_app_desc_t* desc = esp_app_get_description();
  const char* fw_version = SW_VERSION;
  if (!fw_version || !fw_version[0]) fw_version = desc ? desc->version : "0.0.0";
  const bool update_available = CompareVersionLoose(remote_version, fw_version) > 0;
  st.remote_version = remote_version;
  st.remote_url = remote_url;
  st.update_available = update_available;
  st.check_done = true;
  SetFirmwareOtaState(st);

  if (!st.auto_enabled || !update_available) {
    g_firmware_ota_task = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  st.update_attempted = true;
  SetFirmwareOtaState(st);
  ESP_LOGI(kTag, "auto OTA start: channel=%s current=%s target=%s", channel.c_str(), fw_version, remote_version.c_str());
  LvglOtaOverlayBegin(remote_size);
  esp_http_client_config_t http_cfg = {};
  http_cfg.url = remote_url.c_str();
  http_cfg.timeout_ms = 15000;
  http_cfg.keep_alive_enable = true;
  ApplyTlsDefaults(&http_cfg);
  esp_https_ota_config_t ota_cfg = {};
  ota_cfg.http_config = &http_cfg;

  esp_https_ota_handle_t ota_handle = nullptr;
  esp_err_t ota_ret = esp_https_ota_begin(&ota_cfg, &ota_handle);
  if (ota_ret == ESP_OK) {
    while (true) {
      ota_ret = esp_https_ota_perform(ota_handle);
      if (ota_ret != ESP_ERR_HTTPS_OTA_IN_PROGRESS) break;
      const int read_len = esp_https_ota_get_image_len_read(ota_handle);
      const size_t written = read_len > 0 ? static_cast<size_t>(read_len) : 0U;
      const size_t total_for_ui = remote_size > 0 ? remote_size : (written > 0 ? written : 1U);
      LvglOtaOverlayUpdate(written, total_for_ui);
    }
    if (ota_ret == ESP_OK) {
      ota_ret = esp_https_ota_finish(ota_handle);
      ota_handle = nullptr;
    } else {
      esp_https_ota_abort(ota_handle);
      ota_handle = nullptr;
    }
  }

  if (ota_ret == ESP_OK) {
    st.update_success = true;
    st.last_error.clear();
    SetFirmwareOtaState(st);
    LvglOtaOverlayFinalizing();
    ESP_LOGI(kTag, "auto OTA success, rebooting");
    vTaskDelay(pdMS_TO_TICKS(350));
    esp_restart();
  } else {
    st.update_success = false;
    st.last_error = std::string("ota failed: ") + esp_err_to_name(ota_ret);
    SetFirmwareOtaState(st);
    LvglOtaOverlayFail();
    ESP_LOGE(kTag, "auto OTA failed: %s", esp_err_to_name(ota_ret));
  }

  g_firmware_ota_task = nullptr;
  vTaskDelete(nullptr);
}

static bool ParseHm(const cJSON* it, const char* key, int* out_mins) {
  const cJSON* v = cJSON_GetObjectItemCaseSensitive(it, key);
  if (!cJSON_IsString(v) || !v->valuestring) return false;
  int h = -1, m = -1;
  if (sscanf(v->valuestring, "%d:%d", &h, &m) != 2) return false;
  if (h < 0 || h > 23 || m < 0 || m > 59) return false;
  *out_mins = h * 60 + m;
  return true;
}

static bool RuleMatchesWeekday(const cJSON* rule, int weekday_mon0) {
  const cJSON* repeat = cJSON_GetObjectItemCaseSensitive(rule, "repeat");
  const char* rep = (cJSON_IsString(repeat) && repeat->valuestring) ? repeat->valuestring : "everyday";
  if (strcmp(rep, "everyday") == 0) return true;
  if (strcmp(rep, "weekdays") == 0) return weekday_mon0 >= 0 && weekday_mon0 <= 4;
  if (strcmp(rep, "weekends") == 0) return weekday_mon0 == 5 || weekday_mon0 == 6;
  return false;
}

static bool ValidateSchedulerConfig(cJSON* root, std::string* out_err) {
  if (!cJSON_IsObject(root)) { *out_err = "config must be object"; return false; }
  cJSON* mode = cJSON_GetObjectItemCaseSensitive(root, "mode");
  if (!cJSON_IsString(mode) || !mode->valuestring) { *out_err = "mode required"; return false; }
  const std::string mode_s(mode->valuestring);
  if (mode_s != "time" && mode_s != "loop") { *out_err = "invalid mode"; return false; }
  cJSON* en = cJSON_GetObjectItemCaseSensitive(root, "enabled");
  if (en && !cJSON_IsBool(en)) { *out_err = "enabled must be bool"; return false; }
  cJSON* ps_en = cJSON_GetObjectItemCaseSensitive(root, "powerSaverEnabled");
  if (ps_en && !cJSON_IsBool(ps_en)) { *out_err = "powerSaverEnabled must be bool"; return false; }
  const cJSON* off_start = cJSON_GetObjectItemCaseSensitive(root, "screenOffStart");
  const cJSON* on_start = cJSON_GetObjectItemCaseSensitive(root, "screenOnStart");
  if (off_start && !cJSON_IsString(off_start)) { *out_err = "screenOffStart must be string"; return false; }
  if (on_start && !cJSON_IsString(on_start)) { *out_err = "screenOnStart must be string"; return false; }
  const bool power_saver_enabled = cJSON_IsBool(ps_en) && cJSON_IsTrue(ps_en);
  if (power_saver_enabled && (off_start || on_start)) {
    int off_m = 0, on_m = 0;
    cJSON* tmp = cJSON_CreateObject();
    if (off_start && off_start->valuestring) cJSON_AddStringToObject(tmp, "s", off_start->valuestring);
    else cJSON_AddStringToObject(tmp, "s", "23:00");
    if (on_start && on_start->valuestring) cJSON_AddStringToObject(tmp, "e", on_start->valuestring);
    else cJSON_AddStringToObject(tmp, "e", "07:00");
    const bool ok = ParseHm(tmp, "s", &off_m) && ParseHm(tmp, "e", &on_m) && off_m != on_m;
    cJSON_Delete(tmp);
    if (!ok) { *out_err = "invalid power saver time"; return false; }
  }
  cJSON* time_rules = cJSON_GetObjectItemCaseSensitive(root, "timeItems");
  cJSON* loop_rules = cJSON_GetObjectItemCaseSensitive(root, "loopItems");
  cJSON* fallback_app = cJSON_GetObjectItemCaseSensitive(root, "fallbackAppId");
  if (fallback_app && !cJSON_IsString(fallback_app)) { *out_err = "fallbackAppId must be string"; return false; }
  if (fallback_app && fallback_app->valuestring && fallback_app->valuestring[0]) {
    if (!AppInstalled(fallback_app->valuestring)) { *out_err = "invalid fallbackAppId"; return false; }
  }
  if (time_rules && !cJSON_IsArray(time_rules)) { *out_err = "timeItems must be array"; return false; }
  if (loop_rules && !cJSON_IsArray(loop_rules)) { *out_err = "loopItems must be array"; return false; }
  if (time_rules && cJSON_GetArraySize(time_rules) > 32) { *out_err = "too many time rules"; return false; }
  if (loop_rules && cJSON_GetArraySize(loop_rules) > 32) { *out_err = "too many loop rules"; return false; }
  if (time_rules) {
    std::vector<std::pair<int, int>> day_ranges[7];
    cJSON* it = nullptr;
    cJSON_ArrayForEach(it, time_rules) {
      if (!cJSON_IsObject(it)) { *out_err = "invalid time rule"; return false; }
      int sm = 0, em = 0;
      if (!ParseHm(it, "start", &sm) || !ParseHm(it, "end", &em)) { *out_err = "invalid time"; return false; }
      const cJSON* app_id = cJSON_GetObjectItemCaseSensitive(it, "appId");
      if (!cJSON_IsString(app_id) || !app_id->valuestring || !AppInstalled(app_id->valuestring)) { *out_err = "invalid appId"; return false; }
      const cJSON* repeat = cJSON_GetObjectItemCaseSensitive(it, "repeat");
      const char* rep = (cJSON_IsString(repeat) && repeat->valuestring) ? repeat->valuestring : "everyday";
      const bool rep_ok = strcmp(rep, "everyday") == 0 || strcmp(rep, "weekdays") == 0 ||
                          strcmp(rep, "weekends") == 0;
      if (!rep_ok) { *out_err = "invalid repeat"; return false; }

      bool active_days[7] = {false, false, false, false, false, false, false};
      if (strcmp(rep, "everyday") == 0) {
        for (int i = 0; i < 7; ++i) active_days[i] = true;
      } else if (strcmp(rep, "weekdays") == 0) {
        for (int i = 0; i < 5; ++i) active_days[i] = true;
      } else if (strcmp(rep, "weekends") == 0) {
        active_days[5] = true;
        active_days[6] = true;
      }
      for (int d = 0; d < 7; ++d) {
        if (!active_days[d]) continue;
        if (sm <= em) {
          day_ranges[d].push_back({sm, em});
        } else {
          day_ranges[d].push_back({sm, 1440});
          day_ranges[d].push_back({0, em});
        }
      }
    }

    for (int d = 0; d < 7; ++d) {
      auto& arr = day_ranges[d];
      std::sort(arr.begin(), arr.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
      for (size_t i = 1; i < arr.size(); ++i) {
        if (arr[i].first < arr[i - 1].second) {
          *out_err = "overlapping time rules";
          return false;
        }
      }
    }
  }
  if (loop_rules) {
    cJSON* it = nullptr;
    cJSON_ArrayForEach(it, loop_rules) {
      if (!cJSON_IsObject(it)) { *out_err = "invalid loop rule"; return false; }
      const cJSON* app_id = cJSON_GetObjectItemCaseSensitive(it, "appId");
      if (!cJSON_IsString(app_id) || !app_id->valuestring || !AppInstalled(app_id->valuestring)) { *out_err = "invalid appId"; return false; }
      const cJSON* dur = cJSON_GetObjectItemCaseSensitive(it, "duration");
      if (!cJSON_IsNumber(dur) || dur->valuedouble < 1 || dur->valuedouble > 3600) { *out_err = "invalid duration"; return false; }
    }
  }
  return true;
}

static cJSON* DefaultSchedulerConfig() {
  cJSON* root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "enabled", false);
  cJSON_AddStringToObject(root, "mode", "time");
  cJSON_AddBoolToObject(root, "powerSaverEnabled", false);
  cJSON_AddStringToObject(root, "fallbackAppId", "");
  cJSON_AddStringToObject(root, "screenOffStart", "23:00");
  cJSON_AddStringToObject(root, "screenOnStart", "07:00");
  cJSON_AddArrayToObject(root, "timeItems");
  cJSON_AddArrayToObject(root, "loopItems");
  return root;
}

static cJSON* LoadSchedulerConfig() {
  std::string text;
  if (!ReadSmallFile(kSchedulerConfigPath, 32 * 1024, &text) || text.empty()) return DefaultSchedulerConfig();
  cJSON* root = cJSON_ParseWithLength(text.c_str(), text.size());
  if (!root) return DefaultSchedulerConfig();
  std::string err;
  if (!ValidateSchedulerConfig(root, &err)) {
    cJSON_Delete(root);
    return DefaultSchedulerConfig();
  }
  return root;
}

static void SchedulerSwitchApp(const std::string& app_id) {
  if (!g_switch_cb || app_id.empty()) return;
  if (g_scheduler.last_app_id == app_id) return;
  g_switch_cb(app_id.c_str(), static_cast<unsigned>(app_id.size()));
  g_scheduler.last_app_id = app_id;
}

static std::string FindAnyInstalledAppId() {
  DIR* dir = opendir("/littlefs/apps");
  if (!dir) return {};
  struct dirent* ent = nullptr;
  while ((ent = readdir(dir)) != nullptr) {
    if (ent->d_name[0] == '.') continue;
    const std::string id = ent->d_name;
    if (!IsValidAppId(id)) continue;
    const std::string app_bin = std::string("/littlefs/apps/") + id + "/app.bin";
    struct stat st = {};
    if (stat(app_bin.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
      closedir(dir);
      return id;
    }
  }
  closedir(dir);
  return {};
}

static std::string UriWithoutQuery(const char* uri) {
  if (!uri) return {};
  const char* q = strchr(uri, '?');
  if (!q) return std::string(uri);
  return std::string(uri, static_cast<size_t>(q - uri));
}

static bool ReadQueryParam(httpd_req_t* req, const char* key, std::string* out_value) {
  if (out_value) out_value->clear();
  if (!req || !key || !*key || !out_value) return false;

  const size_t qlen = httpd_req_get_url_query_len(req);
  if (qlen == 0) return false;

  std::string query;
  query.resize(qlen + 1, '\0');
  if (httpd_req_get_url_query_str(req, query.data(), query.size()) != ESP_OK) return false;

  char buf[64] = {};
  if (httpd_query_key_value(query.c_str(), key, buf, sizeof(buf)) != ESP_OK) return false;
  *out_value = buf;
  return true;
}

static bool ParsePutUri(const char* uri, std::string* out_app_id, std::string* out_filename) {
  if (out_app_id) out_app_id->clear();
  if (out_filename) out_filename->clear();
  if (!uri || !out_app_id || !out_filename) return false;

  const std::string raw = UriWithoutQuery(uri);
  static const std::string kPrefix = "/api/apps/";
  if (raw.rfind(kPrefix, 0) != 0) return false;

  const std::string tail = raw.substr(kPrefix.size());
  const size_t slash = tail.find('/');
  if (slash == std::string::npos || slash == 0 || slash + 1 >= tail.size()) return false;

  const std::string app_id = tail.substr(0, slash);
  const std::string filename = tail.substr(slash + 1);  // may include subdirs, e.g. icons/btc-24.png
  if (!IsValidAppId(app_id)) return false;
  if (!IsAllowedFilename(filename)) return false;

  *out_app_id = app_id;
  *out_filename = filename;
  return true;
}

static bool ParseAppWebUri(const char* uri, std::string* out_app_id, std::string* out_filename) {
  if (out_app_id) out_app_id->clear();
  if (out_filename) out_filename->clear();
  if (!uri || !out_app_id || !out_filename) return false;

  const std::string raw = UriWithoutQuery(uri);
  static const std::string kPrefix = "/api/apps/web/";
  if (raw.rfind(kPrefix, 0) != 0) return false;

  const std::string tail = raw.substr(kPrefix.size());
  const size_t slash = tail.find('/');
  if (slash == std::string::npos || slash == 0 || slash + 1 >= tail.size()) return false;

  const std::string app_id = tail.substr(0, slash);
  const std::string filename = tail.substr(slash + 1);
  if (!IsValidAppId(app_id)) return false;
  if (!IsAllowedFilename(filename)) return false;

  *out_app_id = app_id;
  *out_filename = filename;
  return true;
}

static bool ParseDeleteAppUri(const char* uri, std::string* out_app_id) {
  if (out_app_id) out_app_id->clear();
  if (!uri || !out_app_id) return false;

  const std::string raw = UriWithoutQuery(uri);
  static const std::string kPrefix = "/api/apps/";
  if (raw.rfind(kPrefix, 0) != 0) return false;

  const std::string tail = raw.substr(kPrefix.size());
  if (tail.empty()) return false;
  if (tail.find('/') != std::string::npos) return false;  // app-level only
  if (!IsValidAppId(tail)) return false;

  *out_app_id = tail;
  return true;
}

static bool ParseSwitchUri(const char* uri, std::string* out_app_id) {
  if (out_app_id) out_app_id->clear();
  if (!uri || !out_app_id) return false;

  const std::string raw = UriWithoutQuery(uri);
  static const std::string kPrefix = "/api/apps/switch/";
  if (raw.rfind(kPrefix, 0) != 0) return false;

  const std::string app_id = raw.substr(kPrefix.size());
  if (!IsValidAppId(app_id)) return false;

  *out_app_id = app_id;
  return true;
}

static bool ParseSwitchQuery(httpd_req_t* req, std::string* out_app_id) {
  if (out_app_id) out_app_id->clear();
  if (!req || !out_app_id) return false;

  const size_t qlen = httpd_req_get_url_query_len(req);
  if (qlen == 0) return false;

  std::string query;
  query.resize(qlen + 1, '\0');
  if (httpd_req_get_url_query_str(req, query.data(), query.size()) != ESP_OK) return false;

  char app_id[64] = {};
  if (httpd_query_key_value(query.c_str(), "app_id", app_id, sizeof(app_id)) != ESP_OK) return false;
  if (!IsValidAppId(app_id)) return false;

  *out_app_id = app_id;
  return true;
}

static bool WriteRequestBodyToFile(httpd_req_t* req, const char* path, int* out_bytes) {
  if (out_bytes) *out_bytes = 0;
  if (!req || !path) return false;

  FILE* f = fopen(path, "wb");
  if (!f) {
    ESP_LOGE(kTag, "fopen failed: %s", path);
    return false;
  }

  int remaining = req->content_len;
  int written = 0;
  char buf[512];
  while (remaining > 0) {
    const int to_read = remaining < static_cast<int>(sizeof(buf)) ? remaining : static_cast<int>(sizeof(buf));
    const int r = httpd_req_recv(req, buf, to_read);
    if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (r <= 0) {
      fclose(f);
      return false;
    }

    const size_t w = fwrite(buf, 1, static_cast<size_t>(r), f);
    if (w != static_cast<size_t>(r)) {
      fclose(f);
      return false;
    }

    remaining -= r;
    written += r;
  }

  fclose(f);
  if (out_bytes) *out_bytes = written;
  return true;
}

static bool ReadRequestBodyToString(httpd_req_t* req, size_t max_bytes, std::string* out) {
  if (out) out->clear();
  if (!req || !out || max_bytes == 0) return false;
  if (static_cast<size_t>(req->content_len) > max_bytes) return false;

  out->reserve(static_cast<size_t>(req->content_len));
  int remaining = req->content_len;
  char buf[256];
  while (remaining > 0) {
    const int to_read = remaining < static_cast<int>(sizeof(buf)) ? remaining : static_cast<int>(sizeof(buf));
    const int r = httpd_req_recv(req, buf, to_read);
    if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (r <= 0) return false;
    out->append(buf, static_cast<size_t>(r));
    remaining -= r;
  }
  return true;
}

static bool RemoveTree(const std::string& path) {
  DIR* dir = opendir(path.c_str());
  if (!dir) return false;

  bool ok = true;
  while (true) {
    struct dirent* ent = readdir(dir);
    if (!ent) break;
    const char* name = ent->d_name;
    if (!name) continue;
    if ((strcmp(name, ".") == 0) || (strcmp(name, "..") == 0)) continue;

    const std::string child = path + "/" + name;
    struct stat st = {};
    if (stat(child.c_str(), &st) != 0) {
      ok = false;
      continue;
    }

    if (S_ISDIR(st.st_mode)) {
      if (!RemoveTree(child)) ok = false;
      if (rmdir(child.c_str()) != 0) ok = false;
    } else {
      if (unlink(child.c_str()) != 0) ok = false;
    }
  }
  closedir(dir);
  return ok;
}

static bool MoveToTrash(const std::string& app_dir, const std::string& app_id) {
  const std::string trash_root = "/littlefs/.trash";
  if (!EnsureDir(trash_root.c_str())) return false;

  char stamp_buf[24] = {};
  const int64_t now_ms = esp_timer_get_time() / 1000;
  snprintf(stamp_buf, sizeof(stamp_buf), "%lld", static_cast<long long>(now_ms));
  const std::string trash_dir = trash_root + "/" + app_id + "-" + stamp_buf;

  if (rename(app_dir.c_str(), trash_dir.c_str()) == 0) {
    if (g_trash_queue) {
      std::string* payload = new std::string(trash_dir);
      if (xQueueSend(g_trash_queue, &payload, 0) != pdTRUE) {
        delete payload;
      }
    }
    return true;
  }

  if (errno == ENOENT) return true;
  return false;
}

static void TrashCleanerTask(void*) {
  while (true) {
    std::string* payload = nullptr;
    if (!g_trash_queue || xQueueReceive(g_trash_queue, &payload, portMAX_DELAY) != pdTRUE) continue;
    if (!payload) continue;
    std::string trash_dir = *payload;
    delete payload;
    if (!trash_dir.empty()) {
      (void)RemoveTree(trash_dir);
      (void)rmdir(trash_dir.c_str());
    }
  }
}

static void EnqueueExistingTrash() {
  const std::string trash_root = "/littlefs/.trash";
  DIR* dir = opendir(trash_root.c_str());
  if (!dir) return;
  while (true) {
    struct dirent* ent = readdir(dir);
    if (!ent) break;
    const char* name = ent->d_name;
    if (!name || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
    const std::string child = trash_root + "/" + name;
    std::string* payload = new std::string(child);
    if (g_trash_queue && xQueueSend(g_trash_queue, &payload, 0) != pdTRUE) {
      delete payload;
    }
  }
  closedir(dir);
}

static bool ReadSmallFile(const std::string& path, size_t max_bytes, std::string* out) {
  if (out) out->clear();
  if (!out || path.empty() || max_bytes == 0) return false;
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return false;
  out->reserve(max_bytes);
  char buf[256];
  size_t total = 0;
  while (true) {
    const size_t can = (max_bytes - total) < sizeof(buf) ? (max_bytes - total) : sizeof(buf);
    if (can == 0) break;
    const size_t n = fread(buf, 1, can, f);
    if (n == 0) break;
    out->append(buf, n);
    total += n;
    if (n < can) break;
  }
  fclose(f);
  return !out->empty();
}

static std::string Trim(std::string s) {
  size_t b = 0;
  while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) b++;
  size_t e = s.size();
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) e--;
  return s.substr(b, e - b);
}

static std::string SanitizeVersionToken(const std::string& input) {
  std::string out;
  out.reserve(input.size());
  for (char ch : input) {
    const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                    ch == '.' || ch == '_' || ch == '-';
    if (ok) out.push_back(ch);
    if (out.size() >= 32) break;
  }
  return out;
}

static bool ExtractJsonString(const std::string& json, const char* key, std::string* out) {
  if (out) out->clear();
  if (!key || !*key || !out) return false;
  const std::string needle = std::string("\"") + key + "\"";
  const size_t k = json.find(needle);
  if (k == std::string::npos) return false;
  size_t p = json.find(':', k + needle.size());
  if (p == std::string::npos) return false;
  p = json.find('"', p + 1);
  if (p == std::string::npos) return false;
  p++;
  std::string v;
  while (p < json.size()) {
    const char ch = json[p++];
    if (ch == '"') break;
    if (ch == '\\' && p < json.size()) {
      const char esc = json[p++];
      if (esc == '"' || esc == '\\' || esc == '/') {
        v.push_back(esc);
      } else if (esc == 'n') {
        v.push_back('\n');
      } else if (esc == 'r') {
        v.push_back('\r');
      } else if (esc == 't') {
        v.push_back('\t');
      } else {
        v.push_back(esc);
      }
      continue;
    }
    v.push_back(ch);
  }
  *out = v;
  return !out->empty();
}

static void AppendJsonEscaped(std::string* out, const std::string& value) {
  if (!out) return;
  for (char ch : value) {
    switch (ch) {
      case '\\':
        out->append("\\\\");
        break;
      case '"':
        out->append("\\\"");
        break;
      case '\n':
        out->append("\\n");
        break;
      case '\r':
        out->append("\\r");
        break;
      case '\t':
        out->append("\\t");
        break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20) {
          char buf[7];
          snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(ch)));
          out->append(buf);
        } else {
          out->push_back(ch);
        }
        break;
    }
  }
}

static bool ReadAppManifestString(const std::string& app_id, const char* key, std::string* out_value) {
  if (out_value) out_value->clear();
  if (!out_value || !key || !*key || !IsValidAppId(app_id)) return false;
  const std::string app_dir = std::string("/littlefs/apps/") + app_id;
  std::string manifest;
  if (!ReadSmallFile(app_dir + "/manifest.json", 2048, &manifest)) return false;
  std::string value;
  if (!ExtractJsonString(manifest, key, &value)) return false;
  *out_value = Trim(value);
  return !out_value->empty();
}

static bool ReadAppManifestJson(const std::string& app_id, size_t max_bytes, cJSON** out_root) {
  if (out_root) *out_root = nullptr;
  if (!out_root || !IsValidAppId(app_id)) return false;
  if (max_bytes < 512) max_bytes = 512;
  const std::string app_dir = std::string("/littlefs/apps/") + app_id;
  std::string manifest;
  if (!ReadSmallFile(app_dir + "/manifest.json", max_bytes, &manifest)) return false;
  cJSON* root = cJSON_ParseWithLength(manifest.c_str(), manifest.size());
  if (!root || !cJSON_IsObject(root)) {
    if (root) cJSON_Delete(root);
    return false;
  }
  *out_root = root;
  return true;
}

static bool AddAppSettingsSchema(const std::string& app_id, cJSON* item) {
  if (!item || !cJSON_IsObject(item)) return false;
  cJSON* root = nullptr;
  if (!ReadAppManifestJson(app_id, 8192, &root)) return false;
  cJSON* settings = cJSON_GetObjectItemCaseSensitive(root, "settings");
  bool ok = false;
  if (settings && cJSON_IsObject(settings)) {
    cJSON* copy = cJSON_Duplicate(settings, 1);
    if (copy && cJSON_AddItemToObject(item, "settings", copy)) {
      ok = true;
    } else if (copy) {
      cJSON_Delete(copy);
    }
  }
  cJSON_Delete(root);
  return ok;
}

static bool ReadAppVersion(const std::string& app_id, std::string* out_version) {
  if (out_version) out_version->clear();
  if (!out_version || !IsValidAppId(app_id)) return false;
  const std::string app_dir = std::string("/littlefs/apps/") + app_id;

  std::string txt;
  if (ReadSmallFile(app_dir + "/.store_version", 64, &txt)) {
    const std::string v = SanitizeVersionToken(Trim(txt));
    if (!v.empty()) {
      *out_version = v;
      return true;
    }
  }

  std::string manifest;
  if (ReadSmallFile(app_dir + "/manifest.json", 2048, &manifest)) {
    std::string v;
    if (ExtractJsonString(manifest, "version", &v)) {
      v = SanitizeVersionToken(Trim(v));
      if (!v.empty()) {
        *out_version = v;
        return true;
      }
    }
  }
  return false;
}

static bool ReadAppName(const std::string& app_id, std::string* out_name) {
  if (out_name) out_name->clear();
  if (!out_name || !IsValidAppId(app_id)) return false;
  if (!ReadAppManifestString(app_id, "name", out_name)) return false;
  return !out_name->empty();
}

static bool AppHasThumbnail(const std::string& app_id) {
  if (!IsValidAppId(app_id)) return false;
  const std::string app_dir = std::string("/littlefs/apps/") + app_id;
  static const char* kNames[] = {"thumbnail.png", "thumbnail.jpg", "thumbnail.jpeg", "thumb.png", "preview.png"};
  struct stat st = {};
  for (const char* name : kNames) {
    const std::string p = app_dir + "/" + name;
    if (stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode)) return true;
  }
  return false;
}

static bool AppHasSettingsPage(const std::string& app_id) {
  if (!IsValidAppId(app_id)) return false;
  const std::string path = std::string("/littlefs/apps/") + app_id + "/settings.html";
  struct stat st = {};
  if (stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) return true;
  const std::string gz_path = path + ".gz";
  return stat(gz_path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

static bool AppHasLoadableEntry(const std::string& app_id) {
  if (!IsValidAppId(app_id)) return false;
  const std::string app_dir = std::string("/littlefs/apps/") + app_id;
  struct stat st = {};

  const std::string app_bin = app_dir + "/app.bin";
  if (stat(app_bin.c_str(), &st) == 0 && S_ISREG(st.st_mode)) return true;

  return false;
}

static cJSON* BuildInstalledAppEntry(const std::string& app_id) {
  if (!IsValidAppId(app_id)) return nullptr;
  const std::string app_dir = std::string("/littlefs/apps/") + app_id;
  struct stat st = {};
  if (stat(app_dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) return nullptr;
  if (!AppHasLoadableEntry(app_id)) return nullptr;

  std::string version;
  (void)ReadAppVersion(app_id, &version);
  std::string display_name;
  if (!ReadAppName(app_id, &display_name)) display_name = app_id;
  const bool has_thumb = AppHasThumbnail(app_id);
  const bool has_settings_page = AppHasSettingsPage(app_id);

  cJSON* item = cJSON_CreateObject();
  if (!item) return nullptr;
  if (!cJSON_AddStringToObject(item, "id", app_id.c_str()) ||
      !cJSON_AddStringToObject(item, "name", display_name.c_str()) ||
      !cJSON_AddStringToObject(item, "version", version.c_str()) ||
      !cJSON_AddBoolToObject(item, "has_thumbnail", has_thumb)) {
    cJSON_Delete(item);
    return nullptr;
  }

  std::string thumbnail_url;
  if (has_thumb) thumbnail_url = "/api/apps/thumbnail/" + app_id;
  if (!cJSON_AddStringToObject(item, "thumbnail_url", thumbnail_url.c_str())) {
    cJSON_Delete(item);
    return nullptr;
  }
  if (!cJSON_AddBoolToObject(item, "has_settings_page", has_settings_page)) {
    cJSON_Delete(item);
    return nullptr;
  }
  std::string settings_page_url;
  if (has_settings_page) settings_page_url = "/api/apps/web/" + app_id + "/settings.html";
  if (!cJSON_AddStringToObject(item, "settings_page_url", settings_page_url.c_str())) {
    cJSON_Delete(item);
    return nullptr;
  }
  (void)AddAppSettingsSchema(app_id, item);
  return item;
}

static cJSON* LoadInstalledAppsIndexRoot() {
  std::string body;
  if (LoadInstalledAppsIndexBody(&body)) {
    cJSON* root = cJSON_ParseWithLength(body.c_str(), body.size());
    if (root) {
      cJSON* apps = cJSON_GetObjectItemCaseSensitive(root, "apps");
      if (cJSON_IsArray(apps)) return root;
      cJSON_Delete(root);
    }
  }

  cJSON* root = cJSON_CreateObject();
  if (!root) return nullptr;
  cJSON* apps = cJSON_CreateArray();
  if (!apps) {
    cJSON_Delete(root);
    return nullptr;
  }
  cJSON_AddItemToObject(root, "ok", cJSON_CreateTrue());
  cJSON_AddItemToObject(root, "apps", apps);
  return root;
}

static cJSON* EnsureInstalledAppsArray(cJSON* root) {
  if (!root || !cJSON_IsObject(root)) return nullptr;
  cJSON* apps = cJSON_GetObjectItemCaseSensitive(root, "apps");
  if (cJSON_IsArray(apps)) return apps;
  apps = cJSON_CreateArray();
  if (!apps) return nullptr;
  cJSON_ReplaceItemInObject(root, "ok", cJSON_CreateTrue());
  cJSON_AddItemToObject(root, "apps", apps);
  return apps;
}

static void RemoveInstalledAppEntryById(cJSON* apps, const std::string& app_id) {
  if (!apps || !cJSON_IsArray(apps) || !IsValidAppId(app_id)) return;
  const int count = cJSON_GetArraySize(apps);
  for (int i = 0; i < count; ++i) {
    cJSON* item = cJSON_GetArrayItem(apps, i);
    const cJSON* id = cJSON_GetObjectItemCaseSensitive(item, "id");
    if (cJSON_IsString(id) && id->valuestring && app_id == id->valuestring) {
      cJSON_DeleteItemFromArray(apps, i);
      return;
    }
  }
}

static bool SaveInstalledAppsIndexRoot(cJSON* root) {
  if (!root) return false;
  cJSON_ReplaceItemInObject(root, "ok", cJSON_CreateTrue());
  char* rendered = cJSON_PrintUnformatted(root);
  if (!rendered) return false;
  const std::string body(rendered);
  cJSON_free(rendered);
  if (!SaveInstalledAppsIndexBody(body)) return false;
  WriteInstalledAppsCache(body);
  return true;
}

static bool UpdateInstalledAppsIndexForApp(const std::string& app_id) {
  if (!IsValidAppId(app_id)) return false;
  cJSON* root = LoadInstalledAppsIndexRoot();
  if (!root) return false;
  cJSON* apps = EnsureInstalledAppsArray(root);
  if (!apps) {
    cJSON_Delete(root);
    return false;
  }

  RemoveInstalledAppEntryById(apps, app_id);
  cJSON* item = BuildInstalledAppEntry(app_id);
  if (item) cJSON_AddItemToArray(apps, item);

  const bool ok = SaveInstalledAppsIndexRoot(root);
  cJSON_Delete(root);
  return ok;
}

static bool RemoveInstalledAppsIndexForApp(const std::string& app_id) {
  if (!IsValidAppId(app_id)) return false;
  cJSON* root = LoadInstalledAppsIndexRoot();
  if (!root) return false;
  cJSON* apps = EnsureInstalledAppsArray(root);
  if (!apps) {
    cJSON_Delete(root);
    return false;
  }

  RemoveInstalledAppEntryById(apps, app_id);
  const bool ok = SaveInstalledAppsIndexRoot(root);
  cJSON_Delete(root);
  return ok;
}

static bool SendFile(httpd_req_t* req, const std::string& path, const char* content_type) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return false;
  httpd_resp_set_status(req, "200 OK");
  if (content_type) httpd_resp_set_type(req, content_type);
  httpd_resp_set_hdr(req, "Connection", "close");
  char buf[768];
  while (true) {
    const size_t n = fread(buf, 1, sizeof(buf), f);
    if (n == 0) break;
    if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) {
      fclose(f);
      return false;
    }
  }
  fclose(f);
  return httpd_resp_send_chunk(req, nullptr, 0) == ESP_OK;
}

static bool ParseAppIdFromPrefix(const char* uri, const char* prefix, std::string* out_app_id) {
  if (out_app_id) out_app_id->clear();
  if (!uri || !prefix || !out_app_id) return false;
  const std::string raw = UriWithoutQuery(uri);
  const std::string pre(prefix);
  if (raw.rfind(pre, 0) != 0) return false;
  const std::string app_id = raw.substr(pre.size());
  if (!IsValidAppId(app_id)) return false;
  *out_app_id = app_id;
  return true;
}

static esp_err_t SendCompressedHtml(httpd_req_t* req, const unsigned char* data, size_t len) {
  httpd_resp_set_status(req, "200 OK");
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(req, "Connection", "close");
  return httpd_resp_send(req, reinterpret_cast<const char*>(data), static_cast<ssize_t>(len));
}

static esp_err_t SendCompressedBinary(httpd_req_t* req, const unsigned char* data, size_t len, const char* content_type) {
  httpd_resp_set_status(req, "200 OK");
  httpd_resp_set_type(req, content_type ? content_type : "application/octet-stream");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=604800");
  httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(req, "Connection", "close");
  return httpd_resp_send(req, reinterpret_cast<const char*>(data), static_cast<ssize_t>(len));
}

static bool ShouldUseCaptivePortal() {
  WifiStatusInfo st = {};
  if (!WifiManagerGetStatus(&st)) return false;
  return st.ap_active && !st.sta_connected;
}

static esp_err_t SendPortalRedirect(httpd_req_t* req) {
  WifiStatusInfo st = {};
  std::string location = "/portal.html";
  if (WifiManagerGetStatus(&st) && st.ap_ip[0] && strcmp(st.ap_ip, "--") != 0) {
    location = "http://";
    location += st.ap_ip;
    location += "/portal.html";
  }
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_set_hdr(req, "Location", location.c_str());
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "Connection", "close");
  return httpd_resp_send(req, "Redirecting to portal...", HTTPD_RESP_USE_STRLEN);
}

static void NotifyReloadRequested() {
  if (g_reload_cb) g_reload_cb();
}

static esp_err_t HandleFirmwareStatus(httpd_req_t* req) {
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_app_desc_t* desc = esp_app_get_description();
  const char* fw_version = SW_VERSION;
  if (!fw_version || !fw_version[0]) fw_version = desc ? desc->version : "unknown";
  const std::string store_channel = GetStoreChannel();
  const FirmwareOtaState fw_ota_state = GetFirmwareOtaState();
  char json[256];
  snprintf(json, sizeof(json),
           "{\"ok\":true,\"service\":\"firmware_ota\",\"project\":\"%s\",\"version\":\"%s\",\"partition\":\"%s\"}",
           desc ? desc->project_name : "unknown", fw_version, running ? running->label : "?");
  SendJson(req, "200 OK", json);
  return ESP_OK;
}

static esp_err_t HandleFirmwareOta(httpd_req_t* req) {
  if (!req || req->content_len <= 0) {
    SendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"empty body\"}");
    return ESP_OK;
  }

  const esp_partition_t* update_partition = esp_ota_get_next_update_partition(nullptr);
  if (!update_partition) {
    SendJson(req, "500 Internal Server Error", "{\"ok\":false,\"error\":\"no update partition\"}");
    return ESP_OK;
  }

  esp_ota_handle_t ota_handle = 0;
  esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "esp_ota_begin failed: %s", esp_err_to_name(err));
    SendJson(req, "500 Internal Server Error", "{\"ok\":false,\"error\":\"ota begin failed\"}");
    return ESP_OK;
  }

  LvglOtaOverlayBegin(static_cast<size_t>(req->content_len));
  int remaining = req->content_len;
  int total = 0;
  char buf[1024];
  while (remaining > 0) {
    const int want = remaining < static_cast<int>(sizeof(buf)) ? remaining : static_cast<int>(sizeof(buf));
    const int r = httpd_req_recv(req, buf, want);
    if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (r <= 0) {
      LvglOtaOverlayFail();
      esp_ota_abort(ota_handle);
      SendJson(req, "500 Internal Server Error", "{\"ok\":false,\"error\":\"recv failed\"}");
      return ESP_OK;
    }
    err = esp_ota_write(ota_handle, buf, static_cast<size_t>(r));
    if (err != ESP_OK) {
      ESP_LOGE(kTag, "esp_ota_write failed: %s", esp_err_to_name(err));
      LvglOtaOverlayFail();
      esp_ota_abort(ota_handle);
      SendJson(req, "500 Internal Server Error", "{\"ok\":false,\"error\":\"ota write failed\"}");
      return ESP_OK;
    }
    remaining -= r;
    total += r;
    LvglOtaOverlayUpdate(static_cast<size_t>(total), static_cast<size_t>(req->content_len));
  }

  err = esp_ota_end(ota_handle);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "esp_ota_end failed: %s", esp_err_to_name(err));
    LvglOtaOverlayFail();
    SendJson(req, "500 Internal Server Error", "{\"ok\":false,\"error\":\"ota end failed\"}");
    return ESP_OK;
  }

  err = esp_ota_set_boot_partition(update_partition);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
    LvglOtaOverlayFail();
    SendJson(req, "500 Internal Server Error", "{\"ok\":false,\"error\":\"set boot partition failed\"}");
    return ESP_OK;
  }

  ESP_LOGI(kTag, "firmware OTA uploaded: %d bytes to %s", total, update_partition->label);
  LvglOtaOverlayFinalizing();
  SendJson(req, "200 OK", "{\"ok\":true,\"ota\":true,\"rebooting\":true}");
  vTaskDelay(pdMS_TO_TICKS(400));
  esp_restart();
  return ESP_OK;
}

static esp_err_t HandleSystemReboot(httpd_req_t* req) {
  SendJson(req, "200 OK", "{\"ok\":true,\"rebooting\":true}");
  vTaskDelay(pdMS_TO_TICKS(200));
  esp_restart();
  return ESP_OK;
}

static esp_err_t HandlePutAppFile(httpd_req_t* req) {
  std::string app_id;
  std::string filename;
  if (!ParsePutUri(req->uri, &app_id, &filename)) {
    SendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"invalid uri\"}");
    return ESP_OK;
  }

  if (req->content_len <= 0) {
    SendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"empty body\"}");
    return ESP_OK;
  }

  static constexpr int kMaxUploadBytes = 1024 * 1024;
  if (req->content_len > kMaxUploadBytes) {
    SendJson(req, "413 Payload Too Large", "{\"ok\":false,\"error\":\"file too large\"}");
    return ESP_OK;
  }

  if (g_install_session_app_id.empty() || g_install_session_stage_dir.empty() || g_install_session_app_id != app_id) {
    SendJson(req, "409 Conflict", "{\"ok\":false,\"error\":\"install session required; call /api/apps/install/begin?app_id=<id>\"}");
    return ESP_OK;
  }

  std::string app_dir = g_install_session_stage_dir;
  if (!EnsureDir(app_dir.c_str())) {
    SendJson(req, "500 Internal Server Error", "{\"ok\":false,\"error\":\"mkdir app dir failed\"}");
    return ESP_OK;
  }

  if (!EnsureParentDirsForFile(app_dir, filename)) {
    SendJson(req, "500 Internal Server Error", "{\"ok\":false,\"error\":\"mkdir subdir failed\"}");
    return ESP_OK;
  }

  const std::string final_path = app_dir + "/" + filename;
  const std::string tmp_path = final_path + ".tmp";
  (void)unlink(tmp_path.c_str());

  if (g_switch_cb) {
    char cur_app_id[64] = {};
    if (LvglGetCurrentLuaAppInfo(cur_app_id, sizeof(cur_app_id), nullptr, 0)) {
      if (app_id == std::string(cur_app_id)) {
        if (g_updating_session_app_id != app_id) {
          g_updating_session_app_id = app_id;
          g_switch_cb("app_updating", static_cast<unsigned>(strlen("app_updating")));
          g_install_session_switched_to_updating = true;
          vTaskDelay(pdMS_TO_TICKS(80));
        }
      }
    }
  }

  int wrote = 0;
  if (!WriteRequestBodyToFile(req, tmp_path.c_str(), &wrote)) {
    (void)unlink(tmp_path.c_str());
    SendJson(req, "500 Internal Server Error", "{\"ok\":false,\"error\":\"write failed\"}");
    return ESP_OK;
  }

  if (rename(tmp_path.c_str(), final_path.c_str()) != 0) {
    (void)unlink(tmp_path.c_str());
    SendJson(req, "500 Internal Server Error", "{\"ok\":false,\"error\":\"rename failed\"}");
    return ESP_OK;
  }

  ESP_LOGI(kTag, "updated %s (%d bytes)", final_path.c_str(), wrote);
  if (!UpdateInstalledAppsIndexForApp(app_id)) {
    InvalidateInstalledAppsCache();
  }

  SendJson(req, "200 OK", "{\"ok\":true}");
  return ESP_OK;
}

static esp_err_t HandleDeleteAppFile(httpd_req_t* req) {
  std::string app_only_id;
  if (ParseDeleteAppUri(req->uri, &app_only_id)) {
    const std::string app_dir = std::string("/littlefs/apps/") + app_only_id;
    struct stat st = {};
    if (stat(app_dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
      SendJson(req, "404 Not Found", "{\"ok\":false,\"error\":\"app not found\"}");
      return ESP_OK;
    }

    if (!MoveToTrash(app_dir, app_only_id)) {
      SendJson(req, "500 Internal Server Error", "{\"ok\":false,\"error\":\"uninstall failed\"}");
      return ESP_OK;
    }

    ESP_LOGI(kTag, "moved app to trash: %s", app_only_id.c_str());
    if (!RemoveInstalledAppsIndexForApp(app_only_id)) {
      InvalidateInstalledAppsCache();
    }
    InvalidateInstalledAppsCache();
    if (g_scheduler.last_app_id == app_only_id) {
      std::string next_app;
      cJSON* fallback = g_scheduler.config ? cJSON_GetObjectItemCaseSensitive(g_scheduler.config, "fallbackAppId") : nullptr;
      if (cJSON_IsString(fallback) && fallback->valuestring && AppInstalled(fallback->valuestring)) {
        next_app = fallback->valuestring;
      } else {
        next_app = FindAnyInstalledAppId();
      }
      if (!next_app.empty() && g_switch_cb) {
        g_switch_cb(next_app.c_str(), static_cast<unsigned>(next_app.size()));
        g_scheduler.last_app_id = next_app;
        ESP_LOGI(kTag, "uninstall switched app: %s -> %s", app_only_id.c_str(), next_app.c_str());
      } else {
        g_scheduler.last_app_id.clear();
      }
    }
    SendJson(req, "200 OK", "{\"ok\":true,\"uninstalled\":true,\"trash\":true}");
    return ESP_OK;
  }

  std::string app_id;
  std::string filename;
  if (!ParsePutUri(req->uri, &app_id, &filename)) {
    SendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"invalid uri\"}");
    return ESP_OK;
  }

  const std::string final_path = std::string("/littlefs/apps/") + app_id + "/" + filename;
  if (unlink(final_path.c_str()) != 0) {
    SendJson(req, "404 Not Found", "{\"ok\":false,\"error\":\"file not found\"}");
    return ESP_OK;
  }

  ESP_LOGI(kTag, "deleted %s", final_path.c_str());
  if (!UpdateInstalledAppsIndexForApp(app_id)) {
    InvalidateInstalledAppsCache();
  }
  SendJson(req, "200 OK", "{\"ok\":true,\"deleted\":true}");
  return ESP_OK;
}

static std::string BuildInstalledAppsBody();

static bool PruneMissingInstalledAppsIndex(std::string* body) {
  if (!body || body->empty()) return false;
  cJSON* root = cJSON_ParseWithLength(body->c_str(), body->size());
  if (!root || !cJSON_IsObject(root)) {
    if (root) cJSON_Delete(root);
    return false;
  }
  cJSON* apps = cJSON_GetObjectItemCaseSensitive(root, "apps");
  if (!cJSON_IsArray(apps)) {
    cJSON_Delete(root);
    return false;
  }

  bool changed = false;
  for (int i = cJSON_GetArraySize(apps) - 1; i >= 0; --i) {
    cJSON* item = cJSON_GetArrayItem(apps, i);
    const cJSON* id = cJSON_GetObjectItemCaseSensitive(item, "id");
    const char* app_id = cJSON_IsString(id) ? id->valuestring : nullptr;
    if (!app_id || !IsValidAppId(app_id)) {
      cJSON_DeleteItemFromArray(apps, i);
      changed = true;
      continue;
    }
    const std::string app_dir = std::string("/littlefs/apps/") + app_id;
    struct stat st = {};
    if (stat(app_dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
      cJSON_DeleteItemFromArray(apps, i);
      changed = true;
    }
  }

  if (!changed) {
    cJSON_Delete(root);
    return false;
  }

  char* rendered = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!rendered) return false;
  *body = rendered;
  cJSON_free(rendered);
  return true;
}

static esp_err_t HandleReload(httpd_req_t* req) {
  InvalidateInstalledAppsCache();
  NotifyReloadRequested();
  if (g_switch_cb && !g_updating_session_app_id.empty()) {
    g_switch_cb(g_updating_session_app_id.c_str(), static_cast<unsigned>(g_updating_session_app_id.size()));
    g_updating_session_app_id.clear();
  }
  SendJson(req, "200 OK", "{\"ok\":true,\"reloaded\":true,\"incremental\":true}");
  return ESP_OK;
}

static esp_err_t HandleInstallBegin(httpd_req_t* req) {
  std::string app_id;
  if (!ReadQueryParam(req, "app_id", &app_id) || !IsValidAppId(app_id)) {
    SendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"invalid app_id\"}");
    return ESP_OK;
  }
  if (!g_install_session_app_id.empty() && g_install_session_app_id != app_id) {
    SendJson(req, "409 Conflict", "{\"ok\":false,\"error\":\"another install session is active\"}");
    return ESP_OK;
  }
  const std::string stage_root = "/littlefs/.staging";
  const std::string stage_apps = "/littlefs/.staging/apps";
  const std::string stage_dir = AppInstallStagingDir(app_id);
  (void)EnsureDir(stage_root.c_str());
  (void)EnsureDir(stage_apps.c_str());
  (void)RemoveTree(stage_dir);
  (void)rmdir(stage_dir.c_str());
  if (!EnsureDir(stage_dir.c_str())) {
    SendJson(req, "500 Internal Server Error", "{\"ok\":false,\"error\":\"mkdir stage failed\"}");
    return ESP_OK;
  }
  g_install_session_app_id = app_id;
  g_install_session_stage_dir = stage_dir;
  g_install_session_switched_to_updating = false;
  SendJson(req, "200 OK", "{\"ok\":true,\"staging\":true}");
  return ESP_OK;
}

static esp_err_t HandleInstallAbort(httpd_req_t* req) {
  (void)req;
  if (!g_install_session_stage_dir.empty()) {
    (void)RemoveTree(g_install_session_stage_dir);
    (void)rmdir(g_install_session_stage_dir.c_str());
  }
  if (g_install_session_switched_to_updating && g_switch_cb && !g_install_session_app_id.empty()) {
    g_switch_cb(g_install_session_app_id.c_str(), static_cast<unsigned>(g_install_session_app_id.size()));
  }
  g_install_session_app_id.clear();
  g_install_session_stage_dir.clear();
  g_install_session_switched_to_updating = false;
  SendJson(req, "200 OK", "{\"ok\":true,\"aborted\":true}");
  return ESP_OK;
}

static esp_err_t HandleInstallCommit(httpd_req_t* req) {
  (void)req;
  if (g_install_session_app_id.empty() || g_install_session_stage_dir.empty()) {
    SendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"no active install session\"}");
    return ESP_OK;
  }
  std::string err;
  if (!ValidateStagedApp(g_install_session_stage_dir, g_install_session_app_id, &err)) {
    SendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"staged app validation failed\"}");
    return ESP_OK;
  }

  const std::string final_dir = std::string("/littlefs/apps/") + g_install_session_app_id;
  const std::string backup_dir = std::string("/littlefs/.staging/backup_") + g_install_session_app_id;
  (void)RemoveTree(backup_dir);
  (void)rmdir(backup_dir.c_str());

  bool had_old = false;
  struct stat st = {};
  if (stat(final_dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
    if (rename(final_dir.c_str(), backup_dir.c_str()) != 0) {
      SendJson(req, "500 Internal Server Error", "{\"ok\":false,\"error\":\"backup existing app failed\"}");
      return ESP_OK;
    }
    had_old = true;
  }

  if (rename(g_install_session_stage_dir.c_str(), final_dir.c_str()) != 0) {
    if (had_old) (void)rename(backup_dir.c_str(), final_dir.c_str());
    SendJson(req, "500 Internal Server Error", "{\"ok\":false,\"error\":\"activate staged app failed\"}");
    return ESP_OK;
  }

  if (had_old) {
    (void)RemoveTree(backup_dir);
    (void)rmdir(backup_dir.c_str());
  }

  if (!UpdateInstalledAppsIndexForApp(g_install_session_app_id)) {
    InvalidateInstalledAppsCache();
  }
  if (g_install_session_switched_to_updating && g_switch_cb) {
    g_switch_cb(g_install_session_app_id.c_str(), static_cast<unsigned>(g_install_session_app_id.size()));
  }
  g_install_session_app_id.clear();
  g_install_session_stage_dir.clear();
  g_install_session_switched_to_updating = false;
  SendJson(req, "200 OK", "{\"ok\":true,\"committed\":true}");
  return ESP_OK;
}

static esp_err_t HandleSwitch(httpd_req_t* req) {
  std::string app_id;
  if (!ParseSwitchUri(req->uri, &app_id) && !ParseSwitchQuery(req, &app_id)) {
    SendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"invalid app_id\"}");
    return ESP_OK;
  }
  if (!g_switch_cb) {
    SendJson(req, "503 Service Unavailable", "{\"ok\":false,\"error\":\"switch callback unavailable\"}");
    return ESP_OK;
  }

  AppUpdateServerNotifyManualAppAction();
  g_switch_cb(app_id.c_str(), static_cast<unsigned>(app_id.size()));
  ESP_LOGI(kTag, "switch app requested: %s", app_id.c_str());
  SendJson(req, "200 OK", "{\"ok\":true,\"switched\":true}");
  return ESP_OK;
}

static esp_err_t HandlePing(httpd_req_t* req) {
  SendJson(req, "200 OK", "{\"ok\":true,\"service\":\"app_update\"}");
  return ESP_OK;
}

static esp_err_t HandleCurrentApp(httpd_req_t* req) {
  char app_id_buf[64] = {};
  char app_dir_buf[96] = {};
  const bool running = LvglGetCurrentLuaAppInfo(app_id_buf, sizeof(app_id_buf), app_dir_buf, sizeof(app_dir_buf));

  const std::string app_id = app_id_buf;
  std::string app_name = app_id;
  std::string app_version;
  bool has_thumb = false;
  if (running && IsValidAppId(app_id)) {
    (void)ReadAppName(app_id, &app_name);
    (void)ReadAppVersion(app_id, &app_version);
    has_thumb = AppHasThumbnail(app_id);
  } else {
    app_name.clear();
  }

  std::string body = "{\"ok\":true,\"running\":";
  body += running ? "true" : "false";
  body += ",\"app_id\":\"";
  AppendJsonEscaped(&body, app_id);
  body += "\",\"app_dir\":\"";
  AppendJsonEscaped(&body, app_dir_buf);
  body += "\",\"name\":\"";
  AppendJsonEscaped(&body, app_name);
  body += "\",\"version\":\"";
  AppendJsonEscaped(&body, app_version);
  body += "\",\"has_thumbnail\":";
  body += has_thumb ? "true" : "false";
  body += ",\"thumbnail_url\":\"";
  if (has_thumb) {
    body += "/api/apps/thumbnail/";
    AppendJsonEscaped(&body, app_id);
  }
  body += "\"}";
  SendJson(req, "200 OK", body.c_str());
  return ESP_OK;
}

static std::string BuildInstalledAppsBody() {
  cJSON* root = cJSON_CreateObject();
  cJSON* apps = cJSON_CreateArray();
  if (!root || !apps) {
    if (root) cJSON_Delete(root);
    if (apps) cJSON_Delete(apps);
    return "{\"ok\":true,\"apps\":[]}";
  }
  cJSON_AddItemToObject(root, "ok", cJSON_CreateTrue());
  cJSON_AddItemToObject(root, "apps", apps);

  DIR* dir = opendir("/littlefs/apps");
  if (dir) {
    while (true) {
      struct dirent* ent = readdir(dir);
      if (!ent) break;
      const char* name = ent->d_name;
      if (!name) continue;
      if ((strcmp(name, ".") == 0) || (strcmp(name, "..") == 0)) continue;
      const std::string app_id = name;
      cJSON* item = BuildInstalledAppEntry(app_id);
      if (item) cJSON_AddItemToArray(apps, item);
    }
    closedir(dir);
  }

  char* rendered = cJSON_PrintUnformatted(root);
  std::string body = rendered ? rendered : "{\"ok\":true,\"apps\":[]}";
  if (rendered) cJSON_free(rendered);
  cJSON_Delete(root);
  return body;
}

static esp_err_t HandleInstalledApps(httpd_req_t* req) {
  std::string body;
  if (!ReadInstalledAppsCache(&body)) {
    if (!LoadInstalledAppsIndexBody(&body)) {
      body = "{\"ok\":true,\"apps\":[]}";
      WriteInstalledAppsCache(body);
    } else {
      WriteInstalledAppsCache(body);
    }
  }
  if (PruneMissingInstalledAppsIndex(&body)) {
    (void)SaveInstalledAppsIndexBody(body);
    WriteInstalledAppsCache(body);
  }
  SendJson(req, "200 OK", body.c_str());
  return ESP_OK;
}

static bool ParseAppFilesUri(const char* uri, std::string* out_app_id) {
  if (out_app_id) out_app_id->clear();
  if (!uri || !out_app_id) return false;
  const std::string raw = UriWithoutQuery(uri);
  static const std::string kPrefix = "/api/apps/files/";
  if (raw.rfind(kPrefix, 0) != 0) return false;
  const std::string app_id = raw.substr(kPrefix.size());
  if (!IsValidAppId(app_id)) return false;
  *out_app_id = app_id;
  return true;
}

static esp_err_t HandleAppFilesList(httpd_req_t* req) {
  std::string app_id;
  if (!ParseAppFilesUri(req->uri, &app_id)) {
    SendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"invalid app_id\"}");
    return ESP_OK;
  }

  std::string rel_dir = "assets";
  std::string q_dir;
  if (ReadQueryParam(req, "dir", &q_dir) && !q_dir.empty()) {
    if (!IsAllowedFilename(q_dir) || q_dir.find("..") != std::string::npos || q_dir.front() == '/' ||
        q_dir.back() == '/') {
      SendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"invalid dir\"}");
      return ESP_OK;
    }
    rel_dir = q_dir;
  }

  const std::string base_dir = std::string("/littlefs/apps/") + app_id + "/" + rel_dir;
  struct stat st = {};
  if (stat(base_dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
    SendJson(req, "404 Not Found", "{\"ok\":false,\"error\":\"dir not found\"}");
    return ESP_OK;
  }

  cJSON* root = cJSON_CreateObject();
  cJSON* files = cJSON_CreateArray();
  if (!root || !files) {
    if (root) cJSON_Delete(root);
    if (files) cJSON_Delete(files);
    SendJson(req, "500 Internal Server Error", "{\"ok\":false,\"error\":\"oom\"}");
    return ESP_OK;
  }
  cJSON_AddItemToObject(root, "ok", cJSON_CreateTrue());
  cJSON_AddStringToObject(root, "app_id", app_id.c_str());
  cJSON_AddStringToObject(root, "dir", rel_dir.c_str());
  cJSON_AddItemToObject(root, "files", files);

  DIR* dir = opendir(base_dir.c_str());
  if (dir) {
    while (true) {
      struct dirent* ent = readdir(dir);
      if (!ent) break;
      const char* name = ent->d_name;
      if (!name || !*name) continue;
      if ((strcmp(name, ".") == 0) || (strcmp(name, "..") == 0)) continue;
      if (name[0] == '.') continue;

      const std::string filename = name;
      if (!IsAllowedFilename(filename)) continue;

      const std::string full = base_dir + "/" + filename;
      struct stat fst = {};
      if (stat(full.c_str(), &fst) != 0) continue;
      if (!S_ISREG(fst.st_mode)) continue;
      cJSON_AddItemToArray(files, cJSON_CreateString(filename.c_str()));
    }
    closedir(dir);
  }

  char* rendered = cJSON_PrintUnformatted(root);
  std::string body = rendered ? rendered : "{\"ok\":true,\"files\":[]}";
  if (rendered) cJSON_free(rendered);
  cJSON_Delete(root);
  SendJson(req, "200 OK", body.c_str());
  return ESP_OK;
}

static esp_err_t HandleAppThumbnail(httpd_req_t* req) {
  std::string app_id;
  if (!ParseAppIdFromPrefix(req->uri, "/api/apps/thumbnail/", &app_id)) {
    SendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"invalid app_id\"}");
    return ESP_OK;
  }
  const std::string app_dir = std::string("/littlefs/apps/") + app_id + "/";
  static const struct {
    const char* name;
    const char* content_type;
  } kCandidates[] = {
      {"thumbnail.png", "image/png"},
      {"thumbnail.jpg", "image/jpeg"},
      {"thumbnail.jpeg", "image/jpeg"},
      {"thumb.png", "image/png"},
      {"preview.png", "image/png"},
  };
  for (const auto& c : kCandidates) {
    if (SendFile(req, app_dir + c.name, c.content_type)) return ESP_OK;
  }
  httpd_resp_set_status(req, "404 Not Found");
  httpd_resp_send(req, "not found", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static const char* ContentTypeFromFilename(const std::string& filename) {
  std::string lower = filename;
  for (char& ch : lower) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  if (lower.size() >= 5 && lower.compare(lower.size() - 5, 5, ".html") == 0) return "text/html; charset=utf-8";
  if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".css") == 0) return "text/css; charset=utf-8";
  if (lower.size() >= 3 && lower.compare(lower.size() - 3, 3, ".js") == 0) return "application/javascript; charset=utf-8";
  if (lower.size() >= 5 && lower.compare(lower.size() - 5, 5, ".json") == 0) return "application/json; charset=utf-8";
  if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".png") == 0) return "image/png";
  if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".jpg") == 0) return "image/jpeg";
  if (lower.size() >= 5 && lower.compare(lower.size() - 5, 5, ".jpeg") == 0) return "image/jpeg";
  if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".svg") == 0) return "image/svg+xml";
  if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".ico") == 0) return "image/x-icon";
  if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".txt") == 0) return "text/plain; charset=utf-8";
  return "application/octet-stream";
}

static esp_err_t HandleAppWebFile(httpd_req_t* req) {
  std::string app_id;
  std::string filename;
  if (!ParseAppWebUri(req->uri, &app_id, &filename)) {
    SendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"invalid path\"}");
    return ESP_OK;
  }
  const std::string full_path = std::string("/littlefs/apps/") + app_id + "/" + filename;
  const std::string gz_path = full_path + ".gz";
  struct stat st = {};
  struct stat gz_st = {};
  const bool has_plain = (stat(full_path.c_str(), &st) == 0 && S_ISREG(st.st_mode));
  const bool has_gzip = (stat(gz_path.c_str(), &gz_st) == 0 && S_ISREG(gz_st.st_mode));
  if (has_gzip) {
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    if (SendFile(req, gz_path, ContentTypeFromFilename(filename))) return ESP_OK;
  } else if (has_plain) {
    if (SendFile(req, full_path, ContentTypeFromFilename(filename))) return ESP_OK;
  } else {
    SendJson(req, "404 Not Found", "{\"ok\":false,\"error\":\"file not found\"}");
    return ESP_OK;
  }
  SendJson(req, "500 Internal Server Error", "{\"ok\":false,\"error\":\"send failed\"}");
  return ESP_OK;
}

static esp_err_t HandleSystemStatus(httpd_req_t* req) {
  WifiStatusInfo wifi = {};
  (void)WifiManagerGetStatus(&wifi);

  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_app_desc_t* desc = esp_app_get_description();
  const char* fw_version = SW_VERSION;
  if (!fw_version || !fw_version[0]) fw_version = desc ? desc->version : "unknown";
  const std::string store_channel = GetStoreChannel();
  const FirmwareOtaState fw_ota_state = GetFirmwareOtaState();
  time_t now = 0;
  time(&now);
  struct tm local_tm = {};
  localtime_r(&now, &local_tm);
  size_t littlefs_total = 0;
  size_t littlefs_used = 0;
  const bool littlefs_ok = esp_littlefs_info("littlefs", &littlefs_total, &littlefs_used) == ESP_OK;
  const unsigned littlefs_pct =
      (littlefs_ok && littlefs_total > 0)
          ? static_cast<unsigned>(((littlefs_used * 100U) + (littlefs_total / 2U)) / littlefs_total)
          : 0U;

  std::string body = "{\"ok\":true,\"firmware\":{\"project\":\"";
  AppendJsonEscaped(&body, desc ? desc->project_name : "unknown");
  body += "\",\"version\":\"";
  AppendJsonEscaped(&body, fw_version);
  body += "\",\"partition\":\"";
  AppendJsonEscaped(&body, running ? running->label : "?");
  body += "\",\"auto_ota_enabled\":";
  body += fw_ota_state.auto_enabled ? "true" : "false";
  body += ",\"ota_channel\":\"";
  AppendJsonEscaped(&body, store_channel);
  body += "\",\"ota_check_done\":";
  body += fw_ota_state.check_done ? "true" : "false";
  body += ",\"ota_update_available\":";
  body += fw_ota_state.update_available ? "true" : "false";
  body += ",\"ota_update_attempted\":";
  body += fw_ota_state.update_attempted ? "true" : "false";
  body += ",\"ota_update_success\":";
  body += fw_ota_state.update_success ? "true" : "false";
  body += ",\"ota_last_check_ms\":";
  body += std::to_string(static_cast<long long>(fw_ota_state.last_check_ms));
  body += ",\"ota_remote_version\":\"";
  AppendJsonEscaped(&body, fw_ota_state.remote_version);
  body += "\",\"ota_last_error\":\"";
  AppendJsonEscaped(&body, fw_ota_state.last_error);
  body += "\"},\"display\":{\"brightness\":";
  body += std::to_string(static_cast<unsigned>(DisplayControlGetBrightness()));
  body += "},\"storage\":{\"mounted\":";
  body += littlefs_ok ? "true" : "false";
  body += ",\"used_bytes\":";
  body += std::to_string(static_cast<unsigned long long>(littlefs_used));
  body += ",\"total_bytes\":";
  body += std::to_string(static_cast<unsigned long long>(littlefs_total));
  body += ",\"used_pct\":";
  body += std::to_string(littlefs_pct);
  body += "},\"time\":{\"unix\":";
  body += std::to_string(static_cast<long long>(now));
  body += ",\"local\":{\"year\":";
  body += std::to_string(local_tm.tm_year + 1900);
  body += ",\"month\":";
  body += std::to_string(local_tm.tm_mon + 1);
  body += ",\"day\":";
  body += std::to_string(local_tm.tm_mday);
  body += ",\"hour\":";
  body += std::to_string(local_tm.tm_hour);
  body += ",\"min\":";
  body += std::to_string(local_tm.tm_min);
  body += ",\"sec\":";
  body += std::to_string(local_tm.tm_sec);
  body += ",\"wday\":";
  body += std::to_string(local_tm.tm_wday + 1);
  body += "}},\"wifi\":{\"mode\":\"";
  AppendJsonEscaped(&body, wifi.mode);
  body += "\",\"saved_ssid\":\"";
  AppendJsonEscaped(&body, wifi.saved_ssid);
  body += "\",\"sta_ssid\":\"";
  AppendJsonEscaped(&body, wifi.sta_ssid);
  body += "\",\"sta_ip\":\"";
  AppendJsonEscaped(&body, wifi.sta_ip);
  body += "\",\"ap_ssid\":\"";
  AppendJsonEscaped(&body, wifi.ap_ssid);
  body += "\",\"ap_ip\":\"";
  AppendJsonEscaped(&body, wifi.ap_ip);
  body += "\",\"sta_connected\":";
  body += wifi.sta_connected ? "true" : "false";
  body += ",\"ap_active\":";
  body += wifi.ap_active ? "true" : "false";
  body += "}}";
  SendJson(req, "200 OK", body.c_str());
  return ESP_OK;
}

static esp_err_t HandleSystemWifiScan(httpd_req_t* req) {
  static constexpr int kMaxScanResults = 24;
  WifiScanResult results[kMaxScanResults] = {};
  const int count = WifiManagerScanNetworks(results, kMaxScanResults, 8000);

  std::string body = "{\"ok\":true,\"aps\":[";
  for (int i = 0; i < count; ++i) {
    if (i > 0) body += ",";
    body += "{\"ssid\":\"";
    AppendJsonEscaped(&body, results[i].ssid);
    body += "\",\"auth\":\"";
    AppendJsonEscaped(&body, results[i].auth);
    body += "\",\"rssi\":";
    body += std::to_string(static_cast<int>(results[i].rssi));
    body += ",\"strength\":";
    body += std::to_string(static_cast<unsigned>(results[i].strength));
    body += "}";
  }
  body += "]}";
  SendJson(req, "200 OK", body.c_str());
  return ESP_OK;
}

static esp_err_t HandleSystemWifiConfig(httpd_req_t* req) {
  std::string body;
  if (!ReadRequestBodyToString(req, 1024, &body)) {
    SendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"invalid body\"}");
    return ESP_OK;
  }

  cJSON* root = cJSON_ParseWithLength(body.c_str(), body.size());
  if (!root) {
    SendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"invalid json\"}");
    return ESP_OK;
  }

  const cJSON* ssid = cJSON_GetObjectItemCaseSensitive(root, "ssid");
  const cJSON* password = cJSON_GetObjectItemCaseSensitive(root, "password");
  const cJSON* reboot = cJSON_GetObjectItemCaseSensitive(root, "reboot");
  const char* ssid_str = cJSON_IsString(ssid) && ssid->valuestring ? ssid->valuestring : nullptr;
  const char* password_str = cJSON_IsString(password) && password->valuestring ? password->valuestring : "";
  const bool reboot_now = cJSON_IsBool(reboot) && cJSON_IsTrue(reboot);

  if (!ssid_str || !ssid_str[0]) {
    cJSON_Delete(root);
    SendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"ssid required\"}");
    return ESP_OK;
  }
  const size_t pwd_len = strnlen(password_str, 128);
  if (pwd_len > 0 && pwd_len < 8) {
    cJSON_Delete(root);
    SendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"password must be at least 8 chars for secured Wi-Fi\"}");
    return ESP_OK;
  }

  const bool ok = WifiManagerSaveStaCredentials(ssid_str, password_str);
  cJSON_Delete(root);
  if (!ok) {
    SendJson(req, "500 Internal Server Error", "{\"ok\":false,\"error\":\"save wifi failed\"}");
    return ESP_OK;
  }

  SendJson(req, "200 OK", reboot_now ? "{\"ok\":true,\"saved\":true,\"rebooting\":true}"
                                     : "{\"ok\":true,\"saved\":true,\"rebooting\":false}");
  if (reboot_now) {
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
  }
  return ESP_OK;
}

static esp_err_t HandleSystemBrightness(httpd_req_t* req) {
  std::string body;
  if (!ReadRequestBodyToString(req, 256, &body)) {
    SendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"invalid body\"}");
    return ESP_OK;
  }

  cJSON* root = cJSON_ParseWithLength(body.c_str(), body.size());
  if (!root) {
    SendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"invalid json\"}");
    return ESP_OK;
  }

  const cJSON* value = cJSON_GetObjectItemCaseSensitive(root, "brightness");
  if (!cJSON_IsNumber(value)) {
    cJSON_Delete(root);
    SendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"brightness required\"}");
    return ESP_OK;
  }

  int brightness = value->valueint;
  if (brightness < 0) brightness = 0;
  if (brightness > 255) brightness = 255;

  const bool ok = DisplayControlSetBrightness(static_cast<uint8_t>(brightness));
  cJSON_Delete(root);
  if (!ok) {
    SendJson(req, "503 Service Unavailable", "{\"ok\":false,\"error\":\"display unavailable\"}");
    return ESP_OK;
  }

  char resp[64];
  snprintf(resp, sizeof(resp), "{\"ok\":true,\"brightness\":%d}", brightness);
  SendJson(req, "200 OK", resp);
  return ESP_OK;
}

static esp_err_t HandleSystemLuaDataGet(httpd_req_t* req) {
  std::string key;
  std::string prefix;
  (void)ReadQueryParam(req, "key", &key);
  (void)ReadQueryParam(req, "prefix", &prefix);

  cJSON* root = LoadLuaDataRootForHttp();
  cJSON* resp = cJSON_CreateObject();
  cJSON_AddBoolToObject(resp, "ok", 1);
  cJSON* items = cJSON_CreateObject();
  cJSON_AddItemToObject(resp, "items", items);

  if (key.empty() && prefix.empty()) {
    cJSON* dup = cJSON_Duplicate(root, 1);
    if (!dup) dup = cJSON_CreateObject();
    cJSON_ReplaceItemInObject(resp, "items", dup);
  } else if (!key.empty()) {
    if (!IsValidLuaDataKey(key)) {
      cJSON_Delete(root);
      cJSON_Delete(resp);
      SendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"invalid key\"}");
      return ESP_OK;
    }
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key.c_str());
    if (item) {
      cJSON_AddItemToObject(items, key.c_str(), cJSON_Duplicate(item, 1));
    } else {
      cJSON_AddNullToObject(items, key.c_str());
    }
  } else {
    cJSON* child = root ? root->child : nullptr;
    while (child) {
      const char* name = child->string;
      if (name && strncmp(name, prefix.c_str(), prefix.size()) == 0) {
        cJSON_AddItemToObject(items, name, cJSON_Duplicate(child, 1));
      }
      child = child->next;
    }
  }

  char* rendered = cJSON_PrintUnformatted(resp);
  SendJson(req, "200 OK", rendered ? rendered : "{\"ok\":true,\"items\":{}}");
  if (rendered) cJSON_free(rendered);
  cJSON_Delete(root);
  cJSON_Delete(resp);
  return ESP_OK;
}

static esp_err_t HandleSystemLuaDataPost(httpd_req_t* req) {
  std::string body;
  if (!ReadRequestBodyToString(req, 8 * 1024, &body)) {
    SendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"invalid body\"}");
    return ESP_OK;
  }

  cJSON* payload = cJSON_ParseWithLength(body.c_str(), body.size());
  if (!payload || !cJSON_IsObject(payload)) {
    if (payload) cJSON_Delete(payload);
    SendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"invalid json\"}");
    return ESP_OK;
  }

  cJSON* root = LoadLuaDataRootForHttp();
  bool changed = false;

  cJSON* items = cJSON_GetObjectItemCaseSensitive(payload, "items");
  if (cJSON_IsObject(items)) {
    for (cJSON* child = items->child; child; child = child->next) {
      if (!child->string || !IsValidLuaDataKey(child->string)) {
        cJSON_Delete(root);
        cJSON_Delete(payload);
        SendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"invalid key\"}");
        return ESP_OK;
      }
      cJSON_DeleteItemFromObjectCaseSensitive(root, child->string);
      if (!cJSON_IsNull(child)) {
        cJSON_AddItemToObject(root, child->string, cJSON_Duplicate(child, 1));
      }
      changed = true;
    }
  } else {
    cJSON* key_item = cJSON_GetObjectItemCaseSensitive(payload, "key");
    const char* key = cJSON_IsString(key_item) ? key_item->valuestring : nullptr;
    if (!key || !IsValidLuaDataKey(key)) {
      cJSON_Delete(root);
      cJSON_Delete(payload);
      SendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"invalid key\"}");
      return ESP_OK;
    }
    cJSON* value_item = cJSON_GetObjectItemCaseSensitive(payload, "value");
    cJSON_DeleteItemFromObjectCaseSensitive(root, key);
    if (value_item && !cJSON_IsNull(value_item)) {
      cJSON_AddItemToObject(root, key, cJSON_Duplicate(value_item, 1));
    }
    changed = true;
  }

  std::string err;
  const bool ok = !changed || SaveLuaDataRootForHttp(root, &err);
  const bool auto_ota_enabled_now = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, kFirmwareAutoOtaKey));
  cJSON_Delete(root);
  cJSON_Delete(payload);
  if (!ok) {
    std::string resp = "{\"ok\":false,\"error\":\"";
    AppendJsonEscaped(&resp, err.empty() ? "save failed" : err);
    resp += "\"}";
    SendJson(req, "500 Internal Server Error", resp.c_str());
    return ESP_OK;
  }
  if (changed) {
    FirmwareOtaState st = GetFirmwareOtaState();
    st.auto_enabled = auto_ota_enabled_now;
    SetFirmwareOtaState(st);
  }
  SendJson(req, "200 OK", changed ? "{\"ok\":true}" : "{\"ok\":true,\"changed\":false}");
  return ESP_OK;
}

static esp_err_t HandleSystemLogs(httpd_req_t* req) {
  std::string scope;
  const bool app_only = ReadQueryParam(req, "scope", &scope) && scope == "app";
  uint8_t min_level = g_logs_min_level;
  std::string level_str;
  if (ReadQueryParam(req, "level", &level_str)) {
    uint8_t parsed = min_level;
    if (!ParseLogLevelParam(level_str, &parsed)) {
      SendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"invalid level\"}");
      return ESP_OK;
    }
    min_level = parsed;
  }
  std::string detailed_str;
  const bool detailed = (ReadQueryParam(req, "detailed", &detailed_str) &&
                         (detailed_str == "1" || detailed_str == "true")) ||
                        g_logs_detailed_default;

  uint32_t after_seq = 0;
  std::string after_str;
  if (ReadQueryParam(req, "after", &after_str)) {
    after_seq = static_cast<uint32_t>(strtoul(after_str.c_str(), nullptr, 10));
  }

  size_t limit = 80;
  std::string limit_str;
  if (ReadQueryParam(req, "limit", &limit_str)) {
    const unsigned long parsed = strtoul(limit_str.c_str(), nullptr, 10);
    if (parsed > 0) limit = static_cast<size_t>(parsed);
  }
  if (limit > 120) limit = 120;

  const auto entries = SnapshotCapturedLogs(after_seq, limit, app_only, min_level);
  const uint32_t next_seq = LatestCapturedLogSeq();

  std::string body = "{\"ok\":true,\"scope\":\"";
  body += app_only ? "app" : "all";
  body += "\",\"level\":\"";
  body += LogLevelName(min_level);
  body += "\",\"detailed\":";
  body += detailed ? "true" : "false";
  body += ",\"next_seq\":";
  body += std::to_string(next_seq);
  body += ",\"logs\":[";
  for (size_t i = 0; i < entries.size(); ++i) {
    const CapturedLogSnapshot& entry = entries[i];
    if (i > 0) body += ",";
    body += "{\"level\":\"";
    body += LogLevelName(entry.level);
    body += "\",\"kind\":\"";
    body += entry.is_app ? "app" : "system";
    body += "\",\"text\":\"";
    AppendJsonEscaped(&body, entry.text);
    body += "\"";
    if (detailed) {
      body += ",\"seq\":";
      body += std::to_string(entry.seq);
      body += ",\"ms\":";
      body += std::to_string(static_cast<long long>(entry.ms));
    }
    body += "}";
  }
  body += "]}";
  SendJson(req, "200 OK", body.c_str());
  return ESP_OK;
}

static esp_err_t HandleSystemLogsConfig(httpd_req_t* req) {
  if (!req) return ESP_OK;
  if (req->method == HTTP_GET) {
    std::string body = "{\"ok\":true,\"min_level\":\"";
    body += LogLevelName(g_logs_min_level);
    body += "\",\"detailed\":";
    body += g_logs_detailed_default ? "true" : "false";
    body += "}";
    SendJson(req, "200 OK", body.c_str());
    return ESP_OK;
  }
  std::string body;
  if (!ReadRequestBodyToString(req, 2048, &body)) {
    SendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"invalid body\"}");
    return ESP_OK;
  }
  cJSON* payload = cJSON_ParseWithLength(body.c_str(), body.size());
  if (!payload || !cJSON_IsObject(payload)) {
    if (payload) cJSON_Delete(payload);
    SendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"invalid json\"}");
    return ESP_OK;
  }
  cJSON* min_level = cJSON_GetObjectItemCaseSensitive(payload, "min_level");
  cJSON* detailed = cJSON_GetObjectItemCaseSensitive(payload, "detailed");
  if (min_level && !cJSON_IsString(min_level)) {
    cJSON_Delete(payload);
    SendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"min_level must be string\"}");
    return ESP_OK;
  }
  if (detailed && !cJSON_IsBool(detailed)) {
    cJSON_Delete(payload);
    SendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"detailed must be bool\"}");
    return ESP_OK;
  }
  if (min_level && min_level->valuestring) {
    uint8_t parsed = g_logs_min_level;
    if (!ParseLogLevelParam(min_level->valuestring, &parsed)) {
      cJSON_Delete(payload);
      SendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"invalid min_level\"}");
      return ESP_OK;
    }
    g_logs_min_level = parsed;
  }
  if (detailed) g_logs_detailed_default = cJSON_IsTrue(detailed);
  cJSON_Delete(payload);
  std::string resp = "{\"ok\":true,\"min_level\":\"";
  resp += LogLevelName(g_logs_min_level);
  resp += "\",\"detailed\":";
  resp += g_logs_detailed_default ? "true" : "false";
  resp += "}";
  SendJson(req, "200 OK", resp.c_str());
  return ESP_OK;
}

static esp_err_t HandleStoreIndex(httpd_req_t* req) {
  const std::string channel = GetStoreChannel();
  const char* default_url = (channel == "beta") ? kStoreBetaIndexUrl : kStoreStableIndexUrl;
  std::string body = "{\"ok\":true,\"channel\":\"";
  body += channel;
  body += "\",\"stable_index_url\":\"";
  body += kStoreStableIndexUrl;
  body += "\",\"beta_index_url\":\"";
  body += kStoreBetaIndexUrl;
  body += "\",\"default_index_url\":\"";
  body += default_url;
  body += "\"}";
  SendJson(req, "200 OK", body.c_str());
  return ESP_OK;
}

static esp_err_t HandleStoreChannel(httpd_req_t* req) {
  if (!req) return ESP_OK;
  if (req->method == HTTP_GET) {
    const std::string channel = GetStoreChannel();
    const char* default_url = (channel == "beta") ? kStoreBetaIndexUrl : kStoreStableIndexUrl;
    std::string body = "{\"ok\":true,\"channel\":\"";
    body += channel;
    body += "\",\"default_index_url\":\"";
    body += default_url;
    body += "\"}";
    SendJson(req, "200 OK", body.c_str());
    return ESP_OK;
  }

  std::string body;
  if (!ReadRequestBodyToString(req, 512, &body)) {
    SendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"invalid body\"}");
    return ESP_OK;
  }
  cJSON* payload = cJSON_ParseWithLength(body.c_str(), body.size());
  if (!payload || !cJSON_IsObject(payload)) {
    if (payload) cJSON_Delete(payload);
    SendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"invalid json\"}");
    return ESP_OK;
  }
  const cJSON* channel = cJSON_GetObjectItemCaseSensitive(payload, "channel");
  const char* ch = cJSON_IsString(channel) ? channel->valuestring : nullptr;
  if (!ch || (strcmp(ch, "stable") != 0 && strcmp(ch, "beta") != 0)) {
    cJSON_Delete(payload);
    SendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"channel must be stable or beta\"}");
    return ESP_OK;
  }
  std::string err;
  const bool ok = SetStoreChannel(ch, &err);
  cJSON_Delete(payload);
  if (!ok) {
    std::string resp = "{\"ok\":false,\"error\":\"";
    AppendJsonEscaped(&resp, err.empty() ? "save failed" : err);
    resp += "\"}";
    SendJson(req, "500 Internal Server Error", resp.c_str());
    return ESP_OK;
  }
  const std::string normalized = NormalizeStoreChannel(ch);
  const char* default_url = (normalized == "beta") ? kStoreBetaIndexUrl : kStoreStableIndexUrl;
  std::string resp = "{\"ok\":true,\"channel\":\"";
  resp += normalized;
  resp += "\",\"default_index_url\":\"";
  resp += default_url;
  resp += "\"}";
  SendJson(req, "200 OK", resp.c_str());
  return ESP_OK;
}

static void SchedulerTickLocked() {
  if (!g_scheduler.config) return;
  const time_t now_t_pre = time(nullptr);
  struct tm now_tm_pre = {};
  localtime_r(&now_t_pre, &now_tm_pre);
  const int now_mins_pre = now_tm_pre.tm_hour * 60 + now_tm_pre.tm_min;
  int off_m = 23 * 60, on_m = 7 * 60;
  cJSON* ps_en = cJSON_GetObjectItemCaseSensitive(g_scheduler.config, "powerSaverEnabled");
  cJSON* off_s = cJSON_GetObjectItemCaseSensitive(g_scheduler.config, "screenOffStart");
  cJSON* on_s = cJSON_GetObjectItemCaseSensitive(g_scheduler.config, "screenOnStart");
  cJSON* tmp_ps = cJSON_CreateObject();
  cJSON_AddStringToObject(tmp_ps, "s", (cJSON_IsString(off_s) && off_s->valuestring) ? off_s->valuestring : "23:00");
  cJSON_AddStringToObject(tmp_ps, "e", (cJSON_IsString(on_s) && on_s->valuestring) ? on_s->valuestring : "07:00");
  const bool ps_ok = ParseHm(tmp_ps, "s", &off_m) && ParseHm(tmp_ps, "e", &on_m) && off_m != on_m;
  cJSON_Delete(tmp_ps);
  const bool power_saver_enabled = cJSON_IsBool(ps_en) && cJSON_IsTrue(ps_en);
  const int64_t uptime_s = esp_timer_get_time() / 1000000;
  const bool power_saver_allowed = uptime_s >= kPowerSaverBootGraceSeconds;
  if (power_saver_enabled && ps_ok && power_saver_allowed) {
    const bool off_range = (off_m < on_m) ? (now_mins_pre >= off_m && now_mins_pre < on_m)
                                          : (now_mins_pre >= off_m || now_mins_pre < on_m);
    if (off_range && !g_scheduler.screen_forced_off && DisplayControlIsReady()) {
      if (!g_scheduler.power_saver_pending_off) {
        const uint8_t cur = DisplayControlGetBrightness();
        g_scheduler.brightness_before_off = cur > 0 ? cur : g_scheduler.brightness_before_off;
        g_scheduler.power_saver_pending_off = true;
        g_scheduler.power_saver_off_at_unix = static_cast<int64_t>(now_t_pre) + kPowerSaverCountdownSeconds;
        g_scheduler.last_power_saver_countdown_sec = -1;
      }
      const int sec_left = static_cast<int>(g_scheduler.power_saver_off_at_unix - static_cast<int64_t>(now_t_pre));
      if (sec_left > 0) {
        if (sec_left != g_scheduler.last_power_saver_countdown_sec) {
          (void)DisplayControlShowSleepCountdown(sec_left);
          g_scheduler.last_power_saver_countdown_sec = sec_left;
        }
      } else {
        const uint8_t start = DisplayControlGetBrightness();
        for (int i = kPowerSaverFadeSteps - 1; i >= 0; --i) {
          const uint8_t level = static_cast<uint8_t>((static_cast<uint32_t>(start) * static_cast<uint32_t>(i)) / static_cast<uint32_t>(kPowerSaverFadeSteps));
          (void)DisplayControlSetBrightness(level);
          vTaskDelay(pdMS_TO_TICKS(kPowerSaverFadeStepMs));
        }
        (void)DisplayControlSetBrightness(0);
        g_scheduler.screen_forced_off = true;
        g_scheduler.power_saver_pending_off = false;
        g_scheduler.power_saver_off_at_unix = 0;
        g_scheduler.last_power_saver_countdown_sec = -1;
      }
    } else if (!off_range && g_scheduler.screen_forced_off && DisplayControlIsReady()) {
      const uint8_t restore = g_scheduler.brightness_before_off > 0 ? g_scheduler.brightness_before_off : 64;
      (void)DisplayControlSetBrightness(restore);
      g_scheduler.screen_forced_off = false;
      g_scheduler.power_saver_pending_off = false;
      g_scheduler.power_saver_off_at_unix = 0;
      g_scheduler.last_power_saver_countdown_sec = -1;
    } else if (!off_range && g_scheduler.power_saver_pending_off) {
      g_scheduler.power_saver_pending_off = false;
      g_scheduler.power_saver_off_at_unix = 0;
      g_scheduler.last_power_saver_countdown_sec = -1;
    }
  } else if (g_scheduler.screen_forced_off && DisplayControlIsReady()) {
    const uint8_t restore = g_scheduler.brightness_before_off > 0 ? g_scheduler.brightness_before_off : 64;
    (void)DisplayControlSetBrightness(restore);
    g_scheduler.screen_forced_off = false;
    g_scheduler.power_saver_pending_off = false;
    g_scheduler.power_saver_off_at_unix = 0;
    g_scheduler.last_power_saver_countdown_sec = -1;
  } else if (g_scheduler.power_saver_pending_off) {
    g_scheduler.power_saver_pending_off = false;
    g_scheduler.power_saver_off_at_unix = 0;
    g_scheduler.last_power_saver_countdown_sec = -1;
  }

  if (!g_scheduler.running) return;
  const int64_t now_unix = static_cast<int64_t>(time(nullptr));
  if (g_scheduler.manual_hold_until_unix > now_unix) return;
  cJSON* mode = cJSON_GetObjectItemCaseSensitive(g_scheduler.config, "mode");
  const std::string mode_s = (cJSON_IsString(mode) && mode->valuestring) ? mode->valuestring : "time";
  const time_t now_t = time(nullptr);
  struct tm now_tm = {};
  localtime_r(&now_t, &now_tm);
  const int now_mins = now_tm.tm_hour * 60 + now_tm.tm_min;
  const int weekday_mon0 = (now_tm.tm_wday + 6) % 7;

  if (mode_s == "loop") {
    cJSON* loop_items = cJSON_GetObjectItemCaseSensitive(g_scheduler.config, "loopItems");
    if (!cJSON_IsArray(loop_items) || cJSON_GetArraySize(loop_items) <= 0) return;
    if (g_scheduler.next_switch_unix <= now_t) {
      const int n = cJSON_GetArraySize(loop_items);
      if (n <= 0) return;
      const int idx = ((g_scheduler.loop_index % n) + n) % n;
      cJSON* it = cJSON_GetArrayItem(loop_items, idx);
      cJSON* app_id = cJSON_GetObjectItemCaseSensitive(it, "appId");
      cJSON* dur = cJSON_GetObjectItemCaseSensitive(it, "duration");
      if (cJSON_IsString(app_id) && app_id->valuestring && cJSON_IsNumber(dur)) {
        SchedulerSwitchApp(app_id->valuestring);
        g_scheduler.next_switch_unix = now_t + static_cast<int>(dur->valuedouble);
        g_scheduler.loop_index = (idx + 1) % n;
      }
    }
    return;
  }

  cJSON* time_items = cJSON_GetObjectItemCaseSensitive(g_scheduler.config, "timeItems");
  if (!cJSON_IsArray(time_items)) return;
  bool matched = false;
  cJSON* it = nullptr;
  cJSON_ArrayForEach(it, time_items) {
    if (!RuleMatchesWeekday(it, weekday_mon0)) continue;
    int sm = 0, em = 0;
    if (!ParseHm(it, "start", &sm) || !ParseHm(it, "end", &em)) continue;
    bool in_range = false;
    if (sm <= em) in_range = (now_mins >= sm && now_mins < em);
    else in_range = (now_mins >= sm || now_mins < em);
    if (!in_range) continue;
    cJSON* app_id = cJSON_GetObjectItemCaseSensitive(it, "appId");
    if (cJSON_IsString(app_id) && app_id->valuestring) {
      SchedulerSwitchApp(app_id->valuestring);
      matched = true;
      break;
    }
  }
  if (!matched) {
    cJSON* fallback_app = cJSON_GetObjectItemCaseSensitive(g_scheduler.config, "fallbackAppId");
    if (cJSON_IsString(fallback_app) && fallback_app->valuestring && fallback_app->valuestring[0]) {
      SchedulerSwitchApp(fallback_app->valuestring);
    }
  }
}

static void SchedulerTask(void*) {
  while (true) {
    if (g_scheduler_mu) xSemaphoreTake(g_scheduler_mu, portMAX_DELAY);
    SchedulerTickLocked();
    if (g_scheduler_mu) xSemaphoreGive(g_scheduler_mu);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

static esp_err_t HandleSchedulerConfigGet(httpd_req_t* req) {
  if (g_scheduler_mu) xSemaphoreTake(g_scheduler_mu, portMAX_DELAY);
  cJSON* dup = g_scheduler.config ? cJSON_Duplicate(g_scheduler.config, 1) : DefaultSchedulerConfig();
  if (dup) cJSON_ReplaceItemInObject(dup, "enabled", cJSON_CreateBool(g_scheduler.enabled));
  char* txt = dup ? cJSON_PrintUnformatted(dup) : nullptr;
  cJSON_Delete(dup);
  if (g_scheduler_mu) xSemaphoreGive(g_scheduler_mu);
  if (!txt) return httpd_resp_send_500(req);
  SendJson(req, "200 OK", txt);
  cJSON_free(txt);
  return ESP_OK;
}

static esp_err_t HandleSchedulerConfigPut(httpd_req_t* req) {
  std::string body;
  if (!ReadRequestBodyToString(req, 32 * 1024, &body)) {
    SendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"invalid body\"}");
    return ESP_OK;
  }
  cJSON* root = cJSON_ParseWithLength(body.c_str(), body.size());
  if (!root) {
    SendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"invalid json\"}");
    return ESP_OK;
  }
  std::string err;
  if (!ValidateSchedulerConfig(root, &err)) {
    cJSON_Delete(root);
    std::string j = std::string("{\"ok\":false,\"error\":\"") + err + "\"}";
    SendJson(req, "400 Bad Request", j.c_str());
    return ESP_OK;
  }
  char* txt = cJSON_PrintUnformatted(root);
  if (!txt || !EnsureDir(kLuaDataDir) || !SaveTextFileAtomic(kSchedulerConfigPath, txt ? txt : "")) {
    if (txt) cJSON_free(txt);
    cJSON_Delete(root);
    SendJson(req, "500 Internal Server Error", "{\"ok\":false,\"error\":\"save failed\"}");
    return ESP_OK;
  }
  cJSON_free(txt);
  if (g_scheduler_mu) xSemaphoreTake(g_scheduler_mu, portMAX_DELAY);
  if (g_scheduler.config) cJSON_Delete(g_scheduler.config);
  g_scheduler.config = root;
  cJSON* en = cJSON_GetObjectItemCaseSensitive(root, "enabled");
  cJSON* mode = cJSON_GetObjectItemCaseSensitive(root, "mode");
  g_scheduler.enabled = cJSON_IsBool(en) && cJSON_IsTrue(en);
  g_scheduler.running = g_scheduler.enabled;
  g_scheduler.mode = (cJSON_IsString(mode) && mode->valuestring) ? mode->valuestring : "time";
  g_scheduler.loop_index = 0;
  g_scheduler.next_switch_unix = 0;
  if (g_scheduler_mu) xSemaphoreGive(g_scheduler_mu);
  SendJson(req, "200 OK", "{\"ok\":true}");
  return ESP_OK;
}

static esp_err_t HandleSchedulerStart(httpd_req_t* req) {
  if (g_scheduler_mu) xSemaphoreTake(g_scheduler_mu, portMAX_DELAY);
  g_scheduler.running = true;
  g_scheduler.enabled = true;
  g_scheduler.next_switch_unix = 0;
  if (g_scheduler_mu) xSemaphoreGive(g_scheduler_mu);
  SendJson(req, "200 OK", "{\"ok\":true,\"running\":true}");
  return ESP_OK;
}

static esp_err_t HandleSchedulerStop(httpd_req_t* req) {
  if (g_scheduler_mu) xSemaphoreTake(g_scheduler_mu, portMAX_DELAY);
  g_scheduler.running = false;
  if (g_scheduler_mu) xSemaphoreGive(g_scheduler_mu);
  SendJson(req, "200 OK", "{\"ok\":true,\"running\":false}");
  return ESP_OK;
}

static esp_err_t HandleSchedulerStatus(httpd_req_t* req) {
  if (g_scheduler_mu) xSemaphoreTake(g_scheduler_mu, portMAX_DELAY);
  char body[320];
  snprintf(body, sizeof(body),
           "{\"ok\":true,\"enabled\":%s,\"running\":%s,\"mode\":\"%s\",\"last_app\":\"%s\",\"loop_index\":%d,\"next_switch_unix\":%lld}",
           g_scheduler.enabled ? "true" : "false", g_scheduler.running ? "true" : "false",
           g_scheduler.mode.c_str(), g_scheduler.last_app_id.c_str(), g_scheduler.loop_index,
           static_cast<long long>(g_scheduler.next_switch_unix));
  if (g_scheduler_mu) xSemaphoreGive(g_scheduler_mu);
  SendJson(req, "200 OK", body);
  return ESP_OK;
}

static esp_err_t HandleStoreUi(httpd_req_t* req) {
  return SendCompressedHtml(req, f_html_gz, f_html_gz_len);
}

static esp_err_t HandlePortalUi(httpd_req_t* req) {
  return SendCompressedHtml(req, portal_html_gz, portal_html_gz_len);
}

static esp_err_t HandleFavicon(httpd_req_t* req) {
  return SendCompressedBinary(req, favicon_ico_gz, favicon_ico_gz_len, "image/x-icon");
}

static esp_err_t HandleRoot(httpd_req_t* req) {
  if (ShouldUseCaptivePortal()) return HandlePortalUi(req);
  return HandleStoreUi(req);
}

static esp_err_t HandleCaptiveProbe(httpd_req_t* req) {
  if (ShouldUseCaptivePortal()) return SendPortalRedirect(req);
  httpd_resp_set_status(req, "204 No Content");
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "Connection", "close");
  return httpd_resp_send(req, nullptr, 0);
}

static esp_err_t SendGatewayRedirect(httpd_req_t* req) {
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "Connection", "close");
  return httpd_resp_send(req, "Redirecting to gateway...", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t HandleAppleCaptiveProbe(httpd_req_t* req) {
  if (ShouldUseCaptivePortal()) return SendGatewayRedirect(req);
  return HandleStoreUi(req);
}

static esp_err_t HandleHttp404(httpd_req_t* req, httpd_err_code_t err) {
  (void)err;
  if (!req) return ESP_FAIL;
  if (req->method == HTTP_GET || req->method == HTTP_HEAD) {
    return ShouldUseCaptivePortal() ? HandlePortalUi(req) : HandleStoreUi(req);
  }
  return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
}

static inline uint8_t Expand5(uint16_t v) { return static_cast<uint8_t>((v << 3) | (v >> 2)); }
static inline uint8_t Expand6(uint16_t v) { return static_cast<uint8_t>((v << 2) | (v >> 4)); }

static esp_err_t HandleScreenCapturePpm(httpd_req_t* req) {
  bool allow_capture = false;
  portENTER_CRITICAL(&g_capture_mux);
  if (!g_capture_busy) {
    g_capture_busy = true;
    allow_capture = true;
  }
  portEXIT_CRITICAL(&g_capture_mux);
  if (!allow_capture) {
    SendJson(req, "429 Too Many Requests", "{\"ok\":false,\"error\":\"capture busy\"}");
    return ESP_OK;
  }

  uint16_t frame[64 * 32];
  size_t w = 0;
  size_t h = 0;
  if (!LvglCaptureLuaAppFrameRgb565(frame, sizeof(frame) / sizeof(frame[0]), &w, &h) || w != 64 || h != 32) {
    portENTER_CRITICAL(&g_capture_mux);
    g_capture_busy = false;
    portEXIT_CRITICAL(&g_capture_mux);
    SendJson(req, "503 Service Unavailable", "{\"ok\":false,\"error\":\"capture unavailable\"}");
    return ESP_OK;
  }

  httpd_resp_set_status(req, "200 OK");
  httpd_resp_set_type(req, "image/x-portable-pixmap");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "Connection", "close");

  char header[64];
  const int header_len = snprintf(header, sizeof(header), "P6\n%u %u\n255\n", static_cast<unsigned>(w),
                                  static_cast<unsigned>(h));
  if (header_len <= 0 || httpd_resp_send_chunk(req, header, static_cast<size_t>(header_len)) != ESP_OK) {
    portENTER_CRITICAL(&g_capture_mux);
    g_capture_busy = false;
    portEXIT_CRITICAL(&g_capture_mux);
    return ESP_FAIL;
  }

  uint8_t row[64 * 3];
  for (size_t y = 0; y < h; y++) {
    for (size_t x = 0; x < w; x++) {
      const uint16_t c = frame[y * 64 + x];
      const uint8_t r = Expand5((c >> 11) & 0x1F);
      const uint8_t g = Expand6((c >> 5) & 0x3F);
      const uint8_t b = Expand5(c & 0x1F);
      row[x * 3 + 0] = r;
      row[x * 3 + 1] = g;
      row[x * 3 + 2] = b;
    }
    if (httpd_resp_send_chunk(req, reinterpret_cast<const char*>(row), sizeof(row)) != ESP_OK) {
      portENTER_CRITICAL(&g_capture_mux);
      g_capture_busy = false;
      portEXIT_CRITICAL(&g_capture_mux);
      return ESP_FAIL;
    }
  }
  (void)httpd_resp_send_chunk(req, nullptr, 0);
  portENTER_CRITICAL(&g_capture_mux);
  g_capture_busy = false;
  portEXIT_CRITICAL(&g_capture_mux);
  return ESP_OK;
}

}  // namespace

bool AppUpdateServerStart(AppUpdateReloadCallback reload_cb, AppUpdateSwitchCallback switch_cb) {
  g_reload_cb = reload_cb;
  g_switch_cb = switch_cb;
  if (!g_scheduler_mu) g_scheduler_mu = xSemaphoreCreateMutex();
  if (!g_firmware_ota_mu) g_firmware_ota_mu = xSemaphoreCreateMutex();
  {
    FirmwareOtaState st = GetFirmwareOtaState();
    st.auto_enabled = GetFirmwareAutoOtaEnabled();
    SetFirmwareOtaState(st);
  }
  if (g_scheduler.config) {
    cJSON_Delete(g_scheduler.config);
    g_scheduler.config = nullptr;
  }
  g_scheduler.config = LoadSchedulerConfig();
  cJSON* en = g_scheduler.config ? cJSON_GetObjectItemCaseSensitive(g_scheduler.config, "enabled") : nullptr;
  cJSON* mode = g_scheduler.config ? cJSON_GetObjectItemCaseSensitive(g_scheduler.config, "mode") : nullptr;
  g_scheduler.enabled = cJSON_IsBool(en) && cJSON_IsTrue(en);
  g_scheduler.running = g_scheduler.enabled;
  g_scheduler.mode = (cJSON_IsString(mode) && mode->valuestring) ? mode->valuestring : "time";
  if (!g_scheduler_task) {
    xTaskCreate(SchedulerTask, "sched_tick", 4096, nullptr, 4, &g_scheduler_task);
  }
  if (g_httpd) return true;
  EnsureLogCaptureInstalled();
  if (!g_installed_apps_cache_mu) g_installed_apps_cache_mu = xSemaphoreCreateMutex();
  if (!g_trash_queue) g_trash_queue = xQueueCreate(12, sizeof(std::string*));
  if (!g_trash_task && g_trash_queue) {
    xTaskCreatePinnedToCore(TrashCleanerTask, "trash_cleaner", 4096, nullptr, 1, &g_trash_task, tskNO_AFFINITY);
    EnqueueExistingTrash();
  }

  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.uri_match_fn = httpd_uri_match_wildcard;
  // Keep headroom for new APIs; avoid startup failure when route count grows.
  cfg.max_uri_handlers = 64;
  cfg.stack_size = 8192;
  cfg.lru_purge_enable = true;
  cfg.keep_alive_enable = false;

  const esp_err_t start_ret = httpd_start(&g_httpd, &cfg);
  if (start_ret != ESP_OK) {
    ESP_LOGE(kTag, "httpd_start failed: %d", static_cast<int>(start_ret));
    g_httpd = nullptr;
    return false;
  }

  if (httpd_register_err_handler(g_httpd, HTTPD_404_NOT_FOUND, HandleHttp404) != ESP_OK) {
    ESP_LOGE(kTag, "register 404 handler failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t put_app = {};
  put_app.uri = "/api/apps/*";
  put_app.method = HTTP_PUT;
  put_app.handler = HandlePutAppFile;
  if (httpd_register_uri_handler(g_httpd, &put_app) != ESP_OK) {
    ESP_LOGE(kTag, "register PUT failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t del_app = {};
  del_app.uri = "/api/apps/*";
  del_app.method = HTTP_DELETE;
  del_app.handler = HandleDeleteAppFile;
  if (httpd_register_uri_handler(g_httpd, &del_app) != ESP_OK) {
    ESP_LOGE(kTag, "register DELETE failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t reload = {};
  reload.uri = "/api/apps/reload";
  reload.method = HTTP_POST;
  reload.handler = HandleReload;
  if (httpd_register_uri_handler(g_httpd, &reload) != ESP_OK) {
    ESP_LOGE(kTag, "register reload failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t sw = {};
  sw.uri = "/api/apps/switch*";
  sw.method = HTTP_POST;
  sw.handler = HandleSwitch;
  if (httpd_register_uri_handler(g_httpd, &sw) != ESP_OK) {
    ESP_LOGE(kTag, "register switch failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t install_begin = {};
  install_begin.uri = "/api/apps/install/begin*";
  install_begin.method = HTTP_POST;
  install_begin.handler = HandleInstallBegin;
  if (httpd_register_uri_handler(g_httpd, &install_begin) != ESP_OK) {
    ESP_LOGE(kTag, "register install begin failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t install_commit = {};
  install_commit.uri = "/api/apps/install/commit";
  install_commit.method = HTTP_POST;
  install_commit.handler = HandleInstallCommit;
  if (httpd_register_uri_handler(g_httpd, &install_commit) != ESP_OK) {
    ESP_LOGE(kTag, "register install commit failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t install_abort = {};
  install_abort.uri = "/api/apps/install/abort";
  install_abort.method = HTTP_POST;
  install_abort.handler = HandleInstallAbort;
  if (httpd_register_uri_handler(g_httpd, &install_abort) != ESP_OK) {
    ESP_LOGE(kTag, "register install abort failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t ping = {};
  ping.uri = "/api/apps/ping";
  ping.method = HTTP_GET;
  ping.handler = HandlePing;
  if (httpd_register_uri_handler(g_httpd, &ping) != ESP_OK) {
    ESP_LOGE(kTag, "register ping failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t current = {};
  current.uri = "/api/apps/current";
  current.method = HTTP_GET;
  current.handler = HandleCurrentApp;
  if (httpd_register_uri_handler(g_httpd, &current) != ESP_OK) {
    ESP_LOGE(kTag, "register current app failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t fw_status = {};
  fw_status.uri = "/api/firmware";
  fw_status.method = HTTP_GET;
  fw_status.handler = HandleFirmwareStatus;
  if (httpd_register_uri_handler(g_httpd, &fw_status) != ESP_OK) {
    ESP_LOGE(kTag, "register firmware status failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t fw_ota = {};
  fw_ota.uri = "/api/firmware/ota";
  fw_ota.method = HTTP_POST;
  fw_ota.handler = HandleFirmwareOta;
  if (httpd_register_uri_handler(g_httpd, &fw_ota) != ESP_OK) {
    ESP_LOGE(kTag, "register firmware ota failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t reboot = {};
  reboot.uri = "/api/system/reboot";
  reboot.method = HTTP_POST;
  reboot.handler = HandleSystemReboot;
  if (httpd_register_uri_handler(g_httpd, &reboot) != ESP_OK) {
    ESP_LOGE(kTag, "register reboot failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t system_status = {};
  system_status.uri = "/api/system/status";
  system_status.method = HTTP_GET;
  system_status.handler = HandleSystemStatus;
  if (httpd_register_uri_handler(g_httpd, &system_status) != ESP_OK) {
    ESP_LOGE(kTag, "register system status failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t wifi_scan = {};
  wifi_scan.uri = "/api/system/wifi/scan";
  wifi_scan.method = HTTP_GET;
  wifi_scan.handler = HandleSystemWifiScan;
  if (httpd_register_uri_handler(g_httpd, &wifi_scan) != ESP_OK) {
    ESP_LOGE(kTag, "register wifi scan failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t wifi_config = {};
  wifi_config.uri = "/api/system/wifi/config";
  wifi_config.method = HTTP_POST;
  wifi_config.handler = HandleSystemWifiConfig;
  if (httpd_register_uri_handler(g_httpd, &wifi_config) != ESP_OK) {
    ESP_LOGE(kTag, "register wifi config failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t brightness = {};
  brightness.uri = "/api/system/display/brightness";
  brightness.method = HTTP_POST;
  brightness.handler = HandleSystemBrightness;
  if (httpd_register_uri_handler(g_httpd, &brightness) != ESP_OK) {
    ESP_LOGE(kTag, "register brightness failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t settings_get = {};
  settings_get.uri = "/api/system/settings";
  settings_get.method = HTTP_GET;
  settings_get.handler = HandleSystemLuaDataGet;
  if (httpd_register_uri_handler(g_httpd, &settings_get) != ESP_OK) {
    ESP_LOGE(kTag, "register settings get failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t settings_post = {};
  settings_post.uri = "/api/system/settings";
  settings_post.method = HTTP_POST;
  settings_post.handler = HandleSystemLuaDataPost;
  if (httpd_register_uri_handler(g_httpd, &settings_post) != ESP_OK) {
    ESP_LOGE(kTag, "register settings post failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t system_logs = {};
  system_logs.uri = "/api/system/logs";
  system_logs.method = HTTP_GET;
  system_logs.handler = HandleSystemLogs;
  if (httpd_register_uri_handler(g_httpd, &system_logs) != ESP_OK) {
    ESP_LOGE(kTag, "register system logs failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t logs_config_get = {};
  logs_config_get.uri = "/api/system/logs/config";
  logs_config_get.method = HTTP_GET;
  logs_config_get.handler = HandleSystemLogsConfig;
  if (httpd_register_uri_handler(g_httpd, &logs_config_get) != ESP_OK) {
    ESP_LOGE(kTag, "register logs config get failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t logs_config_post = {};
  logs_config_post.uri = "/api/system/logs/config";
  logs_config_post.method = HTTP_POST;
  logs_config_post.handler = HandleSystemLogsConfig;
  if (httpd_register_uri_handler(g_httpd, &logs_config_post) != ESP_OK) {
    ESP_LOGE(kTag, "register logs config post failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t installed = {};
  installed.uri = "/api/apps/list";
  installed.method = HTTP_GET;
  installed.handler = HandleInstalledApps;
  if (httpd_register_uri_handler(g_httpd, &installed) != ESP_OK) {
    ESP_LOGE(kTag, "register apps list failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t app_files = {};
  app_files.uri = "/api/apps/files/*";
  app_files.method = HTTP_GET;
  app_files.handler = HandleAppFilesList;
  if (httpd_register_uri_handler(g_httpd, &app_files) != ESP_OK) {
    ESP_LOGE(kTag, "register app files failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t thumb = {};
  thumb.uri = "/api/apps/thumbnail/*";
  thumb.method = HTTP_GET;
  thumb.handler = HandleAppThumbnail;
  if (httpd_register_uri_handler(g_httpd, &thumb) != ESP_OK) {
    ESP_LOGE(kTag, "register thumbnail failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t app_web = {};
  app_web.uri = "/api/apps/web/*";
  app_web.method = HTTP_GET;
  app_web.handler = HandleAppWebFile;
  if (httpd_register_uri_handler(g_httpd, &app_web) != ESP_OK) {
    ESP_LOGE(kTag, "register app web failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t store_index = {};
  store_index.uri = "/api/store/index";
  store_index.method = HTTP_GET;
  store_index.handler = HandleStoreIndex;
  if (httpd_register_uri_handler(g_httpd, &store_index) != ESP_OK) {
    ESP_LOGE(kTag, "register store index failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t store_channel_get = {};
  store_channel_get.uri = "/api/store/channel";
  store_channel_get.method = HTTP_GET;
  store_channel_get.handler = HandleStoreChannel;
  if (httpd_register_uri_handler(g_httpd, &store_channel_get) != ESP_OK) {
    ESP_LOGE(kTag, "register store channel GET failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t store_channel_post = {};
  store_channel_post.uri = "/api/store/channel";
  store_channel_post.method = HTTP_POST;
  store_channel_post.handler = HandleStoreChannel;
  if (httpd_register_uri_handler(g_httpd, &store_channel_post) != ESP_OK) {
    ESP_LOGE(kTag, "register store channel POST failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t sched_get = {};
  sched_get.uri = "/api/scheduler/config";
  sched_get.method = HTTP_GET;
  sched_get.handler = HandleSchedulerConfigGet;
  if (httpd_register_uri_handler(g_httpd, &sched_get) != ESP_OK) return false;
  httpd_uri_t sched_put = {};
  sched_put.uri = "/api/scheduler/config";
  sched_put.method = HTTP_PUT;
  sched_put.handler = HandleSchedulerConfigPut;
  if (httpd_register_uri_handler(g_httpd, &sched_put) != ESP_OK) return false;
  httpd_uri_t sched_start = {};
  sched_start.uri = "/api/scheduler/start";
  sched_start.method = HTTP_POST;
  sched_start.handler = HandleSchedulerStart;
  if (httpd_register_uri_handler(g_httpd, &sched_start) != ESP_OK) return false;
  httpd_uri_t sched_stop = {};
  sched_stop.uri = "/api/scheduler/stop";
  sched_stop.method = HTTP_POST;
  sched_stop.handler = HandleSchedulerStop;
  if (httpd_register_uri_handler(g_httpd, &sched_stop) != ESP_OK) return false;
  httpd_uri_t sched_status = {};
  sched_status.uri = "/api/scheduler/status";
  sched_status.method = HTTP_GET;
  sched_status.handler = HandleSchedulerStatus;
  if (httpd_register_uri_handler(g_httpd, &sched_status) != ESP_OK) return false;

  httpd_uri_t root = {};
  root.uri = "/";
  root.method = HTTP_GET;
  root.handler = HandleRoot;
  if (httpd_register_uri_handler(g_httpd, &root) != ESP_OK) {
    ESP_LOGE(kTag, "register root failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t f_html = {};
  f_html.uri = "/f.html";
  f_html.method = HTTP_GET;
  f_html.handler = HandleStoreUi;
  if (httpd_register_uri_handler(g_httpd, &f_html) != ESP_OK) {
    ESP_LOGE(kTag, "register f html failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t app_html = {};
  app_html.uri = "/app.html";
  app_html.method = HTTP_GET;
  app_html.handler = HandleStoreUi;
  if (httpd_register_uri_handler(g_httpd, &app_html) != ESP_OK) {
    ESP_LOGE(kTag, "register app html failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t favicon = {};
  favicon.uri = "/favicon.ico";
  favicon.method = HTTP_GET;
  favicon.handler = HandleFavicon;
  if (httpd_register_uri_handler(g_httpd, &favicon) != ESP_OK) {
    ESP_LOGE(kTag, "register favicon failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t portal_html = {};
  portal_html.uri = "/portal.html";
  portal_html.method = HTTP_GET;
  portal_html.handler = HandlePortalUi;
  if (httpd_register_uri_handler(g_httpd, &portal_html) != ESP_OK) {
    ESP_LOGE(kTag, "register portal html failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t i_html = {};
  i_html.uri = "/i.html";
  i_html.method = HTTP_GET;
  i_html.handler = HandlePortalUi;
  if (httpd_register_uri_handler(g_httpd, &i_html) != ESP_OK) {
    ESP_LOGE(kTag, "register i html failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t captive_generate_204 = {};
  captive_generate_204.uri = "/generate_204";
  captive_generate_204.method = HTTP_GET;
  captive_generate_204.handler = HandleCaptiveProbe;
  if (httpd_register_uri_handler(g_httpd, &captive_generate_204) != ESP_OK) {
    ESP_LOGE(kTag, "register captive /generate_204 failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t captive_gen_204 = {};
  captive_gen_204.uri = "/gen_204";
  captive_gen_204.method = HTTP_GET;
  captive_gen_204.handler = HandleCaptiveProbe;
  if (httpd_register_uri_handler(g_httpd, &captive_gen_204) != ESP_OK) {
    ESP_LOGE(kTag, "register captive /gen_204 failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t captive_hotspot = {};
  captive_hotspot.uri = "/hotspot-detect.html";
  captive_hotspot.method = HTTP_GET;
  captive_hotspot.handler = HandleAppleCaptiveProbe;
  if (httpd_register_uri_handler(g_httpd, &captive_hotspot) != ESP_OK) {
    ESP_LOGE(kTag, "register captive /hotspot-detect.html failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t captive_fmlink = {};
  captive_fmlink.uri = "/fwlink";
  captive_fmlink.method = HTTP_GET;
  captive_fmlink.handler = HandleCaptiveProbe;
  if (httpd_register_uri_handler(g_httpd, &captive_fmlink) != ESP_OK) {
    ESP_LOGE(kTag, "register captive /fwlink failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t captive_apple = {};
  captive_apple.uri = "/library/test/success.html";
  captive_apple.method = HTTP_GET;
  captive_apple.handler = HandleCaptiveProbe;
  if (httpd_register_uri_handler(g_httpd, &captive_apple) != ESP_OK) {
    ESP_LOGE(kTag, "register captive /library/test/success.html failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t captive_connecttest = {};
  captive_connecttest.uri = "/connecttest.txt";
  captive_connecttest.method = HTTP_GET;
  captive_connecttest.handler = HandleCaptiveProbe;
  if (httpd_register_uri_handler(g_httpd, &captive_connecttest) != ESP_OK) {
    ESP_LOGE(kTag, "register captive /connecttest.txt failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t captive_ncsi = {};
  captive_ncsi.uri = "/ncsi.txt";
  captive_ncsi.method = HTTP_GET;
  captive_ncsi.handler = HandleCaptiveProbe;
  if (httpd_register_uri_handler(g_httpd, &captive_ncsi) != ESP_OK) {
    ESP_LOGE(kTag, "register captive /ncsi.txt failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t captive_success = {};
  captive_success.uri = "/success.txt";
  captive_success.method = HTTP_GET;
  captive_success.handler = HandleCaptiveProbe;
  if (httpd_register_uri_handler(g_httpd, &captive_success) != ESP_OK) {
    ESP_LOGE(kTag, "register captive /success.txt failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t store_ui = {};
  store_ui.uri = "/api/store/ui";
  store_ui.method = HTTP_GET;
  store_ui.handler = HandleStoreUi;
  if (httpd_register_uri_handler(g_httpd, &store_ui) != ESP_OK) {
    ESP_LOGE(kTag, "register store ui failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t capture = {};
  capture.uri = "/api/screen/capture.ppm";
  capture.method = HTTP_GET;
  capture.handler = HandleScreenCapturePpm;
  if (httpd_register_uri_handler(g_httpd, &capture) != ESP_OK) {
    ESP_LOGE(kTag, "register capture failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  ESP_LOGI(kTag, "app update server started");
  if (!g_firmware_ota_task) {
    xTaskCreate(FirmwareAutoOtaTask, "fw_auto_ota", 9216, nullptr, 4, &g_firmware_ota_task);
  }
  return true;
}
static constexpr int kManualHoldSeconds = 10 * 60;

static void MarkManualHoldLocked() {
  const time_t now_t = time(nullptr);
  g_scheduler.manual_hold_until_unix = static_cast<int64_t>(now_t) + kManualHoldSeconds;
}

void AppUpdateServerNotifyManualAppAction(void) {
  if (g_scheduler_mu) xSemaphoreTake(g_scheduler_mu, portMAX_DELAY);
  MarkManualHoldLocked();
  const long long until_ts = static_cast<long long>(g_scheduler.manual_hold_until_unix);
  if (g_scheduler_mu) xSemaphoreGive(g_scheduler_mu);
  ESP_LOGI(kTag, "manual app action: scheduler hold for %d s (until=%lld)", kManualHoldSeconds, until_ts);
}
