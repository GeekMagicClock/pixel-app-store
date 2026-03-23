local app = {}

local state = { anim_ms = 0 }

local C_SKY = 0xFC00
local C_SUN = 0xFFE0
local C_SUN_CORE = 0xFFF5
local C_SUN_BAND = 0xFD20
local C_DUNE_A = 0xFD20
local C_DUNE_B = 0xC220
local C_DUNE_HI = 0xFE28
local C_MESA = 0x7A20
local C_CACTUS = 0x03E0
local C_CACTUS_HI = 0xA7E5
local C_ROCK = 0x5100

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
  local shimmer = math.floor(triangle_wave(2200, 300) * 2)
  fb:fill(C_SKY)
  rect_safe(fb, 38, 4, 8, 1, C_SUN_BAND)
  rect_safe(fb, 36, 5, 12, 1, C_SUN)
  rect_safe(fb, 35, 6, 14, 2, C_SUN)
  rect_safe(fb, 34, 8, 16, 3, C_SUN)
  rect_safe(fb, 35, 11, 14, 2, C_SUN)
  rect_safe(fb, 36, 13, 12, 1, C_SUN)
  rect_safe(fb, 39, 6, 8, 7, C_SUN_CORE)
  rect_safe(fb, 34, 9, 16, 1, C_SUN_BAND)
  rect_safe(fb, 35, 11, 14, 1, C_SUN_BAND)

  rect_safe(fb, 0, 18, 11, 2, C_MESA)
  rect_safe(fb, 8, 16, 7, 3, C_MESA)
  rect_safe(fb, 13, 14, 6, 5, C_MESA)
  rect_safe(fb, 15, 18, 8, 2, C_MESA)
  rect_safe(fb, 20, 19, 5, 1, C_MESA)

  rect_safe(fb, 24, 16, 10, 2, C_DUNE_B)
  rect_safe(fb, 27, 14, 6, 2, C_DUNE_B)
  rect_safe(fb, 26, 18, 9, 1, C_DUNE_A)

  line_safe(fb, 0, 22, 12, 19, C_DUNE_A)
  line_safe(fb, 12, 19, 25, 21, C_DUNE_A)
  line_safe(fb, 25, 21, 41, 18, C_DUNE_A)
  line_safe(fb, 41, 18, 63, 22, C_DUNE_A)
  line_safe(fb, 0, 24, 18, 20, C_DUNE_HI)
  line_safe(fb, 18, 20, 34, 23, C_DUNE_HI)
  line_safe(fb, 12, 26, 33, 21, C_DUNE_B)
  line_safe(fb, 33, 21, 49, 24, C_DUNE_B)
  line_safe(fb, 49, 24, 63, 22, C_DUNE_B)
  rect_safe(fb, 0, 23, 64, 9, C_DUNE_B)
  rect_safe(fb, 0, 23, 64, 1, C_DUNE_HI)

  rect_safe(fb, 10, 15, 2, 11, C_CACTUS)
  rect_safe(fb, 8, 18, 2, 5, C_CACTUS)
  rect_safe(fb, 12, 20, 2, 5, C_CACTUS)
  rect_safe(fb, 11, 16, 1, 9, C_CACTUS_HI)
  rect_safe(fb, 9, 19, 1, 3, C_CACTUS_HI)
  rect_safe(fb, 13, 21, 1, 3, C_CACTUS_HI)

  rect_safe(fb, 49, 24, 4, 2, C_ROCK)
  rect_safe(fb, 52, 23, 2, 2, C_ROCK)
  rect_safe(fb, 46, 25, 2, 1, C_ROCK)
  rect_safe(fb, 30 + shimmer, 26, 3, 1, C_DUNE_HI)
  rect_safe(fb, 20 + shimmer, 28, 4, 1, C_DUNE_A)
end

return app
