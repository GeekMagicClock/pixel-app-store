local app = {}

local state = { anim_ms = 0 }

local C_WATER = 0x03D9
local C_WATER_DARK = 0x01CF
local C_SAND = 0xFEA0
local C_PLANT = 0x07E0
local C_PLANT_DARK = 0x03E0
local C_FISH_A = 0xFD20
local C_FISH_B = 0xFFE0
local C_FISH_C = 0xF81F
local C_BUBBLE = 0xEFFF

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

local function draw_fish(fb, x, y, body, accent)
  rect_safe(fb, x + 1, y, 5, 3, body)
  rect_safe(fb, x + 2, y - 1, 2, 1, accent)
  rect_safe(fb, x, y + 1, 1, 1, body)
  rect_safe(fb, x + 6, y + 1, 2, 1, accent)
  rect_safe(fb, x + 1, y + 3, 4, 1, accent)
  rect_safe(fb, x + 2, y + 1, 1, 1, 0x0000)
end

function app.init()
  state.anim_ms = 0
end

function app.tick(dt_ms)
  state.anim_ms = (state.anim_ms + (dt_ms or 0)) % 60000
end

function app.render_fb(fb)

  fb:fill(C_WATER)
  for y = 0, 23, 3 do
    rect_safe(fb, 0, y, 64, 1, C_WATER_DARK)
  end
  rect_safe(fb, 0, 1, 64, 2, C_BUBBLE)
  rect_safe(fb, 0, 26, 64, 6, C_SAND)
  rect_safe(fb, 4, 24, 10, 3, C_WATER_DARK)
  rect_safe(fb, 25, 23, 8, 4, C_WATER_DARK)
  rect_safe(fb, 47, 24, 12, 3, C_WATER_DARK)
  for x = 2, 62, 6 do
    rect_safe(fb, x, 28 + ((x // 6) % 2), 2, 1, C_WATER_DARK)
  end
  for i = 0, 5 do
    local sway = math.floor((triangle_wave(1800, i * 180) - 0.5) * 4)
    local x = 5 + i * 10
    rect_safe(fb, x + sway, 20, 2, 8, C_PLANT_DARK)
    rect_safe(fb, x + sway + 1, 18, 1, 10, C_PLANT)
    rect_safe(fb, x + sway - 1, 21, 1, 5, C_PLANT)
  end
  local f1 = (state.anim_ms // 90) % 72
  local f2 = (state.anim_ms // 120) % 76
  local f3 = (state.anim_ms // 150) % 80
  draw_fish(fb, 64 - f1, 7, C_FISH_A, C_FISH_B)
  draw_fish(fb, f2 - 8, 13, C_FISH_B, C_FISH_C)
  draw_fish(fb, 64 - f3, 19, C_FISH_C, C_FISH_B)
  draw_fish(fb, 10 + ((state.anim_ms // 160) % 24), 10, C_FISH_A, C_FISH_C)
  for i = 0, 7 do
    local bx = 6 + i * 7 + ((i % 2) * 2)
    local by = 24 - ((state.anim_ms // (80 + i * 8) + i * 3) % 20)
    rect_safe(fb, bx, by, 1, 1, C_BUBBLE)
    if (i % 3) == 0 then rect_safe(fb, bx + 1, by + 1, 1, 1, C_BUBBLE) end
  end
end


-- __GLOBAL_BOOT_SPLASH_WRAPPER_V1__
local __boot_now_ms = now_ms or (sys and sys.now_ms) or function() return 0 end
local __boot_started_ms = 0
local __boot_ms = tonumber(data.get("aquarium_scene.boot_splash_ms") or data.get("app.boot_splash_ms") or 5000) or 5000
if __boot_ms < 0 then __boot_ms = 0 end
local __boot_name = tostring(data.get("aquarium_scene.app_name") or "Aquarium Scene")

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
