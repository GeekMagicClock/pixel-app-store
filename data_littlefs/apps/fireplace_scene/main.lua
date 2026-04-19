local app = {}

local state = { anim_ms = 0 }

local C_BG = 0x0000
local C_WALL = 0x18A3
local C_HEARTH = 0x8200
local C_LOG = 0x59A0
local C_LOG_DARK = 0x38C0
local C_FLAME_A = 0xFD20
local C_FLAME_B = 0xFFE0
local C_FLAME_C = 0xF800
local C_SPARK = 0xFFFF

local function set_px_safe(fb, x, y, c)
  if x < 0 or x >= 64 or y < 0 or y >= 32 then return end
  fb:set_px(x, y, c)
end

local function rect_safe(fb, x, y, w, h, c)
  if w <= 0 or h <= 0 then return end
  fb:rect(x, y, w, h, c)
end

local function line_safe(fb, x0, y0, x1, y1, c)
  local dx = math.abs(x1 - x0)
  local sx = x0 < x1 and 1 or -1
  local dy = -math.abs(y1 - y0)
  local sy = y0 < y1 and 1 or -1
  local err = dx + dy
  while true do
    set_px_safe(fb, x0, y0, c)
    if x0 == x1 and y0 == y1 then break end
    local e2 = err * 2
    if e2 >= dy then
      err = err + dy
      x0 = x0 + sx
    end
    if e2 <= dx then
      err = err + dx
      y0 = y0 + sy
    end
  end
end

local function clamp(v, lo, hi)
  if v < lo then return lo end
  if v > hi then return hi end
  return v
end

local function triangle_wave(period_ms, phase_ms)
  local period = period_ms or 2000
  local t = (state.anim_ms + (phase_ms or 0)) % period
  local half = period / 2
  if t < half then
    return t / half
  end
  return 1 - ((t - half) / half)
end



function app.init()
  state.anim_ms = 0
end

function app.tick(dt_ms)
  state.anim_ms = (state.anim_ms + (dt_ms or 0)) % 60000
end

function app.render_fb(fb)

  fb:fill(C_BG)
  rect_safe(fb, 0, 0, 64, 20, C_WALL)
  rect_safe(fb, 0, 20, 64, 12, C_HEARTH)
  for y = 0, 16, 4 do
    rect_safe(fb, 0, y, 64, 1, C_LOG_DARK)
  end
  for x = 6, 62, 8 do
    rect_safe(fb, x, ((x // 8) % 2 == 0) and 0 or 4, 1, 4, C_LOG_DARK)
    rect_safe(fb, x, ((x // 8) % 2 == 0) and 8 or 12, 1, 4, C_LOG_DARK)
  end
  rect_safe(fb, 7, 12, 50, 2, C_LOG_DARK)
  rect_safe(fb, 8, 14, 48, 14, C_BG)
  rect_safe(fb, 9, 15, 46, 11, C_HEARTH)
  rect_safe(fb, 11, 25, 42, 1, C_LOG_DARK)
  rect_safe(fb, 13, 26, 38, 2, C_LOG_DARK)
  rect_safe(fb, 14, 25, 12, 2, C_LOG)
  rect_safe(fb, 23, 24, 16, 3, C_LOG_DARK)
  rect_safe(fb, 38, 25, 12, 2, C_LOG)
  rect_safe(fb, 18, 24, 3, 1, C_LOG_DARK)
  rect_safe(fb, 42, 24, 3, 1, C_LOG_DARK)
  for i = 0, 6 do
    local x = 12 + i * 6
    local h1 = 5 + math.floor(triangle_wave(900 + i * 110, i * 70) * 8)
    local h2 = 3 + math.floor(triangle_wave(700 + i * 90, i * 130) * 6)
    local h3 = 2 + math.floor(triangle_wave(560 + i * 50, i * 90) * 4)
    rect_safe(fb, x, 26 - h1, 5, h1, C_FLAME_C)
    rect_safe(fb, x + 1, 25 - h2, 3, h2, C_FLAME_A)
    rect_safe(fb, x + 2, 24 - h3, 1, h3, C_FLAME_B)
  end
  for i = 0, 9 do
    local ember_x = 10 + i * 5
    rect_safe(fb, ember_x, 28 + ((i % 2) == 0 and 0 or 1), 1, 1, (i % 3 == 0) and C_FLAME_A or C_FLAME_C)
  end
  for i = 0, 7 do
    local sx = (i * 9 + math.floor(state.anim_ms / 110)) % 54 + 5
    local sy = 18 - ((state.anim_ms // 90 + i * 3) % 14)
    if sy >= 2 then rect_safe(fb, sx, sy, 1, 1, C_SPARK) end
  end
end


-- __GLOBAL_BOOT_SPLASH_WRAPPER_V1__
local __boot_now_ms = now_ms or (sys and sys.now_ms) or function() return 0 end
local __boot_started_ms = 0
local __boot_ms = tonumber(data.get("fireplace_scene.boot_splash_ms") or data.get("app.boot_splash_ms") or 1200) or 1200
if __boot_ms < 0 then __boot_ms = 0 end
local __boot_name = tostring(data.get("fireplace_scene.app_name") or "Fireplace Scene")

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
