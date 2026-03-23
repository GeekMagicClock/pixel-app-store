local app = {}

local state = { anim_ms = 0 }

local C_BG = 0x0000
local C_ROAD = 0x18C3
local C_LANE = 0xFFFF
local C_HEAD = 0xFFE0
local C_TAIL = 0xF800
local C_CITY = 0x2104

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
  rect_safe(fb, 0, 20, 64, 12, C_ROAD)
  rect_safe(fb, 0, 18, 64, 2, C_CITY)
  line_safe(fb, 20, 20, 8, 31, C_LANE)
  line_safe(fb, 44, 20, 56, 31, C_LANE)
  rect_safe(fb, 31, 22, 2, 2, C_LANE)
  rect_safe(fb, 31, 27, 2, 2, C_LANE)
  rect_safe(fb, 2, 15, 6, 5, C_CITY)
  rect_safe(fb, 10, 11, 8, 9, C_CITY)
  rect_safe(fb, 22, 13, 5, 7, C_CITY)
  rect_safe(fb, 52, 9, 9, 11, C_CITY)
  rect_safe(fb, 12, 13, 1, 2, C_HEAD)
  rect_safe(fb, 55, 12, 1, 2, C_HEAD)
  rect_safe(fb, 58, 12, 1, 2, C_HEAD)
  local p = (state.anim_ms // 70) % 40
  rect_safe(fb, 22 + (p // 4), 24 + (p // 6), 2, 1, C_HEAD)
  rect_safe(fb, 40 - (p // 4), 24 + (p // 6), 2, 1, C_TAIL)
  rect_safe(fb, 26 + (p // 5), 21 + (p // 8), 2, 1, C_HEAD)
  rect_safe(fb, 36 - (p // 5), 21 + (p // 8), 2, 1, C_TAIL)
  rect_safe(fb, 28, 19, 8, 1, C_CITY)
  rect_safe(fb, 31, 18, 2, 1, C_LANE)
end

return app
