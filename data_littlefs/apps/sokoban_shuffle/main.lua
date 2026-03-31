
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

local C_BG = 0x2104
local C_WALL = 0x001F
local C_WALL_HI = 0x7DFF
local C_WALL_DK = 0x000C
local C_FLOOR = 0x9D13
local C_TARGET = 0xFFE0
local C_TARGET2 = 0xFD20
local C_BOX = 0xA145
local C_BOX_HI = 0xD6BA
local C_BOX_EDGE = 0x6145
local C_PLAYER = 0xFFFF
local C_PLAYER_BODY = 0x07FF
local C_SHADOW = 0x4208
local CELL = 4
local ORGX = 14
local ORGY = 4
local WALLS = {'11111111','10000011','10000001','10000001','10000001','11111111'}
local FRAMES = {
  {p={1,1}, b={{3,2},{4,3}}}, {p={2,1}, b={{3,2},{4,3}}}, {p={3,1}, b={{4,2},{4,3}}},
  {p={4,1}, b={{5,2},{4,3}}}, {p={4,2}, b={{5,2},{4,3}}}, {p={4,3}, b={{5,2},{4,4}}}, {p={3,3}, b={{5,2},{4,4}}},
}
local TARGETS = {{5,2},{4,4}}
function app.render_fb(fb)
  fb:fill(C_BG)
  local frame = FRAMES[math.floor(state.anim_ms / 500) % #FRAMES + 1]
  rect_safe(fb, 13, 3, 33, 1, 0x4208)
  for y = 0, 5 do
    for x = 0, 7 do
      local wx = ORGX + x * CELL
      local wy = ORGY + y * CELL
      local ch = string.sub(WALLS[y + 1], x + 1, x + 1)
      if ch == '1' then
        rect_safe(fb, wx, wy, CELL - 1, CELL - 1, C_WALL)
        rect_safe(fb, wx, wy, CELL - 1, 1, C_WALL_HI)
        rect_safe(fb, wx, wy, 1, CELL - 1, C_WALL_HI)
        rect_safe(fb, wx + CELL - 2, wy + 1, 1, CELL - 2, C_WALL_DK)
      else
        rect_safe(fb, wx, wy, CELL - 1, CELL - 1, C_FLOOR)
        if ((x + y) % 2 == 0) then rect_safe(fb, wx + 1, wy + 1, 1, 1, C_SHADOW) end
        if ((x + y + math.floor(state.anim_ms / 400)) % 5) == 0 then
          set_px_safe(fb, wx + 2, wy + 2, 0xFFFF)
        end
      end
    end
  end
  for _, t in ipairs(TARGETS) do
    local tx = ORGX + t[1] * CELL
    local ty = ORGY + t[2] * CELL
    draw_sprite(fb, tx, ty, {'.yy.','yyyy','.yy.'}, {y=C_TARGET}, 1)
    rect_safe(fb, tx + 1, ty + 1, 1, 1, C_TARGET2)
  end
  for _, b in ipairs(frame.b) do
    local bx = ORGX + b[1] * CELL
    local by = ORGY + b[2] * CELL
    rect_safe(fb, bx, by, CELL - 1, CELL - 1, C_BOX)
    rect_safe(fb, bx, by, CELL - 1, 1, C_BOX_HI)
    rect_safe(fb, bx, by, 1, CELL - 1, C_BOX_HI)
    rect_safe(fb, bx + CELL - 2, by + 1, 1, CELL - 2, C_BOX_EDGE)
    rect_safe(fb, bx + 1, by + 2, CELL - 3, 1, C_BOX_EDGE)
    rect_safe(fb, bx + 1, by + 1, 1, 1, C_BOX_HI)
  end
  draw_sprite(fb, ORGX + frame.p[1] * CELL, ORGY + frame.p[2] * CELL, {
    ".ww.",
    "wbbw",
    ".bb.",
    "b..b",
  }, { w = C_PLAYER, b = C_PLAYER_BODY }, 1)
  if blink(320, 0.5, 0) then
    rect_safe(fb, ORGX + frame.p[1] * CELL + 1, ORGY + frame.p[2] * CELL + 4, 2, 1, C_SHADOW)
  end
  rect_safe(fb, 47, 9, 11, 1, 0x4208)
  rect_safe(fb, 47, 9, 2 + math.floor(cyc(2200, 0) * 8), 1, C_TARGET)
  draw_number(fb, "12", 47, 3, 1, 0xFFFF, 0x0000, 1)
end

return app
