
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
local C_FLOOR = 0x18C3
local C_WALL = 0x001F
local C_WALL_HI = 0x7DFF
local C_BLOCK = 0xA145
local C_BLOCK_HI = 0xD6BA
local C_HERO = 0xFFFF
local C_HERO_BODY = 0x07FF
local C_ENEMY = 0xF81F
local C_ENEMY2 = 0xFD20
local C_SPARK = 0xFFE0
local C_BOOM = 0xFD20
local C_BOOM2 = 0xFFE0
local CELL = 4
local ORGX = 16
local ORGY = 4
local GRID = {'11111111','10101011','10000001','10101001','10000001','11111111'}
function app.render_fb(fb)
  fb:fill(C_BG)
  rect_safe(fb, 15, 2, 18, 1, 0x3186)
  rect_safe(fb, 35, 2, 12, 1, 0x39E7)
  for y = 0, 5 do
    for x = 0, 7 do
      local ch = string.sub(GRID[y + 1], x + 1, x + 1)
      local gx = ORGX + x * CELL
      local gy = ORGY + y * CELL
      if ch == '1' then
        rect_safe(fb, gx, gy, CELL - 1, CELL - 1, C_WALL)
        rect_safe(fb, gx, gy, CELL - 1, 1, C_WALL_HI)
        rect_safe(fb, gx, gy, 1, CELL - 1, C_WALL_HI)
      else
        rect_safe(fb, gx, gy, CELL - 1, CELL - 1, C_FLOOR)
        if ((x + y) % 2 == 0) then rect_safe(fb, gx + 1, gy + 1, 1, 1, 0x39E7) end
        if ((x + y) % 3 == 0) then
          rect_safe(fb, gx, gy, CELL - 1, CELL - 1, C_BLOCK)
          rect_safe(fb, gx, gy, CELL - 1, 1, C_BLOCK_HI)
          rect_safe(fb, gx + 1, gy + 1, 1, 1, C_BLOCK_HI)
        end
      end
    end
  end
  local hx = 1 + math.floor(cyc(4200, 0) * 5)
  local hy = ({1,1,2,2,3,3})[math.floor(cyc(4200, 0) * 6) + 1] or 3
  draw_sprite(fb, ORGX + hx * CELL, ORGY + hy * CELL, {
    ".ww.",
    "wbbw",
    "bbbb",
    "b..b",
  }, { w = C_HERO, b = C_HERO_BODY }, 1)
  draw_sprite(fb, ORGX + 6 * CELL, ORGY + 1 * CELL, {
    ".rr.",
    "rrrr",
    "roor",
    ".rr.",
  }, { r = C_ENEMY, o = C_ENEMY2 }, 1)
  draw_sprite(fb, ORGX + (5 - math.floor(cyc(3600, 0) * 4)) * CELL, ORGY + 4 * CELL, {
    ".rr.",
    "rrrr",
    "roor",
    ".rr.",
  }, { r = 0xF81F, o = 0xFFE0 }, 1)
  local phase = state.anim_ms % 1800
  local bx = ORGX + 3 * CELL + 1
  local by = ORGY + 2 * CELL + 1
  if phase < 1100 then
    draw_sprite(fb, bx - 1, by - 1, {'.ss.','sbbs','.ss.'}, {s=C_SPARK,b=0x0000}, 1)
    rect_safe(fb, bx, by, 2, 2, 0x0000)
  else
    rect_safe(fb, bx - 7, by, 16, 2, C_BOOM)
    rect_safe(fb, bx, by - 7, 2, 16, C_BOOM)
    rect_safe(fb, bx - 5, by, 12, 1, C_BOOM2)
    rect_safe(fb, bx, by - 5, 1, 12, C_BOOM2)
    rect_safe(fb, bx - 4, by - 4, 8, 8, 0xF800)
  end
  if blink(260, 0.4, 120) then
    rect_safe(fb, ORGX + 4 * CELL + 1, ORGY + 4 * CELL + 1, 2, 2, C_SPARK)
  end
  rect_safe(fb, ORGX + 7, ORGY + 1, 1, 22, 0x2104)
  draw_number(fb, "99", 47, 3, 1, 0xFFFF, 0x0000, 1)
end

return app
