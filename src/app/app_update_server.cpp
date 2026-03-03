#include "app/app_update_server.h"

#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "ui/lvgl_lua_app_screen.h"

static const char* kTag = "app_update";

namespace {

static httpd_handle_t g_httpd = nullptr;
static AppUpdateReloadCallback g_reload_cb = nullptr;
static AppUpdateSwitchCallback g_switch_cb = nullptr;
static const char* kDefaultStoreIndexUrl =
    "https://raw.githubusercontent.com/GeekMagicClock/pixel-app-store/main/dist/store/apps-index.json";

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

static void NotifyReloadRequested() {
  if (g_reload_cb) g_reload_cb();
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

    const std::string main_lua = app_dir + "/main.lua";
    if (stat(main_lua.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;

    std::string version;
    (void)ReadAppVersion(app_id, &version);
    const bool has_thumb = AppHasThumbnail(app_id);

    if (!first) body += ",";
    first = false;
    body += "{\"id\":\"";
    body += app_id;
    body += "\",\"version\":\"";
    body += version;
    body += "\",\"has_thumbnail\":";
    body += has_thumb ? "true" : "false";
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

static esp_err_t HandleStoreIndex(httpd_req_t* req) {
  std::string body = "{\"ok\":true,\"default_index_url\":\"";
  body += kDefaultStoreIndexUrl;
  body += "\"}";
  SendJson(req, "200 OK", body.c_str());
  return ESP_OK;
}

static esp_err_t HandleStoreUi(httpd_req_t* req) {
  static const char kHtml[] = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <title>ESP32 App Store</title>
  <style>
    :root {
      --bg:#0f1115; --panel:#171a21; --panel2:#1f2430; --border:#2c3342;
      --text:#edf0f7; --muted:#a9b1c5; --ok:#3ecf8e; --warn:#ffb020; --bad:#ff6b6b;
    }
    * { box-sizing:border-box; }
    body { margin:0; padding:16px; background:var(--bg); color:var(--text); font-family: ui-monospace, SFMono-Regular, Menlo, monospace; }
    .toolbar { display:flex; gap:8px; flex-wrap:wrap; align-items:center; margin-bottom:12px; }
    input[type=text] { width:min(760px,100%); padding:8px; color:var(--text); background:#11151c; border:1px solid var(--border); border-radius:8px; }
    button { border:1px solid var(--border); color:var(--text); background:var(--panel2); border-radius:8px; padding:7px 10px; cursor:pointer; }
    button:hover { filter:brightness(1.1); }
    button:disabled { opacity:.5; cursor:not-allowed; }
    .muted { color:var(--muted); }
    .ok { color:var(--ok); }
    .err { color:var(--bad); }
    .warn { color:var(--warn); }
    #apps { display:flex; flex-direction:column; gap:14px; }
    .sec { border:1px solid var(--border); border-radius:10px; background:#131821; padding:8px; }
    .sec-hd { font-size:12px; color:#cfd7ea; margin:0 0 8px; }
    .sec-grid { display:grid; grid-template-columns:repeat(auto-fill,minmax(200px,1fr)); gap:8px; }
    .card { border:1px solid var(--border); background:var(--panel); border-radius:12px; overflow:hidden; }
    .thumb { height:116px; background:#0a0d12; display:flex; align-items:center; justify-content:center; position:relative; }
    .thumb img { width:100%; height:100%; object-fit:contain; image-rendering:pixelated; image-rendering:crisp-edges; }
    .placeholder { width:100%; height:100%; display:flex; align-items:center; justify-content:center; font-size:30px; color:#9da7c0; background:linear-gradient(135deg,#20283a,#121722); }
    .content { padding:8px; }
    .title { font-size:13px; font-weight:700; line-height:1.2; }
    .id { font-size:11px; color:var(--muted); margin-top:2px; }
    .desc { margin-top:4px; min-height:16px; font-size:11px; color:#ced5e5; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
    .status { margin-top:5px; font-size:11px; color:var(--muted); }
    .actions { margin-top:6px; display:flex; gap:4px; flex-wrap:wrap; }
    .actions button { padding:4px 7px; font-size:11px; }
    #log { margin-top:12px; min-height:100px; border:1px solid var(--border); border-radius:10px; background:#0a0d12; padding:8px; font-size:12px; }
  </style>
  <script src="https://cdn.jsdelivr.net/npm/jszip@3.10.1/dist/jszip.min.js"></script>
</head>
<body>
  <h3 style="margin:0 0 10px;">ESP32 App Store</h3>
  <div class="toolbar">
    <input id="indexUrl" />
    <button id="loadBtn">Load</button>
    <button id="refreshBtn">Refresh Installed</button>
    <label class="muted"><input id="switchAfter" type="checkbox" checked /> Switch after install</label>
  </div>
  <div id="meta" class="muted"></div>
  <div id="apps"></div>
  <h4 style="margin:14px 0 8px;">Log</h4>
  <div id="log"></div>

<script>
const logEl = document.getElementById('log');
const indexInput = document.getElementById('indexUrl');
const appsEl = document.getElementById('apps');
const metaEl = document.getElementById('meta');
const switchAfterEl = document.getElementById('switchAfter');
const LS_KEY = 'esp32_store_index_url';
let storeApps = [];
let installed = {};
let storeCategoryOrder = [];
let storeCategoryLabels = {};

function log(msg, cls) {
  const line = document.createElement('div');
  if (cls) line.className = cls;
  line.textContent = `[${new Date().toLocaleTimeString()}] ${msg}`;
  logEl.prepend(line);
}

function encPath(path) {
  return String(path || '').split('/').map(encodeURIComponent).join('/');
}

function normalizeApps(json) {
  if (Array.isArray(json)) return json;
  if (json && Array.isArray(json.apps)) return json.apps;
  return [];
}

function toMap(arr) {
  const m = {};
  for (const it of arr || []) {
    const id = String(it.id || '').trim();
    if (!id) continue;
    m[id] = { version: String(it.version || '').trim(), has_thumbnail: !!it.has_thumbnail };
  }
  return m;
}

function semverCmp(a, b) {
  const pa = String(a || '').split('.').map(x => parseInt(x, 10) || 0);
  const pb = String(b || '').split('.').map(x => parseInt(x, 10) || 0);
  const n = Math.max(pa.length, pb.length);
  for (let i = 0; i < n; i++) {
    const da = pa[i] || 0, db = pb[i] || 0;
    if (da > db) return 1;
    if (da < db) return -1;
  }
  return 0;
}

function detectStripPrefix(paths) {
  if (!paths.length) return '';
  const first = paths[0].split('/');
  if (first.length < 2) return '';
  const prefix = first[0];
  if (!prefix || prefix.startsWith('.')) return '';
  for (const p of paths) {
    const parts = p.split('/');
    if (parts.length < 2 || parts[0] !== prefix) return '';
  }
  return prefix + '/';
}

async function api(method, path, body, contentType) {
  const h = {};
  if (contentType) h['Content-Type'] = contentType;
  const r = await fetch(path, { method, headers: h, body, cache: 'no-store' });
  if (!r.ok) throw new Error(`${method} ${path} -> HTTP ${r.status}`);
  return r;
}

async function installApp(item) {
  const id = String(item.id || item.app_id || '').trim();
  const zipUrl = String(item.zip_url || item.url || '').trim();
  if (!id || !zipUrl) throw new Error('invalid app item: missing id/zip_url');

  const zipFetchUrl = zipUrl + (zipUrl.includes('?') ? '&' : '?') + '_ts=' + Date.now();
  log(`install ${id} from ${zipFetchUrl}`);
  const resp = await fetch(zipFetchUrl, { cache: 'no-store' });
  if (!resp.ok) throw new Error(`download zip failed: HTTP ${resp.status}`);
  const zipBuf = await resp.arrayBuffer();

  const zip = await JSZip.loadAsync(zipBuf);
  const entries = Object.values(zip.files).filter(f => !f.dir);
  if (!entries.length) throw new Error('zip has no files');

  const rawPaths = entries.map(f => f.name).filter(n => !n.startsWith('__MACOSX/'));
  const strip = detectStripPrefix(rawPaths);
  const files = [];
  for (const e of entries) {
    if (e.name.startsWith('__MACOSX/')) continue;
    let rel = e.name;
    if (strip && rel.startsWith(strip)) rel = rel.substring(strip.length);
    if (!rel || rel.endsWith('/')) continue;
    files.push({ rel, entry: e });
  }
  if (!files.length) throw new Error('zip has no usable files');

  for (const f of files) {
    const bytes = await f.entry.async('uint8array');
    const path = `/api/apps/${encodeURIComponent(id)}/${encPath(f.rel)}`;
    await api('PUT', path, bytes);
    log(`uploaded ${id}/${f.rel}`);
  }

  const v = String(item.version || '').trim();
  if (v) {
    await api('PUT', `/api/apps/${encodeURIComponent(id)}/.store_version`, new TextEncoder().encode(v));
    log(`saved ${id}/.store_version=${v}`);
  }

  await api('POST', '/api/apps/reload');
  log(`reloaded apps`, 'ok');
  if (switchAfterEl.checked) {
    await api('POST', `/api/apps/switch/${encodeURIComponent(id)}`);
    log(`switched to ${id}`, 'ok');
  }
  log(`install done: ${id}`, 'ok');
  await refreshInstalled();
  renderApps(storeApps);
}

async function uninstallApp(id) {
  if (!confirm(`Uninstall ${id}?`)) return;
  await api('DELETE', `/api/apps/${encodeURIComponent(id)}`);
  await api('POST', '/api/apps/reload');
  log(`uninstalled ${id}`, 'ok');
  await refreshInstalled();
  renderApps(storeApps);
}

async function switchApp(id) {
  await api('POST', `/api/apps/switch/${encodeURIComponent(id)}`);
  log(`switched to ${id}`, 'ok');
}

function pickThumbUrl(item, id) {
  const cands = [
    item.thumbnail_url, item.thumb_url, item.icon_url,
    `/api/apps/thumbnail/${encodeURIComponent(id)}`
  ].filter(Boolean);
  return cands[0] || '';
}

function appStatus(item) {
  const id = String(item.id || item.app_id || '');
  const local = installed[id];
  const sv = String(item.version || '');
  if (!local) return '<span class="warn">Not installed</span>';
  if (!local.version) return `<span class="ok">Installed</span> <span class="muted">(store ${sv || '?'})</span>`;
  if (sv && semverCmp(sv, local.version) > 0) {
    return `<span class="warn">Update available</span> ${local.version} → ${sv}`;
  }
  return `<span class="ok">Up to date</span> ${local.version}`;
}

function categoryKey(raw) {
  const s = String(raw || '').trim().toLowerCase();
  return s || 'other';
}

function categoryLabel(key) {
  const k = String(key || 'other');
  if (storeCategoryLabels && typeof storeCategoryLabels === 'object' && storeCategoryLabels[k]) {
    return String(storeCategoryLabels[k]);
  }
  return k
    .replace(/[_-]+/g, ' ')
    .replace(/\b[a-z]/g, m => m.toUpperCase());
}

function renderApps(apps) {
  appsEl.innerHTML = '';
  if (!apps.length) {
    appsEl.innerHTML = '<div class="muted">No apps in index</div>';
    return;
  }
  const groups = {};
  const seen = new Set();
  const discovered = [];
  for (const it of apps) {
    const c = categoryKey(it.category);
    if (!groups[c]) groups[c] = [];
    groups[c].push(it);
    if (!seen.has(c)) {
      seen.add(c);
      discovered.push(c);
    }
  }

  const order = [];
  for (const c of (Array.isArray(storeCategoryOrder) ? storeCategoryOrder : [])) {
    const k = categoryKey(c);
    if (groups[k] && !order.includes(k)) order.push(k);
  }
  for (const c of discovered) {
    if (!order.includes(c)) order.push(c);
  }

  for (const c of order) {
    const arr = groups[c] || [];
    if (!arr.length) continue;
    const sec = document.createElement('section');
    sec.className = 'sec';
    const hd = document.createElement('h4');
    hd.className = 'sec-hd';
    hd.textContent = `${categoryLabel(c)} (${arr.length})`;
    const grid = document.createElement('div');
    grid.className = 'sec-grid';
    sec.appendChild(hd);
    sec.appendChild(grid);
    for (const it of arr) {
      const id = String(it.id || it.app_id || '');
      const title = String(it.name || id || 'Unknown');
      const desc = String(it.description || '');
      const ver = String(it.version || '');
      const thumb = pickThumbUrl(it, id);
      const card = document.createElement('div');
      card.className = 'card';
      card.innerHTML = `
        <div class="thumb">${thumb ? `<img loading="lazy" src="${thumb}" alt="${title}" />` : `<div class="placeholder">${(id[0] || '?').toUpperCase()}</div>`}</div>
        <div class="content">
          <div class="title">${title}</div>
          <div class="id">${id} · v${ver || '?'}</div>
          <div class="desc">${desc}</div>
          <div class="status">${appStatus(it)}</div>
          <div class="actions">
            <button data-a="install">Install</button>
            <button data-a="switch">Switch</button>
            <button data-a="uninstall">Uninstall</button>
          </div>
        </div>`;
      const installBtn = card.querySelector('[data-a="install"]');
      const switchBtn = card.querySelector('[data-a="switch"]');
      const uninstallBtn = card.querySelector('[data-a="uninstall"]');

      installBtn.onclick = async () => {
        installBtn.disabled = true;
        try { await installApp(it); }
        catch (e) { log(String(e && e.message || e), 'err'); }
        finally { installBtn.disabled = false; }
      };
      switchBtn.onclick = async () => {
        switchBtn.disabled = true;
        try { await switchApp(id); }
        catch (e) { log(String(e && e.message || e), 'err'); }
        finally { switchBtn.disabled = false; }
      };
      uninstallBtn.onclick = async () => {
        uninstallBtn.disabled = true;
        try { await uninstallApp(id); }
        catch (e) { log(String(e && e.message || e), 'err'); }
        finally { uninstallBtn.disabled = false; }
      };
      grid.appendChild(card);
    }
    appsEl.appendChild(sec);
  }
}

async function refreshInstalled() {
  try {
    const j = await fetch('/api/apps/list', { cache: 'no-store' }).then(r => r.json());
    installed = toMap(j.apps || []);
    log(`installed apps: ${Object.keys(installed).length}`, 'ok');
  } catch (e) {
    installed = {};
    log(`load installed failed: ${String(e && e.message || e)}`, 'err');
  }
}

async function loadIndex() {
  const url = indexInput.value.trim();
  if (!url) return;
  localStorage.setItem(LS_KEY, url);
  log(`loading ${url}`);
  const loadUrl = url + (url.includes('?') ? '&' : '?') + '_ts=' + Date.now();
  const r = await fetch(loadUrl, { cache: 'no-store' });
  if (!r.ok) throw new Error(`index fetch failed: HTTP ${r.status}`);
  const json = await r.json();
  storeApps = normalizeApps(json);
  storeCategoryOrder = (json && Array.isArray(json.category_order)) ? json.category_order.slice() : [];
  storeCategoryLabels = (json && json.category_labels && typeof json.category_labels === 'object') ? json.category_labels : {};
  await refreshInstalled();
  renderApps(storeApps);
  const count = storeApps.length;
  metaEl.textContent = `Loaded ${count} app(s) from ${url}`;
  log(`loaded ${count} app(s)`, 'ok');
}

document.getElementById('loadBtn').onclick = async () => {
  try { await loadIndex(); } catch (e) { log(String(e && e.message || e), 'err'); }
};
document.getElementById('refreshBtn').onclick = async () => {
  await refreshInstalled();
  renderApps(storeApps);
};

(async function init() {
  try {
    const conf = await fetch('/api/store/index', { cache: 'no-store' }).then(r => r.json()).catch(() => ({}));
    indexInput.value = localStorage.getItem(LS_KEY) || conf.default_index_url || '';
    if (indexInput.value) await loadIndex();
  } catch (e) {
    log(String(e && e.message || e), 'err');
  }
})();
</script>
</body>
</html>
)HTML";

  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_set_status(req, "200 OK");
  httpd_resp_send(req, kHtml, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
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
  cfg.max_uri_handlers = 15;
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
