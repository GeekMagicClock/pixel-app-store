
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

local C_BG = 0x0010
local C_STAR = 0x9D75
local C_PLATFORM_TOP = 0xFFFF
local C_PLATFORM = 0x5AEB
local C_LAVA = 0xF800
local C_LAVA_HL = 0xFD20
local C_RIDER = 0xFFE0
local C_PLAYER_BIRD = 0xFD20
local C_ENEMY = 0xF81F
local C_ENEMY_BIRD = 0x781F
local C_WING = 0xFFFF
local C_EGG = 0xFFFF
local C_SPARK = 0xFFE0

local function bird(fb, x, y, rider, birdc, flap)
  draw_sprite(fb, x, y, {
    "...bb...",
    "..bbbb..",
    ".bbwwbb.",
    "rrbbbbb.",
    "...b.bb.",
  }, { b = birdc, w = C_WING, r = rider }, 1)
  rect_safe(fb, x + 5, y + 2, 2, 1, 0xFFE0)
  line_safe(fb, x + 2, y + 5, x + 2, y + 7, 0xFFFF)
  line_safe(fb, x + 5, y + 5, x + 5, y + 7, 0xFFFF)
  if flap then
    rect_safe(fb, x + 1, y + 1, 2, 1, C_WING)
  else
    rect_safe(fb, x + 2, y + 4, 2, 1, C_WING)
  end
end
function app.render_fb(fb)
  rect_safe(fb, 0, 0, 64, 29, C_BG)
  rect_safe(fb, 0, 28, 64, 4, C_LAVA)
  for i = 0, 10 do
    set_px_safe(fb, (i * 17 + math.floor(state.anim_ms / 40)) % 64, (i * 7) % 15 + 1, C_STAR)
  end
  for i = 0, 4 do
    local cx = (i * 14 + math.floor(state.anim_ms / 55)) % 64
    rect_safe(fb, cx, 4 + (i % 3), 4, 1, 0x4208)
  end
  for x = 0, 60, 12 do
    rect_safe(fb, (x + math.floor(state.anim_ms / 70)) % 64, 29, 3, 1, C_LAVA_HL)
  end
  for x = 6, 54, 16 do
    if blink(260, 0.5, x) then
      rect_safe(fb, x, 27, 1, 1, C_SPARK)
    end
  end

  rect_safe(fb, 7, 20, 14, 1, C_PLATFORM_TOP)
  rect_safe(fb, 7, 21, 14, 2, C_PLATFORM)
  rect_safe(fb, 26, 11, 12, 1, C_PLATFORM_TOP)
  rect_safe(fb, 26, 12, 12, 2, C_PLATFORM)
  rect_safe(fb, 44, 19, 12, 1, C_PLATFORM_TOP)
  rect_safe(fb, 44, 20, 12, 2, C_PLATFORM)
  rect_safe(fb, 10, 23, 6, 1, 0x0000)
  rect_safe(fb, 29, 14, 5, 1, 0x0000)
  rect_safe(fb, 47, 22, 5, 1, 0x0000)

  local x1 = 9 + math.sin(state.anim_ms / 520) * 9
  local y1 = 15 - math.abs(math.sin(state.anim_ms / 350) * 7)
  local x2 = 42 + math.sin(state.anim_ms / 470 + 1.2) * 9
  local y2 = 10 - math.abs(math.sin(state.anim_ms / 290 + 0.5) * 6)
  bird(fb, math.floor(x1), math.floor(y1), C_RIDER, C_PLAYER_BIRD, blink(220, 0.5, 0))
  bird(fb, math.floor(x2), math.floor(y2), C_ENEMY, C_ENEMY_BIRD, blink(200, 0.5, 120))
  line_safe(fb, math.floor(x1) + 7, math.floor(y1) + 2, math.floor(x1) + 10, math.floor(y1) + 1, 0xFFE0)
  line_safe(fb, math.floor(x2), math.floor(y2) + 3, math.floor(x2) - 3, math.floor(y2) + 4, 0xFD20)
  draw_sprite(fb, 31, 22, { ".ee.", "eeee", ".ee." }, { e = C_EGG }, 1)
  if blink(380, 0.5, 0) then
    rect_safe(fb, 32, 25, 1, 1, 0xFD20)
  end
  draw_number(fb, "75", 2, 2, 1, 0xFFFF, 0x0000, 1)
end

return app
