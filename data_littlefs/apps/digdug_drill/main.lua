
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

local C_SKY = 0x03FF
local C_GRASS = 0x03E0
local C_SOIL1 = 0xF3A0
local C_SOIL2 = 0xE2C0
local C_SOIL3 = 0xA940
local C_TUNNEL = 0x0000
local C_HERO = 0x07FF
local C_SUIT = 0x001F
local C_HELM = 0xFFFF
local C_POOKA = 0xF800
local C_POOKA2 = 0xFD20
local C_FYGAR = 0x07E0
local C_EYE = 0xFFFF
local C_ROCK = 0x8410
local C_PUMP = 0xFFFF
local C_TUNNEL_EDGE = 0x6320

local function soil_dot(fb, y, c1, c2)
  for x = (y % 4), 63, 4 do
    set_px_safe(fb, x, y, c1)
    if x + 2 < 64 then set_px_safe(fb, x + 2, y, c2) end
  end
end

function app.render_fb(fb)
  rect_safe(fb, 0, 0, 64, 3, C_SKY)
  rect_safe(fb, 0, 3, 64, 2, C_GRASS)
  rect_safe(fb, 6, 1, 1, 2, 0xFFE0)
  rect_safe(fb, 7, 1, 1, 1, 0xFFE0)
  rect_safe(fb, 56, 1, 1, 2, 0xF81F)
  rect_safe(fb, 0, 5, 64, 9, C_SOIL1)
  rect_safe(fb, 0, 14, 64, 8, C_SOIL2)
  rect_safe(fb, 0, 22, 64, 10, C_SOIL3)
  for y = 6, 13 do soil_dot(fb, y, 0xF4C3, 0xE3A1) end
  for y = 14, 21 do soil_dot(fb, y, 0xEBC7, 0xD2E4) end
  for y = 22, 31 do soil_dot(fb, y, 0xC1A2, 0x98E0) end

  rect_safe(fb, 9, 6, 8, 19, C_TUNNEL_EDGE)
  rect_safe(fb, 10, 7, 6, 17, C_TUNNEL)
  rect_safe(fb, 9, 13, 41, 10, C_TUNNEL_EDGE)
  rect_safe(fb, 10, 14, 39, 8, C_TUNNEL)
  rect_safe(fb, 30, 14, 7, 7, C_TUNNEL_EDGE)
  rect_safe(fb, 31, 15, 5, 5, C_TUNNEL)

  local hx = 18 + math.floor(math.sin(state.anim_ms / 650) * 3)
  draw_sprite(fb, hx, 13, {
    "..hhh.",
    ".hwwh.",
    ".bbbb.",
    "bb..bb",
    "b.bb.b",
    ".b..b.",
    "..bb..",
  }, { h = C_HELM, b = C_SUIT, w = C_HERO }, 1)

  local inflate = 2 + math.floor(tri(700, 0) * 2)
  rect_safe(fb, 34 - inflate, 14 - inflate, 8 + inflate * 2, 8 + inflate * 2, C_POOKA)
  rect_safe(fb, 36 - inflate, 17 - inflate, 1, 1, C_EYE)
  rect_safe(fb, 41 + inflate - 1, 17 - inflate, 1, 1, C_EYE)
  rect_safe(fb, 37, 21 + inflate - 1, 5, 1, C_POOKA2)
  line_safe(fb, hx + 6, 17, 34 - inflate, 18, C_PUMP)

  draw_sprite(fb, 48, 23 + math.floor(tri(1100, 0) * 1), {
    ".ffff.",
    "ffwwff",
    ".ffff.",
    "..ff..",
  }, { f = C_FYGAR, w = C_EYE }, 1)
  rect_safe(fb, 53, 24, 3, 1, 0xFD20)
  rect_safe(fb, 56, 24, 3, 1, 0xFD20)
  if blink(260, 0.6, 60) then
    rect_safe(fb, 58, 23, 2, 1, 0xFFE0)
  end
  draw_sprite(fb, 24 + math.floor(tri(1800, 0) * 2), 18, {
    ".pp.",
    "pppp",
    ".pp.",
  }, { p = 0xF81F }, 1)

  rect_safe(fb, 48, 7 + math.floor(tri(1800, 0) * 3), 9, 6, C_ROCK)
  rect_safe(fb, 50, 9 + math.floor(tri(1800, 0) * 3), 2, 1, 0xBDF7)
  if blink(220, 0.5, 0) then
    rect_safe(fb, 34 - inflate, 18, 2, 1, 0xFFFF)
  end
  draw_number(fb, "40", 50, 1, 1, 0xFFFF, 0x0000, 1)
end

return app
