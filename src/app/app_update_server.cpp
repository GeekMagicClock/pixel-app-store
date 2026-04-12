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

#include "cJSON.h"
#include "app/display_control.h"
#include "app/wifi_manager.h"
#include "esp_timer.h"
#include "esp_littlefs.h"
#include "esp_ota_ops.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "f_html_gz.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "ui/lvgl_lua_app_screen.h"

static const char* kTag = "app_update";

namespace {

static httpd_handle_t g_httpd = nullptr;
static AppUpdateReloadCallback g_reload_cb = nullptr;
static AppUpdateSwitchCallback g_switch_cb = nullptr;
static const char* kDefaultStoreIndexUrl =
    "http://ota.geekmagic.cc:8001/fw/pixel64x32V2/apps/apps-index.json";
static const char* kLuaDataDir = "/littlefs/.sys";
static const char* kLuaDataPath = "/littlefs/.sys/lua_data.json";
static const char* kInstalledAppsIndexPath = "/littlefs/.sys/installed_apps.json";
static constexpr size_t kLogRingSize = 160;
static constexpr size_t kLogTextBytes = 256;

struct CapturedLogEntry {
  uint32_t seq = 0;
  int64_t ms = 0;
  bool is_app = false;
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

static bool EnsureDir(const char* path);
static bool ReadSmallFile(const std::string& path, size_t max_bytes, std::string* out);

static bool LooksLikeAppLog(const char* text) {
  if (!text || !*text) return false;
  return strstr(text, " lua_app:") != nullptr || strstr(text, "[lua]") != nullptr;
}

static void CaptureLogLine(const char* text) {
  if (!text || !*text) return;

  char line[kLogTextBytes] = {};
  size_t len = strnlen(text, sizeof(line) - 1);
  while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r')) len--;
  if (len == 0) return;
  memcpy(line, text, len);
  line[len] = '\0';

  portENTER_CRITICAL(&g_log_mux);
  CapturedLogEntry& entry = g_log_ring[g_log_ring_head];
  entry.seq = ++g_log_last_seq;
  entry.ms = esp_timer_get_time() / 1000;
  entry.is_app = LooksLikeAppLog(line);
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
  std::string text;
};

static std::vector<CapturedLogSnapshot> SnapshotCapturedLogs(uint32_t after_seq, size_t limit, bool app_only) {
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
    copied[copied_count++] = entry;
  }
  portEXIT_CRITICAL(&g_log_mux);

  out.reserve(copied_count);
  for (size_t i = 0; i < copied_count; ++i) {
    CapturedLogSnapshot snap = {};
    snap.seq = copied[i].seq;
    snap.ms = copied[i].ms;
    snap.is_app = copied[i].is_app;
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

static void SendJson(httpd_req_t* req, const char* status, const char* json) {
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_status(req, status);
  httpd_resp_set_hdr(req, "Connection", "close");
  httpd_resp_sendstr(req, json);
}

static cJSON* LoadLuaDataRootForHttp() {
  std::string text;
  if (!ReadSmallFile(kLuaDataPath, 16 * 1024, &text) || text.empty()) {
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
  const std::string tmp_path = std::string(kLuaDataPath) + ".tmp";
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

  if (rename(tmp_path.c_str(), kLuaDataPath) == 0) return true;
  const int rename_errno_1 = errno;

  bool replaced = false;
  if (unlink(kLuaDataPath) == 0 || errno == ENOENT) {
    if (rename(tmp_path.c_str(), kLuaDataPath) == 0) {
      replaced = true;
    }
  }
  const int replace_errno = errno;
  if (replaced) return true;

  // Fallback: rewrite in place when LittleFS blocks unlink/rename due open FD.
  FILE* wf = fopen(kLuaDataPath, "wb");
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

  std::string entry;
  if (ReadAppManifestString(app_id, "entry", &entry)) {
    const bool safe_rel = !entry.empty() && entry.front() != '/' && entry.find("..") == std::string::npos;
    if (safe_rel) {
      const std::string configured = app_dir + "/" + entry;
      if (stat(configured.c_str(), &st) == 0 && S_ISREG(st.st_mode)) return true;
    }
  }

  const std::string main_lua = app_dir + "/main.lua";
  if (stat(main_lua.c_str(), &st) == 0 && S_ISREG(st.st_mode)) return true;

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

static void NotifyReloadRequested() {
  if (g_reload_cb) g_reload_cb();
}

static esp_err_t HandleFirmwareStatus(httpd_req_t* req) {
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_app_desc_t* desc = esp_app_get_description();
  char json[256];
  snprintf(json, sizeof(json),
           "{\"ok\":true,\"service\":\"firmware_ota\",\"project\":\"%s\",\"version\":\"%s\",\"partition\":\"%s\"}",
           desc ? desc->project_name : "unknown", desc ? desc->version : "unknown", running ? running->label : "?");
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

  if (!EnsureDir("/littlefs/apps")) {
    SendJson(req, "500 Internal Server Error", "{\"ok\":false,\"error\":\"mkdir /littlefs/apps failed\"}");
    return ESP_OK;
  }

  const std::string app_dir = std::string("/littlefs/apps/") + app_id;
  if (!EnsureDir(app_dir.c_str())) {
    SendJson(req, "500 Internal Server Error", "{\"ok\":false,\"error\":\"mkdir app dir failed\"}");
    return ESP_OK;
  }

  // Ensure nested parent dirs exist, e.g. icons/btc-24.png
  {
    size_t pos = 0;
    while (true) {
      pos = filename.find('/', pos);
      if (pos == std::string::npos) break;
      const std::string sub = filename.substr(0, pos);
      if (!sub.empty()) {
        const std::string sub_dir = app_dir + "/" + sub;
        if (!EnsureDir(sub_dir.c_str())) {
          SendJson(req, "500 Internal Server Error", "{\"ok\":false,\"error\":\"mkdir subdir failed\"}");
          return ESP_OK;
        }
      }
      pos++;
    }
  }

  const std::string final_path = app_dir + "/" + filename;
  const std::string tmp_path = final_path + ".tmp";
  (void)unlink(tmp_path.c_str());

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

    if (!RemoveTree(app_dir) || rmdir(app_dir.c_str()) != 0) {
      SendJson(req, "500 Internal Server Error", "{\"ok\":false,\"error\":\"uninstall failed\"}");
      return ESP_OK;
    }

    ESP_LOGI(kTag, "uninstalled app: %s", app_only_id.c_str());
    if (!RemoveInstalledAppsIndexForApp(app_only_id)) {
      InvalidateInstalledAppsCache();
    }
    SendJson(req, "200 OK", "{\"ok\":true,\"uninstalled\":true}");
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

static esp_err_t HandleReload(httpd_req_t* req) {
  InvalidateInstalledAppsCache();
  const std::string body = BuildInstalledAppsBody();
  if (!SaveInstalledAppsIndexBody(body)) {
    SendJson(req, "500 Internal Server Error", "{\"ok\":false,\"error\":\"refresh installed apps index failed\"}");
    return ESP_OK;
  }
  WriteInstalledAppsCache(body);
  NotifyReloadRequested();
  SendJson(req, "200 OK", "{\"ok\":true,\"reloaded\":true}");
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
      body = BuildInstalledAppsBody();
      (void)SaveInstalledAppsIndexBody(body);
    }
    WriteInstalledAppsCache(body);
  }
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
  AppendJsonEscaped(&body, desc ? desc->version : "unknown");
  body += "\",\"partition\":\"";
  AppendJsonEscaped(&body, running ? running->label : "?");
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
  cJSON_Delete(root);
  cJSON_Delete(payload);
  if (!ok) {
    std::string resp = "{\"ok\":false,\"error\":\"";
    AppendJsonEscaped(&resp, err.empty() ? "save failed" : err);
    resp += "\"}";
    SendJson(req, "500 Internal Server Error", resp.c_str());
    return ESP_OK;
  }
  SendJson(req, "200 OK", changed ? "{\"ok\":true}" : "{\"ok\":true,\"changed\":false}");
  return ESP_OK;
}

static esp_err_t HandleSystemLogs(httpd_req_t* req) {
  std::string scope;
  const bool app_only = ReadQueryParam(req, "scope", &scope) && scope == "app";

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

  const auto entries = SnapshotCapturedLogs(after_seq, limit, app_only);
  const uint32_t next_seq = LatestCapturedLogSeq();

  std::string body = "{\"ok\":true,\"scope\":\"";
  body += app_only ? "app" : "all";
  body += "\",\"next_seq\":";
  body += std::to_string(next_seq);
  body += ",\"logs\":[";
  for (size_t i = 0; i < entries.size(); ++i) {
    const CapturedLogSnapshot& entry = entries[i];
    if (i > 0) body += ",";
    body += "{\"seq\":";
    body += std::to_string(entry.seq);
    body += ",\"ms\":";
    body += std::to_string(static_cast<long long>(entry.ms));
    body += ",\"kind\":\"";
    body += entry.is_app ? "app" : "system";
    body += "\",\"text\":\"";
    AppendJsonEscaped(&body, entry.text);
    body += "\"}";
  }
  body += "]}";
  SendJson(req, "200 OK", body.c_str());
  return ESP_OK;
}

static esp_err_t HandleStoreIndex(httpd_req_t* req) {
  std::string body = "{\"ok\":true,\"default_index_url\":\"";
  body += kDefaultStoreIndexUrl;
  body += "\"}";
  SendJson(req, "200 OK", body.c_str());
  return ESP_OK;
}

static esp_err_t HandleStoreUi(httpd_req_t* req) {
  return SendCompressedHtml(req, f_html_gz, f_html_gz_len);
}

static esp_err_t HandleRoot(httpd_req_t* req) {
  return HandleStoreUi(req);
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
  if (g_httpd) return true;
  EnsureLogCaptureInstalled();
  if (!g_installed_apps_cache_mu) g_installed_apps_cache_mu = xSemaphoreCreateMutex();

  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.uri_match_fn = httpd_uri_match_wildcard;
  cfg.max_uri_handlers = 30;
  cfg.stack_size = 8192;
  cfg.lru_purge_enable = true;
  cfg.keep_alive_enable = false;

  const esp_err_t start_ret = httpd_start(&g_httpd, &cfg);
  if (start_ret != ESP_OK) {
    ESP_LOGE(kTag, "httpd_start failed: %d", static_cast<int>(start_ret));
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

  httpd_uri_t lua_data_get = {};
  lua_data_get.uri = "/api/system/lua-data";
  lua_data_get.method = HTTP_GET;
  lua_data_get.handler = HandleSystemLuaDataGet;
  if (httpd_register_uri_handler(g_httpd, &lua_data_get) != ESP_OK) {
    ESP_LOGE(kTag, "register lua-data get failed");
    httpd_stop(g_httpd);
    g_httpd = nullptr;
    return false;
  }

  httpd_uri_t lua_data_post = {};
  lua_data_post.uri = "/api/system/lua-data";
  lua_data_post.method = HTTP_POST;
  lua_data_post.handler = HandleSystemLuaDataPost;
  if (httpd_register_uri_handler(g_httpd, &lua_data_post) != ESP_OK) {
    ESP_LOGE(kTag, "register lua-data post failed");
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
  return true;
}
