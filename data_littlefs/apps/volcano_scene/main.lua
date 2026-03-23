local app = {}

local state = { anim_ms = 0 }

local C_SKY = 0x3006
local C_MTN = 0x18C3
local C_LAVA = 0xF800
local C_LAVA_HI = 0xFD20
local C_SMOKE = 0x8410
local C_GROUND = 0x0841

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
  rect_safe(fb, 0, 26, 64, 6, C_GROUND)
  line_safe(fb, 0, 24, 12, 18, C_GROUND)
  line_safe(fb, 52, 18, 63, 24, C_GROUND)
  line_safe(fb, 6, 26, 28, 8, C_MTN)
  line_safe(fb, 28, 8, 50, 26, C_MTN)
  rect_safe(fb, 22, 12, 12, 2, C_LAVA)
  rect_safe(fb, 26, 14, 2, 9, C_LAVA_HI)
  rect_safe(fb, 31, 14, 2, 7, C_LAVA)
  rect_safe(fb, 24, 23, 12, 2, C_LAVA)
  rect_safe(fb, 26, 24, 8, 1, C_LAVA_HI)
  for i = 0, 4 do
    local sx = 24 + i * 3
    local sy = 7 - ((state.anim_ms // (140 + i * 20) + i) % 6)
    if sy >= 0 then rect_safe(fb, sx, sy, 2, 1, C_SMOKE) end
  end
  for i = 0, 4 do
    local lx = 18 + i * 5
    rect_safe(fb, lx, 25 + ((i % 2) == 0 and 0 or 1), 1, 1, C_LAVA_HI)
  end
end

return app
