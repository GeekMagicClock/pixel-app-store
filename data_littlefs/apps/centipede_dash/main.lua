
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
local C_MCAP = 0xF81F
local C_MCAP2 = 0xFD20
local C_MSTEM = 0xFFFF
local C_BODY = 0x07E0
local C_HEAD = 0xFFE0
local C_EYE = 0xF800
local C_PLAYER = 0x07FF
local C_PLAYER_MUZZ = 0xF800
local C_SHOT = 0xFFFF
local C_SPIDER = 0xC81F
local MUSH = {
  {8, 6}, {16, 8}, {24, 5}, {31, 9}, {40, 7}, {48, 10}, {55, 6},
  {12, 14}, {21, 16}, {30, 13}, {39, 15}, {48, 17},
}

local function cent_pos(step)
  local row = math.floor(step / 24)
  local col = step % 24
  local x = (row % 2 == 0) and (7 + col * 2) or (53 - col * 2)
  local y = 4 + row * 4
  return x, y
end

local function draw_mushroom(fb, x, y, cap)
  draw_sprite(fb, x, y, {".ccc.", "ccccc", ".sss.", "..s.."}, {c = cap, s = C_MSTEM}, 1)
end

local function draw_segment(fb, x, y, head)
  rect_safe(fb, x, y, 2, 2, head and C_HEAD or C_BODY)
  if head then
    set_px_safe(fb, x, y, C_EYE)
    set_px_safe(fb, x + 1, y, C_EYE)
  else
    set_px_safe(fb, x - 1, y + 1, C_BODY)
    set_px_safe(fb, x + 2, y + 1, C_BODY)
  end
end

function app.render_fb(fb)
  fb:fill(C_BG)
  rect_safe(fb, 0, 0, 64, 1, 0x7BEF)
  for i = 0, 5 do
    local sx = (i * 11 + math.floor(state.anim_ms / 36)) % 64
    set_px_safe(fb, sx, 2 + (i % 2), 0x39E7)
  end
  for i, m in ipairs(MUSH) do
    draw_mushroom(fb, m[1], m[2], (i % 2 == 0) and C_MCAP or C_MCAP2)
    if (i % 3) == 0 then
      set_px_safe(fb, m[1] + 2, m[2], 0xFFFF)
    end
  end
  local head_step = math.floor(state.anim_ms / 110) % 96
  for i = 11, 0, -1 do
    local x, y = cent_pos((head_step - i * 2 + 96) % 96)
    draw_segment(fb, x, y, i == 0)
    if i > 0 then
      set_px_safe(fb, x + 1, y + 2, C_BODY)
    end
  end
  local px = 28 + math.floor(math.sin(state.anim_ms / 700) * 10)
  draw_sprite(fb, px, 25, {
    ".pp.",
    "pppp",
    ".rr.",
    "r..r",
  }, {p = C_PLAYER, r = C_PLAYER_MUZZ}, 1)
  rect_safe(fb, px + 1, 17 + math.floor((state.anim_ms / 22) % 7), 1, 11, C_SHOT)
  draw_sprite(fb, 8 + math.floor(tri(1500, 0) * 46), 22, {"s..s", ".ss.", "ssss", "s..s"}, {s = C_SPIDER}, 1)
  if blink(220, 0.5, 0) then
    rect_safe(fb, px + 2, 24, 1, 2, 0xFD20)
  end
  rect_safe(fb, 46, 7, 12, 1, 0x4208)
  rect_safe(fb, 46, 7, 3 + math.floor(cyc(3000, 0) * 8), 1, 0x07E0)
  draw_number(fb, "100", 46, 1, 1, 0xFFFF, 0x4208, 1)
end

return app
