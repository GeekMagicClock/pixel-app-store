
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

local C_SKY = 0x45BF
local C_CLOUD = 0xFFFF
local C_TREE = 0x03A0
local C_GRASS = 0x03E0
local C_REED = 0x0320
local C_SUN = 0xFFE0
local C_HEAD = 0x05E0
local C_BODY = 0xA145
local C_WING = 0xFFFF
local C_BEAK = 0xFD20
local C_CROSS = 0xF800
local C_DOG = 0xA145
local C_DOG_FUR = 0xD6BA

local function duck(fb, x, y, flap)
  draw_sprite(fb, x, y, {
    "..hh...",
    ".hwwb..",
    "hbbbbbb",
    ".bwwbb.",
  }, { h = C_HEAD, w = C_WING, b = C_BODY }, 1)
  rect_safe(fb, x + 5, y + 1, 2, 1, C_BEAK)
  if flap then
    rect_safe(fb, x + 2, y - 1, 2, 1, C_WING)
  else
    rect_safe(fb, x + 1, y + 3, 3, 1, C_WING)
  end
end

local function dog(fb, x, y)
  draw_sprite(fb, x, y, {
    "..ff....",
    ".fddf...",
    "fddddf..",
    "fwwddff.",
    ".fdddff.",
    "..f.f...",
  }, { d = C_DOG, f = C_DOG_FUR, w = 0x0000 }, 1)
end
function app.render_fb(fb)
  rect_safe(fb, 0, 0, 64, 18, C_SKY)
  rect_safe(fb, 0, 18, 64, 4, C_TREE)
  rect_safe(fb, 0, 22, 64, 10, C_GRASS)
  draw_sprite(fb, 4, 3, {".ccc..","cccccc",".cccc."}, { c = C_CLOUD }, 1)
  draw_sprite(fb, 43, 2, {".ccc..","cccccc",".cccc."}, { c = C_CLOUD }, 1)
  rect_safe(fb, 54, 4, 5, 5, C_SUN)
  for i = 0, 7 do
    rect_safe(fb, 2 + i * 8, 23, 2, 7, C_REED)
    rect_safe(fb, 4 + i * 8, 24, 1, 6, C_REED)
  end

  local x1 = 6 + cyc(3400, 0) * 42
  local y1 = 7 + math.sin(state.anim_ms / 260) * 3
  local x2 = 56 - cyc(3000, 0) * 34
  local y2 = 10 + math.sin(state.anim_ms / 220 + 1.4) * 4
  duck(fb, math.floor(x1), math.floor(y1), blink(220, 0.5, 0))
  duck(fb, math.floor(x2), math.floor(y2), blink(180, 0.5, 80))
  dog(fb, 6, 23)

  local cx = x1 + (x2 - x1) * 0.35
  local cy = y1 + (y2 - y1) * 0.35
  rect_safe(fb, math.floor(cx) - 3, math.floor(cy), 7, 1, C_CROSS)
  rect_safe(fb, math.floor(cx), math.floor(cy) - 3, 1, 7, C_CROSS)
  draw_number(fb, "10", 50, 1, 1, 0xFFFF, 0x0000, 1)
end

return app
