
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
local C_STAR = 0x9D75
local C_SHIP = 0xFFFF
local C_SHIP_ACCENT = 0x07FF
local C_BEE = 0xFFE0
local C_BUG = 0xF81F
local C_BOSS = 0x07FF
local C_SHOT = 0xF800
local C_SCORE = 0x7BEF
local BEE = {".yy.", "yyyy", "y.yy", ".yy."}
local BUG = {"r..r", ".rr.", "rrrr", "r..r"}
local BOSS = {".bb.", "bbbb", "b.bb", "bbbb"}
local BOSS2 = {"bbbb", "b..b", "bbbb", ".bb."}

function app.render_fb(fb)
  fb:fill(C_BG)
  for i = 0, 11 do
    set_px_safe(fb, (i * 13 + math.floor(state.anim_ms / 30)) % 64, (i * 7 + math.floor(state.anim_ms / 53)) % 32, C_STAR)
  end
  for i = 0, 5 do
    local sx = (i * 17 + math.floor(state.anim_ms / 24)) % 64
    set_px_safe(fb, sx, 30 - (i % 3), 0x39E7)
  end
  draw_number(fb, "1570", 3, 2, 1, C_SCORE, 0x4208, 1)
  rect_safe(fb, 48, 2, 10, 1, 0x39E7)
  rect_safe(fb, 48, 2, 3 + math.floor(cyc(2600, 0) * 7), 1, 0x07E0)

  local ox = 10 + math.floor(math.sin(state.anim_ms / 900) * 3)
  local flap = blink(320, 0.5, 0)
  for c = 0, 4 do draw_sprite(fb, ox + c * 9, 5, flap and BUG or {"rrrr", ".rr.", "r..r", "rrrr"}, {r = C_BUG}, 1) end
  for c = 0, 4 do draw_sprite(fb, ox + c * 9, 11, flap and BEE or {"y..y", ".yy.", "yyyy", ".yy."}, {y = C_BEE}, 1) end
  draw_sprite(fb, ox + 13, 17, flap and BOSS or BOSS2, {b = C_BOSS}, 1)
  draw_sprite(fb, ox + 22, 17, flap and BOSS or BOSS2, {b = C_BOSS}, 1)
  if blink(480, 0.5, 120) then
    draw_sprite(fb, ox + 39, 17, BOSS2, {b = 0xFD20}, 1)
  end

  local p = cyc(2500, 0)
  local dx = 31 + math.sin(p * math.pi * 2) * 16
  local dy = 5 + p * 18
  for i = 0, 4 do
    set_px_safe(fb, 31 + math.sin((p - i * 0.04) * math.pi * 2) * 16, 5 + (p - i * 0.04) * 18, 0x39E7)
  end
  draw_sprite(fb, dx, dy, BUG, {r = C_BUG}, 1)
  rect_safe(fb, dx + 1, dy + 4, 1, 3, 0xFD20)
  draw_sprite(fb, 27, 25, {
    "..ww..",
    ".wbbw.",
    "wwbbww",
    "..ww..",
  }, {w = C_SHIP, b = C_SHIP_ACCENT}, 1)
  rect_safe(fb, 31, 17, 1, 10, C_SHOT)
  rect_safe(fb, dx + 2, dy + 4, 1, 6, C_SHOT)
  rect_safe(fb, 27, 3, 10, 1, C_SCORE)
  if blink(200, 0.5, 0) then
    rect_safe(fb, 30, 24, 2, 1, 0xFFFF)
  end
end

return app
