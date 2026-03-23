local app = {}

local state = { anim_ms = 0 }

local C_BG = 0x0000
local C_LOW = 0x07E0
local C_MID = 0xFFE0
local C_HI = 0xF800
local C_DIM = 0x2104

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
  rect_safe(fb, 2, 2, 60, 27, C_DIM)
  rect_safe(fb, 3, 3, 58, 25, C_BG)
  rect_safe(fb, 3, 24, 58, 1, C_DIM)
  for i = 0, 7 do
    local x = 4 + i * 7
    local h = 4 + math.floor(triangle_wave(800 + i * 110, i * 90) * 20)
    local peak_y = 28 - h
    for y = 0, 19 do
      local py = 28 - y
      if y < h then
        local c = C_LOW
        if y > 8 then c = C_MID end
        if y > 14 then c = C_HI end
        rect_safe(fb, x, py, 4, 1, c)
      else
        rect_safe(fb, x, py, 4, 1, C_DIM)
      end
    end
    rect_safe(fb, x, peak_y - 1, 4, 1, C_MID)
  end
  for x = 4, 56, 8 do
    rect_safe(fb, x, 6, 1, 16, C_DIM)
  end
end

return app
