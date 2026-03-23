#include "app/lua_app_runtime.h"

#include <cstdio>
#include <ctime>
#include <cstring>
#include <cstdint>
#include <climits>
#include <cerrno>
#include <memory>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <dirent.h>
#include <cstdlib>
#include <cctype>
#include <sys/stat.h>
#include <unistd.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/netdb.h"

#ifndef LUA_USE_C89
#define LUA_USE_C89 1
#endif

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

extern "C" {
#include "lvgl.h"
#include "misc/cache/instance/lv_image_cache.h"
#include "misc/cache/instance/lv_image_header_cache.h"
}

#include "third_party/animatedgif/AnimatedGIF.h"
#include "ui/lvgl_mem_utils.h"

static const char* kTag = "lua_app";

LV_FONT_DECLARE(lv_font_silkscreen_regular_8);
LV_FONT_DECLARE(lv_font_pressstart2p_regular_8);

namespace {

static constexpr const char* kRegistryAppKey = "__app";
static constexpr const char* kRegistryRenderKey = "__render";
static constexpr const char* kLuaDataDir = "/littlefs/.sys";
static constexpr const char* kLuaDataPath = "/littlefs/.sys/lua_data.json";
static void ReleaseGifPlayerForState(lua_State* L);

static bool TryParseLineIndex(const std::string& key, int* out_idx_1based) {
  if (out_idx_1based) *out_idx_1based = 0;
  if (key.size() < 5) return false;  // "line" + digit
  if (key.rfind("line", 0) != 0) return false;
  const char* p = key.c_str() + 4;
  if (!*p) return false;
  int v = 0;
  while (*p) {
    if (*p < '0' || *p > '9') return false;
    v = v * 10 + (*p - '0');
    p++;
  }
  if (v <= 0) return false;
  if (out_idx_1based) *out_idx_1based = v;
  return true;
}

static void SetFieldFn(lua_State* L, const char* table_name, const char* fn_name, lua_CFunction fn) {
  lua_getglobal(L, table_name);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_setglobal(L, table_name);
    lua_getglobal(L, table_name);
  }
  lua_pushcfunction(L, fn);
  lua_setfield(L, -2, fn_name);
  lua_pop(L, 1);
}

static bool ReadFileToString(const char* path, std::string* out) {
  if (out) out->clear();
  if (!path || !out) return false;
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  std::string s;
  char buf[256];
  while (true) {
    const size_t n = fread(buf, 1, sizeof(buf), f);
    if (n > 0) s.append(buf, n);
    if (n < sizeof(buf)) break;
  }
  fclose(f);
  *out = std::move(s);
  return true;
}

static bool EnsureDirExists(const char* path) {
  if (!path || !*path) return false;
  if (mkdir(path, 0755) == 0) return true;
  return errno == EEXIST;
}

static bool WriteStringToFile(const char* path, const std::string& text, std::string* out_err) {
  if (out_err) out_err->clear();
  if (!path || !*path) {
    if (out_err) *out_err = "invalid path";
    return false;
  }
  if (!EnsureDirExists(kLuaDataDir)) {
    if (out_err) *out_err = "mkdir failed";
    return false;
  }

  const std::string tmp_path = std::string(path) + ".tmp";
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
  if (rename(tmp_path.c_str(), path) != 0) {
    unlink(tmp_path.c_str());
    if (out_err) *out_err = "rename failed";
    return false;
  }
  return true;
}

static cJSON* LoadLuaDataRoot() {
  std::string text;
  if (!ReadFileToString(kLuaDataPath, &text) || text.empty()) {
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

static bool SaveLuaDataRoot(cJSON* root, std::string* out_err) {
  if (out_err) out_err->clear();
  if (!root || !cJSON_IsObject(root)) {
    if (out_err) *out_err = "invalid root";
    return false;
  }
  char* rendered = cJSON_PrintUnformatted(root);
  if (!rendered) {
    if (out_err) *out_err = "json render failed";
    return false;
  }
  const std::string text(rendered);
  cJSON_free(rendered);
  return WriteStringToFile(kLuaDataPath, text, out_err);
}

struct HttpReq {
  int id = 0;
  uintptr_t owner = 0;
  std::string url;
  int timeout_ms = 5000;
  int max_body_bytes = 4096;
  bool done = false;
  bool abandoned = false;
  int64_t done_ms = 0;
  int status = 0;
  std::string body_or_err;
};

static int64_t NowMs() { return esp_timer_get_time() / 1000; }

class HttpClientMgr;
static HttpClientMgr& HttpMgr();

static std::string UrlHost(const std::string& url) {
  const size_t scheme = url.find("://");
  const size_t host_start = (scheme == std::string::npos) ? 0 : (scheme + 3);
  if (host_start >= url.size()) return {};
  size_t host_end = url.find('/', host_start);
  if (host_end == std::string::npos) host_end = url.size();
  const size_t at = url.find('@', host_start);
  const size_t authority_start = (at != std::string::npos && at < host_end) ? (at + 1) : host_start;
  size_t port = url.find(':', authority_start);
  if (port == std::string::npos || port > host_end) port = host_end;
  return url.substr(authority_start, port - authority_start);
}

static std::string PrintablePreview(const char* data, size_t len) {
  if (!data || len == 0) return {};
  const size_t n = std::min<size_t>(len, 96);
  std::string out;
  out.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    const unsigned char ch = static_cast<unsigned char>(data[i]);
    out.push_back(std::isprint(ch) ? static_cast<char>(ch) : ' ');
  }
  return out;
}

static void LogHttpResolve(const HttpReq* req) {
  if (!req) return;
  const std::string host = UrlHost(req->url);
  if (host.empty()) return;

  struct addrinfo hints = {};
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo* result = nullptr;
  const int rc = getaddrinfo(host.c_str(), nullptr, &hints, &result);
  if (rc != 0 || !result) {
    ESP_LOGW(kTag, "http req dns failed id=%d host=%s rc=%d", req->id, host.c_str(), rc);
    if (result) freeaddrinfo(result);
    return;
  }

  char addr_buf[64] = {};
  bool logged = false;
  for (struct addrinfo* it = result; it; it = it->ai_next) {
    void* src = nullptr;
    if (it->ai_family == AF_INET) {
      src = &reinterpret_cast<struct sockaddr_in*>(it->ai_addr)->sin_addr;
    } else if (it->ai_family == AF_INET6) {
      src = &reinterpret_cast<struct sockaddr_in6*>(it->ai_addr)->sin6_addr;
    } else {
      continue;
    }
    if (inet_ntop(it->ai_family, src, addr_buf, sizeof(addr_buf))) {
      ESP_LOGI(kTag, "http req dns id=%d host=%s addr=%s", req->id, host.c_str(), addr_buf);
      logged = true;
      break;
    }
  }
  if (!logged) {
    ESP_LOGW(kTag, "http req dns unresolved id=%d host=%s", req->id, host.c_str());
  }
  freeaddrinfo(result);
}

class HttpClientMgr {
 public:
  HttpClientMgr() {
    mu_ = xSemaphoreCreateMutex();
    run_mu_ = xSemaphoreCreateBinary();
    if (run_mu_) (void)xSemaphoreGive(run_mu_);
  }
  ~HttpClientMgr() {
    if (mu_) vSemaphoreDelete(mu_);
    mu_ = nullptr;
    if (run_mu_) vSemaphoreDelete(run_mu_);
    run_mu_ = nullptr;
  }

  int StartGet(const std::string& url,
               int timeout_ms,
               int max_body_bytes,
               uintptr_t owner,
               std::string* out_err) {
    if (out_err) out_err->clear();
    if (!mu_) {
      if (out_err) *out_err = "mutex missing";
      return 0;
    }
    if (url.empty()) {
      if (out_err) *out_err = "url empty";
      return 0;
    }

    HttpReq* req = new HttpReq();
    req->id = ++next_id_;
    req->owner = owner;
    req->url = url;
    req->timeout_ms = timeout_ms;
    req->max_body_bytes = max_body_bytes;

    Lock();
    ReapDoneLocked(NowMs());
    reqs_[req->id] = req;
    Unlock();

    const BaseType_t ok = xTaskCreateWithCaps(&HttpTaskTrampoline,
                                              "lua_http",
                                              8192,
                                              req,
                                              tskIDLE_PRIORITY + 1,
                                              nullptr,
                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ok != pdPASS) {
      Lock();
      reqs_.erase(req->id);
      Unlock();
      delete req;
      if (out_err) *out_err = "xTaskCreate failed";
      return 0;
    }

    return req->id;
  }

  bool PollAndPop(int id, bool* out_done, int* out_status, std::string* out_body_or_err) {
    if (out_done) *out_done = false;
    if (out_status) *out_status = 0;
    if (out_body_or_err) out_body_or_err->clear();
    if (!mu_) return false;

    Lock();
    ReapDoneLocked(NowMs());
    auto it = reqs_.find(id);
    if (it == reqs_.end()) {
      Unlock();
      return false;
    }
    HttpReq* req = it->second;
    const bool abandoned = req->abandoned;
    if (!req->done || abandoned) {
      Unlock();
      return !abandoned;
    }

    if (out_done) *out_done = true;
    if (out_status) *out_status = req->status;
    if (out_body_or_err) *out_body_or_err = std::move(req->body_or_err);

    reqs_.erase(it);
    Unlock();
    delete req;
    return true;
  }

  void AbandonByOwner(uintptr_t owner) {
    if (!mu_ || owner == 0) return;
    Lock();
    const int64_t now = NowMs();
    for (auto it = reqs_.begin(); it != reqs_.end();) {
      HttpReq* req = it->second;
      if (!req) {
        it = reqs_.erase(it);
        continue;
      }
      if (req->owner != owner) {
        ++it;
        continue;
      }
      req->abandoned = true;
      req->owner = 0;
      if (req->done) {
        delete req;
        it = reqs_.erase(it);
      } else {
        ++it;
      }
    }
    ReapDoneLocked(now);
    Unlock();
  }

  void AbandonById(int id) {
    if (!mu_ || id <= 0) return;
    Lock();
    auto it = reqs_.find(id);
    if (it != reqs_.end() && it->second) {
      it->second->abandoned = true;
      it->second->owner = 0;
      if (it->second->done) {
        delete it->second;
        reqs_.erase(it);
      }
    }
    ReapDoneLocked(NowMs());
    Unlock();
  }

 private:
  void Lock() { (void)xSemaphoreTake(mu_, portMAX_DELAY); }
  void Unlock() { (void)xSemaphoreGive(mu_); }

  bool TakeRunSlot(HttpReq* req, int64_t* out_wait_ms) {
    if (out_wait_ms) *out_wait_ms = 0;
    if (!run_mu_) return true;
    const int64_t wait_start_ms = NowMs();
    while (true) {
      if (xSemaphoreTake(run_mu_, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (out_wait_ms) *out_wait_ms = NowMs() - wait_start_ms;
        return true;
      }
      if (req && req->abandoned) {
        if (out_wait_ms) *out_wait_ms = NowMs() - wait_start_ms;
        return false;
      }
    }
  }

  void ReleaseRunSlot() {
    if (run_mu_) (void)xSemaphoreGive(run_mu_);
  }

  void FinishById(int id, int status, std::string body_or_err) {
    if (!mu_ || id <= 0) return;
    Lock();
    auto it = reqs_.find(id);
    if (it != reqs_.end() && it->second) {
      HttpReq* req = it->second;
      req->status = status;
      req->body_or_err = std::move(body_or_err);
      req->done_ms = NowMs();
      req->done = true;
    }
    Unlock();
  }

  void ReapDoneLocked(int64_t now_ms) {
    static constexpr int64_t kDoneRetainMs = 10 * 1000;
    for (auto it = reqs_.begin(); it != reqs_.end();) {
      HttpReq* req = it->second;
      if (!req) {
        it = reqs_.erase(it);
        continue;
      }
      if (!req->done) {
        ++it;
        continue;
      }
      if (!req->abandoned && (now_ms - req->done_ms) < kDoneRetainMs) {
        ++it;
        continue;
      }
      delete req;
      it = reqs_.erase(it);
    }
  }

  static void HttpTaskTrampoline(void* arg) {
    HttpReq* req = static_cast<HttpReq*>(arg);
    DoHttp(req);
    vTaskDeleteWithCaps(nullptr);
  }

  static void DoHttp(HttpReq* req) {
    if (!req) return;
    const int req_id = req->id;
    const std::string url = req->url;
    const int timeout_ms = req->timeout_ms;
    const int max_body_bytes = req->max_body_bytes;
    const int64_t request_start_ms = NowMs();
    auto mem_snapshot = []() {
      const uint32_t free8 = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_8BIT));
      const uint32_t min8 = static_cast<uint32_t>(heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
      const uint32_t largest8 = static_cast<uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
      const uint32_t free_internal =
          static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
      const uint32_t min_internal =
          static_cast<uint32_t>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
      const uint32_t largest_internal =
          static_cast<uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
      const uint32_t free32 = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_32BIT));
      const uint32_t largest32 = static_cast<uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_32BIT));
      ESP_LOGI(kTag,
               "http mem free8=%u min8=%u largest8=%u free_internal=%u min_internal=%u largest_internal=%u free32=%u largest32=%u",
               static_cast<unsigned>(free8),
               static_cast<unsigned>(min8),
               static_cast<unsigned>(largest8),
               static_cast<unsigned>(free_internal),
               static_cast<unsigned>(min_internal),
               static_cast<unsigned>(largest_internal),
               static_cast<unsigned>(free32),
               static_cast<unsigned>(largest32));
    };

    struct RunSlotGuard {
      HttpClientMgr& mgr;
      bool held = false;
      ~RunSlotGuard() {
        if (held) mgr.ReleaseRunSlot();
      }
    } run_slot{HttpMgr(), false};

    int64_t queue_wait_ms = 0;
    if (!HttpMgr().TakeRunSlot(req, &queue_wait_ms)) {
      HttpMgr().FinishById(req_id, 0, "abandoned");
      return;
    }
    run_slot.held = true;

    // Make HTTP safe even if WiFi boot flow is skipped (e.g. debug UI simulation).
    // If WiFi isn't started/connected, the request will still fail, but should not crash.
    (void)esp_netif_init();
    {
      const esp_err_t loop_ret = esp_event_loop_create_default();
      if (loop_ret != ESP_OK && loop_ret != ESP_ERR_INVALID_STATE) {
        // Keep going; HTTP will likely fail anyway.
      }
    }

    ESP_LOGI(kTag,
             "http req start id=%d timeout=%d max_body=%d queue_wait_ms=%lld url=%s",
             req_id,
             timeout_ms,
             max_body_bytes,
             static_cast<long long>(queue_wait_ms),
             url.c_str());
    LogHttpResolve(req);

    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = timeout_ms;
    cfg.method = HTTP_METHOD_GET;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.disable_auto_redirect = false;
    cfg.user_agent = "esp32-pixel/1.0";
    cfg.keep_alive_enable = false;
    cfg.buffer_size = 4096;
    cfg.buffer_size_tx = 2048;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
      ESP_LOGW(kTag, "http init failed url=%s timeout=%d max_body=%d", url.c_str(), timeout_ms, max_body_bytes);
      mem_snapshot();
      HttpMgr().FinishById(req_id, 0, "http init failed");
      return;
    }

    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_header(client, "Cache-Control", "no-cache");
    esp_http_client_set_header(client, "Connection", "close");

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
      const int saved_errno = errno;
      ESP_LOGW(kTag,
               "http open failed err=%s(0x%x) errno=%d(%s) url=%s timeout=%d max_body=%d total_elapsed_ms=%lld",
               esp_err_to_name(err),
               static_cast<unsigned>(err),
               saved_errno,
               strerror(saved_errno),
               url.c_str(),
               timeout_ms,
               max_body_bytes,
               static_cast<long long>(NowMs() - request_start_ms));
      mem_snapshot();
      esp_http_client_cleanup(client);
      HttpMgr().FinishById(req_id, 0, esp_err_to_name(err));
      return;
    }

    const int fetch_ret = esp_http_client_fetch_headers(client);
    const int http_status = esp_http_client_get_status_code(client);
    const int64_t content_len = esp_http_client_get_content_length(client);
    const bool chunked = esp_http_client_is_chunked_response(client);
    ESP_LOGI(kTag,
             "http req headers id=%d fetch_ret=%d status=%d content_len=%lld chunked=%d elapsed_ms=%lld",
             req_id,
             fetch_ret,
             http_status,
             static_cast<long long>(content_len),
             chunked ? 1 : 0,
             static_cast<long long>(NowMs() - request_start_ms));
    if (fetch_ret < 0 && http_status <= 0) {
      ESP_LOGW(kTag,
               "http fetch headers failed id=%d err=%s(%d) url=%s",
               req_id,
               esp_err_to_name(static_cast<esp_err_t>(fetch_ret)),
               fetch_ret,
               url.c_str());
      mem_snapshot();
      (void)esp_http_client_close(client);
      esp_http_client_cleanup(client);
      HttpMgr().FinishById(req_id, 0, esp_err_to_name(static_cast<esp_err_t>(fetch_ret)));
      return;
    }

    const int cap = (max_body_bytes > 0) ? max_body_bytes : 256;
    char* body = static_cast<char*>(LvglAllocPreferPsram(static_cast<size_t>(cap) + 1));
    if (!body) {
      ESP_LOGW(kTag, "http body alloc failed cap=%d url=%s", cap, url.c_str());
      mem_snapshot();
      (void)esp_http_client_close(client);
      esp_http_client_cleanup(client);
      HttpMgr().FinishById(req_id, 0, "body alloc failed");
      return;
    }
    body[0] = '\0';

    char buf[1024];
    int total = 0;
    while (true) {
      const int r = esp_http_client_read(client, buf, sizeof(buf));
      if (r < 0) {
        ESP_LOGW(kTag, "http read failed id=%d url=%s", req_id, url.c_str());
        mem_snapshot();
        LvglFreePreferPsram(body);
        (void)esp_http_client_close(client);
        esp_http_client_cleanup(client);
        HttpMgr().FinishById(req_id, 0, "http read failed");
        return;
      }
      if (r == 0) break;

      if (total + r > cap) {
        ESP_LOGW(kTag, "http body too large cap=%d need=%d url=%s", cap, total + r, url.c_str());
        mem_snapshot();
        LvglFreePreferPsram(body);
        (void)esp_http_client_close(client);
        esp_http_client_cleanup(client);
        HttpMgr().FinishById(req_id, 0, "body too large");
        return;
      }

      memcpy(body + total, buf, static_cast<size_t>(r));
      total += r;
    }
    body[total] = '\0';

    (void)esp_http_client_close(client);
    esp_http_client_cleanup(client);

    static constexpr int kLogFullBodyBytes = 1024;
    if (total <= kLogFullBodyBytes) {
      ESP_LOGI(kTag,
               "http req done id=%d status=%d body_len=%d elapsed_ms=%lld body=%s",
               req_id,
               http_status,
               total,
               static_cast<long long>(NowMs() - request_start_ms),
               (total > 0) ? body : "<empty>");
    } else {
      const std::string body_truncated = PrintablePreview(body, static_cast<size_t>(kLogFullBodyBytes));
      ESP_LOGI(kTag,
               "http req done id=%d status=%d body_len=%d elapsed_ms=%lld body_truncated=%s",
               req_id,
               http_status,
               total,
               static_cast<long long>(NowMs() - request_start_ms),
               body_truncated.empty() ? "<empty>" : body_truncated.c_str());
    }
    HttpMgr().FinishById(req_id, http_status, std::string(body, static_cast<size_t>(total)));
    LvglFreePreferPsram(body);
  }

  SemaphoreHandle_t mu_ = nullptr;
  SemaphoreHandle_t run_mu_ = nullptr;
  int next_id_ = 0;
  std::unordered_map<int, HttpReq*> reqs_;
};

static HttpClientMgr& HttpMgr() {
  static HttpClientMgr g;
  return g;
}

struct HttpCacheEntry {
  std::string body;
  int64_t updated_ms = 0;
  int inflight_id = 0;
  uintptr_t owner = 0;
};

class CachedHttpMgr {
 public:
  CachedHttpMgr() { mu_ = xSemaphoreCreateMutex(); }
  ~CachedHttpMgr() {
    if (mu_) vSemaphoreDelete(mu_);
    mu_ = nullptr;
  }

  void ReleaseOwner(uintptr_t owner) {
    if (!mu_ || owner == 0) return;
    std::vector<int> ids_to_abandon;
    Lock();
    for (auto it = inflight_url_.begin(); it != inflight_url_.end();) {
      const int id = it->first;
      auto oit = inflight_owner_.find(id);
      const uintptr_t req_owner = (oit == inflight_owner_.end()) ? 0 : oit->second;
      if (req_owner == owner) {
        const std::string url = it->second;
        auto cit = cache_.find(url);
        if (cit != cache_.end() && cit->second.inflight_id == id) {
          cit->second.inflight_id = 0;
        }
        ids_to_abandon.push_back(id);
        it = inflight_url_.erase(it);
        if (oit != inflight_owner_.end()) inflight_owner_.erase(oit);
      } else {
        ++it;
      }
    }

    for (auto it = cache_.begin(); it != cache_.end();) {
      if (it->second.owner == owner && it->second.inflight_id == 0) {
        it = cache_.erase(it);
      } else {
        ++it;
      }
    }
    TrimCacheLocked();
    Unlock();

    for (int id : ids_to_abandon) {
      HttpMgr().AbandonById(id);
    }
  }

  bool CachedGet(const std::string& url,
                 int ttl_ms,
                 int timeout_ms,
                 int max_body_bytes,
                 uintptr_t owner,
                 int* out_req_id,
                 std::string* out_body,
                 int* out_age_ms,
                 std::string* out_err) {
    if (out_req_id) *out_req_id = 0;
    if (out_body) out_body->clear();
    if (out_age_ms) *out_age_ms = 0;
    if (out_err) out_err->clear();
    if (!mu_) {
      if (out_err) *out_err = "mutex missing";
      return false;
    }
    if (url.empty()) {
      if (out_err) *out_err = "url empty";
      return false;
    }
    if (ttl_ms < 0) ttl_ms = 0;

    const int64_t now = NowMs();

    Lock();
    TrimCacheLocked();
    HttpCacheEntry& e = cache_[url];
    if (owner != 0) e.owner = owner;
    const bool has_body = !e.body.empty() && e.updated_ms > 0;
    const int age = has_body ? static_cast<int>(now - e.updated_ms) : 0;
    const bool fresh = has_body && ttl_ms > 0 && age <= ttl_ms;

    if (fresh) {
      if (out_body) *out_body = e.body;
      if (out_age_ms) *out_age_ms = age;
      Unlock();
      return true;
    }

    // Stale/missing. If already inflight, reuse it and optionally return stale body.
    if (e.inflight_id > 0) {
      if (out_req_id) *out_req_id = e.inflight_id;
      if (has_body && out_body) *out_body = e.body;
      if (has_body && out_age_ms) *out_age_ms = age;
      Unlock();
      return true;
    }

    Unlock();

    // Start a new request (outside lock).
    std::string start_err;
    const int id = HttpMgr().StartGet(url, timeout_ms, max_body_bytes, owner, &start_err);
    if (id <= 0) {
      if (out_err) *out_err = start_err.empty() ? "http_get failed" : start_err;
      return false;
    }

    // Record inflight request.
    Lock();
    HttpCacheEntry& e2 = cache_[url];
    e2.inflight_id = id;
    if (owner != 0) e2.owner = owner;
    inflight_url_[id] = url;
    inflight_owner_[id] = owner;
    const bool has_body2 = !e2.body.empty() && e2.updated_ms > 0;
    const int age2 = has_body2 ? static_cast<int>(now - e2.updated_ms) : 0;
    if (out_req_id) *out_req_id = id;
    if (has_body2 && out_body) *out_body = e2.body;
    if (has_body2 && out_age_ms) *out_age_ms = age2;
    Unlock();
    return true;
  }

  bool CachedPoll(int id, bool* out_done, int* out_status, std::string* out_body_or_err) {
    if (out_done) *out_done = false;
    if (out_status) *out_status = 0;
    if (out_body_or_err) out_body_or_err->clear();
    if (id <= 0) return false;

    bool done = false;
    int status = 0;
    std::string body;
    const bool ok = HttpMgr().PollAndPop(id, &done, &status, &body);
    if (!ok) return false;

    if (!done) {
      if (out_done) *out_done = false;
      return true;
    }

    // Resolve URL and update cache (only keep successful 200 responses with non-empty body).
    std::string url;
    Lock();
    auto it = inflight_url_.find(id);
    if (it != inflight_url_.end()) {
      url = it->second;
      inflight_url_.erase(it);
      inflight_owner_.erase(id);
      auto cit = cache_.find(url);
      if (cit != cache_.end() && cit->second.inflight_id == id) {
        cit->second.inflight_id = 0;
        if (status == 200 && !body.empty()) {
          cit->second.body = body;
          cit->second.updated_ms = NowMs();
        }
      }
    }
    TrimCacheLocked();
    Unlock();

    if (out_done) *out_done = true;
    if (out_status) *out_status = status;
    if (out_body_or_err) *out_body_or_err = body;
    return true;
  }

 private:
  void Lock() { (void)xSemaphoreTake(mu_, portMAX_DELAY); }
  void Unlock() { (void)xSemaphoreGive(mu_); }

  void TrimCacheLocked() {
    static constexpr size_t kMaxEntries = 8;
    static constexpr size_t kMaxBodyBytes = 24 * 1024;

    auto current_bytes = [this]() -> size_t {
      size_t n = 0;
      for (const auto& kv : cache_) n += kv.second.body.size();
      return n;
    };

    size_t total = current_bytes();
    while ((cache_.size() > kMaxEntries || total > kMaxBodyBytes) && !cache_.empty()) {
      auto victim = cache_.end();
      int64_t oldest = INT64_MAX;
      for (auto it = cache_.begin(); it != cache_.end(); ++it) {
        const HttpCacheEntry& e = it->second;
        if (e.inflight_id > 0) continue;
        if (e.updated_ms <= oldest) {
          oldest = e.updated_ms;
          victim = it;
        }
      }
      if (victim == cache_.end()) break;
      total -= victim->second.body.size();
      cache_.erase(victim);
    }
  }

  SemaphoreHandle_t mu_ = nullptr;
  std::unordered_map<std::string, HttpCacheEntry> cache_;
  std::unordered_map<int, std::string> inflight_url_;
  std::unordered_map<int, uintptr_t> inflight_owner_;
};

static CachedHttpMgr& CachedMgr() {
  static CachedHttpMgr g;
  return g;
}

static bool ReadSmallTextFile(const std::string& path, std::string* out) {
  if (out) out->clear();
  if (!out) return false;
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return false;
  std::string s;
  char buf[256];
  while (true) {
    const size_t n = fread(buf, 1, sizeof(buf), f);
    if (n > 0) s.append(buf, n);
    if (n < sizeof(buf)) break;
  }
  fclose(f);
  *out = std::move(s);
  return true;
}

static std::string HexBytes(const uint8_t* p, size_t n) {
  static const char* kHex = "0123456789abcdef";
  if (!p || n == 0) return {};
  std::string s;
  s.reserve(n * 3);
  for (size_t i = 0; i < n; i++) {
    const uint8_t v = p[i];
    s.push_back(kHex[(v >> 4) & 0x0F]);
    s.push_back(kHex[v & 0x0F]);
    if (i + 1 < n) s.push_back(' ');
  }
  return s;
}

static void LogChunkHeader(const std::string& path, const uint8_t* head, size_t n, long file_size) {
  if (!head || n == 0) return;
  const bool is_luac = (n >= 4 && head[0] == 0x1b && head[1] == 'L' && head[2] == 'u' && head[3] == 'a');
  const std::string hex = HexBytes(head, n);
  if (!is_luac) {
    std::string preview;
    preview.reserve(n);
    for (size_t i = 0; i < n; i++) {
      const unsigned char c = head[i];
      preview.push_back(std::isprint(c) ? static_cast<char>(c) : ' ');
    }
    ESP_LOGI(kTag, "entry loaded text-like (%s) size=%ld head=%s", path.c_str(), file_size, preview.c_str());
    return;
  }

  int b_cint = -1, b_sizet = -1, b_ins = -1, b_int = -1, b_num = -1;
  if (n >= 17) {
    b_cint = static_cast<int>(head[12]);
    b_sizet = static_cast<int>(head[13]);
    b_ins = static_cast<int>(head[14]);
    b_int = static_cast<int>(head[15]);
    b_num = static_cast<int>(head[16]);
  }
  ESP_LOGI(kTag,
           "entry loaded bytecode (%s) size=%ld head_hex=%s",
           path.c_str(),
           file_size,
           hex.c_str());
  ESP_LOGI(kTag,
           "bytecode abi: cint=%d sizet=%d instr=%d int=%d num=%d ; runtime int=%u num=%u",
           b_cint,
           b_sizet,
           b_ins,
           b_int,
           b_num,
           static_cast<unsigned>(sizeof(lua_Integer)),
           static_cast<unsigned>(sizeof(lua_Number)));
}

static bool IsSafeEntryFilename(const std::string& s) {
  if (s.empty() || s.size() > 96) return false;
  if (s.find("..") != std::string::npos) return false;
  if (s.front() == '/' || s.back() == '/') return false;
  for (char ch : s) {
    const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                    ch == '_' || ch == '-' || ch == '.' || ch == '/';
    if (!ok) return false;
  }
  return true;
}

static std::string ResolveAppEntryFile(const std::string& app_dir) {
  // Default legacy entry.
  std::string entry = "main.lua";

  // Optional override from manifest.json: { "entry": "app.bin" }
  std::string manifest_txt;
  if (ReadSmallTextFile(app_dir + "/manifest.json", &manifest_txt)) {
    cJSON* root = cJSON_ParseWithLength(manifest_txt.c_str(), manifest_txt.size());
    if (root) {
      cJSON* e = cJSON_GetObjectItem(root, "entry");
      if (cJSON_IsString(e) && e->valuestring && e->valuestring[0]) {
        std::string v = e->valuestring;
        if (IsSafeEntryFilename(v)) {
          entry = v;
        }
      }
      cJSON_Delete(root);
    }
  }

  // If configured entry is missing, keep backward compatibility with main.lua.
  const std::string configured = app_dir + "/" + entry;
  FILE* f = fopen(configured.c_str(), "rb");
  if (f) {
    fclose(f);
    ESP_LOGI(kTag, "app entry resolved: %s", configured.c_str());
    return configured;
  }
  const std::string fallback = app_dir + "/main.lua";
  f = fopen(fallback.c_str(), "rb");
  if (f) {
    fclose(f);
    ESP_LOGW(kTag, "app entry fallback to main.lua: configured missing (%s)", configured.c_str());
    return fallback;
  }
  ESP_LOGW(kTag, "app entry configured but missing: %s", configured.c_str());
  return configured;
}

static void ReleaseNetResourcesForState(lua_State* L) {
  const uintptr_t owner = reinterpret_cast<uintptr_t>(L);
  CachedMgr().ReleaseOwner(owner);
  HttpMgr().AbandonByOwner(owner);
}

static int LuaNetHttpGet(lua_State* L) {
  const char* url = luaL_checkstring(L, 1);
  int timeout_ms = 5000;
  int max_body = 4096;
  if (lua_gettop(L) >= 2 && lua_isnumber(L, 2)) timeout_ms = static_cast<int>(lua_tointeger(L, 2));
  if (lua_gettop(L) >= 3 && lua_isnumber(L, 3)) max_body = static_cast<int>(lua_tointeger(L, 3));
  if (timeout_ms < 500) timeout_ms = 500;
  if (max_body < 256) max_body = 256;
  if (max_body > 64 * 1024) max_body = 64 * 1024;

  std::string err;
  const int id =
      HttpMgr().StartGet(url ? url : "", timeout_ms, max_body, reinterpret_cast<uintptr_t>(L), &err);
  if (id <= 0) {
    lua_pushnil(L);
    lua_pushstring(L, err.empty() ? "http_get failed" : err.c_str());
    return 2;
  }
  lua_pushinteger(L, static_cast<lua_Integer>(id));
  return 1;
}

static int LuaNetHttpPoll(lua_State* L) {
  const int id = static_cast<int>(luaL_checkinteger(L, 1));
  bool done = false;
  int status = 0;
  std::string body;
  const bool ok = HttpMgr().PollAndPop(id, &done, &status, &body);
  if (!ok) {
    lua_pushboolean(L, 1);
    lua_pushinteger(L, 0);
    lua_pushstring(L, "unknown request id");
    return 3;
  }

  lua_pushboolean(L, done ? 1 : 0);
  if (!done) return 1;

  lua_pushinteger(L, static_cast<lua_Integer>(status));
  lua_pushlstring(L, body.data(), body.size());
  return 3;
}

static int LuaNetCachedGet(lua_State* L) {
  const char* url = luaL_checkstring(L, 1);
  int ttl_ms = static_cast<int>(luaL_checkinteger(L, 2));
  int timeout_ms = 5000;
  int max_body = 4096;
  if (lua_gettop(L) >= 3 && lua_isnumber(L, 3)) timeout_ms = static_cast<int>(lua_tointeger(L, 3));
  if (lua_gettop(L) >= 4 && lua_isnumber(L, 4)) max_body = static_cast<int>(lua_tointeger(L, 4));
  if (ttl_ms < 0) ttl_ms = 0;
  if (timeout_ms < 500) timeout_ms = 500;
  if (max_body < 256) max_body = 256;
  if (max_body > 64 * 1024) max_body = 64 * 1024;

  int req_id = 0;
  std::string body;
  int age_ms = 0;
  std::string err;
  const bool ok = CachedMgr().CachedGet(url ? url : "",
                                        ttl_ms,
                                        timeout_ms,
                                        max_body,
                                        reinterpret_cast<uintptr_t>(L),
                                        &req_id,
                                        &body,
                                        &age_ms,
                                        &err);
  if (!ok) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushstring(L, err.empty() ? "cached_get failed" : err.c_str());
    return 4;
  }

  if (req_id > 0) lua_pushinteger(L, static_cast<lua_Integer>(req_id));
  else lua_pushnil(L);

  if (!body.empty()) lua_pushlstring(L, body.data(), body.size());
  else lua_pushnil(L);

  if (!body.empty()) lua_pushinteger(L, static_cast<lua_Integer>(age_ms));
  else lua_pushnil(L);

  lua_pushnil(L);
  return 4;
}

static int LuaNetCachedPoll(lua_State* L) {
  const int id = static_cast<int>(luaL_checkinteger(L, 1));
  bool done = false;
  int status = 0;
  std::string body;
  const bool ok = CachedMgr().CachedPoll(id, &done, &status, &body);
  if (!ok) {
    lua_pushboolean(L, 1);
    lua_pushinteger(L, 0);
    lua_pushstring(L, "unknown request id");
    return 3;
  }

  lua_pushboolean(L, done ? 1 : 0);
  if (!done) return 1;

  lua_pushinteger(L, static_cast<lua_Integer>(status));
  lua_pushlstring(L, body.data(), body.size());
  return 3;
}

static void PushNetModule(lua_State* L) {
  lua_newtable(L);
  lua_pushcfunction(L, LuaNetHttpGet);
  lua_setfield(L, -2, "http_get");
  lua_pushcfunction(L, LuaNetHttpPoll);
  lua_setfield(L, -2, "http_poll");
  lua_pushcfunction(L, LuaNetCachedGet);
  lua_setfield(L, -2, "cached_get");
  lua_pushcfunction(L, LuaNetCachedPoll);
  lua_setfield(L, -2, "cached_poll");
  lua_setglobal(L, "net");

  SetFieldFn(L, "net", "http_get", LuaNetHttpGet);
  SetFieldFn(L, "net", "http_poll", LuaNetHttpPoll);
  SetFieldFn(L, "net", "cached_get", LuaNetCachedGet);
  SetFieldFn(L, "net", "cached_poll", LuaNetCachedPoll);
}

static void JsonToLua(lua_State* L, const cJSON* v, int depth);
static cJSON* LuaToJson(lua_State* L, int idx, int depth);

static void JsonObjectToLua(lua_State* L, const cJSON* obj, int depth) {
  lua_newtable(L);
  const cJSON* it = obj ? obj->child : nullptr;
  while (it) {
    if (it->string) {
      lua_pushstring(L, it->string);
      JsonToLua(L, it, depth + 1);
      lua_settable(L, -3);
    }
    it = it->next;
  }
}

static void JsonArrayToLua(lua_State* L, const cJSON* arr, int depth) {
  lua_newtable(L);
  int idx = 1;
  const cJSON* it = arr ? arr->child : nullptr;
  while (it) {
    JsonToLua(L, it, depth + 1);
    lua_rawseti(L, -2, idx++);
    it = it->next;
  }
}

static void JsonToLua(lua_State* L, const cJSON* v, int depth) {
  if (depth > 24) {
    lua_pushnil(L);
    return;
  }
  if (!v) {
    lua_pushnil(L);
    return;
  }
  if (cJSON_IsNull(v)) {
    lua_pushnil(L);
  } else if (cJSON_IsBool(v)) {
    lua_pushboolean(L, cJSON_IsTrue(v));
  } else if (cJSON_IsNumber(v)) {
    lua_pushnumber(L, v->valuedouble);
  } else if (cJSON_IsString(v)) {
    lua_pushstring(L, v->valuestring ? v->valuestring : "");
  } else if (cJSON_IsArray(v)) {
    JsonArrayToLua(L, v, depth);
  } else if (cJSON_IsObject(v)) {
    JsonObjectToLua(L, v, depth);
  } else {
    lua_pushnil(L);
  }
}

static bool LuaTableIsDenseArray(lua_State* L, int idx, int* out_len) {
  if (out_len) *out_len = 0;
  idx = lua_absindex(L, idx);
  int count = 0;
  int max_index = 0;

  lua_pushnil(L);
  while (lua_next(L, idx) != 0) {
    if (!lua_isinteger(L, -2)) {
      lua_pop(L, 2);
      return false;
    }
    const lua_Integer raw = lua_tointeger(L, -2);
    if (raw < 1 || raw > INT_MAX) {
      lua_pop(L, 2);
      return false;
    }
    const int key = static_cast<int>(raw);
    if (key > max_index) max_index = key;
    count += 1;
    lua_pop(L, 1);
  }

  if (count == 0 || max_index != count) return false;
  if (out_len) *out_len = max_index;
  return true;
}

static cJSON* LuaTableToJson(lua_State* L, int idx, int depth) {
  idx = lua_absindex(L, idx);
  int arr_len = 0;
  if (LuaTableIsDenseArray(L, idx, &arr_len)) {
    cJSON* arr = cJSON_CreateArray();
    if (!arr) return nullptr;
    for (int i = 1; i <= arr_len; ++i) {
      lua_geti(L, idx, i);
      cJSON* item = LuaToJson(L, -1, depth + 1);
      lua_pop(L, 1);
      if (!item) item = cJSON_CreateNull();
      cJSON_AddItemToArray(arr, item);
    }
    return arr;
  }

  cJSON* obj = cJSON_CreateObject();
  if (!obj) return nullptr;
  lua_pushnil(L);
  while (lua_next(L, idx) != 0) {
    if (lua_type(L, -2) == LUA_TSTRING) {
      const char* key = lua_tostring(L, -2);
      if (key && *key) {
        cJSON* item = LuaToJson(L, -1, depth + 1);
        if (!item) item = cJSON_CreateNull();
        cJSON_AddItemToObject(obj, key, item);
      }
    }
    lua_pop(L, 1);
  }
  return obj;
}

static cJSON* LuaToJson(lua_State* L, int idx, int depth) {
  if (depth > 24) return cJSON_CreateNull();

  idx = lua_absindex(L, idx);
  switch (lua_type(L, idx)) {
    case LUA_TNIL:
      return cJSON_CreateNull();
    case LUA_TBOOLEAN:
      return cJSON_CreateBool(lua_toboolean(L, idx) ? 1 : 0);
    case LUA_TNUMBER:
      return cJSON_CreateNumber(lua_tonumber(L, idx));
    case LUA_TSTRING: {
      size_t len = 0;
      const char* s = lua_tolstring(L, idx, &len);
      return cJSON_CreateString(s ? s : "");
    }
    case LUA_TTABLE:
      return LuaTableToJson(L, idx, depth);
    default:
      return nullptr;
  }
}

static int LuaJsonDecode(lua_State* L) {
  size_t len = 0;
  const char* s = luaL_checklstring(L, 1, &len);
  if (!s || len == 0) {
    lua_pushnil(L);
    lua_pushstring(L, "empty json");
    return 2;
  }

  cJSON* root = cJSON_ParseWithLength(s, len);
  if (!root) {
    lua_pushnil(L);
    lua_pushstring(L, "json parse failed");
    return 2;
  }

  JsonToLua(L, root, 0);
  cJSON_Delete(root);
  return 1;
}

static void PushJsonModule(lua_State* L) {
  lua_newtable(L);
  lua_pushcfunction(L, LuaJsonDecode);
  lua_setfield(L, -2, "decode");
  lua_setglobal(L, "json");

  SetFieldFn(L, "json", "decode", LuaJsonDecode);
}

}  // namespace

LuaAppRuntime::LuaAppRuntime() = default;

LuaAppRuntime::~LuaAppRuntime() { DestroyState(); }

void* LuaAppRuntime::LuaAllocPreferPsram(void* ud, void* ptr, size_t osize, size_t nsize) {
  (void)ud;
  (void)osize;

  if (nsize == 0) {
    if (ptr) heap_caps_free(ptr);
    return nullptr;
  }

  void* next = nullptr;
  if (!ptr) {
    next = heap_caps_malloc(nsize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!next) next = heap_caps_malloc(nsize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return next;
  }

  next = heap_caps_realloc(ptr, nsize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!next) next = heap_caps_realloc(ptr, nsize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return next;
}

bool LuaAppRuntime::CreateState() {
  DestroyState();

  L_ = lua_newstate(&LuaAllocPreferPsram, nullptr);
  if (!L_) {
    last_error_ = "lua_newstate failed";
    return false;
  }

  static bool logged_abi = false;
  if (!logged_abi) {
    logged_abi = true;
    ESP_LOGI(kTag,
             "lua abi: ver=%s int_bytes=%u num_bytes=%u",
             LUA_RELEASE,
             static_cast<unsigned>(sizeof(lua_Integer)),
             static_cast<unsigned>(sizeof(lua_Number)));
    ESP_LOGI(kTag, "lua allocator: PSRAM preferred, internal fallback");
  }

  // Store runtime pointer for C callbacks (framebuffer helpers).
  *reinterpret_cast<LuaAppRuntime**>(lua_getextraspace(L_)) = this;

  // Open a small, deterministic subset of libs. Avoid IO/OS/package.
  luaL_requiref(L_, "_G", luaopen_base, 1);
  lua_pop(L_, 1);
  luaL_requiref(L_, LUA_TABLIBNAME, luaopen_table, 1);
  lua_pop(L_, 1);
  luaL_requiref(L_, LUA_STRLIBNAME, luaopen_string, 1);
  lua_pop(L_, 1);
  luaL_requiref(L_, LUA_MATHLIBNAME, luaopen_math, 1);
  lua_pop(L_, 1);
  luaL_requiref(L_, LUA_UTF8LIBNAME, luaopen_utf8, 1);
  lua_pop(L_, 1);
  luaL_requiref(L_, LUA_COLIBNAME, luaopen_coroutine, 1);
  lua_pop(L_, 1);

  PushSysModule();
  PushDataModule();
  PushNetModule(L_);
  PushJsonModule(L_);

  // Forbid `dofile`/`loadfile` by removing them from base lib.
  lua_getglobal(L_, "dofile");
  if (!lua_isnil(L_, -1)) {
    lua_pop(L_, 1);
    lua_pushnil(L_);
    lua_setglobal(L_, "dofile");
  } else {
    lua_pop(L_, 1);
  }
  lua_pushnil(L_);
  lua_setglobal(L_, "loadfile");

  // Keep GC responsive in long-running app loops.
  gc_tick_acc_ms_ = 0;
  gc_full_acc_ms_ = 0;
  lua_gc(L_, LUA_GCSTOP, 0);
  lua_gc(L_, LUA_GCRESTART, 0);

  return true;
}

void LuaAppRuntime::DestroyState() {
  if (L_) {
    // Best-effort final collection before closing Lua state.
    lua_gc(L_, LUA_GCCOLLECT, 0);
    ReleaseNetResourcesForState(L_);
    // GIF player is allocated per Lua state on demand; release it before closing Lua.
    ReleaseGifPlayerForState(L_);
    lua_close(L_);
    L_ = nullptr;
  }
  loaded_ = false;
  inited_ = false;
  fb_tmp_strings_.clear();
  gc_tick_acc_ms_ = 0;
  gc_full_acc_ms_ = 0;
}

bool LuaAppRuntime::LoadFromDir(const std::string& app_dir) {
  app_dir_ = app_dir;
  last_error_.clear();
  logged_empty_render_ = false;
  logged_empty_lines_ = false;
  logged_float_key_fallback_ = false;
  logged_first_lines_ = false;

  if (!CreateState()) return false;

  const std::string main_path = ResolveAppEntryFile(app_dir_);
  if (!LoadMain(main_path)) return false;

  inited_ = false;
  loaded_ = true;
  return true;
}

bool LuaAppRuntime::LoadMain(const std::string& main_path) {
  FILE* f = fopen(main_path.c_str(), "rb");
  if (!f) {
    last_error_ = "failed to open " + main_path;
    return false;
  }

  // Help verify LittleFS content: log chunk header / text preview.
  {
    uint8_t head[24] = {};
    const size_t n = fread(head, 1, sizeof(head), f);
    long file_size = -1;
    if (fseek(f, 0, SEEK_END) == 0) {
      file_size = ftell(f);
    }
    LogChunkHeader(main_path, head, n, file_size);
    fseek(f, 0, SEEK_SET);
  }

  struct Reader {
    FILE* f = nullptr;
    char buf[512];
  } reader;
  reader.f = f;

  auto read_fn = [](lua_State* L, void* ud, size_t* sz) -> const char* {
    (void)L;
    Reader* r = static_cast<Reader*>(ud);
    if (!r || !r->f) {
      if (sz) *sz = 0;
      return nullptr;
    }
    const size_t n = fread(r->buf, 1, sizeof(r->buf), r->f);
    if (n == 0) {
      if (sz) *sz = 0;
      return nullptr;
    }
    if (sz) *sz = n;
    return r->buf;
  };

  ESP_LOGI(kTag, "LoadMain: before lua_load (%s)", main_path.c_str());
  const int load_ret = lua_load(L_, read_fn, &reader, main_path.c_str(), nullptr);
  ESP_LOGI(kTag, "LoadMain: after lua_load ret=%d", load_ret);
  fclose(f);
  if (load_ret != LUA_OK) {
    SetErrorFromTop();
    ESP_LOGE(kTag,
             "loadbuffer failed: %s (runtime int=%u num=%u)",
             last_error_.c_str(),
             static_cast<unsigned>(sizeof(lua_Integer)),
             static_cast<unsigned>(sizeof(lua_Number)));
    return false;
  }

  ESP_LOGI(kTag, "LoadMain: before pcall");
  const int call_ret = lua_pcall(L_, 0, 1, 0);
  ESP_LOGI(kTag, "LoadMain: after pcall ret=%d", call_ret);
  if (call_ret != LUA_OK) {
    SetErrorFromTop();
    ESP_LOGE(kTag, "pcall failed: %s", last_error_.c_str());
    return false;
  }

  if (!lua_istable(L_, -1)) {
    lua_pop(L_, 1);
    last_error_ = "main.lua must return an app table";
    return false;
  }

  // Store app table in registry by string key (scripts can't access it).
  lua_setfield(L_, LUA_REGISTRYINDEX, kRegistryAppKey);  // pops app table
  return true;
}

bool LuaAppRuntime::PushAppTable(std::string* out_err) {
  if (out_err) out_err->clear();
  if (!L_ || !loaded_) {
    if (out_err) *out_err = "app not loaded";
    return false;
  }

  lua_getfield(L_, LUA_REGISTRYINDEX, kRegistryAppKey);
  if (!lua_istable(L_, -1)) {
    if (out_err) *out_err = "app table missing";
    lua_pop(L_, 1);
    return false;
  }
  return true;
}

void LuaAppRuntime::EnsureInited() {
  if (!L_ || inited_ || !loaded_) return;

  std::string err;
  if (!PushAppTable(&err)) {
    last_error_ = err;
    ESP_LOGE(kTag, "EnsureInited: %s", last_error_.c_str());
    inited_ = true;  // avoid spinning on repeated errors
    return;
  }

  lua_getfield(L_, -1, "init");
  if (!lua_isfunction(L_, -1)) {
    lua_pop(L_, 2);
    inited_ = true;
    return;
  }

  lua_newtable(L_);  // config table (empty for V1 demo)
  const int ret = lua_pcall(L_, 1, 0, 0);
  if (ret != LUA_OK) {
    SetErrorFromTop();
    ESP_LOGE(kTag, "init error: %s", last_error_.c_str());
  }

  lua_pop(L_, 1);  // app table
  inited_ = true;
}

void LuaAppRuntime::Tick(uint32_t ms) {
  if (!L_ || !loaded_) return;
  EnsureInited();

  // Proactive GC:
  // - frequent small incremental steps to limit pause time
  // - occasional full collect to compact long-lived fragmentation
  gc_tick_acc_ms_ += ms;
  gc_full_acc_ms_ += ms;
  if (gc_tick_acc_ms_ >= 200) {
    gc_tick_acc_ms_ = 0;
    lua_gc(L_, LUA_GCSTEP, 24);
  }
  if (gc_full_acc_ms_ >= 10000) {
    gc_full_acc_ms_ = 0;
    lua_gc(L_, LUA_GCCOLLECT, 0);
  }

  std::string err;
  if (!PushAppTable(&err)) {
    last_error_ = err;
    ESP_LOGE(kTag, "Tick: %s", last_error_.c_str());
    return;
  }
  lua_getfield(L_, -1, "tick");
  if (!lua_isfunction(L_, -1)) {
    lua_pop(L_, 2);
    return;
  }

  lua_pushinteger(L_, static_cast<lua_Integer>(ms));
  const int ret = lua_pcall(L_, 1, 0, 0);
  if (ret != LUA_OK) {
    SetErrorFromTop();
    ESP_LOGE(kTag, "tick error: %s", last_error_.c_str());
  }

  lua_pop(L_, 1);  // app table
}

void LuaAppRuntime::FullGcNow() {
  if (!L_) return;
  lua_gc(L_, LUA_GCCOLLECT, 0);
}

bool LuaAppRuntime::Render(std::vector<std::string>* out_lines) {
  if (out_lines) out_lines->clear();
  if (!L_ || !loaded_ || !out_lines) return false;
  EnsureInited();

  std::string err;
  if (!PushAppTable(&err)) {
    last_error_ = err;
    ESP_LOGE(kTag, "Render: %s", last_error_.c_str());
    return false;
  }
  lua_getfield(L_, -1, "render");
  if (!lua_isfunction(L_, -1)) {
    lua_pop(L_, 2);
    last_error_ = "app.render missing";
    return false;
  }

  const int ret = lua_pcall(L_, 0, 1, 0);
  if (ret != LUA_OK) {
    SetErrorFromTop();
    ESP_LOGE(kTag, "render error: %s", last_error_.c_str());
    lua_pop(L_, 1);  // app table
    return false;
  }

  // Allow render() to return either:
  // - a table array: { "l1", "l2", ... }
  // - a table with field `lines`: { lines = { ... } }
  if (!lua_istable(L_, -1)) {
    lua_pop(L_, 2);
    last_error_ = "render must return a table";
    return false;
  }

  // Support { lines = { ... } }
  lua_getfield(L_, -1, "lines");
  if (lua_istable(L_, -1)) {
    lua_remove(L_, -2);  // drop outer table, keep lines table on top
  } else {
    lua_pop(L_, 1);  // drop non-table `lines` field
  }

  // Keep a copy of the render output in registry for debugging/inspection.
  lua_pushvalue(L_, -1);
  lua_setfield(L_, LUA_REGISTRYINDEX, kRegistryRenderKey);

  const size_t raw_len = lua_rawlen(L_, -1);
  if (raw_len == 0 && !logged_empty_render_) {
    logged_empty_render_ = true;

    // Heuristic: detect if render() accidentally returned the app table itself.
    lua_getfield(L_, -1, "render");
    const bool looks_like_app_table = lua_isfunction(L_, -1);
    lua_pop(L_, 1);

    ESP_LOGW(kTag, "render returned empty table (dir=%s) looks_like_app=%d", app_dir_.c_str(),
             looks_like_app_table ? 1 : 0);

    // Dump a few keys to help diagnose.
    int dumped = 0;
    lua_pushnil(L_);
    while (dumped < 6 && lua_next(L_, -2) != 0) {
      size_t klen = 0;
      const char* k = luaL_tolstring(L_, -2, &klen);
      lua_pop(L_, 1);
      size_t vlen = 0;
      const char* v = luaL_tolstring(L_, -1, &vlen);
      lua_pop(L_, 1);
      ESP_LOGW(kTag, "  key[%d]=%.*s val=%.*s", dumped, static_cast<int>(klen), k ? k : "",
               static_cast<int>(vlen), v ? v : "");
      lua_pop(L_, 1);  // original value (lua_next kept it on stack)
      dumped++;
    }
  }

  const int table_abs = lua_absindex(L_, -1);

  // Robust extraction:
  // Some builds/configs can make `lua_rawgeti(t, i)` fail to find values even when
  // numeric keys exist (e.g. stored as non-integer number keys). To avoid subtle
  // key-type issues, iterate numeric keys and map them to 1..4.
  std::string lines[4] = {"", "", "", ""};
  bool any_nonempty = false;

  lua_pushnil(L_);
  while (lua_next(L_, table_abs) != 0) {
    // key at -2, value at -1
    int ki = 0;
    const int kt = lua_type(L_, -2);

    if (kt == LUA_TNUMBER) {
      const double k = static_cast<double>(lua_tonumber(L_, -2));
      const int k_round = static_cast<int>(k + (k >= 0 ? 0.5 : -0.5));  // round
      const double diff = (k > k_round) ? (k - k_round) : (k_round - k);
      if (diff < 1e-6) ki = k_round;
    } else if (kt == LUA_TSTRING) {
      // Support dict-style returns: { line1="...", line2="...", ... }
      size_t klen = 0;
      const char* ks = lua_tolstring(L_, -2, &klen);
      if (ks && klen >= 5 && strncmp(ks, "line", 4) == 0) {
        int v = 0;
        bool ok = true;
        for (size_t j = 4; j < klen; j++) {
          const char c = ks[j];
          if (c < '0' || c > '9') {
            ok = false;
            break;
          }
          v = v * 10 + (c - '0');
        }
        if (ok) ki = v;
      }
    }

    if (ki >= 1 && ki <= 4) {
      size_t len = 0;
      const char* s = luaL_tolstring(L_, -1, &len);  // pushes coerced string
      lines[ki - 1] = s ? std::string(s, len) : std::string();
      if (!lines[ki - 1].empty()) any_nonempty = true;
      lua_pop(L_, 1);  // coerced string
    }

    lua_pop(L_, 1);  // pop value, keep key for lua_next
  }

  out_lines->clear();
  out_lines->reserve(4);
  for (int i = 0; i < 4; i++) out_lines->push_back(lines[i]);

  if (!any_nonempty && !logged_empty_lines_) {
    logged_empty_lines_ = true;
    ESP_LOGW(kTag, "render produced 4 empty lines (dir=%s rawlen=%u)", app_dir_.c_str(),
             static_cast<unsigned>(raw_len));

    // Dump array indices 1..4.
    for (int i = 1; i <= 4; i++) {
      lua_rawgeti(L_, -1, i);
      const int t = lua_type(L_, -1);
      ESP_LOGW(kTag, "  idx[%d] type=%s", i, lua_typename(L_, t));
      if (t != LUA_TNIL) {
        size_t sl = 0;
        const char* ss = luaL_tolstring(L_, -1, &sl);
        ESP_LOGW(kTag, "    as_str='%.*s'", static_cast<int>(sl), ss ? ss : "");
        lua_pop(L_, 1);  // coerced
      }
      lua_pop(L_, 1);  // original
    }

    // Dump up to a few keys to understand what the table actually contains.
    int dumped = 0;
    lua_pushnil(L_);
    while (dumped < 10 && lua_next(L_, -2) != 0) {
      // key at -2, value at -1
      const int kt = lua_type(L_, -2);
      const int vt = lua_type(L_, -1);

      if (kt == LUA_TNUMBER) {
        lua_Number kn = lua_tonumber(L_, -2);
        ESP_LOGW(kTag, "  key[%d]=<num %.0f> val_type=%s", dumped, static_cast<double>(kn),
                 lua_typename(L_, vt));
      } else {
        size_t klen = 0;
        const char* k = luaL_tolstring(L_, -2, &klen);
        lua_pop(L_, 1);
        ESP_LOGW(kTag, "  key[%d]=<%s %.*s> val_type=%s", dumped, lua_typename(L_, kt),
                 static_cast<int>(klen), k ? k : "", lua_typename(L_, vt));
      }

      // value -> string (best-effort)
      size_t vlen = 0;
      const char* v = luaL_tolstring(L_, -1, &vlen);
      ESP_LOGW(kTag, "    val_str='%.*s'", static_cast<int>(vlen), v ? v : "");
      lua_pop(L_, 1);  // coerced string

      lua_pop(L_, 1);  // pop value, keep key for lua_next
      dumped++;
    }
  }

  if (!logged_first_lines_) {
    logged_first_lines_ = true;
    ESP_LOGI(kTag, "render lines (dir=%s): [1]='%s' [2]='%s' [3]='%s' [4]='%s'", app_dir_.c_str(),
             (out_lines->size() > 0) ? (*out_lines)[0].c_str() : "",
             (out_lines->size() > 1) ? (*out_lines)[1].c_str() : "",
             (out_lines->size() > 2) ? (*out_lines)[2].c_str() : "",
             (out_lines->size() > 3) ? (*out_lines)[3].c_str() : "");
  }

  lua_pop(L_, 2);  // lines/render table, app table
  return true;
}

bool LuaAppRuntime::RenderBinds(const std::vector<std::string>& keys, std::vector<std::string>* out_values) {
  if (out_values) out_values->clear();
  if (!out_values) return false;
  if (!L_ || !loaded_) return false;
  EnsureInited();

  std::string err;
  if (!PushAppTable(&err)) {
    last_error_ = err;
    ESP_LOGE(kTag, "RenderBinds: %s", last_error_.c_str());
    return false;
  }

  // Call app.render() -> table.
  lua_getfield(L_, -1, "render");
  if (!lua_isfunction(L_, -1)) {
    lua_pop(L_, 2);
    last_error_ = "app.render missing";
    return false;
  }

  const int ret = lua_pcall(L_, 0, 1, 0);
  if (ret != LUA_OK) {
    SetErrorFromTop();
    ESP_LOGE(kTag, "render error: %s", last_error_.c_str());
    lua_pop(L_, 1);  // app table
    return false;
  }

  if (!lua_istable(L_, -1)) {
    lua_pop(L_, 2);
    last_error_ = "render must return a table";
    return false;
  }

  // Support { lines = { ... } }
  lua_getfield(L_, -1, "lines");
  if (lua_istable(L_, -1)) {
    lua_remove(L_, -2);  // drop outer table, keep lines table on top
  } else {
    lua_pop(L_, 1);
  }

  // Store for debugging.
  lua_pushvalue(L_, -1);
  lua_setfield(L_, LUA_REGISTRYINDEX, kRegistryRenderKey);

  const int table_abs = lua_absindex(L_, -1);
  out_values->reserve(keys.size());

  for (const auto& key : keys) {
    // Try direct string key.
    lua_getfield(L_, table_abs, key.c_str());
    if (lua_isnil(L_, -1)) {
      lua_pop(L_, 1);

      // Fallback for "lineN": try numeric key.
      int idx1 = 0;
      if (TryParseLineIndex(key, &idx1) && idx1 > 0) {
        lua_pushnumber(L_, static_cast<lua_Number>(idx1));
        lua_rawget(L_, table_abs);
      } else {
        lua_pushnil(L_);
      }
    }

    if (lua_isnil(L_, -1)) {
      out_values->push_back("");
      lua_pop(L_, 1);
      continue;
    }

    size_t len = 0;
    const char* s = luaL_tolstring(L_, -1, &len);
    out_values->push_back(s ? std::string(s, len) : std::string());
    lua_pop(L_, 1);  // coerced string
    lua_pop(L_, 1);  // original value
  }

  lua_pop(L_, 2);  // table, app table
  return true;
}

bool LuaAppRuntime::SupportsFrameBuffer() {
  if (!L_ || !loaded_) return false;

  std::string err;
  if (!PushAppTable(&err)) {
    last_error_ = err;
    return false;
  }

  lua_getfield(L_, -1, "render_fb");
  const bool ok = lua_isfunction(L_, -1);
  lua_pop(L_, 2);  // render_fb, app table
  return ok;
}

namespace {

static const char* kFbMeta = "esp32_pixel.fb";

struct LuaFb {
  uint8_t* data = nullptr;            // start of framebuffer (top-left)
  uint32_t stride_bytes = 0;          // bytes per row
  int w = 0;
  int h = 0;
  lv_layer_t* layer = nullptr;        // optional (for text/image helpers)
  const lv_font_t* default_font = nullptr;
  lv_area_t base_clip = {0, 0, 0, 0};
  lv_area_t base_phy_clip = {0, 0, 0, 0};
};

struct GifPlayer {
  AnimatedGIF gif;
  bool inited = false;
  bool open = false;
  std::string src;
  int w = 0;
  int h = 0;
  std::vector<uint16_t> frame565;
  std::vector<uint8_t> workbuf;
  int64_t next_due_ms = 0;
  int32_t frame_delay_ms = 100;
  bool looped_once = false;
};

static GifPlayer* g_gif_active = nullptr;
static GifPlayer* g_gif_player = nullptr;
static lua_State* g_gif_player_owner = nullptr;

static GifPlayer* AllocGifPlayer() {
  void* mem = LvglAllocPreferPsram(sizeof(GifPlayer));
  if (!mem) return nullptr;
  // Placement new: avoid C++ heap operator new throw path on low memory.
  return new (mem) GifPlayer();
}

static void FreeGifPlayer(GifPlayer* p) {
  if (!p) return;
  p->~GifPlayer();
  LvglFreePreferPsram(p);
}

static void* GifOpenFile(const char* filename, int32_t* file_size) {
  FILE* f = fopen(filename, "rb");
  if (!f) return nullptr;
  fseek(f, 0, SEEK_END);
  const long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (file_size) *file_size = static_cast<int32_t>(sz);
  return f;
}

static void GifCloseFile(void* handle) {
  if (!handle) return;
  fclose(reinterpret_cast<FILE*>(handle));
}

static int32_t GifReadFile(GIFFILE* f, uint8_t* buf, int32_t len) {
  if (!f || !buf || len <= 0) return 0;
  FILE* fp = reinterpret_cast<FILE*>(f->fHandle);
  if (!fp) return 0;

  int32_t to_read = len;
  const int32_t remain = f->iSize - f->iPos;
  if (remain < to_read) to_read = remain;
  if (to_read <= 0) return 0;

  const int32_t n = static_cast<int32_t>(fread(buf, 1, static_cast<size_t>(to_read), fp));
  f->iPos += n;
  return n;
}

static int32_t GifSeekFile(GIFFILE* f, int32_t pos) {
  if (!f) return 0;
  FILE* fp = reinterpret_cast<FILE*>(f->fHandle);
  if (!fp) return 0;

  if (pos < 0) pos = 0;
  if (pos >= f->iSize) pos = f->iSize - 1;
  if (pos < 0) pos = 0;
  f->iPos = pos;
  fseek(fp, pos, SEEK_SET);
  return pos;
}

static void GifDraw(GIFDRAW* draw) {
  if (!draw || !draw->pPixels) return;
  GifPlayer* p = g_gif_active;
  if (!p) return;
  if (p->w <= 0 || p->h <= 0) return;
  if (p->frame565.empty()) return;

  const int y = draw->iY + draw->y;
  if (y < 0 || y >= p->h) return;
  const int x = draw->iX;
  if (x >= p->w) return;
  if (x < 0) return;

  int copy_w = draw->iWidth;
  if (x + copy_w > p->w) copy_w = p->w - x;
  if (copy_w <= 0) return;

  uint16_t* dst = &p->frame565[static_cast<size_t>(y) * static_cast<size_t>(p->w) + static_cast<size_t>(x)];
  memcpy(dst, draw->pPixels, static_cast<size_t>(copy_w) * sizeof(uint16_t));
}

static void GifClosePlayer(GifPlayer* p) {
  if (!p) return;
  if (p->open) {
    p->gif.close();
    p->open = false;
  }
  p->src.clear();
  p->w = 0;
  p->h = 0;
  p->frame565.clear();
  p->workbuf.clear();
  p->next_due_ms = 0;
  p->frame_delay_ms = 100;
  p->looped_once = false;
}

static GifPlayer* GetGifPlayerForState(lua_State* L, bool create_if_missing) {
  if (!L) return nullptr;
  if (g_gif_player_owner == L) {
    if (g_gif_player) return g_gif_player;
    if (!create_if_missing) return nullptr;
    g_gif_player = AllocGifPlayer();
    if (!g_gif_player) {
      ESP_LOGW(kTag, "fb:gif alloc failed owner reuse");
      return nullptr;
    }
    return g_gif_player;
  }
  if (!create_if_missing) return nullptr;

  if (g_gif_player_owner && g_gif_player_owner != L && g_gif_player) {
    GifClosePlayer(g_gif_player);
    FreeGifPlayer(g_gif_player);
    g_gif_player = nullptr;
  }
  if (!g_gif_player) {
    g_gif_player = AllocGifPlayer();
    if (!g_gif_player) {
      ESP_LOGW(kTag, "fb:gif alloc failed");
      return nullptr;
    }
  }
  g_gif_player_owner = L;
  return g_gif_player;
}

static void ReleaseGifPlayerForState(lua_State* L) {
  if (!L) return;
  if (g_gif_player_owner != L) return;
  if (g_gif_player) {
    GifClosePlayer(g_gif_player);
    FreeGifPlayer(g_gif_player);
    g_gif_player = nullptr;
  }
  g_gif_player_owner = nullptr;
}

static std::string NormalizeGifPath(const char* src) {
  if (!src) return {};
  // LVGL image paths often use "S:/littlefs/..."; stdio fopen needs "/littlefs/...".
  if (strncmp(src, "S:/", 3) == 0) return std::string(src + 2);
  return std::string(src);
}

static std::string NormalizeLvglImageSrc(const char* src) {
  if (!src || !*src) return {};
  // Allow "/littlefs/..." as shorthand and normalize to LVGL stdio driver path.
  if (strncmp(src, "/littlefs/", 10) == 0) {
    std::string lv_src = "S:";
    lv_src += src;
    return lv_src;
  }
  return {};
}

static bool GifOpenPlayerIfNeeded(GifPlayer* p, const char* src, std::string* out_err) {
  if (out_err) out_err->clear();
  if (!p || !src || !*src) {
    if (out_err) *out_err = "gif src empty";
    return false;
  }

  const std::string norm_src = NormalizeGifPath(src);
  if (p->open && p->src == norm_src) return true;
  GifClosePlayer(p);

  if (!p->inited) {
    p->gif.begin(GIF_PALETTE_RGB565_LE);
    p->inited = true;
  }

  const int ok = p->gif.open(norm_src.c_str(), GifOpenFile, GifCloseFile, GifReadFile, GifSeekFile, GifDraw);
  if (!ok) {
    ESP_LOGW(kTag, "fb:gif open failed src=%s normalized=%s", src, norm_src.c_str());
    if (out_err) *out_err = "gif open failed";
    return false;
  }

  p->w = p->gif.getCanvasWidth();
  p->h = p->gif.getCanvasHeight();
  if (p->w <= 0 || p->h <= 0) {
    p->gif.close();
    if (out_err) *out_err = "gif bad size";
    return false;
  }

  p->frame565.assign(static_cast<size_t>(p->w) * static_cast<size_t>(p->h), 0);
  p->workbuf.assign(static_cast<size_t>(p->w) * static_cast<size_t>(p->h + 2), 0);
  p->gif.setDrawType(GIF_DRAW_COOKED);
  p->gif.setFrameBuf(p->workbuf.data());

  p->open = true;
  p->src = norm_src;
  p->next_due_ms = 0;
  p->frame_delay_ms = 100;
  p->looped_once = false;
  return true;
}

static void GifAdvanceIfDue(GifPlayer* p) {
  if (!p || !p->open) return;
  const int64_t now = NowMs();
  if (now < p->next_due_ms) return;
  p->looped_once = false;

  int next_ms = 100;
  g_gif_active = p;
  int has_next = p->gif.playFrame(false, &next_ms, nullptr);
  if (!has_next) {
    p->gif.reset();
    has_next = p->gif.playFrame(false, &next_ms, nullptr);
    p->looped_once = true;
  }
  g_gif_active = nullptr;

  if (next_ms < 60) next_ms = 60;
  p->frame_delay_ms = static_cast<int32_t>(next_ms);
  p->next_due_ms = now + static_cast<int64_t>(next_ms);
}

static void GifAdvanceNow(GifPlayer* p) {
  if (!p || !p->open) return;
  p->looped_once = false;

  int next_ms = 100;
  g_gif_active = p;
  int has_next = p->gif.playFrame(false, &next_ms, nullptr);
  if (!has_next) {
    p->gif.reset();
    has_next = p->gif.playFrame(false, &next_ms, nullptr);
    p->looped_once = true;
  }
  g_gif_active = nullptr;

  if (next_ms < 60) next_ms = 60;
  p->frame_delay_ms = static_cast<int32_t>(next_ms);
  p->next_due_ms = NowMs() + static_cast<int64_t>(next_ms);
}

static LuaAppRuntime* GetRuntime(lua_State* L) {
  if (!L) return nullptr;
  void* p = lua_getextraspace(L);
  if (!p) return nullptr;
  return *reinterpret_cast<LuaAppRuntime**>(p);
}

static const char* InternFbString(lua_State* L, const char* s) {
  LuaAppRuntime* rt = GetRuntime(L);
  if (!rt || !s) return s ? s : "";
  return rt->InternFrameBufferString(s);
}

static LuaFb* CheckFb(lua_State* L, int idx) {
  void* ud = luaL_checkudata(L, idx, kFbMeta);
  return static_cast<LuaFb*>(ud);
}

static uint16_t CheckColor565(lua_State* L, int idx) {
  const lua_Integer v = luaL_checkinteger(L, idx);
  if (v < 0 || v > 0xFFFF) luaL_error(L, "color565 out of range");
  return static_cast<uint16_t>(v);
}

static void FbSetPixel(LuaFb* fb, int x, int y, uint16_t c16) {
  if (!fb || !fb->data) return;
  if (x < 0 || y < 0 || x >= fb->w || y >= fb->h) return;
  uint8_t* row = fb->data + static_cast<uint32_t>(y) * fb->stride_bytes;
  uint16_t* p = reinterpret_cast<uint16_t*>(row) + x;
  *p = c16;
}

static lv_color_t ColorFrom565(uint16_t c16) {
  const uint8_t r5 = (c16 >> 11) & 0x1F;
  const uint8_t g6 = (c16 >> 5) & 0x3F;
  const uint8_t b5 = (c16 >> 0) & 0x1F;
  const uint8_t r = static_cast<uint8_t>((static_cast<unsigned>(r5) * 255u) / 31u);
  const uint8_t g = static_cast<uint8_t>((static_cast<unsigned>(g6) * 255u) / 63u);
  const uint8_t b = static_cast<uint8_t>((static_cast<unsigned>(b5) * 255u) / 31u);
  return lv_color_make(r, g, b);
}

static const lv_font_t* DefaultFontOrFallback(const LuaFb* fb) {
  if (fb && fb->default_font) return fb->default_font;
  return &lv_font_silkscreen_regular_8;
}

static const lv_font_t* GetBuiltinFontById(const char* id) {
  if (!id) return nullptr;
  // Keep this list small and stable for V1.
  if (strcmp(id, "silkscreen_regular_8") == 0) return &lv_font_silkscreen_regular_8;
  if (strcmp(id, "pressstart2p_regular_8") == 0) return &lv_font_pressstart2p_regular_8;
  return nullptr;
}

static const lv_font_t* ResolveFont(const LuaFb* fb, const char* font_id_or_path, int font_size) {
  (void)font_size;
  if (!font_id_or_path || !*font_id_or_path) return DefaultFontOrFallback(fb);

  // "builtin:silkscreen_regular_8"
  static const char* kBuiltinPrefix = "builtin:";
  if (strncmp(font_id_or_path, kBuiltinPrefix, strlen(kBuiltinPrefix)) == 0) {
    const char* id = font_id_or_path + strlen(kBuiltinPrefix);
    const lv_font_t* f = GetBuiltinFontById(id);
    if (f) {
      static bool logged_silkscreen = false;
      static bool logged_pressstart = false;
      if (strcmp(id, "silkscreen_regular_8") == 0 && !logged_silkscreen) {
        logged_silkscreen = true;
        ESP_LOGI(kTag, "ResolveFont builtin=%s -> ptr=%p line_height=%d base_line=%d", id, (const void*)f,
                 static_cast<int>(f->line_height), static_cast<int>(f->base_line));
      } else if (strcmp(id, "pressstart2p_regular_8") == 0 && !logged_pressstart) {
        logged_pressstart = true;
        ESP_LOGI(kTag, "ResolveFont builtin=%s -> ptr=%p line_height=%d base_line=%d", id, (const void*)f,
                 static_cast<int>(f->line_height), static_cast<int>(f->base_line));
      }
      return f;
    }

    static bool logged_builtin_miss = false;
    if (!logged_builtin_miss) {
      logged_builtin_miss = true;
      ESP_LOGW(kTag, "ResolveFont builtin id not found: %s (fallback default)", id);
    }
  }

  // TinyTTF (disabled in our current target config by design).
  const lv_font_t* f = nullptr;
#if LV_USE_TINY_TTF
  if (font_id_or_path && *font_id_or_path && font_size > 0) {
    f = GetOrLoadTtfFont(font_id_or_path, font_size);
  }
#endif
  if (f) return f;
  return DefaultFontOrFallback(fb);
}

#if LV_USE_TINY_TTF
static const lv_font_t* GetOrLoadTtfFont(const char* path, int size) {
  if (!path || !*path || size <= 0) return nullptr;
  static std::unordered_map<std::string, lv_font_t*> cache;
  const std::string key = std::string(path) + "@" + std::to_string(size);
  auto it = cache.find(key);
  if (it != cache.end()) return it->second;

  lv_font_t* f = lv_tiny_ttf_create_file_ex(path, size, LV_FONT_KERNING_NONE, 96);
  if (!f) {
    ESP_LOGW(kTag, "fb:text failed to load font %s size=%d", path, size);
    return nullptr;
  }
  cache.emplace(key, f);
  return f;
}
#endif

static bool ApplyClipArea(LuaFb* fb, const lv_area_t& req) {
  if (!fb || !fb->layer) return false;
  lv_area_t out;
  out.x1 = LV_MAX(req.x1, fb->base_clip.x1);
  out.y1 = LV_MAX(req.y1, fb->base_clip.y1);
  out.x2 = LV_MIN(req.x2, fb->base_clip.x2);
  out.y2 = LV_MIN(req.y2, fb->base_clip.y2);
  if (out.x1 > out.x2 || out.y1 > out.y2) {
    // Empty clip => nothing will draw.
    out.x1 = 0;
    out.y1 = 0;
    out.x2 = -1;
    out.y2 = -1;
  }

  fb->layer->_clip_area = out;
  // Keep phy clip consistent; use same for canvas.
  fb->layer->phy_clip_area = out;
  return true;
}

static int LuaFbSetClip(lua_State* L) {
  LuaFb* fb = CheckFb(L, 1);
  const int x = static_cast<int>(luaL_checkinteger(L, 2));
  const int y = static_cast<int>(luaL_checkinteger(L, 3));
  const int w = static_cast<int>(luaL_checkinteger(L, 4));
  const int h = static_cast<int>(luaL_checkinteger(L, 5));
  if (!fb || !fb->layer) return 0;
  if (w <= 0 || h <= 0) return 0;

  const lv_area_t req = {
      static_cast<lv_coord_t>(x),
      static_cast<lv_coord_t>(y),
      static_cast<lv_coord_t>(x + w - 1),
      static_cast<lv_coord_t>(y + h - 1),
  };
  (void)ApplyClipArea(fb, req);
  return 0;
}

static int LuaFbClearClip(lua_State* L) {
  LuaFb* fb = CheckFb(L, 1);
  if (!fb || !fb->layer) return 0;
  fb->layer->_clip_area = fb->base_clip;
  fb->layer->phy_clip_area = fb->base_phy_clip;
  return 0;
}

static int LuaFbClipScoped(lua_State* L) {
  LuaFb* fb = CheckFb(L, 1);
  const int x = static_cast<int>(luaL_checkinteger(L, 2));
  const int y = static_cast<int>(luaL_checkinteger(L, 3));
  const int w = static_cast<int>(luaL_checkinteger(L, 4));
  const int h = static_cast<int>(luaL_checkinteger(L, 5));
  luaL_checktype(L, 6, LUA_TFUNCTION);
  if (!fb || !fb->layer) return 0;
  if (w <= 0 || h <= 0) return 0;

  const lv_area_t prev_clip = fb->layer->_clip_area;
  const lv_area_t prev_phy = fb->layer->phy_clip_area;

  const lv_area_t req = {
      static_cast<lv_coord_t>(x),
      static_cast<lv_coord_t>(y),
      static_cast<lv_coord_t>(x + w - 1),
      static_cast<lv_coord_t>(y + h - 1),
  };
  (void)ApplyClipArea(fb, req);

  lua_pushvalue(L, 6);
  const int ret = lua_pcall(L, 0, 0, 0);

  fb->layer->_clip_area = prev_clip;
  fb->layer->phy_clip_area = prev_phy;

  if (ret != LUA_OK) return lua_error(L);
  return 0;
}

static int LuaFbFill(lua_State* L) {
  LuaFb* fb = CheckFb(L, 1);
  const uint16_t c16 = CheckColor565(L, 2);
  if (!fb || !fb->data) return 0;

  for (int y = 0; y < fb->h; y++) {
    uint8_t* row = fb->data + static_cast<uint32_t>(y) * fb->stride_bytes;
    uint16_t* p = reinterpret_cast<uint16_t*>(row);
    for (int x = 0; x < fb->w; x++) p[x] = c16;
  }
  return 0;
}

static int LuaFbText(lua_State* L) {
  LuaFb* fb = CheckFb(L, 1);
  const int x = static_cast<int>(luaL_checkinteger(L, 2));
  const int y = static_cast<int>(luaL_checkinteger(L, 3));
  const char* text = luaL_checkstring(L, 4);
  const uint16_t c16 = CheckColor565(L, 5);

  const char* font_path = nullptr;
  int font_size = 0;
  if (lua_gettop(L) >= 6 && lua_isstring(L, 6)) font_path = lua_tostring(L, 6);
  if (lua_gettop(L) >= 7 && lua_isnumber(L, 7)) font_size = static_cast<int>(lua_tointeger(L, 7));

  if (!fb || !fb->layer) return 0;
  if (!text) text = "";
  text = InternFbString(L, text);

  const lv_font_t* font = ResolveFont(fb, font_path, font_size);

  lv_draw_label_dsc_t dsc;
  lv_draw_label_dsc_init(&dsc);
  dsc.font = font;
  dsc.color = ColorFrom565(c16);
  dsc.text = text;
  dsc.opa = LV_OPA_COVER;

  lv_area_t a = {
      static_cast<lv_coord_t>(x),
      static_cast<lv_coord_t>(y),
      static_cast<lv_coord_t>(fb->w - 1),
      static_cast<lv_coord_t>(fb->h - 1),
  };
  lv_draw_label(fb->layer, &dsc, &a);
  return 0;
}

static int32_t MeasureAsciiWidth(const lv_font_t* font, const char* s) {
  if (!font || !s) return 0;
  int32_t w = 0;
  for (size_t i = 0; s[i] != '\0'; i++) {
    const uint32_t cur = static_cast<uint8_t>(s[i]);
    const uint32_t next = static_cast<uint8_t>(s[i + 1]);
    const int32_t cw = lv_font_get_glyph_width(font, cur, next);
    if (cw > 0) w += cw;
  }
  return w;
}

static std::string EllipsizeAscii(const lv_font_t* font, const char* s, int32_t max_w) {
  if (!s) return {};
  const int32_t w = MeasureAsciiWidth(font, s);
  if (w <= max_w) return std::string(s);

  const char* ell = "...";
  const int32_t ew = MeasureAsciiWidth(font, ell);
  if (ew >= max_w) return std::string(".");

  int32_t cur = 0;
  size_t cut = 0;
  for (size_t i = 0; s[i] != '\0'; i++) {
    const uint32_t ch = static_cast<uint8_t>(s[i]);
    const uint32_t next = static_cast<uint8_t>(s[i + 1]);
    const int32_t cw = lv_font_get_glyph_width(font, ch, next);
    if (cw <= 0) continue;
    if (cur + cw + ew > max_w) break;
    cur += cw;
    cut = i + 1;
  }

  std::string out(s, cut);
  out += ell;
  return out;
}

static lv_text_align_t ParseAlignStr(const char* s) {
  if (!s) return LV_TEXT_ALIGN_LEFT;
  if (strcmp(s, "right") == 0) return LV_TEXT_ALIGN_RIGHT;
  if (strcmp(s, "center") == 0) return LV_TEXT_ALIGN_CENTER;
  return LV_TEXT_ALIGN_LEFT;
}

static int LuaFbTextBox(lua_State* L) {
  LuaFb* fb = CheckFb(L, 1);
  const int x = static_cast<int>(luaL_checkinteger(L, 2));
  const int y = static_cast<int>(luaL_checkinteger(L, 3));
  const int w = static_cast<int>(luaL_checkinteger(L, 4));
  const int h = static_cast<int>(luaL_checkinteger(L, 5));
  const char* text = luaL_checkstring(L, 6);
  const uint16_t c16 = CheckColor565(L, 7);

  const char* font_path = nullptr;
  int font_size = 0;
  const char* align = nullptr;
  bool ellipsis = false;
  if (lua_gettop(L) >= 8 && lua_isstring(L, 8)) font_path = lua_tostring(L, 8);
  if (lua_gettop(L) >= 9 && lua_isnumber(L, 9)) font_size = static_cast<int>(lua_tointeger(L, 9));
  if (lua_gettop(L) >= 10 && lua_isstring(L, 10)) align = lua_tostring(L, 10);
  if (lua_gettop(L) >= 11 && lua_isboolean(L, 11)) ellipsis = lua_toboolean(L, 11);

  if (!fb || !fb->layer) return 0;
  if (!text) text = "";
  if (w <= 0 || h <= 0) return 0;

  const lv_font_t* font = ResolveFont(fb, font_path, font_size);

  std::string tmp;
  if (ellipsis) tmp = EllipsizeAscii(font, text, w);
  const char* use_text = ellipsis ? tmp.c_str() : text;
  use_text = InternFbString(L, use_text);

  const lv_area_t prev_clip = fb->layer->_clip_area;
  const lv_area_t prev_phy = fb->layer->phy_clip_area;

  const lv_area_t req = {
      static_cast<lv_coord_t>(x),
      static_cast<lv_coord_t>(y),
      static_cast<lv_coord_t>(x + w - 1),
      static_cast<lv_coord_t>(y + h - 1),
  };
  (void)ApplyClipArea(fb, req);

  lv_draw_label_dsc_t dsc;
  lv_draw_label_dsc_init(&dsc);
  dsc.font = font;
  dsc.color = ColorFrom565(c16);
  dsc.text = use_text;
  dsc.opa = LV_OPA_COVER;
  dsc.align = ParseAlignStr(align);

  lv_draw_label(fb->layer, &dsc, &req);

  fb->layer->_clip_area = prev_clip;
  fb->layer->phy_clip_area = prev_phy;
  return 0;
}

static int LuaFbImage(lua_State* L) {
  LuaFb* fb = CheckFb(L, 1);
  const int x = static_cast<int>(luaL_checkinteger(L, 2));
  const int y = static_cast<int>(luaL_checkinteger(L, 3));
  const int w = static_cast<int>(luaL_checkinteger(L, 4));
  const int h = static_cast<int>(luaL_checkinteger(L, 5));
  const char* src = luaL_checkstring(L, 6);

  if (!fb || !fb->layer) return 0;
  if (!src || !*src) return 0;
  src = InternFbString(L, src);
  {
    const std::string norm_src = NormalizeLvglImageSrc(src);
    if (!norm_src.empty()) src = InternFbString(L, norm_src.c_str());
  }
  if (w <= 0 || h <= 0) return 0;

  lv_image_header_t header;
  lv_result_t r = lv_image_decoder_get_info(src, &header);
  if (r != LV_RESULT_OK || header.w == 0 || header.h == 0) {
    // Stale cache/low-memory can make decoding intermittently fail after hot updates.
    lv_image_cache_drop(src);
    lv_image_header_cache_drop(src);
    r = lv_image_decoder_get_info(src, &header);
    if (r != LV_RESULT_OK || header.w == 0 || header.h == 0) {
      ESP_LOGW(kTag, "fb:image decode failed src=%s free=%u min=%u",
               src,
               static_cast<unsigned>(esp_get_free_heap_size()),
               static_cast<unsigned>(esp_get_minimum_free_heap_size()));
      return 0;
    }
  }

  lv_draw_image_dsc_t imgd;
  lv_draw_image_dsc_init(&imgd);
  imgd.src = src;
  imgd.opa = LV_OPA_COVER;

  lv_area_t a = {
      static_cast<lv_coord_t>(x),
      static_cast<lv_coord_t>(y),
      static_cast<lv_coord_t>(x + w - 1),
      static_cast<lv_coord_t>(y + h - 1),
  };
  lv_draw_image(fb->layer, &imgd, &a);
  return 0;
}

static int LuaFbImageNative(lua_State* L) {
  LuaFb* fb = CheckFb(L, 1);
  const int x = static_cast<int>(luaL_checkinteger(L, 2));
  const int y = static_cast<int>(luaL_checkinteger(L, 3));
  const char* src = luaL_checkstring(L, 4);

  if (!fb || !fb->layer) return 0;
  if (!src || !*src) return 0;
  src = InternFbString(L, src);
  {
    const std::string norm_src = NormalizeLvglImageSrc(src);
    if (!norm_src.empty()) src = InternFbString(L, norm_src.c_str());
  }

  lv_image_header_t header;
  lv_result_t r = lv_image_decoder_get_info(src, &header);
  if (r != LV_RESULT_OK || header.w == 0 || header.h == 0) {
    lv_image_cache_drop(src);
    lv_image_header_cache_drop(src);
    r = lv_image_decoder_get_info(src, &header);
    if (r != LV_RESULT_OK || header.w == 0 || header.h == 0) {
      ESP_LOGW(kTag, "fb:image_native decode failed src=%s free=%u min=%u",
               src,
               static_cast<unsigned>(esp_get_free_heap_size()),
               static_cast<unsigned>(esp_get_minimum_free_heap_size()));
      return 0;
    }
  }

  lv_draw_image_dsc_t imgd;
  lv_draw_image_dsc_init(&imgd);
  imgd.src = src;
  imgd.opa = LV_OPA_COVER;

  lv_area_t a = {
      static_cast<lv_coord_t>(x),
      static_cast<lv_coord_t>(y),
      static_cast<lv_coord_t>(x + static_cast<int>(header.w) - 1),
      static_cast<lv_coord_t>(y + static_cast<int>(header.h) - 1),
  };
  lv_draw_image(fb->layer, &imgd, &a);
  return 0;
}

static int LuaFbGif(lua_State* L) {
  LuaFb* fb = CheckFb(L, 1);
  const int x = static_cast<int>(luaL_checkinteger(L, 2));
  const int y = static_cast<int>(luaL_checkinteger(L, 3));
  (void)luaL_checkinteger(L, 4);  // keep API shape: w (ignored for speed)
  (void)luaL_checkinteger(L, 5);  // keep API shape: h (ignored for speed)
  const char* src = luaL_checkstring(L, 6);

  if (!fb || !fb->data) return 0;
  if (!src || !*src) return 0;

  GifPlayer* player = GetGifPlayerForState(L, true);
  if (!player) {
    lua_pushboolean(L, 0);
    return 1;
  }

  std::string err;
  if (!GifOpenPlayerIfNeeded(player, src, &err)) {
    lua_pushboolean(L, 0);
    lua_pushinteger(L, 0);
    return 2;
  }

  bool advance_now = false;
  bool has_advance_arg = false;
  if (lua_gettop(L) >= 7 && lua_isboolean(L, 7)) {
    has_advance_arg = true;
    advance_now = lua_toboolean(L, 7) != 0;
  }
  if (has_advance_arg) {
    if (advance_now) GifAdvanceNow(player);
  } else {
    GifAdvanceIfDue(player);
  }

  if (player->frame565.empty() || player->w <= 0 || player->h <= 0) {
    lua_pushboolean(L, 0);
    lua_pushinteger(L, player->frame_delay_ms);
    return 2;
  }

  int x0 = x;
  int y0 = y;
  int x1 = x + player->w;
  int y1 = y + player->h;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > fb->w) x1 = fb->w;
  if (y1 > fb->h) y1 = fb->h;
  if (x0 >= x1 || y0 >= y1) {
    lua_pushboolean(L, player->looped_once ? 1 : 0);
    lua_pushinteger(L, player->frame_delay_ms);
    player->looped_once = false;
    return 2;
  }

  const int copy_w = x1 - x0;
  const int copy_h = y1 - y0;
  const int sx0 = x0 - x;
  const int sy0 = y0 - y;

  for (int row = 0; row < copy_h; row++) {
    uint8_t* dst_row = fb->data + static_cast<uint32_t>(y0 + row) * fb->stride_bytes + static_cast<uint32_t>(x0) * 2;
    const uint16_t* src_row =
        &player->frame565[static_cast<size_t>(sy0 + row) * static_cast<size_t>(player->w) + static_cast<size_t>(sx0)];
    memcpy(dst_row, src_row, static_cast<size_t>(copy_w) * sizeof(uint16_t));
  }
  lua_pushboolean(L, player->looped_once ? 1 : 0);
  lua_pushinteger(L, player->frame_delay_ms);
  player->looped_once = false;
  return 2;
}

static int LuaFbGifClose(lua_State* L) {
  GifPlayer* player = GetGifPlayerForState(L, false);
  if (!player) return 0;
  GifClosePlayer(player);
  return 0;
}

static int LuaFbSetPx(lua_State* L) {
  LuaFb* fb = CheckFb(L, 1);
  const int x = static_cast<int>(luaL_checkinteger(L, 2));
  const int y = static_cast<int>(luaL_checkinteger(L, 3));
  const uint16_t c16 = CheckColor565(L, 4);
  FbSetPixel(fb, x, y, c16);
  return 0;
}

static int LuaFbRect(lua_State* L) {
  LuaFb* fb = CheckFb(L, 1);
  int x0 = static_cast<int>(luaL_checkinteger(L, 2));
  int y0 = static_cast<int>(luaL_checkinteger(L, 3));
  int w = static_cast<int>(luaL_checkinteger(L, 4));
  int h = static_cast<int>(luaL_checkinteger(L, 5));
  const uint16_t c16 = CheckColor565(L, 6);
  if (!fb || !fb->data) return 0;
  if (w <= 0 || h <= 0) return 0;

  int x1 = x0 + w - 1;
  int y1 = y0 + h - 1;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 >= fb->w) x1 = fb->w - 1;
  if (y1 >= fb->h) y1 = fb->h - 1;
  if (x0 > x1 || y0 > y1) return 0;

  for (int y = y0; y <= y1; y++) {
    uint8_t* row = fb->data + static_cast<uint32_t>(y) * fb->stride_bytes;
    uint16_t* p = reinterpret_cast<uint16_t*>(row);
    for (int x = x0; x <= x1; x++) p[x] = c16;
  }
  return 0;
}

static int LuaFbWidth(lua_State* L) {
  LuaFb* fb = CheckFb(L, 1);
  lua_pushinteger(L, fb ? fb->w : 0);
  return 1;
}

static int LuaFbHeight(lua_State* L) {
  LuaFb* fb = CheckFb(L, 1);
  lua_pushinteger(L, fb ? fb->h : 0);
  return 1;
}

static void EnsureFbMeta(lua_State* L) {
  if (luaL_newmetatable(L, kFbMeta) == 0) {
    lua_pop(L, 1);
    return;
  }

  lua_newtable(L);
  lua_pushcfunction(L, LuaFbSetClip);
  lua_setfield(L, -2, "set_clip");
  lua_pushcfunction(L, LuaFbClearClip);
  lua_setfield(L, -2, "clear_clip");
  lua_pushcfunction(L, LuaFbClipScoped);
  lua_setfield(L, -2, "clip");
  lua_pushcfunction(L, LuaFbFill);
  lua_setfield(L, -2, "fill");
  lua_pushcfunction(L, LuaFbSetPx);
  lua_setfield(L, -2, "set_px");
  lua_pushcfunction(L, LuaFbRect);
  lua_setfield(L, -2, "rect");
  lua_pushcfunction(L, LuaFbText);
  lua_setfield(L, -2, "text");
  lua_pushcfunction(L, LuaFbTextBox);
  lua_setfield(L, -2, "text_box");
  lua_pushcfunction(L, LuaFbImage);
  lua_setfield(L, -2, "image");
  lua_pushcfunction(L, LuaFbImageNative);
  lua_setfield(L, -2, "image_native");
  lua_pushcfunction(L, LuaFbGif);
  lua_setfield(L, -2, "gif");
  lua_pushcfunction(L, LuaFbGifClose);
  lua_setfield(L, -2, "gif_close");
  lua_pushcfunction(L, LuaFbWidth);
  lua_setfield(L, -2, "width");
  lua_pushcfunction(L, LuaFbHeight);
  lua_setfield(L, -2, "height");

  lua_setfield(L, -2, "__index");
  lua_pop(L, 1);  // metatable
}

static bool BlitRawRgb565To(uint8_t* dst, uint32_t dst_stride_bytes, int w, int h, const char* src, size_t len) {
  if (!dst || !src) return false;
  if (w <= 0 || h <= 0) return false;
  const size_t expected = static_cast<size_t>(w) * static_cast<size_t>(h) * 2;
  if (len != expected) return false;
  if (dst_stride_bytes < static_cast<uint32_t>(w * 2)) return false;

  for (int y = 0; y < h; y++) {
    memcpy(dst + static_cast<uint32_t>(y) * dst_stride_bytes, src + static_cast<size_t>(y) * w * 2, w * 2);
  }
  return true;
}

}  // namespace

bool LuaAppRuntime::RenderFrameBufferTo(int w,
                                        int h,
                                        uint8_t* out_rgb565,
                                        uint32_t out_stride_bytes,
                                        lv_layer_t* layer,
                                        const lv_font_t* default_font) {
  if (!out_rgb565) return false;
  if (!L_ || !loaded_) return false;
  EnsureInited();
  fb_tmp_strings_.clear();

  std::string err;
  if (!PushAppTable(&err)) {
    last_error_ = err;
    ESP_LOGE(kTag, "RenderFrameBufferTo: %s", last_error_.c_str());
    return false;
  }

  lua_getfield(L_, -1, "render_fb");
  if (!lua_isfunction(L_, -1)) {
    lua_pop(L_, 2);
    last_error_ = "app.render_fb missing";
    return false;
  }

  // Preferred: render_fb(fb) and draw directly via methods.
  EnsureFbMeta(L_);
  LuaFb* fb = static_cast<LuaFb*>(lua_newuserdata(L_, sizeof(LuaFb)));
  fb->data = out_rgb565;
  fb->stride_bytes = out_stride_bytes;
  fb->w = w;
  fb->h = h;
  fb->layer = layer;
  fb->default_font = default_font;
  if (layer) {
    fb->base_clip = layer->_clip_area;
    fb->base_phy_clip = layer->phy_clip_area;
  } else {
    fb->base_clip = {0, 0, static_cast<lv_coord_t>(w - 1), static_cast<lv_coord_t>(h - 1)};
    fb->base_phy_clip = fb->base_clip;
  }
  luaL_setmetatable(L_, kFbMeta);

  int ret = lua_pcall(L_, 1, 1, 0);
  if (ret == LUA_OK) {
    // Optional legacy: render_fb(fb) may still return raw bytes to blit.
    if (lua_isstring(L_, -1)) {
      size_t len = 0;
      const char* p = lua_tolstring(L_, -1, &len);
      if (!BlitRawRgb565To(out_rgb565, out_stride_bytes, w, h, p, len)) {
        lua_pop(L_, 2);  // result, app table
        last_error_ = "render_fb returned bad size";
        return false;
      }
    }
    lua_pop(L_, 2);  // result, app table
    return true;
  }

  // Fallback: render_fb(w, h) -> raw RGB565 bytes (legacy).
  const char* err_fb = lua_tostring(L_, -1);
  std::string err1 = err_fb ? err_fb : "render_fb(fb) failed";
  lua_pop(L_, 1);  // error message

  lua_getfield(L_, -1, "render_fb");
  lua_pushinteger(L_, static_cast<lua_Integer>(w));
  lua_pushinteger(L_, static_cast<lua_Integer>(h));

  ret = lua_pcall(L_, 2, 1, 0);
  if (ret != LUA_OK) {
    const char* err_wh = lua_tostring(L_, -1);
    std::string err2 = err_wh ? err_wh : "render_fb(w,h) failed";
    lua_pop(L_, 2);  // error, app table
    last_error_ = err1 + " | " + err2;
    return false;
  }

  if (!lua_isstring(L_, -1)) {
    lua_pop(L_, 2);
    last_error_ = "render_fb must return string in legacy mode";
    return false;
  }

  size_t len = 0;
  const char* p = lua_tolstring(L_, -1, &len);
  if (!BlitRawRgb565To(out_rgb565, out_stride_bytes, w, h, p, len)) {
    lua_pop(L_, 2);
    last_error_ = "render_fb bad size";
    return false;
  }

  lua_pop(L_, 2);  // result, app table
  return true;
}

void LuaAppRuntime::SetErrorFromTop() {
  const char* msg = lua_tostring(L_, -1);
  last_error_ = msg ? msg : "unknown lua error";
  lua_pop(L_, 1);
}

void LuaAppRuntime::ClearFrameBufferTemps() { fb_tmp_strings_.clear(); }

const char* LuaAppRuntime::InternFrameBufferString(const char* s) {
  if (!s) return "";
  fb_tmp_strings_.emplace_back(s);
  return fb_tmp_strings_.back().c_str();
}

int LuaAppRuntime::LuaSysLog(lua_State* L) {
  const char* msg = lua_tostring(L, 1);
  ESP_LOGI(kTag, "[lua] %s", msg ? msg : "");
  return 0;
}

int LuaAppRuntime::LuaSysNowMs(lua_State* L) {
  const int64_t ms = esp_timer_get_time() / 1000;
  lua_pushinteger(L, static_cast<lua_Integer>(ms));
  return 1;
}

int LuaAppRuntime::LuaSysLocalTime(lua_State* L) {
  const time_t now = time(nullptr);
  struct tm tm_now = {};
  localtime_r(&now, &tm_now);

  lua_newtable(L);

  lua_pushinteger(L, static_cast<lua_Integer>(tm_now.tm_year + 1900));
  lua_setfield(L, -2, "year");
  lua_pushinteger(L, static_cast<lua_Integer>(tm_now.tm_mon + 1));
  lua_setfield(L, -2, "month");
  lua_pushinteger(L, static_cast<lua_Integer>(tm_now.tm_mday));
  lua_setfield(L, -2, "day");
  lua_pushinteger(L, static_cast<lua_Integer>(tm_now.tm_hour));
  lua_setfield(L, -2, "hour");
  lua_pushinteger(L, static_cast<lua_Integer>(tm_now.tm_min));
  lua_setfield(L, -2, "min");
  lua_pushinteger(L, static_cast<lua_Integer>(tm_now.tm_sec));
  lua_setfield(L, -2, "sec");
  lua_pushinteger(L, static_cast<lua_Integer>(tm_now.tm_wday + 1));
  lua_setfield(L, -2, "wday");
  lua_pushinteger(L, static_cast<lua_Integer>(tm_now.tm_yday + 1));
  lua_setfield(L, -2, "yday");
  lua_pushboolean(L, tm_now.tm_isdst > 0 ? 1 : 0);
  lua_setfield(L, -2, "isdst");
  return 1;
}

int LuaAppRuntime::LuaSysUnixTime(lua_State* L) {
  const time_t now = time(nullptr);
  lua_pushinteger(L, static_cast<lua_Integer>(now));
  return 1;
}

int LuaAppRuntime::LuaSysListDir(lua_State* L) {
  const char* path = luaL_checkstring(L, 1);
  if (!path || !*path) {
    lua_newtable(L);
    return 1;
  }

  DIR* d = opendir(path);
  if (!d) {
    lua_newtable(L);
    return 1;
  }

  std::vector<std::string> names;
  while (true) {
    struct dirent* e = readdir(d);
    if (!e) break;
    const char* n = e->d_name;
    if (!n || !*n) continue;
    if (strcmp(n, ".") == 0 || strcmp(n, "..") == 0) continue;
    names.emplace_back(n);
  }
  closedir(d);

  std::sort(names.begin(), names.end());

  lua_newtable(L);
  int idx = 1;
  for (const auto& n : names) {
    lua_pushstring(L, n.c_str());
    lua_rawseti(L, -2, idx++);
  }
  return 1;
}

int LuaAppRuntime::LuaDataGet(lua_State* L) {
  const char* key = lua_tostring(L, 1);
  if (!key) {
    lua_pushnil(L);
    return 1;
  }

  const int64_t ms = esp_timer_get_time() / 1000;

  // V1 demo: fake data that animates deterministically with uptime.
  if (strcmp(key, "weather.temp_c") == 0) {
    const int v = 18 + static_cast<int>((ms / 3000) % 10);
    lua_pushinteger(L, v);
    return 1;
  }
  if (strcmp(key, "weather.cond") == 0) {
    static const char* kConds[] = {"Sunny", "Cloudy", "Rain", "Wind"};
    lua_pushstring(L, kConds[(ms / 7000) % 4]);
    return 1;
  }
  if (strcmp(key, "stocks.symbol") == 0) {
    lua_pushstring(L, "AAPL");
    return 1;
  }
  if (strcmp(key, "stocks.price") == 0) {
    const int base = 180;
    const int v = base + static_cast<int>((ms / 1500) % 20);
    lua_pushnumber(L, static_cast<lua_Number>(v) + 0.12);
    return 1;
  }
  if (strcmp(key, "stocks.change_pct") == 0) {
    const int phase = static_cast<int>((ms / 5000) % 6);
    const double pct = (phase - 3) * 0.12;
    lua_pushnumber(L, static_cast<lua_Number>(pct));
    return 1;
  }

  cJSON* root = LoadLuaDataRoot();
  if (root) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (item) {
      JsonToLua(L, item, 0);
      cJSON_Delete(root);
      return 1;
    }
    cJSON_Delete(root);
  }

  lua_pushnil(L);
  return 1;
}

int LuaAppRuntime::LuaDataSet(lua_State* L) {
  const char* key = luaL_checkstring(L, 1);
  if (!key || !*key) {
    lua_pushboolean(L, 0);
    lua_pushstring(L, "key required");
    return 2;
  }

  cJSON* root = LoadLuaDataRoot();
  if (!root) {
    lua_pushboolean(L, 0);
    lua_pushstring(L, "load store failed");
    return 2;
  }

  cJSON_DeleteItemFromObjectCaseSensitive(root, key);
  if (!(lua_gettop(L) >= 2 && lua_isnil(L, 2))) {
    cJSON* item = LuaToJson(L, 2, 0);
    if (!item) {
      cJSON_Delete(root);
      lua_pushboolean(L, 0);
      lua_pushstring(L, "unsupported value");
      return 2;
    }
    cJSON_AddItemToObject(root, key, item);
  }

  std::string err;
  const bool ok = SaveLuaDataRoot(root, &err);
  cJSON_Delete(root);

  lua_pushboolean(L, ok ? 1 : 0);
  if (!ok) {
    lua_pushstring(L, err.empty() ? "save failed" : err.c_str());
    return 2;
  }
  return 1;
}

void LuaAppRuntime::PushSysModule() {
  if (!L_) return;

  lua_newtable(L_);
  lua_pushcfunction(L_, LuaSysLog);
  lua_setfield(L_, -2, "log");
  lua_pushcfunction(L_, LuaSysNowMs);
  lua_setfield(L_, -2, "now_ms");
  lua_pushcfunction(L_, LuaSysLocalTime);
  lua_setfield(L_, -2, "local_time");
  lua_pushcfunction(L_, LuaSysUnixTime);
  lua_setfield(L_, -2, "unix_time");
  lua_pushcfunction(L_, LuaSysListDir);
  lua_setfield(L_, -2, "listdir");
  lua_setglobal(L_, "sys");

  // Also allow sys.* as functions via global table injection.
  SetFieldFn(L_, "sys", "log", LuaSysLog);
  SetFieldFn(L_, "sys", "now_ms", LuaSysNowMs);
  SetFieldFn(L_, "sys", "local_time", LuaSysLocalTime);
  SetFieldFn(L_, "sys", "unix_time", LuaSysUnixTime);
  SetFieldFn(L_, "sys", "listdir", LuaSysListDir);
}

void LuaAppRuntime::PushDataModule() {
  if (!L_) return;

  lua_newtable(L_);
  lua_pushcfunction(L_, LuaDataGet);
  lua_setfield(L_, -2, "get");
  lua_pushcfunction(L_, LuaDataSet);
  lua_setfield(L_, -2, "set");
  lua_setglobal(L_, "data");

  SetFieldFn(L_, "data", "get", LuaDataGet);
  SetFieldFn(L_, "data", "set", LuaDataSet);
}
