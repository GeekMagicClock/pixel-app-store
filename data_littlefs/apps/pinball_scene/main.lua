local app = {}

local state = { anim_ms = 0 }

local C_BG = 0x0001
local C_RAIL = 0xFFFF
local C_BUMPER_A = 0xF81F
local C_BUMPER_B = 0x07FF
local C_BUMPER_C = 0xFFE0
local C_BALL = 0xFFFF
local C_FLIP = 0xF800
local C_LIGHT = 0x07E0

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



function app.init()
  state.anim_ms = 0
end

function app.tick(dt_ms)
  state.anim_ms = (state.anim_ms + (dt_ms or 0)) % 60000
end

function app.render_fb(fb)

  fb:fill(C_BG)
  line_safe(fb, 5, 2, 2, 30, C_RAIL)
  line_safe(fb, 58, 2, 61, 30, C_RAIL)
  line_safe(fb, 5, 2, 58, 2, C_RAIL)
  rect_safe(fb, 9, 4, 16, 2, C_LIGHT)
  rect_safe(fb, 39, 4, 16, 2, C_LIGHT)
  rect_safe(fb, 10, 8, 6, 6, C_BUMPER_A)
  rect_safe(fb, 27, 6, 6, 6, C_BUMPER_B)
  rect_safe(fb, 44, 10, 6, 6, C_BUMPER_C)
  rect_safe(fb, 17, 14, 4, 4, C_BUMPER_B)
  rect_safe(fb, 38, 16, 4, 4, C_BUMPER_A)
  for i = 0, 5 do
    rect_safe(fb, 10 + i * 8, 18, 2, 2, ((state.anim_ms // 120 + i) % 2 == 0) and C_LIGHT or C_BG)
  end
  local bx = 8 + ((state.anim_ms // 70) % 44)
  local by = 5 + math.abs(((state.anim_ms // 80) % 22) - 11)
  rect_safe(fb, bx, by, 2, 2, C_BALL)
  line_safe(fb, 10, 24, 18, 16, C_RAIL)
  line_safe(fb, 54, 24, 46, 16, C_RAIL)
  line_safe(fb, 18, 28, 28, 24, C_FLIP)
  line_safe(fb, 46, 28, 36, 24, C_FLIP)
  rect_safe(fb, 28, 24, 8, 1, C_LIGHT)
end

return app
