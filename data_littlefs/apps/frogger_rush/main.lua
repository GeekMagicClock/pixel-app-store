
local app = {}
local state = { anim_ms = 0 }

local DIGITS = {
  ["0"] = {"111","101","101","101","111"},
  ["1"] = {"010","110","010","010","111"},
  ["2"] = {"111","001","111","100","111"},
  ["3"] = {"111","001","111","001","111"},
  ["4"] = {"101","101","111","001","001"},
  ["5"] = {"111","100","111","001","111"},
  ["6"] = {"111","100","111","101","111"},
  ["7"] = {"111","001","001","010","010"},
  ["8"] = {"111","101","111","101","111"},
  ["9"] = {"111","101","111","001","111"},
}

local function clamp(v, lo, hi)
  if v < lo then return lo end
  if v > hi then return hi end
  return v
end

local function set_px_safe(fb, x, y, c)
  x = math.floor(x + 0.5)
  y = math.floor(y + 0.5)
  if x < 0 or x >= 64 or y < 0 or y >= 32 then return end
  fb:set_px(x, y, c)
end

local function rect_safe(fb, x, y, w, h, c)
  if w <= 0 or h <= 0 then return end
  fb:rect(math.floor(x + 0.5), math.floor(y + 0.5), math.floor(w + 0.5), math.floor(h + 0.5), c)
end

local function line_safe(fb, x0, y0, x1, y1, c)
  x0 = math.floor(x0 + 0.5)
  y0 = math.floor(y0 + 0.5)
  x1 = math.floor(x1 + 0.5)
  y1 = math.floor(y1 + 0.5)
  local dx = math.abs(x1 - x0)
  local sx = x0 < x1 and 1 or -1
  local dy = -math.abs(y1 - y0)
  local sy = y0 < y1 and 1 or -1
  local err = dx + dy
  while true do
    set_px_safe(fb, x0, y0, c)
    if x0 == x1 and y0 == y1 then break end
    local e2 = err * 2
    if e2 >= dy then err = err + dy; x0 = x0 + sx end
    if e2 <= dx then err = err + dx; y0 = y0 + sy end
  end
end

local function tri(period_ms, phase_ms)
  local period = period_ms or 1000
  local t = (state.anim_ms + (phase_ms or 0)) % period
  local half = period / 2
  if t < half then return t / half end
  return 1 - ((t - half) / half)
end

local function cyc(period_ms, phase_ms)
  local period = period_ms or 1000
  return ((state.anim_ms + (phase_ms or 0)) % period) / period
end

local function blink(period_ms, duty, phase_ms)
  return cyc(period_ms, phase_ms) < (duty or 0.5)
end

local function draw_sprite(fb, x, y, sprite, palette, scale)
  scale = scale or 1
  x = math.floor(x + 0.5)
  y = math.floor(y + 0.5)
  for row = 1, #sprite do
    local line = sprite[row]
    for col = 1, #line do
      local key = string.sub(line, col, col)
      local c = palette[key]
      if c then
        rect_safe(fb, x + (col - 1) * scale, y + (row - 1) * scale, scale, scale, c)
      end
    end
  end
end

local function draw_digit(fb, ch, x, y, s, c, shadow)
  local pat = DIGITS[ch]
  if not pat then return end
  for row = 1, #pat do
    local line = pat[row]
    for col = 1, #line do
      if string.sub(line, col, col) == '1' then
        if shadow then rect_safe(fb, x + (col - 1) * s + 1, y + (row - 1) * s + 1, s, s, shadow) end
        rect_safe(fb, x + (col - 1) * s, y + (row - 1) * s, s, s, c)
      end
    end
  end
end

local function draw_number(fb, text, x, y, s, c, shadow, gap)
  gap = gap or s
  for i = 1, #text do
    draw_digit(fb, string.sub(text, i, i), x + (i - 1) * (3 * s + gap), y, s, c, shadow)
  end
end

function app.init()
  state.anim_ms = 0
end

function app.tick(dt_ms)
  state.anim_ms = (state.anim_ms + (dt_ms or 0)) % 600000
end

local C_WATER = 0x0190
local C_WATER2 = 0x02B6
local C_LOG = 0xA145
local C_LOG2 = 0x8241
local C_TURTLE = 0x03EF
local C_TURTLE2 = 0x0010
local C_GRASS = 0x0320
local C_GRASS2 = 0x03E0
local C_ROAD = 0x18C3
local C_LINE = 0xFFE0
local C_CAR1 = 0xF800
local C_CAR2 = 0x07FF
local C_CAR3 = 0xFD20
local C_TRUCK = 0xF81F
local C_FROG = 0x07E0
local C_FROG2 = 0x03A0
local C_HOME = 0xFFE0
local C_CURB = 0xFFFF
local C_ROAD_EDGE = 0x4208
local PATH = {{30,28},{30,24},{12,24},{12,20},{42,20},{42,16},{18,16},{18,10},{34,10},{34,4}}
local function interp_path(frames, t)
  local n = #frames
  local pos = t * (n - 1)
  local idx = math.floor(pos) + 1
  if idx >= n then return frames[n][1], frames[n][2] end
  local frac = pos - math.floor(pos)
  local a, b = frames[idx], frames[idx + 1]
  return a[1] + (b[1] - a[1]) * frac, a[2] + (b[2] - a[2]) * frac
end

local function draw_car(fb, x, y, body, accent, w)
  w = w or 8
  rect_safe(fb, x, y + 1, w, 3, body)
  rect_safe(fb, x + 1, y, w - 2, 2, accent)
  rect_safe(fb, x + 1, y + 1, 2, 1, 0xFFFF)
  rect_safe(fb, x + w - 3, y + 1, 2, 1, 0xFFFF)
  rect_safe(fb, x + 1, y + 4, 2, 1, 0x0000)
  rect_safe(fb, x + w - 3, y + 4, 2, 1, 0x0000)
end

function app.render_fb(fb)
  rect_safe(fb, 0, 0, 64, 12, C_WATER)
  for y = 1, 11, 3 do
    line_safe(fb, 0, y, 63, y, C_WATER2)
  end
  for x = 0, 63, 9 do
    if ((x + math.floor(state.anim_ms / 45)) % 18) < 9 then
      rect_safe(fb, x, 11, 3, 1, 0x03EF)
    end
  end
  rect_safe(fb, 0, 12, 64, 4, C_GRASS)
  rect_safe(fb, 0, 16, 64, 12, C_ROAD)
  rect_safe(fb, 0, 28, 64, 4, C_GRASS2)
  rect_safe(fb, 0, 15, 64, 1, C_CURB)
  rect_safe(fb, 0, 16, 64, 1, C_ROAD_EDGE)
  for i = 0, 4 do
    rect_safe(fb, 4 + i * 12, 1, 8, 3, C_HOME)
    rect_safe(fb, 6 + i * 12, 2, 4, 1, 0x0000)
    if blink(420, 0.45, i * 90) then
      rect_safe(fb, 5 + i * 12, 4, 6, 1, 0x07E0)
    end
  end
  for i = 0, 2 do
    local x = (math.floor(state.anim_ms / 35) + i * 22) % 86 - 18
    rect_safe(fb, x, 4 + i * 3, 18, 3, C_LOG)
    rect_safe(fb, x + 1, 4 + i * 3, 16, 1, C_LOG2)
    rect_safe(fb, x + 4, 4 + i * 3 + 1, 1, 2, C_LOG2)
    rect_safe(fb, x + 11, 4 + i * 3 + 1, 1, 2, C_LOG2)
  end
  for i = 0, 2 do
    local x = 8 + ((math.floor(state.anim_ms / 42) + i * 18) % 72)
    rect_safe(fb, x, 10, 5, 2, C_TURTLE)
    rect_safe(fb, x + 1, 9, 3, 1, C_TURTLE2)
    rect_safe(fb, x + 1, 10, 1, 1, 0xFFFF)
    rect_safe(fb, x + 3, 10, 1, 1, 0xFFFF)
  end
  for y = 18, 26, 4 do
    for x = 0, 63, 8 do rect_safe(fb, x + 2, y, 4, 1, C_LINE) end
  end
  rect_safe(fb, 0, 27, 64, 1, C_CURB)
  draw_car(fb, (56 - math.floor(state.anim_ms / 28)) % 84 - 14, 16, C_CAR1, 0xFD20, 12)
  draw_car(fb, (math.floor(state.anim_ms / 31) + 10) % 84 - 12, 20, C_CAR2, 0x001F, 10)
  draw_car(fb, (60 - math.floor(state.anim_ms / 24)) % 88 - 16, 24, C_CAR3, 0xFFE0, 12)
  draw_car(fb, (math.floor(state.anim_ms / 27) + 28) % 92 - 18, 24, C_TRUCK, 0xFBE0, 16)
  local fx, fy = interp_path(PATH, cyc(5200, 0))
  draw_sprite(fb, fx, fy, {
    ".gg.",
    "gggg",
    "g..g",
    ".gg.",
  }, { g = C_FROG }, 1)
  rect_safe(fb, fx + 1, fy + 1, 2, 1, C_FROG2)
  rect_safe(fb, fx + 1, fy, 1, 1, 0xFFFF)
  rect_safe(fb, fx + 2, fy, 1, 1, 0xFFFF)
  rect_safe(fb, fx + 1, fy + 3, 1, 1, C_FROG2)
  rect_safe(fb, fx + 2, fy + 3, 1, 1, C_FROG2)
  if blink(240, 0.5, 0) then
    rect_safe(fb, fx - 1, fy + 1, 1, 1, C_FROG)
    rect_safe(fb, fx + 4, fy + 1, 1, 1, C_FROG)
  end
  draw_number(fb, "03", 53, 29, 1, 0xFFFF, 0x0000, 1)
end


-- __GLOBAL_BOOT_SPLASH_WRAPPER_V1__
local __boot_now_ms = now_ms or (sys and sys.now_ms) or function() return 0 end
local __boot_started_ms = 0
local __boot_ms = tonumber(data.get("frogger_rush.boot_splash_ms") or data.get("app.boot_splash_ms") or 5000) or 5000
if __boot_ms < 0 then __boot_ms = 0 end
local __boot_name = tostring(data.get("frogger_rush.app_name") or "Frogger Rush")

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
