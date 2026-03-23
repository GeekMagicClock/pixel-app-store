local app = {}

local state = { anim_ms = 0 }

local C_BG = 0x0000
local C_FRAME = 0x07FF
local C_SCAN = 0x07E0
local C_TARGET = 0xF800
local C_STAR = 0xFFFF
local C_DIM = 0x3186

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
  for i = 0, 11 do
    local x = (i * 11 + (state.anim_ms // 120)) % 64
    local y = (i * 7 + 3) % 24
    rect_safe(fb, x, y, 1, 1, (i % 3 == 0) and C_STAR or C_DIM)
  end
  rect_safe(fb, 3, 4, 58, 24, C_FRAME)
  rect_safe(fb, 4, 5, 56, 22, C_BG)
  rect_safe(fb, 8, 8, 6, 1, C_FRAME)
  rect_safe(fb, 50, 8, 6, 1, C_FRAME)
  rect_safe(fb, 8, 24, 6, 1, C_FRAME)
  rect_safe(fb, 50, 24, 6, 1, C_FRAME)
  rect_safe(fb, 18, 16, 28, 1, C_FRAME)
  rect_safe(fb, 31, 9, 1, 14, C_FRAME)
  local sy = 6 + ((state.anim_ms // 90) % 18)
  rect_safe(fb, 6, sy, 52, 1, C_SCAN)
  rect_safe(fb, 10, 12, 12, 1, C_DIM)
  rect_safe(fb, 10, 20, 12, 1, C_DIM)
  rect_safe(fb, 40, 12, 4, 1, C_TARGET)
  rect_safe(fb, 41, 11, 2, 3, C_TARGET)
  rect_safe(fb, 38, 10, 8, 5, C_DIM)
  rect_safe(fb, 17, 10, 2, 12, C_DIM)
  rect_safe(fb, 45, 10, 2, 12, C_DIM)
  rect_safe(fb, 6, 27, 10, 1, C_FRAME)
  rect_safe(fb, 48, 27, 10, 1, C_FRAME)
  rect_safe(fb, 8, 29, 48, 1, C_FRAME)
  rect_safe(fb, 8, 29, math.floor(((state.anim_ms % 3000) / 3000) * 48), 1, C_SCAN)
end

return app
