local app = {}

local state = { anim_ms = 0 }

local C_BG = 0x0000
local C_PANEL = 0x39E7
local C_TILE = 0x0000
local C_TILE_LIT = 0xFD20
local C_TILE_DIM = 0x9C00
local C_ARROW = 0xFFFF

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
  rect_safe(fb, 2, 4, 60, 24, C_PANEL)
  rect_safe(fb, 4, 6, 56, 20, C_TILE)
  rect_safe(fb, 4, 6, 56, 2, C_TILE_DIM)
  rect_safe(fb, 4, 24, 56, 2, C_TILE_DIM)
  rect_safe(fb, 6, 7, 8, 1, C_ARROW)
  rect_safe(fb, 50, 7, 6, 1, C_ARROW)
  for row = 0, 2 do
    for col = 0, 7 do
      local x = 6 + col * 6
      local y = 8 + row * 6
      rect_safe(fb, x, y, 5, 4, (((col + row + (state.anim_ms // 240)) % 5) == 0) and C_TILE_LIT or C_TILE_DIM)
      rect_safe(fb, x, y + 2, 5, 1, C_BG)
      if ((state.anim_ms // 180) + col + row) % 7 == 0 then
        rect_safe(fb, x, y, 5, 1, C_ARROW)
      end
    end
  end
  local arrow_x = 8 + ((state.anim_ms // 120) % 36)
  rect_safe(fb, arrow_x, 24, 4, 1, C_ARROW)
  rect_safe(fb, arrow_x + 4, 23, 1, 3, C_ARROW)
  rect_safe(fb, 8, 27, 48, 1, C_TILE_DIM)
  rect_safe(fb, 8, 27, 12 + ((state.anim_ms // 90) % 24), 1, C_TILE_LIT)
end

return app
