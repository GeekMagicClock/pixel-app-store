local app = {}

local state = { anim_ms = 0 }

local C_SKY = 0x080C
local C_BUILD = 0x0001
local C_WIN = 0xFFE0
local C_NEON_A = 0xF81F
local C_NEON_B = 0x07FF
local C_RAIN = 0xB7FF
local C_STREET = 0x2104
local C_REFLECT = 0x780F

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

  fb:fill(C_SKY)
  rect_safe(fb, 0, 23, 64, 9, C_STREET)
  rect_safe(fb, 0, 21, 64, 2, C_BUILD)
  rect_safe(fb, 2, 11, 14, 12, C_BUILD)
  rect_safe(fb, 19, 7, 12, 16, C_BUILD)
  rect_safe(fb, 34, 13, 10, 10, C_BUILD)
  rect_safe(fb, 47, 5, 15, 18, C_BUILD)
  for x = 4, 13, 4 do
    rect_safe(fb, x, 14, 2, 2, C_WIN)
    rect_safe(fb, x, 18, 2, 2, C_WIN)
  end
  for x = 21, 28, 4 do
    rect_safe(fb, x, 10, 2, 2, C_WIN)
    rect_safe(fb, x, 14, 2, 2, C_WIN)
  end
  for x = 50, 58, 4 do
    rect_safe(fb, x, 8, 2, 2, C_WIN)
    rect_safe(fb, x, 12, 2, 2, C_WIN)
  end
  rect_safe(fb, 21, 16, 10, 2, C_NEON_A)
  rect_safe(fb, 21, 18, 8, 1, C_REFLECT)
  rect_safe(fb, 47, 15, 12, 2, C_NEON_B)
  rect_safe(fb, 47, 17, 10, 1, C_REFLECT)
  rect_safe(fb, 7, 22, 14, 1, C_REFLECT)
  rect_safe(fb, 25, 22, 8, 1, C_REFLECT)
  rect_safe(fb, 22, 24, 9, 1, C_REFLECT)
  rect_safe(fb, 48, 25, 10, 1, C_NEON_B)
  local car = (state.anim_ms // 90) % 70
  rect_safe(fb, (car % 64), 27, 3, 1, C_NEON_A)
  rect_safe(fb, (car + 20) % 64, 29, 3, 1, C_NEON_B)
  for i = 0, 13 do
    local x = (i * 5 + (state.anim_ms // 70)) % 70 - 3
    line_safe(fb, x, 0, x - 3, 17, C_RAIN)
  end
  for i = 0, 8 do
    local x = (i * 7 + (state.anim_ms // 110)) % 72 - 4
    line_safe(fb, x, 10, x - 2, 31, C_RAIN)
  end
end

return app
