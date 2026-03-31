
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
local C_INV1 = 0x07E0
local C_INV2 = 0xAFE5
local C_INV3 = 0xFD20
local C_CANNON = 0xFFFF
local C_SHIELD = 0x07FF
local C_LASER = 0xF800
local C_UFO = 0xF81F
local INV_A1 = {'.gg.','gggg','g.gg','g.gg'}
local INV_A2 = {'g..g','.gg.','gggg','g..g'}
local INV_B1 = {'y..y','.yy.','yyyy','y..y'}
local INV_B2 = {'.yy.','yyyy','y.yy','.yy.'}
local INV_C1 = {'.oo.','oooo','o.oo','oooo'}
local INV_C2 = {'o..o','oooo','.oo.','o..o'}
function app.render_fb(fb)
  fb:fill(C_BG)
  local ox = 7 + math.floor(tri(1800, 0) * 14)
  local frame = blink(420, 0.5, 0)
  draw_number(fb, '150', 2, 2, 1, 0xFFFF, 0x0000, 1)
  rect_safe(fb, 48, 2, 11, 1, 0x39E7)
  rect_safe(fb, 48, 2, 2 + math.floor(cyc(3200, 0) * 9), 1, 0x07E0)
  for c = 0, 5 do
    draw_sprite(fb, ox + c * 8, 5, frame and INV_A1 or INV_A2, {g=C_INV1}, 1)
  end
  for c = 0, 5 do
    draw_sprite(fb, ox + c * 8, 11, frame and INV_B1 or INV_B2, {y=C_INV2}, 1)
  end
  for c = 0, 5 do
    draw_sprite(fb, ox + c * 8, 17, frame and INV_C1 or INV_C2, {o=C_INV3}, 1)
  end
  draw_sprite(fb, 8 + math.floor(cyc(4000,0) * 48), 1, {'rrrrrr','.rrrr.'}, {r=C_UFO}, 1)
  if blink(360, 0.5, 0) then
    rect_safe(fb, 11 + math.floor(cyc(4000,0) * 48), 3, 2, 1, 0xFFFF)
  end
  rect_safe(fb, 8, 23, 10, 4, C_SHIELD)
  rect_safe(fb, 11, 25, 2, 2, 0x0000)
  rect_safe(fb, 27, 23, 10, 4, C_SHIELD)
  rect_safe(fb, 30, 25, 2, 2, 0x0000)
  rect_safe(fb, 46, 23, 10, 4, C_SHIELD)
  rect_safe(fb, 49, 25, 2, 2, 0x0000)
  draw_sprite(fb, 29, 27, {'.ww.','wwww','w..w'}, {w=C_CANNON}, 1)
  rect_safe(fb, 31, 14 + math.floor((state.anim_ms / 28) % 8), 1, 11, C_LASER)
  rect_safe(fb, ox + 19, 9 + math.floor((state.anim_ms / 36) % 10), 1, 9, C_LASER)
  rect_safe(fb, ox + 35, 15 + math.floor((state.anim_ms / 40) % 7), 1, 7, C_LASER)
  if blink(210, 0.5, 0) then
    rect_safe(fb, 30, 26, 3, 1, 0xFD20)
  end
end

return app
