local app = {}

local state = { anim_ms = 0 }

local C_BG = 0x0000
local C_DROP = 0x03E0
local C_HEAD = 0xE7FF
local C_DIM = 0x0140

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
  for i = 0, 12 do
    local x = 1 + i * 5
    local head = (state.anim_ms // (70 + i * 7) + i * 2) % 36 - 4
    for j = 0, 5 do
      local y = head - j * 3
      if y >= 0 and y < 32 then
        rect_safe(fb, x, y, 2, 2, (j == 0) and C_HEAD or ((j < 3) and C_DROP or C_DIM))
      end
    end
    if (i % 3) == 0 then
      rect_safe(fb, x + 1, (head + 7) % 32, 1, 1, C_DROP)
    end
  end
  for y = 2, 30, 6 do
    rect_safe(fb, 61, y, 1, 1, C_HEAD)
  end
end

return app
