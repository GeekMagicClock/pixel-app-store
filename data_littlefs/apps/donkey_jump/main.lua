
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
local C_GIRDER = 0xF800
local C_GIRDER_HI = 0xFD20
local C_LADDER = 0x07FF
local C_MARIO_HAT = 0xF800
local C_MARIO_BODY = 0x07FF
local C_MARIO_SKIN = 0xFD20
local C_DK = 0xA145
local C_DK_FACE = 0xFD20
local C_BARREL = 0xA145
local C_BARREL_RING = 0xFD20
local C_PAULINE = 0xF81F
local C_PAULINE_SKIN = 0xFFE0
local C_OIL = 0x07E0
local C_FLAME = 0xFFE0
local C_STEEL = 0x7BEF

local MARIO_PATH = {
  {x = 10, y = 24},
  {x = 18, y = 22},
  {x = 18, y = 18},
  {x = 26, y = 18},
  {x = 39, y = 15},
  {x = 39, y = 11},
  {x = 46, y = 8},
  {x = 52, y = 6},
}

local function draw_girder(fb, x0, y0, x1, y1)
  line_safe(fb, x0, y0, x1, y1, C_GIRDER)
  line_safe(fb, x0, y0 + 1, x1, y1 + 1, C_GIRDER)
  line_safe(fb, x0, y0 + 2, x1, y1 + 2, C_GIRDER_HI)
  for i = 0, 4 do
    local t = i / 4
    local bx = x0 + (x1 - x0) * t
    local by = y0 + (y1 - y0) * t
    rect_safe(fb, bx + 1, by + 1, 1, 1, 0xFFFF)
  end
end

local function draw_ladder(fb, x, y, h)
  rect_safe(fb, x, y, 1, h, C_LADDER)
  rect_safe(fb, x + 3, y, 1, h, C_LADDER)
  for r = 1, h - 2, 2 do
    rect_safe(fb, x + 1, y + r, 2, 1, 0xFFFF)
  end
end

local function draw_barrel(fb, x, y)
  draw_sprite(fb, x, y, {".oo.", "orro", "orro", ".oo."}, {o = C_BARREL, r = C_BARREL_RING}, 1)
end

local function draw_mario(fb, x, y)
  draw_sprite(
    fb,
    x,
    y,
    {".hh.", "hssh", ".bb.", "b..b"},
    {h = C_MARIO_HAT, s = C_MARIO_SKIN, b = C_MARIO_BODY},
    1
  )
end

function app.render_fb(fb)
  fb:fill(C_BG)
  draw_girder(fb, 5, 6, 58, 4)
  draw_girder(fb, 5, 13, 58, 15)
  draw_girder(fb, 5, 21, 58, 19)
  draw_girder(fb, 5, 27, 58, 29)
  draw_ladder(fb, 17, 6, 9)
  draw_ladder(fb, 39, 14, 8)
  draw_ladder(fb, 24, 19, 8)
  rect_safe(fb, 16, 15, 1, 3, C_STEEL)
  rect_safe(fb, 38, 23, 1, 3, C_STEEL)

  draw_sprite(fb, 6, 0, {"dddd..", "dffddd", "dddddd", "d....d"}, {d = C_DK, f = C_DK_FACE}, 1)
  draw_sprite(fb, 50, 0, {".pp.", "pssp", "pppp", ".pp."}, {p = C_PAULINE, s = C_PAULINE_SKIN}, 1)
  draw_sprite(fb, 4, 24, {"oooo", "oggo", "ofgo", "oooo"}, {o = C_OIL, g = 0x4208, f = C_FLAME}, 1)
  if blink(220, 0.55, 0) then
    rect_safe(fb, 7, 23, 1, 2, C_FLAME)
  end

  local step = math.floor(state.anim_ms / 440) % #MARIO_PATH + 1
  local mario = MARIO_PATH[step]
  draw_mario(fb, mario.x, mario.y)

  local b1x = 14 + math.floor((state.anim_ms / 28) % 34)
  local b2x = 15 + math.floor((state.anim_ms / 24 + 16) % 30)
  draw_barrel(fb, b1x, 7 + math.floor((b1x - 14) * 0.04))
  draw_barrel(fb, b2x, 22 - math.floor((b2x - 15) * 0.04))
  draw_barrel(fb, 33 + math.floor(tri(1600, 0) * 12), 14)

  rect_safe(fb, 45, 16, 2, 4, 0xFFE0)
  rect_safe(fb, 47, 16, 2, 2, 0xFD20)
  draw_number(fb, "25", 52, 24, 1, 0xFFFF, 0x4208, 1)
end

return app
