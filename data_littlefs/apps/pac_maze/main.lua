
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

local C_BG = 0x0006
local C_WALL = 0x041F
local C_WALL_HI = 0x7DFF
local C_PELLET = 0xFFFF
local C_PAC = 0xFFE0
local C_G1 = 0xF81F
local C_G2 = 0x07FF
local C_G3 = 0xFD20
local C_EYE = 0xFFFF
local C_TEXT = 0xFFFF
local PAC_R = {'..yyyy.','.yyyyyy','yyyyy..','.yyyyyy','..yyyy.'}
local PAC_L = {'.yyyy..','yyyyyy.','..yyyyy','yyyyyy.','.yyyy..'}
local PAC_D = {'..yy...','.yyyy..','yyyyyy.','yyyyyy.','.yyyy..'}
local PAC_U = {'.yyyy..','yyyyyy.','yyyyyy.','.yyyy..','..yy...'}
local GHOST = {'.cccc.','cccccc','cwwwwc','cccccc','c.c.c.'}
local function pac_xy(p)
  if p < 0.25 then return 7 + p / 0.25 * 46, 7 end
  if p < 0.5 then return 53, 7 + (p - 0.25) / 0.25 * 16 end
  if p < 0.75 then return 53 - (p - 0.5) / 0.25 * 46, 23 end
  return 7, 23 - (p - 0.75) / 0.25 * 16
end
function app.render_fb(fb)
  fb:fill(C_BG)
  rect_safe(fb, 2, 2, 60, 28, C_WALL)
  rect_safe(fb, 4, 4, 56, 24, C_BG)
  rect_safe(fb, 2, 2, 60, 1, C_WALL_HI)
  rect_safe(fb, 10, 8, 13, 2, C_WALL)
  rect_safe(fb, 41, 8, 13, 2, C_WALL)
  rect_safe(fb, 10, 20, 13, 2, C_WALL)
  rect_safe(fb, 41, 20, 13, 2, C_WALL)
  rect_safe(fb, 28, 8, 8, 14, C_WALL)
  rect_safe(fb, 4, 14, 12, 2, C_WALL)
  rect_safe(fb, 48, 14, 12, 2, C_WALL)
  rect_safe(fb, 0, 14, 4, 2, C_BG)
  rect_safe(fb, 60, 14, 4, 2, C_BG)
  for i = 0, 11 do rect_safe(fb, 8 + i * 4, 12, 1, 1, C_PELLET) end
  for i = 0, 11 do if i ~= 5 and i ~= 6 then rect_safe(fb, 8 + i * 4, 18, 1, 1, C_PELLET) end end
  rect_safe(fb, 8, 8, 2, 2, C_PELLET)
  rect_safe(fb, 54, 8, 2, 2, C_PELLET)
  rect_safe(fb, 8, 22, 2, 2, C_PELLET)
  rect_safe(fb, 54, 22, 2, 2, C_PELLET)
  for i = 0, 5 do
    local x = 12 + i * 8
    if ((i + math.floor(state.anim_ms / 300)) % 2) == 0 then
      rect_safe(fb, x, 6, 1, 1, C_PELLET)
      rect_safe(fb, x, 26, 1, 1, C_PELLET)
    end
  end
  rect_safe(fb, 30, 15, 4, 2, 0xF800)
  local p = cyc(5200, 0)
  local px, py = pac_xy(p)
  local pac
  if p < 0.25 then
    pac = blink(260, 0.5, 0) and PAC_R or {'..yyyy.','.yyyyyy','yyyyyyy','.yyyyyy','..yyyy.'}
  elseif p < 0.5 then
    pac = blink(260, 0.5, 0) and PAC_D or {'..yyyy.','.yyyyyy','yyyyyyy','.yyyyyy','..yyyy.'}
  elseif p < 0.75 then
    pac = blink(260, 0.5, 0) and PAC_L or {'..yyyy.','.yyyyyy','yyyyyyy','.yyyyyy','..yyyy.'}
  else
    pac = blink(260, 0.5, 0) and PAC_U or {'..yyyy.','.yyyyyy','yyyyyyy','.yyyyyy','..yyyy.'}
  end
  draw_sprite(fb, px, py, pac, {y=C_PAC}, 1)
  draw_sprite(fb, select(1, pac_xy((p + 0.62) % 1.0)), select(2, pac_xy((p + 0.62) % 1.0)), GHOST, {c=C_G1,w=C_EYE}, 1)
  draw_sprite(fb, select(1, pac_xy((p + 0.37) % 1.0)), select(2, pac_xy((p + 0.37) % 1.0)), GHOST, {c=C_G2,w=C_EYE}, 1)
  draw_sprite(fb, 28, 13 + math.floor(tri(900, 0) * 3), GHOST, {c=C_G3,w=C_EYE}, 1)
  if blink(280, 0.5, 0) then
    rect_safe(fb, px + 6, py + 2, 1, 1, C_PELLET)
  end
  if blink(640, 0.5, 200) then
    rect_safe(fb, 29, 14, 6, 4, 0x0010)
  end
  draw_number(fb, "240", 3, 1, 1, C_TEXT, 0x0000, 1)
end

return app
