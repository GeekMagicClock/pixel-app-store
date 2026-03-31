
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

local C_BG = 0x0000
local C_STAR = 0x8C71
local C_CITY = 0x07E0
local C_BASE = 0x07FF
local C_MISSILE = 0xF800
local C_DEF = 0xFFFF
local C_BOOM = 0xFFE0
local C_BOOM2 = 0xFD20
local C_HILL = 0x03A0
local function burst(fb, x, y, r)
  rect_safe(fb, x - r, y, r * 2 + 1, 1, C_BOOM)
  rect_safe(fb, x, y - r, 1, r * 2 + 1, C_BOOM)
  if r > 1 then
    rect_safe(fb, x - r + 1, y + 1, r * 2 - 1, 1, C_BOOM2)
    rect_safe(fb, x + 1, y - r + 1, 1, r * 2 - 1, C_BOOM2)
  end
end
function app.render_fb(fb)
  fb:fill(C_BG)
  for i = 0, 8 do
    set_px_safe(fb, (i * 19 + math.floor(state.anim_ms / 80)) % 64, (i * 7) % 12 + 1, C_STAR)
  end
  rect_safe(fb, 1, 2, 16, 1, 0x3186)
  rect_safe(fb, 19, 2, 8 + math.floor(tri(2200, 0) * 8), 1, 0x07FF)
  line_safe(fb, 0, 26, 12, 23, C_HILL)
  line_safe(fb, 12, 23, 24, 26, C_HILL)
  line_safe(fb, 24, 26, 38, 22, C_HILL)
  line_safe(fb, 38, 22, 52, 26, C_HILL)
  line_safe(fb, 52, 26, 64, 24, C_HILL)
  for i = 0, 5 do
    rect_safe(fb, 4 + i * 10, 27, 6, 3, C_CITY)
    rect_safe(fb, 5 + i * 10, 26, 1, 1, C_CITY)
    rect_safe(fb, 8 + i * 10, 26, 1, 1, C_CITY)
    if i == 1 or i == 4 then
      rect_safe(fb, 4 + i * 10, 28, 6, 1, 0xF800)
    end
  end
  draw_sprite(fb, 3, 22, {".bb.","bbbb","b..b"}, {b=C_BASE}, 1)
  draw_sprite(fb, 29, 22, {".bb.","bbbb","b..b"}, {b=C_BASE}, 1)
  draw_sprite(fb, 54, 22, {".bb.","bbbb","b..b"}, {b=C_BASE}, 1)
  line_safe(fb, 8, 0, 14 + math.floor((state.anim_ms / 36) % 8), 26, C_MISSILE)
  line_safe(fb, 30, 0, 27, 24 - math.floor((state.anim_ms / 46) % 6), C_MISSILE)
  line_safe(fb, 58, 0, 44, 24 - math.floor((state.anim_ms / 40) % 8), C_MISSILE)
  line_safe(fb, 45, 2, 36, 24 - math.floor((state.anim_ms / 38) % 7), C_MISSILE)
  line_safe(fb, 56, 24, 40, 8, C_DEF)
  line_safe(fb, 7, 24, 18, 12, C_DEF)
  line_safe(fb, 32, 24, 28, 10, C_DEF)
  line_safe(fb, 57, 24, 44, 14, C_DEF)
  burst(fb, 18, 12, 1 + math.floor(tri(900, 0) * 3))
  burst(fb, 28, 10, 1 + math.floor(tri(700, 200) * 2))
  burst(fb, 44, 14, 1 + math.floor(tri(1100, 120) * 3))
  burst(fb, 40, 8, 1 + math.floor(tri(800, 60) * 2))
  rect_safe(fb, 30, 21, 4, 1, 0x7BEF)
  draw_number(fb, "12", 53, 1, 1, 0xFFFF, 0x0000, 1)
end

return app
