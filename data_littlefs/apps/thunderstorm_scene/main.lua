local app = {}

local state = { anim_ms = 0 }

local C_SKY = 0x1082
local C_CLOUD = 0x4208
local C_RAIN = 0xB7FF
local C_GROUND = 0x0000
local C_BOLT = 0xFFE0
local C_FLASH = 0xFFFF

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

  local flash = ((state.anim_ms // 700) % 7) == 0
  fb:fill(flash and C_FLASH or C_SKY)
  rect_safe(fb, 0, 24, 64, 8, C_GROUND)
  rect_safe(fb, 6, 22, 8, 2, C_CLOUD)
  rect_safe(fb, 48, 22, 10, 2, C_CLOUD)
  rect_safe(fb, 4, 5, 18, 5, C_CLOUD)
  rect_safe(fb, 18, 3, 20, 6, C_CLOUD)
  rect_safe(fb, 38, 6, 18, 5, C_CLOUD)
  for i = 0, 11 do
    local x = (i * 6 + (state.anim_ms // 80)) % 68 - 2
    line_safe(fb, x, 10, x - 2, 24, C_RAIN)
  end
  if flash then
    line_safe(fb, 30, 9, 26, 16, C_BOLT)
    line_safe(fb, 26, 16, 31, 16, C_BOLT)
    line_safe(fb, 31, 16, 27, 24, C_BOLT)
    line_safe(fb, 28, 17, 24, 21, C_BOLT)
  end
  rect_safe(fb, 14, 29, 8, 1, flash and C_FLASH or C_RAIN)
  rect_safe(fb, 40, 28, 10, 1, flash and C_FLASH or C_RAIN)
end

return app
