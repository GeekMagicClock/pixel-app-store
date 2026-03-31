
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
local C_MOON = 0x8410
local C_MOON_HI = 0xBDF7
local C_PAD = 0x07E0
local C_PAD_BLINK = 0xFFE0
local C_LANDER = 0xFFFF
local C_GLASS = 0x07FF
local C_FLAME = 0xFD20
local C_HUD = 0x7BEF
local C_HUD_ACCENT = 0xF800

local function terrain_y(x)
  if x < 10 then return 24 - math.floor(x * 0.3) end
  if x < 18 then return 21 + math.floor((x - 10) * 0.4) end
  if x < 29 then return 24 - math.floor((x - 18) * 0.2) end
  if x < 39 then return 21 end
  if x < 50 then return 21 + math.floor((x - 39) * 0.4) end
  return 26 - math.floor((x - 50) * 0.25)
end

function app.render_fb(fb)
  fb:fill(C_BG)
  for i = 0, 11 do
    set_px_safe(fb, (i * 19 + math.floor(state.anim_ms / 60)) % 64, (i * 7 + i) % 18, C_STAR)
  end
  for i = 0, 5 do
    set_px_safe(fb, 8 + i * 9, 14 + ((i + math.floor(state.anim_ms / 700)) % 3), 0x39E7)
  end
  rect_safe(fb, 2, 2, 18, 2, C_HUD)
  rect_safe(fb, 3, 3, 1 + math.floor((1 - cyc(4300, 0)) * 15), 1, C_FLAME)
  draw_number(fb, "82", 3, 6, 1, C_HUD, 0x0000, 1)
  draw_number(fb, "32", 46, 2, 1, C_HUD, 0x4208, 1)
  rect_safe(fb, 46, 6, 12, 1, 0x39E7)
  rect_safe(fb, 46, 6, 4 + math.floor((1 - cyc(4300, 0)) * 8), 1, 0xFFE0)

  for x = 0, 63 do
    local y = terrain_y(x)
    rect_safe(fb, x, y, 1, 32 - y, C_MOON)
    if x % 6 == 0 then
      set_px_safe(fb, x, y, C_MOON_HI)
    end
  end
  rect_safe(fb, 29, 21, 10, 2, C_PAD)
  rect_safe(fb, 30, 20, 8, 1, C_MOON_HI)
  if blink(240, 0.5, 0) then
    rect_safe(fb, 29, 20, 1, 1, C_PAD_BLINK)
    rect_safe(fb, 38, 20, 1, 1, C_PAD_BLINK)
  end
  if blink(180, 0.5, 90) then
    rect_safe(fb, 28, 23, 2, 1, 0x7BEF)
    rect_safe(fb, 38, 23, 2, 1, 0x7BEF)
  end

  local p = cyc(4300, 0)
  local lx = 8 + p * 28
  local ly = 4 + p * 14 - math.sin(p * math.pi * 2) * 4
  draw_sprite(fb, lx, ly, {
    "..ww..",
    ".wggw.",
    "wwwwww",
    ".w..w.",
  }, {w = C_LANDER, g = C_GLASS}, 1)
  line_safe(fb, lx + 1, ly + 4, lx - 1, ly + 7, C_LANDER)
  line_safe(fb, lx + 4, ly + 4, lx + 6, ly + 7, C_LANDER)
  line_safe(fb, lx + 2, ly + 4, 34, 20, 0x39E7)
  rect_safe(fb, lx + 2, ly + 4, 1, 2, C_LANDER)
  if blink(160, 0.65, 0) then
    rect_safe(fb, lx + 2, ly + 5, 2, 4, C_FLAME)
    rect_safe(fb, lx + 1, ly + 8, 4, 1, 0xFFE0)
  end

  rect_safe(fb, 47, 7, 10, 1, C_HUD)
  rect_safe(fb, 47, 10, 8, 1, C_HUD_ACCENT)
  draw_number(fb, "12", 49, 12, 1, C_HUD, 0x4208, 1)
  rect_safe(fb, 44, 23, 12, 1, C_HUD)
  rect_safe(fb, 44, 24, 6 + math.floor((1 - p) * 5), 1, C_HUD_ACCENT)
end

return app
