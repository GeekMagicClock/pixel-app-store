
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
local C_LINE = 0x7BEF
local C_SCAN = 0x0841
local C_PAD = 0xFFFF
local C_BALL = 0xFFE0
local C_GLOW = 0x07FF
function app.render_fb(fb)
  fb:fill(C_BG)
  for y = 0, 31, 2 do
    if y % 4 == 0 then rect_safe(fb, 0, y, 64, 1, C_SCAN) end
  end
  draw_number(fb, '1', 20, 3, 1, 0xFFFF, 0x4208, 1)
  draw_number(fb, '7', 40, 3, 1, 0xFFFF, 0x4208, 1)
  for y = 7, 28, 4 do rect_safe(fb, 31, y, 2, 2, C_LINE) end
  rect_safe(fb, 0, 0, 64, 1, C_LINE)
  rect_safe(fb, 0, 31, 64, 1, C_LINE)
  local t = state.anim_ms / 1000
  local bx = 32 + math.sin(t * 1.7) * 23
  local by = 17 + math.sin(t * 2.9) * 9
  local ly = clamp(by - 4 + math.sin(t * 1.3) * 1.2, 6, 23)
  local ry = clamp(by - 4 - math.sin(t * 1.6) * 1.2, 6, 23)
  rect_safe(fb, 4, ly, 3, 8, C_PAD)
  rect_safe(fb, 5, ly + 1, 1, 6, 0x7BEF)
  rect_safe(fb, 57, ry, 3, 8, C_PAD)
  rect_safe(fb, 58, ry + 1, 1, 6, 0x7BEF)
  rect_safe(fb, bx - 5, by, 2, 1, 0x39E7)
  rect_safe(fb, bx - 2, by, 2, 1, C_GLOW)
  rect_safe(fb, bx, by, 2, 2, C_BALL)
  rect_safe(fb, bx + 2, by + 1, 1, 1, 0xFFFF)
end

return app
