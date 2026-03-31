
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
local C_TOP = 0xFFE0
local C_TOP_ON = 0xFFFF
local C_SIDE = 0xFD20
local C_SIDE_D = 0xD200
local C_EDGE = 0x8200
local C_Q = 0xFD20
local C_NOSE = 0xF800
local C_EYE = 0xFFFF
local C_ENEMY = 0xF81F
local C_ENEMY2 = 0x781F
local NODES = {{31,6},{24,11},{38,11},{17,16},{31,16},{45,16},{10,21},{24,21},{38,21},{52,21}}
local PATH = {1,2,4,5,8,9,6,3,1,2,5,7,8,10}
local function cube(fb, x, y, on)
  local top = on and C_TOP_ON or C_TOP
  rect_safe(fb, x - 2, y, 4, 1, top)
  rect_safe(fb, x - 3, y + 1, 6, 1, top)
  rect_safe(fb, x - 2, y + 2, 4, 1, top)
  line_safe(fb, x - 4, y + 2, x - 1, y + 5, C_SIDE_D)
  line_safe(fb, x + 3, y + 2, x, y + 5, C_SIDE)
  rect_safe(fb, x - 3, y + 3, 3, 2, C_SIDE_D)
  rect_safe(fb, x + 1, y + 3, 3, 2, C_SIDE)
  line_safe(fb, x - 4, y + 2, x - 2, y, C_EDGE)
  line_safe(fb, x + 3, y + 2, x + 1, y, C_EDGE)
  line_safe(fb, x - 1, y + 5, x, y + 5, C_EDGE)
end
function app.render_fb(fb)
  fb:fill(C_BG)
  local hop = math.floor(state.anim_ms / 420) % #PATH + 1
  for i, n in ipairs(NODES) do cube(fb, n[1], n[2], i <= hop) end
  for i = 1, hop do
    local n = NODES[PATH[i]]
    set_px_safe(fb, n[1], n[2] - 1, 0xFFFF)
  end
  local node = NODES[PATH[hop]]
  local bob = math.floor(math.abs(math.sin(state.anim_ms / 140)) * 2)
  draw_sprite(fb, node[1] - 3, node[2] - 7 - bob, {
    "..qq..",
    ".qqqq.",
    ".qqqn.",
    "..q...",
    ".q.qq.",
  }, { q = C_Q, n = C_NOSE }, 1)
  rect_safe(fb, node[1] - 1, node[2] - 6 - bob, 1, 1, C_EYE)
  local enemy = NODES[((hop + 5) % #NODES) + 1]
  draw_sprite(fb, enemy[1] - 2, enemy[2] - 4 + bob, {
    ".pp..",
    "pppp.",
    ".ppp.",
    "..pp.",
  }, { p = C_ENEMY2 }, 1)
  rect_safe(fb, enemy[1] - 1, enemy[2] - 3 + bob, 1, 1, C_EYE)
  if blink(260, 0.5, 90) then
    rect_safe(fb, enemy[1] + 1, enemy[2] - 1 + bob, 1, 1, C_ENEMY)
  end
  rect_safe(fb, 46, 2, 12, 1, 0x4208)
  rect_safe(fb, 46, 2, 4 + math.floor(cyc(2600, 0) * 7), 1, 0xFFE0)
  draw_number(fb, "50", 2, 2, 1, 0xFFFF, 0x0000, 1)
end

return app
