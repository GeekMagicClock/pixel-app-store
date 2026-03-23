local app = {}

local state = { anim_ms = 0 }

local C_SKY_A = 0xFC40
local C_SKY_B = 0xFD20
local C_SKY_C = 0xF800
local C_WATER = 0x0452
local C_WATER_HI = 0x2D7F
local C_BRIDGE = 0x39C7
local C_POST = 0x18C3
local C_BODY = 0x0000
local C_FACE = 0xEF5D

local function px(fb, x, y, c)
  if x < 0 or x >= 64 or y < 0 or y >= 32 then return end
  fb:set_px(x, y, c)
end

local function rect(fb, x, y, w, h, c)
  if w <= 0 or h <= 0 then return end
  fb:rect(x, y, w, h, c)
end

local function line(fb, x0, y0, x1, y1, c)
  local dx = math.abs(x1 - x0)
  local sx = x0 < x1 and 1 or -1
  local dy = -math.abs(y1 - y0)
  local sy = y0 < y1 and 1 or -1
  local err = dx + dy
  while true do
    px(fb, x0, y0, c)
    if x0 == x1 and y0 == y1 then break end
    local e2 = err * 2
    if e2 >= dy then err = err + dy; x0 = x0 + sx end
    if e2 <= dx then err = err + dx; y0 = y0 + sy end
  end
end

function app.init()
  state.anim_ms = 0
end

function app.tick(dt_ms)
  state.anim_ms = (state.anim_ms + (dt_ms or 0)) % 60000
end

function app.render_fb(fb)
  local wobble = math.floor(math.sin(state.anim_ms * 0.003) * 1.2)
  fb:fill(C_SKY_A)
  for y = 0, 8 do rect(fb, 0, y, 64, 1, C_SKY_B) end
  for y = 9, 15 do rect(fb, 0, y, 64, 1, C_SKY_C) end
  for x = 0, 63 do
    local y1 = 3 + math.floor(math.sin(x * 0.15 + state.anim_ms * 0.003) * 2)
    local y2 = 9 + math.floor(math.sin(x * 0.18 - state.anim_ms * 0.002) * 2)
    rect(fb, x, y1, 1, 2, C_SKY_C)
    rect(fb, x, y2, 1, 2, C_SKY_B)
  end

  rect(fb, 0, 16, 64, 16, C_WATER)
  for x = 0, 63, 5 do
    rect(fb, x, 20 + ((x / 5 + wobble) % 2), 3, 1, C_WATER_HI)
    rect(fb, x + 2, 25 + ((x / 5) % 2), 3, 1, C_WATER_HI)
  end
  rect(fb, 0, 14, 27, 4, 0x2945)
  rect(fb, 0, 17, 20, 3, 0x18C3)
  rect(fb, 41, 12, 14, 3, 0x2945)

  line(fb, 34, 16, 63, 20, 0xDEDB)
  line(fb, 34, 18, 63, 22, C_BRIDGE)
  for x = 38, 60, 6 do
    line(fb, x, 17, x - 1, 23, C_POST)
  end

  rect(fb, 27, 14, 9, 10, C_FACE)
  rect(fb, 29, 15, 5, 6, 0xFFFF)
  rect(fb, 30, 17, 1, 1, C_BODY)
  rect(fb, 32, 17, 1, 1, C_BODY)
  rect(fb, 31, 19, 1, 2, C_BODY)
  rect(fb, 29, 21, 5, 1, C_BODY)
  rect(fb, 25, 23, 13, 8, C_BODY)
  rect(fb, 24, 20, 2, 7, C_FACE)
  rect(fb, 37, 20, 2, 7, C_FACE)
  rect(fb, 24, 24, 1, 4, C_BODY)
  rect(fb, 38, 24, 1, 4, C_BODY)
end

return app
