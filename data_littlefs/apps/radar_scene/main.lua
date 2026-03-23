local app = {}

local state = { anim_ms = 0 }

local C_BG = 0x0000
local C_PANEL = 0x0120
local C_GRID = 0x03E0
local C_SWEEP = 0xAFE5
local C_BLIP = 0xFFFF

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
  rect_safe(fb, 8, 1, 48, 30, C_PANEL)
  rect_safe(fb, 9, 2, 46, 28, C_BG)
  rect_safe(fb, 10, 3, 44, 26, C_PANEL)
  rect_safe(fb, 11, 4, 42, 24, C_BG)
  rect_safe(fb, 31, 4, 1, 24, C_GRID)
  rect_safe(fb, 12, 16, 38, 1, C_GRID)
  rect_safe(fb, 15, 9, 32, 1, C_GRID)
  rect_safe(fb, 15, 23, 32, 1, C_GRID)
  rect_safe(fb, 19, 6, 1, 20, C_GRID)
  rect_safe(fb, 43, 6, 1, 20, C_GRID)
  local sweep = (state.anim_ms // 180) % 8
  local endpoints = {{50,6},{52,12},{52,20},{48,26},{32,29},{16,26},{12,20},{12,10}}
  local p = endpoints[sweep + 1]
  line_safe(fb, 32, 16, (p[1] + 32) // 2, (p[2] + 16) // 2, C_GRID)
  line_safe(fb, 32, 16, p[1], p[2], C_SWEEP)
  if sweep > 0 then
    local prev = endpoints[sweep]
    line_safe(fb, 32, 16, prev[1], prev[2], C_GRID)
  end
  rect_safe(fb, 24, 12, 2, 2, ((state.anim_ms // 240) % 2 == 0) and C_BLIP or C_GRID)
  rect_safe(fb, 39, 21, 2, 2, ((state.anim_ms // 180) % 2 == 0) and C_BLIP or C_GRID)
  rect_safe(fb, 17, 19, 1, 1, ((state.anim_ms // 130) % 2 == 0) and C_BLIP or C_GRID)
  rect_safe(fb, 12, 29, 12, 1, C_GRID)
  rect_safe(fb, 40, 29, 8, 1, C_GRID)
end

return app
