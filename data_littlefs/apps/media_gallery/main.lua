local app = {}

local font = "builtin:silkscreen_regular_8"
local ASSETS_DIR = "/littlefs/apps/media_gallery/assets"
local ASSETS_LV_PREFIX = "S:/littlefs/apps/media_gallery/assets/"

local HOLD_GIF_MS = 6000
local HOLD_PNG_MS = 3500
local HOLD_JPG_MS = 3500

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
  if string.match(n, "%.gif$") then return "gif", HOLD_GIF_MS end
  if string.match(n, "%.png$") then return "png", HOLD_PNG_MS end
  if string.match(n, "%.jpg$") or string.match(n, "%.jpeg$") then return "jpg", HOLD_JPG_MS end
  return nil, nil
end

local function rebuild_playlist()
  playlist = {}
  local files = {}
  if sys and sys.listdir then
    files = sys.listdir(ASSETS_DIR) or {}
  end

  for i = 1, #files do
    local name = tostring(files[i] or "")
    local kind, hold = kind_from_name(name)
    if kind then
      playlist[#playlist + 1] = {
        kind = kind,
        label = string.upper(kind),
        src = ASSETS_LV_PREFIX .. name,
        hold_ms = hold,
      }
    end
  end

  if #playlist == 0 then
    -- Safe fallback if listdir is unavailable or directory is empty.
    playlist = {
      {kind = "gif", label = "GIF", src = ASSETS_LV_PREFIX .. "weather.gif", hold_ms = HOLD_GIF_MS},
      {kind = "png", label = "PNG", src = ASSETS_LV_PREFIX .. "logo.png", hold_ms = HOLD_PNG_MS},
      {kind = "jpg", label = "JPG", src = ASSETS_LV_PREFIX .. "photo.jpg", hold_ms = HOLD_JPG_MS},
    }
  end
end

local function current_item()
  if #playlist == 0 then return nil end
  return playlist[state.idx] or playlist[1]
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
      state.gif_loop_done = false
      state.gif_hold_reached = false
      state.elapsed_ms = 0
    end

    local now = sys.now_ms and sys.now_ms() or 0
    if now >= (state.gif_next_due_ms or 0) then
      state.gif_should_advance = true
    end
  end

  if it.kind == "gif" then
    local hold = (it and it.hold_ms) or HOLD_GIF_MS
    state.elapsed_ms = state.elapsed_ms + (dt_ms or 0)
    if state.elapsed_ms >= hold then
      state.gif_hold_reached = true
    end

    if state.gif_hold_reached and state.gif_loop_done then
      state.gif_loop_done = false
      state.gif_hold_reached = false
      state.elapsed_ms = 0
      state.idx = state.idx + 1
      if state.idx > #playlist then state.idx = 1 end
    end
    return
  end

  local hold = (it and it.hold_ms) or 3000
  state.elapsed_ms = state.elapsed_ms + (dt_ms or 0)
  if state.elapsed_ms >= hold then
    state.elapsed_ms = 0
    state.gif_loop_done = false
    state.gif_hold_reached = false
    state.gif_src = ""
    state.gif_has_frame = false
    state.gif_should_advance = true
    state.gif_next_due_ms = 0
    state.idx = state.idx + 1
    if state.idx > #playlist then state.idx = 1 end
  end
end

function app.render_fb(fb)
  fb:fill(C_BG)
  local it = current_item()
  if not it then
    return
  end

  if it.kind == "gif" then
    local advance = state.gif_should_advance or (not state.gif_has_frame)
    local wrapped, delay_ms = fb:gif(0, 0, 64, 32, it.src, advance)
    if wrapped then state.gif_loop_done = true end
    if advance then
      state.gif_should_advance = false
      state.gif_has_frame = true
      local d = tonumber(delay_ms) or 100
      if d < 60 then d = 60 end
      local now = sys.now_ms and sys.now_ms() or 0
      state.gif_next_due_ms = now + d
    end
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

return app
