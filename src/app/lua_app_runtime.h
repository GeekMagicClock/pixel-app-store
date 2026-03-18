#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct lua_State;
typedef struct _lv_layer_t lv_layer_t;
typedef struct _lv_font_t lv_font_t;

// Minimal, sandboxed-ish Lua app runtime for V1 demo:
// - Each app is a directory under LittleFS: /littlefs/apps/<app_id>/
// - Needs entry chunk: main.lua, or manifest.json with {"entry":"..."} override
// - Entry chunk should `return app` table with optional functions:
//     app.init(config_table)
//     app.tick(ms)
//     app.render() -> table (array) of up to 4 strings
class LuaAppRuntime {
 public:
  LuaAppRuntime();
  ~LuaAppRuntime();

  LuaAppRuntime(const LuaAppRuntime&) = delete;
  LuaAppRuntime& operator=(const LuaAppRuntime&) = delete;

  bool LoadFromDir(const std::string& app_dir);
  const std::string& app_dir() const { return app_dir_; }

  // Calls app.init({}) once if present.
  void EnsureInited();

  // Calls app.tick(ms) if present. Safe to call frequently.
  void Tick(uint32_t ms);
  // Force a full Lua GC cycle. Intended for app-exit cleanup.
  void FullGcNow();

  // Calls app.render() and fills up to 4 lines.
  // Returns false on Lua error.
  bool Render(std::vector<std::string>* out_lines);

  // Calls app.render() and fetches a set of named bindings (dict-like).
  // - For missing string keys, it also supports "lineN" => numeric key N.
  // Missing keys map to "".
  bool RenderBinds(const std::vector<std::string>& keys, std::vector<std::string>* out_values);

  // Framebuffer mode (V1 experiment):
  // If the app defines app.render_fb(...), the runtime can bypass UI widgets and blit a framebuffer.
  // Two supported styles:
  //  1) app.render_fb(fb) -> nil/true  (writes directly to fb via methods like fb:fill()/fb:rect())
  //  2) app.render_fb(w, h) -> string (legacy: returns raw RGB565 bytes, len = w*h*2)
  bool SupportsFrameBuffer();
  bool RenderFrameBufferTo(int w,
                           int h,
                           uint8_t* out_rgb565,
                           uint32_t out_stride_bytes,
                           lv_layer_t* layer,
                           const lv_font_t* default_font);

  // Clear temp strings used by framebuffer draw tasks.
  // Call after `lv_canvas_finish_layer`.
  void ClearFrameBufferTemps();

  // Internal: keep string memory alive across `lv_canvas_finish_layer`.
  const char* InternFrameBufferString(const char* s);

  const char* last_error() const { return last_error_.c_str(); }

 private:
  static void* LuaAllocPreferPsram(void* ud, void* ptr, size_t osize, size_t nsize);
  bool CreateState();
  void DestroyState();
  bool LoadMain(const std::string& main_path);
  void SetErrorFromTop();

  bool PushAppTable(std::string* out_err);

  // Lua C functions we expose.
  static int LuaSysLog(lua_State* L);
  static int LuaSysNowMs(lua_State* L);
  static int LuaSysUnixTime(lua_State* L);
  static int LuaSysListDir(lua_State* L);
  static int LuaDataGet(lua_State* L);

  void PushDataModule();
  void PushSysModule();

  lua_State* L_ = nullptr;
  bool loaded_ = false;
  bool inited_ = false;
  bool logged_empty_render_ = false;
  bool logged_empty_lines_ = false;
  bool logged_float_key_fallback_ = false;
  bool logged_first_lines_ = false;
  uint32_t gc_tick_acc_ms_ = 0;
  uint32_t gc_full_acc_ms_ = 0;

  std::string app_dir_;
  std::string last_error_;

  std::vector<std::string> fb_tmp_strings_;
};
