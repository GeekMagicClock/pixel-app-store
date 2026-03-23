#include "app/app_update_server.h"

#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "cJSON.h"
#include "app/display_control.h"
#include "app/wifi_manager.h"
#include "esp_littlefs.h"
#include "esp_ota_ops.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "f_html_gz.h"
#include "ui/lvgl_lua_app_screen.h"

static const char* kTag = "app_update";

namespace {

static httpd_handle_t g_httpd = nullptr;
static AppUpdateReloadCallback g_reload_cb = nullptr;
static AppUpdateSwitchCallback g_switch_cb = nullptr;
static const char* kDefaultStoreIndexUrl =
    "http://ota.geekmagic.cc:8001/fw/pixel64x32V2/apps/apps-index.json";

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
  httpd_resp_sendstr(req, json);
}

static std::string UriWithoutQuery(const char* uri) {
  if (!uri) return {};
  const char* q = strchr(uri, '?');
  if (!q) return std::string(uri);
  return std::string(uri, static_cast<size_t>(q - uri));
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

static bool SendFile(httpd_req_t* req, const std::string& path, const char* content_type) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return false;
  httpd_resp_set_status(req, "200 OK");
  if (content_type) httpd_resp_set_type(req, content_type);
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
  SendJson(req, "200 OK", "{\"ok\":true,\"deleted\":true}");
  return ESP_OK;
}

static esp_err_t HandleReload(httpd_req_t* req) {
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

static esp_err_t HandleInstalledApps(httpd_req_t* req) {
  DIR* dir = opendir("/littlefs/apps");
  if (!dir) {
    SendJson(req, "200 OK", "{\"ok\":true,\"apps\":[]}");
    return ESP_OK;
  }

  std::string body = "{\"ok\":true,\"apps\":[";
  bool first = true;
  while (true) {
    struct dirent* ent = readdir(dir);
    if (!ent) break;
    const char* name = ent->d_name;
    if (!name) continue;
    if ((strcmp(name, ".") == 0) || (strcmp(name, "..") == 0)) continue;
    const std::string app_id = name;
    if (!IsValidAppId(app_id)) continue;

    const std::string app_dir = std::string("/littlefs/apps/") + app_id;
    struct stat st = {};
    if (stat(app_dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) continue;

    if (!AppHasLoadableEntry(app_id)) continue;

    std::string version;
    (void)ReadAppVersion(app_id, &version);
    std::string display_name;
    if (!ReadAppName(app_id, &display_name)) display_name = app_id;
    const bool has_thumb = AppHasThumbnail(app_id);

    if (!first) body += ",";
    first = false;
    body += "{\"id\":\"";
    AppendJsonEscaped(&body, app_id);
    body += "\",\"name\":\"";
    AppendJsonEscaped(&body, display_name);
    body += "\",\"version\":\"";
    AppendJsonEscaped(&body, version);
    body += "\",\"has_thumbnail\":";
    body += has_thumb ? "true" : "false";
    body += ",\"thumbnail_url\":\"";
    if (has_thumb) {
      body += "/api/apps/thumbnail/";
      AppendJsonEscaped(&body, app_id);
    }
    body += "\"";
    body += "}";
  }
  closedir(dir);
  body += "]}";
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

static esp_err_t HandleSystemStatus(httpd_req_t* req) {
  WifiStatusInfo wifi = {};
  (void)WifiManagerGetStatus(&wifi);

  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_app_desc_t* desc = esp_app_get_description();
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
  body += "},\"wifi\":{\"mode\":\"";
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
  uint16_t frame[64 * 32];
  size_t w = 0;
  size_t h = 0;
  if (!LvglCaptureLuaAppFrameRgb565(frame, sizeof(frame) / sizeof(frame[0]), &w, &h) || w != 64 || h != 32) {
    SendJson(req, "503 Service Unavailable", "{\"ok\":false,\"error\":\"capture unavailable\"}");
    return ESP_OK;
  }

  httpd_resp_set_status(req, "200 OK");
  httpd_resp_set_type(req, "image/x-portable-pixmap");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");

  char header[64];
  const int header_len = snprintf(header, sizeof(header), "P6\n%u %u\n255\n", static_cast<unsigned>(w),
                                  static_cast<unsigned>(h));
  if (header_len <= 0 || httpd_resp_send_chunk(req, header, static_cast<size_t>(header_len)) != ESP_OK) {
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
      return ESP_FAIL;
    }
  }
  (void)httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

}  // namespace

bool AppUpdateServerStart(AppUpdateReloadCallback reload_cb, AppUpdateSwitchCallback switch_cb) {
  g_reload_cb = reload_cb;
  g_switch_cb = switch_cb;
  if (g_httpd) return true;

  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.uri_match_fn = httpd_uri_match_wildcard;
  cfg.max_uri_handlers = 24;
  cfg.stack_size = 8192;

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
