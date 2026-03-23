local app = {}

local state = { anim_ms = 0 }

local C_FRAME = 0xC618
local C_GLASS = 0x44FF
local C_HILL = 0x07E0
local C_GROUND = 0x03A0
local C_POLE = 0xFFFF
local C_SUN = 0xFFE0

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

  fb:fill(C_FRAME)
  rect_safe(fb, 5, 4, 54, 24, C_GLASS)
  rect_safe(fb, 6, 5, 52, 2, C_SUN)
  rect_safe(fb, 8, 8, 18, 1, C_FRAME)
  rect_safe(fb, 36, 9, 16, 1, C_FRAME)
  rect_safe(fb, 7, 16, 50, 2, C_FRAME)
  rect_safe(fb, 7, 18, 50, 10, C_HILL)
  rect_safe(fb, 7, 21, 50, 2, C_GLASS)
  rect_safe(fb, 7, 22, 50, 6, C_GROUND)
  rect_safe(fb, 42, 7, 8, 5, C_SUN)
  local shift = (state.anim_ms // 70) % 22
  for i = 0, 3 do
    local x = 12 + i * 16 - shift
    rect_safe(fb, x, 9, 2, 19, C_POLE)
  end
  rect_safe(fb, 0, 24, 5, 8, C_FRAME)
  rect_safe(fb, 59, 24, 5, 8, C_FRAME)
  rect_safe(fb, 0, 28, 64, 4, 0x2104)
end

return app
