
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
local C_FRAME = 0x7BEF
local C_PANEL = 0x1082
local C_GRID = 0x18C3
local C_WELL = 0x0008
local C_FLASH = 0xFFFF
local COLORS = {['1']=0x07FF,['2']=0xFFE0,['3']=0xC81F,['4']=0x07E0,['5']=0xF800,['6']=0xFD20,['7']=0xFFFF}
local BOARD = {
  '0000000000','0000000000','0000000000','0000000000','0000000000','0000000000',
  '0000660000','0000330000','0011330000','0011222000','0115222000','4455522000','4457772000','4457772000'
}
local PIECES = {
  {x=1,y=-1,color=0x07FF,cells={{0,0},{1,0},{2,0},{3,0}}},
  {x=4,y=-1,color=0xFFE0,cells={{0,0},{1,0},{0,1},{1,1}}},
  {x=3,y=-1,color=0xC81F,cells={{1,0},{0,1},{1,1},{2,1}}},
  {x=2,y=-1,color=0x07E0,cells={{1,0},{2,0},{0,1},{1,1}}},
  {x=5,y=-1,color=0xF800,cells={{0,0},{1,0},{1,1},{2,1}}},
  {x=6,y=-1,color=0xFD20,cells={{0,0},{0,1},{1,1},{2,1}}},
}
local function cell(fb, gx, gy, color)
  local x = 4 + gx * 2
  local y = 2 + gy * 2
  rect_safe(fb, x, y, 2, 2, color)
  rect_safe(fb, x, y, 1, 2, 0xFFFF)
  rect_safe(fb, x, y, 2, 1, 0xFFFF)
end
function app.render_fb(fb)
  fb:fill(C_BG)
  rect_safe(fb, 2, 0, 22, 30, C_FRAME)
  rect_safe(fb, 3, 1, 20, 28, C_WELL)
  for gy = 0, 13 do
    for gx = 0, 9 do
      rect_safe(fb, 4 + gx * 2, 2 + gy * 2, 1, 1, C_GRID)
      local ch = string.sub(BOARD[gy + 1], gx + 1, gx + 1)
      local c = COLORS[ch]
      if c then cell(fb, gx, gy, c) end
    end
  end
  local idx = math.floor(state.anim_ms / 2200) % #PIECES + 1
  local piece = PIECES[idx]
  local next_piece = PIECES[(idx % #PIECES) + 1]
  local drop = cyc(2200, 0)
  local py = math.floor(drop * 9)
  for _, p in ipairs(piece.cells) do cell(fb, piece.x + p[1], py + p[2] + 3, 0x39E7) end
  for _, p in ipairs(piece.cells) do cell(fb, piece.x + p[1], py + p[2], piece.color) end
  if drop > 0.84 then rect_safe(fb, 4, 24, 20, 2, C_FLASH) end
  rect_safe(fb, 26, 2, 14, 10, C_PANEL)
  rect_safe(fb, 27, 3, 12, 8, C_BG)
  for _, p in ipairs(next_piece.cells) do rect_safe(fb, 29 + p[1] * 2, 4 + p[2] * 2, 2, 2, next_piece.color) end
  draw_number(fb, '40', 44, 4, 1, 0xFFFF, 0x4208, 1)
  draw_number(fb, '15', 44, 12, 1, 0xFFE0, 0x4208, 1)
  rect_safe(fb, 42, 2, 20, 1, C_FRAME)
  rect_safe(fb, 42, 17, 20, 1, C_FRAME)
  draw_number(fb, '40', 44, 4, 1, 0xFFFF, 0x4208, 1)
  draw_number(fb, '15', 44, 12, 1, 0xFFE0, 0x4208, 1)
  rect_safe(fb, 26, 18, 16, 10, C_PANEL)
  rect_safe(fb, 27, 19, 14, 8, C_BG)
  for i = 0, 5 do
    local h = 2 + math.floor(6 * math.abs(math.sin(state.anim_ms / 700 + i * 0.6)))
    rect_safe(fb, 29 + i * 2, 26 - h, 1, h, (i % 2 == 0) and 0xFD20 or 0xF800)
  end
  draw_number(fb, '9999', 44, 22, 1, 0x07FF, 0x2104, 1)
end

return app
