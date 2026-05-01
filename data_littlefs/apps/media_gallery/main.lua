local app = {}

local font = "builtin:silkscreen_regular_8"
local ASSETS_DIR = "/littlefs/apps/media_gallery/assets"
local ASSETS_LV_PREFIX = "S:/littlefs/apps/media_gallery/assets/"
local DEFAULT_HOLD_MS = 3500

local C_BG = 0x0000
local C_TEXT = 0xFFFF
local C_MUTED = 0x9CF3

local playlist = {}

local state = {
  idx = 1,
  elapsed_ms = 0,
  gif_loop_done = false,
  gif_hold_reached = false,
  gif_src = "",
  gif_next_due_ms = 0,
  gif_has_frame = false,
  gif_should_advance = true,
}

local function lower(s)
  return string.lower(tostring(s or ""))
end

local function kind_from_name(name)
  local n = lower(name)
  if string.match(n, "%.gif$") then return "gif" end
  if string.match(n, "%.png$") then return "png" end
  if string.match(n, "%.jpg$") or string.match(n, "%.jpeg$") then return "jpg" end
  return nil
end

local function build_item_from_name(name)
  local kind = kind_from_name(name)
  if not kind then return nil end
  return {
    kind = kind,
    label = string.upper(kind),
    src = ASSETS_LV_PREFIX .. name,
  }
end

local function cfg_hold_ms()
  local n = tonumber(data.get("media_gallery.interval_ms") or data.get("media_gallery.hold_ms") or DEFAULT_HOLD_MS) or DEFAULT_HOLD_MS
  if n < 500 then n = 500 end
  if n > 60000 then n = 60000 end
  return math.floor(n)
end

local function cfg_random_play()
  local v = data.get("media_gallery.random_play")
  if v == nil then return false end
  local s = lower(v)
  return (v == true) or s == "1" or s == "true" or s == "yes" or s == "on"
end

local function rebuild_playlist()
  playlist = {}
  local files = {}
  if sys and sys.listdir then
    files = sys.listdir(ASSETS_DIR) or {}
  end

  for i = 1, #files do
    local name = tostring(files[i] or "")
    local item = build_item_from_name(name)
    if item then playlist[#playlist + 1] = item end
  end

  if #playlist == 0 then
    -- Safe fallback if listdir is unavailable or directory is empty.
    playlist = {
      {kind = "png", label = "PNG", src = ASSETS_LV_PREFIX .. "logo.png"},
      {kind = "jpg", label = "JPG", src = ASSETS_LV_PREFIX .. "photo.jpg"},
    }
  end
end

local function current_item()
  if #playlist == 0 then return nil end
  return playlist[state.idx] or playlist[1]
end

local function advance_playlist()
  state.elapsed_ms = 0
  state.gif_src = ""
  state.gif_has_frame = false
  state.gif_should_advance = true
  state.gif_next_due_ms = 0
  state.gif_loop_done = false
  state.gif_hold_reached = false
  if cfg_random_play() and #playlist > 1 then
    local next_idx = state.idx
    local guard = 0
    while next_idx == state.idx and guard < 16 do
      next_idx = (math.floor(math.random() * #playlist) % #playlist) + 1
      guard = guard + 1
    end
    state.idx = next_idx
  else
    state.idx = state.idx + 1
    if state.idx > #playlist then state.idx = 1 end
  end
end

function app.init(config)
  sys.log("media_gallery init")
  rebuild_playlist()
  state.idx = 1
  state.elapsed_ms = 0
  state.gif_loop_done = false
  state.gif_hold_reached = false
  state.gif_src = ""
  state.gif_next_due_ms = 0
  state.gif_has_frame = false
  state.gif_should_advance = true
end

function app.tick(dt_ms)
  local it = current_item()
  if not it then return end

  if it.kind == "gif" then
    if state.gif_src ~= it.src then
      state.gif_src = it.src
      state.gif_next_due_ms = 0
      state.gif_has_frame = false
      state.gif_should_advance = true
      state.elapsed_ms = 0
      state.gif_loop_done = false
      state.gif_hold_reached = false
    end

    local now = sys.now_ms and sys.now_ms() or 0
    local hold = cfg_hold_ms()
    state.elapsed_ms = state.elapsed_ms + (dt_ms or 0)
    if state.elapsed_ms >= hold then
      state.gif_hold_reached = true
    end

    if state.gif_hold_reached and state.gif_loop_done then
      advance_playlist()
    end
    return
  end

  local hold = cfg_hold_ms()
  state.elapsed_ms = state.elapsed_ms + (dt_ms or 0)
  if state.elapsed_ms >= hold then
    advance_playlist()
  end
end

function app.render_fb(fb)
  fb:fill(C_BG)
  local it = current_item()
  if not it then
    return
  end

  if it.kind == "gif" then
    local now = sys.now_ms and sys.now_ms() or 0
    local due = (state.gif_next_due_ms or 0)
    local need_advance = state.gif_should_advance or (not state.gif_has_frame) or (due > 0 and now >= due) or (due <= 0)

    if not need_advance then
      fb:gif(0, 0, 64, 32, it.src, false)
      return
    end

    -- Time-based catch-up: if render loop was late, advance multiple frames so GIF stays smooth.
    local catch_count = 0
    repeat
      local wrapped, delay_ms = fb:gif(0, 0, 64, 32, it.src, true)
      if wrapped then state.gif_loop_done = true end
      state.gif_has_frame = true
      local d = tonumber(delay_ms) or 100
      if d < 20 then d = 20 end
      if state.gif_next_due_ms <= 0 then
        state.gif_next_due_ms = now + d
      else
        state.gif_next_due_ms = state.gif_next_due_ms + d
      end
      catch_count = catch_count + 1
    until (not (now >= state.gif_next_due_ms and catch_count < 4))

    state.gif_should_advance = false
  else
    if fb.gif_close then fb:gif_close() end
    -- Prefer native-size draw for still images to reduce decode+scale pressure.
    if fb.image_native then
      fb:image_native(0, 0, it.src)
    else
      fb:image(0, 0, 64, 32, it.src)
    end
  end
end


-- __GLOBAL_BOOT_SPLASH_WRAPPER_V1__
local __boot_now_ms = now_ms or (sys and sys.now_ms) or function() return 0 end
local __boot_started_ms = 0
local __boot_ms = tonumber(data.get("media_gallery.boot_splash_ms") or data.get("app.boot_splash_ms") or 5000) or 5000
if __boot_ms < 0 then __boot_ms = 0 end
local __boot_name = tostring(data.get("media_gallery.app_name") or "Media Gallery")

local function __boot_compact_text(s, limit)
  s = tostring(s or "")
  s = string.gsub(s, "%s+", " ")
  s = string.gsub(s, "^%s+", "")
  s = string.gsub(s, "%s+$", "")
  local n = tonumber(limit) or 16
  if #s > n then return string.sub(s, 1, n - 1) .. "…" end
  return s
end

local function __boot_split_title_lines(name)
  local text = tostring(name or "")
  text = string.gsub(text, "%s+", " ")
  text = string.gsub(text, "^%s+", "")
  text = string.gsub(text, "%s+$", "")
  if text == "" then return "APP", "" end
  local mid = math.floor(#text / 2)
  local cut = nil
  local best = 999
  for i = 1, #text do
    if string.sub(text, i, i) == " " then
      local d = math.abs(i - mid)
      if d < best then
        best = d
        cut = i
      end
    end
  end
  if not cut then return text, "" end
  local a = string.gsub(string.sub(text, 1, cut - 1), "%s+$", "")
  local b = string.gsub(string.sub(text, cut + 1), "^%s+", "")
  return a, b
end

local function __boot_is_active()
  if __boot_started_ms <= 0 then return false end
  return (__boot_now_ms() - __boot_started_ms) < __boot_ms
end

local __orig_init = app.init
app.init = function(...)
  __boot_started_ms = __boot_now_ms()
  if __orig_init then return __orig_init(...) end
end

local __orig_render_fb = app.render_fb
if __orig_render_fb then
  app.render_fb = function(...)
    local fb = select(1, ...)
    if __boot_is_active() and fb and fb.fill and fb.text_box then
      local t1, t2 = __boot_split_title_lines(__boot_name)
      fb:fill(0x0000)
      if t2 ~= "" then
        fb:text_box(0, 8, 64, 8, __boot_compact_text(t1, 14), 0x07FF, "builtin:silkscreen_regular_8", 8, "center", false)
        fb:text_box(0, 16, 64, 8, __boot_compact_text(t2, 14), 0x07FF, "builtin:silkscreen_regular_8", 8, "center", false)
      else
        fb:text_box(0, 12, 64, 8, __boot_compact_text(t1, 14), 0x07FF, "builtin:silkscreen_regular_8", 8, "center", false)
      end
      return true
    end
    return __orig_render_fb(...)
  end
end

local __orig_render = app.render
if __orig_render then
  app.render = function(...)
    if __boot_is_active() then
      local t1, t2 = __boot_split_title_lines(__boot_name)
      if t2 ~= "" then
        return {"", __boot_compact_text(t1, 16), __boot_compact_text(t2, 16), ""}
      end
      return {"", __boot_compact_text(t1, 16), "", ""}
    end
    return __orig_render(...)
  end
end

return app
