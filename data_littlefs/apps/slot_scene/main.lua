local app = {}

local state = { anim_ms = 0 }

local C_BG = 0x3000
local C_FRAME = 0xFFE0
local C_REEL = 0xFFFF
local C_REEL_DARK = 0xC618
local C_SEVEN = 0xF800
local C_BAR = 0x0000
local C_CHERRY = 0xF81F
local C_LEAF = 0x07E0
local C_LIGHT = 0xFD20

local function set_px_safe(fb, x, y, c)
  if x < 0 or x >= 64 or y < 0 or y >= 32 then return end
  fb:set_px(x, y, c)
end

local function rect_safe(fb, x, y, w, h, c)
  if w <= 0 or h <= 0 then return end
  fb:rect(x, y, w, h, c)
end

local function line_safe(fb, x0, y0, x1, y1, c)
  local dx = math.abs(x1 - x0)
  local sx = x0 < x1 and 1 or -1
  local dy = -math.abs(y1 - y0)
  local sy = y0 < y1 and 1 or -1
  local err = dx + dy
  while true do
    set_px_safe(fb, x0, y0, c)
    if x0 == x1 and y0 == y1 then break end
    local e2 = err * 2
    if e2 >= dy then
      err = err + dy
      x0 = x0 + sx
    end
    if e2 <= dx then
      err = err + dx
      y0 = y0 + sy
    end
  end
end

local function clamp(v, lo, hi)
  if v < lo then return lo end
  if v > hi then return hi end
  return v
end

local function triangle_wave(period_ms, phase_ms)
  local period = period_ms or 2000
  local t = (state.anim_ms + (phase_ms or 0)) % period
  local half = period / 2
  if t < half then
    return t / half
  end
  return 1 - ((t - half) / half)
end

local function draw_reel_symbol(fb, x, y, mode)
  if mode == 0 then
    rect_safe(fb, x + 1, y + 1, 5, 1, C_BAR)
    rect_safe(fb, x + 1, y + 3, 5, 1, C_BAR)
  elseif mode == 1 then
    rect_safe(fb, x + 1, y + 1, 4, 1, C_SEVEN)
    rect_safe(fb, x + 3, y + 2, 1, 3, C_SEVEN)
  else
    rect_safe(fb, x + 1, y + 2, 2, 2, C_CHERRY)
    rect_safe(fb, x + 4, y + 2, 2, 2, C_CHERRY)
    rect_safe(fb, x + 3, y + 1, 1, 1, C_LEAF)
  end
  rect_safe(fb, x, y, 7, 1, C_REEL_DARK)
  rect_safe(fb, x, y + 6, 7, 1, C_REEL_DARK)
end

function app.init()
  state.anim_ms = 0
end

function app.tick(dt_ms)
  state.anim_ms = (state.anim_ms + (dt_ms or 0)) % 60000
end

function app.render_fb(fb)

  fb:fill(C_BG)
  rect_safe(fb, 4, 4, 56, 24, C_FRAME)
  rect_safe(fb, 6, 6, 52, 20, C_REEL_DARK)
  for i = 0, 8 do
    rect_safe(fb, 6 + i * 6, 2, 2, 2, ((state.anim_ms // 120 + i) % 2 == 0) and C_LIGHT or C_FRAME)
  end
  for i = 0, 2 do
    local x = 10 + i * 15
    rect_safe(fb, x, 9, 11, 11, C_REEL)
    rect_safe(fb, x, 9, 11, 1, C_REEL_DARK)
    rect_safe(fb, x, 19, 11, 1, C_REEL_DARK)
    draw_reel_symbol(fb, x + 2, 11, ((state.anim_ms // 180) + i) % 3)
  end
  rect_safe(fb, 52, 10, 2, 10, C_FRAME)
  rect_safe(fb, 54, 12, 2, 4, C_LIGHT)
  rect_safe(fb, 12, 15, 36, 1, C_LIGHT)
end

return app
