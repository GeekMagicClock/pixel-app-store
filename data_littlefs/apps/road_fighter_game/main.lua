
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

local C_GRASS_D = 0x0200
local C_GRASS_L = 0x03E0
local C_SHOULDER = 0x8A22
local C_ROAD = 0x4208
local C_EDGE = 0xFFFF
local C_LINE = 0xFFE0
local C_RUMBLE1 = 0xF800
local C_RUMBLE2 = 0xFFFF
local C_PLAYER = 0xF800
local C_PLAYER_HL = 0xFD20
local C_GLASS = 0x7DFF
local C_TIRE = 0x0000
local C_CAR1 = 0x07E0
local C_CAR2 = 0x001F
local C_CAR3 = 0xFFE0
local C_HUD = 0xFFFF
local C_BAR = 0xFD20

local function road_bounds(y)
  local inset = math.floor((31 - y) * 0.18)
  return 15 + inset, 48 - inset
end

local function lane_x(y, t)
  local left, right = road_bounds(y)
  return math.floor(left + (right - left) * t) - 3
end

local function draw_car(fb, x, y, body, accent)
  draw_sprite(fb, x, y, {
    "..tt..",
    ".bbbb.",
    "bbbbbb",
    "bwbbwb",
    "baaaab",
    "b....b",
    ".tttt.",
    "..tt..",
  }, { b = body, w = C_GLASS, t = C_TIRE, a = accent }, 1)
end

function app.render_fb(fb)
  for y = 0, 31 do
    local grass = y < 20 and C_GRASS_D or C_GRASS_L
    line_safe(fb, 0, y, 63, y, grass)
    local left, right = road_bounds(y)
    line_safe(fb, left - 2, y, left - 1, y, C_SHOULDER)
    line_safe(fb, right + 1, y, right + 2, y, C_SHOULDER)
    line_safe(fb, left, y, right, y, C_ROAD)
    set_px_safe(fb, left, y, C_EDGE)
    set_px_safe(fb, right, y, C_EDGE)
    if ((y + math.floor(state.anim_ms / 35)) % 6) < 3 then
      set_px_safe(fb, left - 3, y, C_RUMBLE1)
      set_px_safe(fb, right + 3, y, C_RUMBLE2)
    end
    if ((y + math.floor(state.anim_ms / 45)) % 10) < 5 then
      local mid = math.floor((left + right) * 0.5)
      line_safe(fb, mid - 1, y, mid + 1, y, C_LINE)
    end
  end
  for i = 0, 3 do
    local mx = 5 + i * 16
    rect_safe(fb, mx, 4 + (i % 2), 2, 4, 0x4208)
    rect_safe(fb, mx, 3 + (i % 2), 2, 1, 0xBDF7)
  end

  for y = 2, 28, 6 do
    local left, right = road_bounds(y + 10)
    rect_safe(fb, left - 6, y + ((math.floor(state.anim_ms / 60) + y) % 5), 2, 2, 0xFD20)
    rect_safe(fb, right + 4, y + ((math.floor(state.anim_ms / 60) + y + 2) % 5), 2, 2, 0xFFFF)
  end

  local sway = math.sin(state.anim_ms / 650) * 0.18
  draw_car(fb, lane_x(24, 0.50 + sway), 22, C_PLAYER, C_PLAYER_HL)
  draw_car(fb, lane_x(math.floor((state.anim_ms / 34) % 42) - 8, 0.28), math.floor((state.anim_ms / 34) % 42) - 8, C_CAR1, 0x03EF)
  draw_car(fb, lane_x(math.floor((state.anim_ms / 30 + 12) % 46) - 9, 0.73), math.floor((state.anim_ms / 30 + 12) % 46) - 9, C_CAR2, 0x781F)
  draw_car(fb, lane_x(math.floor((state.anim_ms / 27 + 26) % 48) - 9, 0.50), math.floor((state.anim_ms / 27 + 26) % 48) - 9, C_CAR3, 0xFBE0)
  if blink(260, 0.5, 0) then
    rect_safe(fb, lane_x(24, 0.50 + sway) + 1, 20, 1, 2, 0xFD20)
    rect_safe(fb, lane_x(24, 0.50 + sway) + 4, 20, 1, 2, 0xFD20)
  end

  rect_safe(fb, 3, 3, 11, 5, 0x0000)
  draw_number(fb, string.format("%03d", 180 + math.floor(cyc(5000, 0) * 60)), 4, 4, 1, C_HUD, 0x0000, 1)
  rect_safe(fb, 49, 3, 11, 4, 0x0000)
  rect_safe(fb, 50, 4, 9, 2, 0x4208)
  rect_safe(fb, 50, 4, 1 + math.floor(cyc(6200, 0) * 8), 2, C_BAR)
  rect_safe(fb, 48, 8, 12, 1, 0x39E7)
  rect_safe(fb, 2, 28, 8, 2, 0x0000)
  draw_number(fb, "12", 3, 28, 1, 0xFFFF, 0x0000, 1)
end


-- __GLOBAL_BOOT_SPLASH_WRAPPER_V1__
local __boot_now_ms = now_ms or (sys and sys.now_ms) or function() return 0 end
local __boot_started_ms = 0
local __boot_ms = tonumber(data.get("road_fighter_game.boot_splash_ms") or data.get("app.boot_splash_ms") or 5000) or 5000
if __boot_ms < 0 then __boot_ms = 0 end
local __boot_name = tostring(data.get("road_fighter_game.app_name") or "Road Fighter")

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
