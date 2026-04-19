local app = {}

local state = { anim_ms = 0 }

local C_SKY = 0xFC00
local C_SUN = 0xFFE0
local C_SUN_CORE = 0xFFF5
local C_SUN_BAND = 0xFD20
local C_DUNE_A = 0xFD20
local C_DUNE_B = 0xC220
local C_DUNE_HI = 0xFE28
local C_MESA = 0x7A20
local C_CACTUS = 0x03E0
local C_CACTUS_HI = 0xA7E5
local C_ROCK = 0x5100

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
  local shimmer = math.floor(triangle_wave(2200, 300) * 2)
  fb:fill(C_SKY)
  rect_safe(fb, 38, 4, 8, 1, C_SUN_BAND)
  rect_safe(fb, 36, 5, 12, 1, C_SUN)
  rect_safe(fb, 35, 6, 14, 2, C_SUN)
  rect_safe(fb, 34, 8, 16, 3, C_SUN)
  rect_safe(fb, 35, 11, 14, 2, C_SUN)
  rect_safe(fb, 36, 13, 12, 1, C_SUN)
  rect_safe(fb, 39, 6, 8, 7, C_SUN_CORE)
  rect_safe(fb, 34, 9, 16, 1, C_SUN_BAND)
  rect_safe(fb, 35, 11, 14, 1, C_SUN_BAND)

  rect_safe(fb, 0, 18, 11, 2, C_MESA)
  rect_safe(fb, 8, 16, 7, 3, C_MESA)
  rect_safe(fb, 13, 14, 6, 5, C_MESA)
  rect_safe(fb, 15, 18, 8, 2, C_MESA)
  rect_safe(fb, 20, 19, 5, 1, C_MESA)

  rect_safe(fb, 24, 16, 10, 2, C_DUNE_B)
  rect_safe(fb, 27, 14, 6, 2, C_DUNE_B)
  rect_safe(fb, 26, 18, 9, 1, C_DUNE_A)

  line_safe(fb, 0, 22, 12, 19, C_DUNE_A)
  line_safe(fb, 12, 19, 25, 21, C_DUNE_A)
  line_safe(fb, 25, 21, 41, 18, C_DUNE_A)
  line_safe(fb, 41, 18, 63, 22, C_DUNE_A)
  line_safe(fb, 0, 24, 18, 20, C_DUNE_HI)
  line_safe(fb, 18, 20, 34, 23, C_DUNE_HI)
  line_safe(fb, 12, 26, 33, 21, C_DUNE_B)
  line_safe(fb, 33, 21, 49, 24, C_DUNE_B)
  line_safe(fb, 49, 24, 63, 22, C_DUNE_B)
  rect_safe(fb, 0, 23, 64, 9, C_DUNE_B)
  rect_safe(fb, 0, 23, 64, 1, C_DUNE_HI)

  rect_safe(fb, 10, 15, 2, 11, C_CACTUS)
  rect_safe(fb, 8, 18, 2, 5, C_CACTUS)
  rect_safe(fb, 12, 20, 2, 5, C_CACTUS)
  rect_safe(fb, 11, 16, 1, 9, C_CACTUS_HI)
  rect_safe(fb, 9, 19, 1, 3, C_CACTUS_HI)
  rect_safe(fb, 13, 21, 1, 3, C_CACTUS_HI)

  rect_safe(fb, 49, 24, 4, 2, C_ROCK)
  rect_safe(fb, 52, 23, 2, 2, C_ROCK)
  rect_safe(fb, 46, 25, 2, 1, C_ROCK)
  rect_safe(fb, 30 + shimmer, 26, 3, 1, C_DUNE_HI)
  rect_safe(fb, 20 + shimmer, 28, 4, 1, C_DUNE_A)
end


-- __GLOBAL_BOOT_SPLASH_WRAPPER_V1__
local __boot_now_ms = now_ms or (sys and sys.now_ms) or function() return 0 end
local __boot_started_ms = 0
local __boot_ms = tonumber(data.get("desert_sun_scene.boot_splash_ms") or data.get("app.boot_splash_ms") or 1200) or 1200
if __boot_ms < 0 then __boot_ms = 0 end
local __boot_name = tostring(data.get("desert_sun_scene.app_name") or "Desert Sun Scene")

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
