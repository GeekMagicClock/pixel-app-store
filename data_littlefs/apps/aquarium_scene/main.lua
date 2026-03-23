local app = {}

local state = { anim_ms = 0 }

local C_WATER = 0x03D9
local C_WATER_DARK = 0x01CF
local C_SAND = 0xFEA0
local C_PLANT = 0x07E0
local C_PLANT_DARK = 0x03E0
local C_FISH_A = 0xFD20
local C_FISH_B = 0xFFE0
local C_FISH_C = 0xF81F
local C_BUBBLE = 0xEFFF

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

local function draw_fish(fb, x, y, body, accent)
  rect_safe(fb, x + 1, y, 5, 3, body)
  rect_safe(fb, x + 2, y - 1, 2, 1, accent)
  rect_safe(fb, x, y + 1, 1, 1, body)
  rect_safe(fb, x + 6, y + 1, 2, 1, accent)
  rect_safe(fb, x + 1, y + 3, 4, 1, accent)
  rect_safe(fb, x + 2, y + 1, 1, 1, 0x0000)
end

function app.init()
  state.anim_ms = 0
end

function app.tick(dt_ms)
  state.anim_ms = (state.anim_ms + (dt_ms or 0)) % 60000
end

function app.render_fb(fb)

  fb:fill(C_WATER)
  for y = 0, 23, 3 do
    rect_safe(fb, 0, y, 64, 1, C_WATER_DARK)
  end
  rect_safe(fb, 0, 1, 64, 2, C_BUBBLE)
  rect_safe(fb, 0, 26, 64, 6, C_SAND)
  rect_safe(fb, 4, 24, 10, 3, C_WATER_DARK)
  rect_safe(fb, 25, 23, 8, 4, C_WATER_DARK)
  rect_safe(fb, 47, 24, 12, 3, C_WATER_DARK)
  for x = 2, 62, 6 do
    rect_safe(fb, x, 28 + ((x // 6) % 2), 2, 1, C_WATER_DARK)
  end
  for i = 0, 5 do
    local sway = math.floor((triangle_wave(1800, i * 180) - 0.5) * 4)
    local x = 5 + i * 10
    rect_safe(fb, x + sway, 20, 2, 8, C_PLANT_DARK)
    rect_safe(fb, x + sway + 1, 18, 1, 10, C_PLANT)
    rect_safe(fb, x + sway - 1, 21, 1, 5, C_PLANT)
  end
  local f1 = (state.anim_ms // 90) % 72
  local f2 = (state.anim_ms // 120) % 76
  local f3 = (state.anim_ms // 150) % 80
  draw_fish(fb, 64 - f1, 7, C_FISH_A, C_FISH_B)
  draw_fish(fb, f2 - 8, 13, C_FISH_B, C_FISH_C)
  draw_fish(fb, 64 - f3, 19, C_FISH_C, C_FISH_B)
  draw_fish(fb, 10 + ((state.anim_ms // 160) % 24), 10, C_FISH_A, C_FISH_C)
  for i = 0, 7 do
    local bx = 6 + i * 7 + ((i % 2) * 2)
    local by = 24 - ((state.anim_ms // (80 + i * 8) + i * 3) % 20)
    rect_safe(fb, bx, by, 1, 1, C_BUBBLE)
    if (i % 3) == 0 then rect_safe(fb, bx + 1, by + 1, 1, 1, C_BUBBLE) end
  end
end

return app
