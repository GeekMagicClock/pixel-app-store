local app = {}

local state = { anim_ms = 0 }

local C_SKY = 0x0000
local C_STAR = 0xFFFF
local C_EARTH = 0x07FF
local C_MOON = 0xC618
local C_BASE = 0x8410
local C_LIGHT = 0xF800

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
  for i = 0, 9 do rect_safe(fb, (i * 7 + 3) % 63, (i * 5 + 2) % 15, 1, 1, C_STAR) end
  rect_safe(fb, 44, 3, 8, 6, C_EARTH)
  rect_safe(fb, 0, 24, 64, 8, C_MOON)
  rect_safe(fb, 10, 20, 10, 4, C_BASE)
  rect_safe(fb, 24, 18, 12, 6, C_BASE)
  rect_safe(fb, 40, 21, 12, 3, C_BASE)
  rect_safe(fb, 28, 14, 2, 4, C_BASE)
  rect_safe(fb, 29, 12, 1, 2, C_LIGHT)
  rect_safe(fb, 13, 19, 4, 1, C_STAR)
  rect_safe(fb, 28, 17, 4, 1, C_STAR)
  rect_safe(fb, 44, 20, 4, 1, C_STAR)
  rect_safe(fb, 6, 27, 4, 2, C_BASE)
  rect_safe(fb, 7, 26, 2, 1, C_LIGHT)
  rect_safe(fb, 18, 27, 2, 1, C_BASE)
  rect_safe(fb, 35, 26, 6, 1, C_BASE)
  rect_safe(fb, 37, 24, 1, 2, C_LIGHT)
end

return app
