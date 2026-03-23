
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
local C_STAR = 0xBDF7
local C_SHIP = 0xFFFF
local C_THRUST = 0xFD20
local C_ROCK = 0xB596
local C_SHOT = 0x07FF
local C_UFO = 0xF81F
local function rock(fb, x, y, s)
  line_safe(fb, x + 1, y, x + s - 1, y + 1, C_ROCK)
  line_safe(fb, x + s - 1, y + 1, x + s, y + s - 2, C_ROCK)
  line_safe(fb, x + s, y + s - 2, x + s - 2, y + s, C_ROCK)
  line_safe(fb, x + s - 2, y + s, x, y + s - 1, C_ROCK)
  line_safe(fb, x, y + s - 1, x, y + 1, C_ROCK)
  if s >= 6 then
    line_safe(fb, x + 2, y + 2, x + s - 2, y + s - 3, C_ROCK)
  end
end
function app.render_fb(fb)
  fb:fill(C_BG)
  for i = 0, 13 do
    set_px_safe(fb, (i * 17 + math.floor(state.anim_ms / 28)) % 64, (i * 11 + math.floor(state.anim_ms / 41)) % 32, C_STAR)
  end
  draw_number(fb, '050', 3, 2, 1, 0xFFFF, 0x0000, 1)
  line_safe(fb, 28, 26, 32, 18, C_SHIP)
  line_safe(fb, 32, 18, 36, 26, C_SHIP)
  line_safe(fb, 30, 24, 34, 24, C_SHIP)
  line_safe(fb, 28, 26, 31, 24, C_SHIP)
  line_safe(fb, 36, 26, 33, 24, C_SHIP)
  if blink(180, 0.6, 0) then
    line_safe(fb, 31, 26, 32, 29, C_THRUST)
    line_safe(fb, 33, 26, 32, 29, C_THRUST)
  end
  rock(fb, 8 + math.floor(math.sin(state.anim_ms / 550) * 3), 7, 10)
  rock(fb, 44 - math.floor((state.anim_ms / 33) % 10), 8, 8)
  rock(fb, 48 - math.floor((state.anim_ms / 29) % 22), 21, 6)
  rect_safe(fb, 40, 4 + math.floor(math.sin(state.anim_ms / 500) * 2), 9, 3, C_UFO)
  rect_safe(fb, 42, 3 + math.floor(math.sin(state.anim_ms / 500) * 2), 5, 1, C_UFO)
  rect_safe(fb, 32, 14, 1, 5, C_SHOT)
  rect_safe(fb, 33, 10, 1, 2, C_SHOT)
  rect_safe(fb, 38, 18, 5, 1, C_SHOT)
end

return app
