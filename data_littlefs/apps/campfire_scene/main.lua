local app = {}

local state = { anim_ms = 0 }

local C_SKY = 0x0006
local C_STAR = 0xFFFF
local C_MOON = 0xEF7D
local C_MOON_HI = 0xFFFF
local C_MOON_SHADE = 0x9CF3
local C_MOON_EDGE = 0x84B2
local C_MTN = 0x18C3
local C_TREE = 0x0100
local C_TENT = 0xFC40
local C_TENT_HI = 0xFD85
local C_TENT_SHADE = 0xA220
local C_FIRE_A = 0xFD20
local C_FIRE_B = 0xFFE0
local C_FIRE_C = 0xFFFF
local C_LOG = 0x79E0
local C_LOG_DARK = 0x48C0
local C_LOG_HI = 0xAB69
local C_GROUND = 0x03A0
local C_GLOW = 0x7B40

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

local function cycle_progress(period_ms, phase_ms)
  local period = period_ms or 1000
  return ((state.anim_ms + (phase_ms or 0)) % period) / period
end

local function draw_ember(fb, base_x, base_y, rise_px, drift_px, period_ms, phase_ms, hot_color, cool_color)
  local p = cycle_progress(period_ms, phase_ms)
  if p > 0.84 then return end
  local x = math.floor(base_x + p * drift_px)
  local y = math.floor(base_y - p * rise_px)
  local c = p < 0.28 and hot_color or cool_color
  rect_safe(fb, x, y, 1, 1, c)
  if p < 0.18 then
    rect_safe(fb, x, y + 1, 1, 1, C_GLOW)
  end
end

local function draw_shooting_star(fb, start_x, start_y, dx, dy, period_ms, phase_ms)
  local p = cycle_progress(period_ms, phase_ms)
  if p < 0.74 or p > 0.88 then return end
  local t = (p - 0.74) / 0.14
  local bright = math.sin(t * math.pi)
  local x = math.floor(start_x + dx * t)
  local y = math.floor(start_y + dy * t)
  local head = blend565(C_MOON_HI, C_STAR, bright * 0.75)
  local tail1 = blend565(C_MOON, C_SKY, 0.25 + (1 - bright) * 0.35)
  local tail2 = blend565(C_MOON_SHADE, C_SKY, 0.4 + (1 - bright) * 0.35)
  local tail3 = blend565(C_MOON_EDGE, C_SKY, 0.55 + (1 - bright) * 0.3)
  set_px_safe(fb, x, y, head)
  set_px_safe(fb, x + 1, y - 1, tail1)
  set_px_safe(fb, x + 2, y - 2, tail2)
  set_px_safe(fb, x + 3, y - 2, tail3)
end



function app.init()
  state.anim_ms = 0
end

function app.tick(dt_ms)
  state.anim_ms = (state.anim_ms + (dt_ms or 0)) % 60000
end

function app.render_fb(fb)
  local flicker = math.floor(triangle_wave(420, 0) * 2)
  local sway = math.floor(triangle_wave(1800, 350) * 2)
  fb:fill(C_SKY)
  rect_safe(fb, 0, 24, 64, 8, C_GROUND)
  rect_safe(fb, 51, 2, 2, 1, C_MOON_EDGE)
  rect_safe(fb, 50, 3, 4, 1, C_MOON_EDGE)
  rect_safe(fb, 49, 4, 5, 1, C_MOON_EDGE)
  rect_safe(fb, 48, 5, 6, 2, C_MOON_EDGE)
  rect_safe(fb, 49, 7, 5, 1, C_MOON_EDGE)
  rect_safe(fb, 50, 8, 4, 1, C_MOON_EDGE)
  rect_safe(fb, 51, 9, 2, 1, C_MOON_EDGE)
  rect_safe(fb, 50, 3, 2, 1, C_MOON_SHADE)
  rect_safe(fb, 49, 4, 3, 1, C_MOON)
  rect_safe(fb, 48, 5, 4, 2, C_MOON)
  rect_safe(fb, 49, 7, 3, 1, C_MOON)
  rect_safe(fb, 50, 8, 2, 1, C_MOON_SHADE)
  rect_safe(fb, 49, 4, 1, 3, C_MOON_HI)
  rect_safe(fb, 48, 5, 1, 2, C_MOON_HI)
  rect_safe(fb, 51, 4, 3, 1, C_SKY)
  rect_safe(fb, 52, 5, 2, 2, C_SKY)
  rect_safe(fb, 51, 7, 3, 1, C_SKY)
  rect_safe(fb, 52, 8, 2, 1, C_SKY)
  line_safe(fb, 0, 23, 14, 14, C_MTN)
  line_safe(fb, 14, 14, 30, 23, C_MTN)
  line_safe(fb, 18, 23, 36, 12, C_MTN)
  line_safe(fb, 36, 12, 55, 23, C_MTN)
  line_safe(fb, 40, 23, 52, 15, C_MTN)
  line_safe(fb, 52, 15, 63, 23, C_MTN)
  for i = 0, 7 do rect_safe(fb, (i * 7 + 5) % 60, (i * 5 + 3) % 13, 1, 1, C_STAR) end
  draw_shooting_star(fb, 59, 2, -24, 8, 17000, 2400)
  draw_shooting_star(fb, 48, 3, -18, 6, 23000, 9900)
  rect_safe(fb, 7, 15, 3, 9, C_TREE)
  rect_safe(fb, 10, 17, 2, 7, C_TREE)
  rect_safe(fb, 56, 16, 3, 8, C_TREE)
  rect_safe(fb, 59, 18, 2, 6, C_TREE)

  rect_safe(fb, 37, 22, 17, 2, C_TENT_SHADE)
  line_safe(fb, 38, 24, 46, 13, C_TENT)
  line_safe(fb, 46, 13, 56, 24, C_TENT_HI)
  line_safe(fb, 38, 24, 56, 24, C_TENT)
  line_safe(fb, 46, 13, 46, 24, C_TENT_HI)
  rect_safe(fb, 41, 20, 4, 4, C_TENT_SHADE)
  line_safe(fb, 46, 14, 53, 23, C_TENT_HI)
  line_safe(fb, 38, 24, 43, 18, C_TENT_SHADE)
  rect_safe(fb, 42, 19, 3, 5, C_SKY)
  rect_safe(fb, 43, 20, 1, 4, C_GLOW)

  line_safe(fb, 23, 24, 30, 21, C_LOG_DARK)
  line_safe(fb, 30, 24, 24, 21, C_LOG_DARK)
  line_safe(fb, 24, 24, 31, 22, C_LOG)
  line_safe(fb, 29, 24, 23, 22, C_LOG)
  rect_safe(fb, 23, 24, 1, 1, C_LOG_HI)
  rect_safe(fb, 30, 21, 1, 1, C_LOG_HI)
  rect_safe(fb, 30, 24, 1, 1, C_LOG_HI)
  rect_safe(fb, 24, 21, 1, 1, C_LOG_HI)
  rect_safe(fb, 24, 23, 6, 1, C_GLOW)
  rect_safe(fb, 23, 22, 8, 1, C_GLOW)
  rect_safe(fb, 25, 22 - flicker, 4, 3 + flicker, C_FIRE_A)
  rect_safe(fb, 26, 21 - flicker, 2, 4 + flicker, C_FIRE_B)
  rect_safe(fb, 27, 20 - flicker, 1, 3 + flicker, C_FIRE_C)
  rect_safe(fb, 24 + sway, 20, 1, 2, C_FIRE_B)
  rect_safe(fb, 29 - sway, 21, 1, 2, C_FIRE_A)
  draw_ember(fb, 26, 19, 8, -2, 1100, 0, C_FIRE_C, C_FIRE_B)
  draw_ember(fb, 27, 18, 10, 1, 1300, 420, C_FIRE_B, C_STAR)
  draw_ember(fb, 28, 20, 12, 2, 1600, 770, C_FIRE_A, C_FIRE_B)
  draw_ember(fb, 25, 18, 7, -1, 900, 260, C_FIRE_B, C_STAR)
end


-- __GLOBAL_BOOT_SPLASH_WRAPPER_V1__
local __boot_now_ms = now_ms or (sys and sys.now_ms) or function() return 0 end
local __boot_started_ms = 0
local __boot_ms = tonumber(data.get("campfire_scene.boot_splash_ms") or data.get("app.boot_splash_ms") or 1200) or 1200
if __boot_ms < 0 then __boot_ms = 0 end
local __boot_name = tostring(data.get("campfire_scene.app_name") or "Campfire Scene")

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
