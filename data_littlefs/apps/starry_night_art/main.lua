local app = {}

local state = { anim_ms = 0 }

local C_SKY = 0x0844
local C_SKY_DEEP = 0x0023
local C_SWIRL = 0x1D7F
local C_SWIRL_HI = 0x869F
local C_MOON = 0xFFE0
local C_STAR = 0xFFFF
local C_CYPRESS = 0x0020
local C_HILL = 0x18C6
local C_TOWN = 0x632C
local C_LIGHT = 0xFFD0

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
  local phase = state.anim_ms * 0.002
  fb:fill(C_SKY_DEEP)
  rect(fb, 0, 8, 64, 15, C_SKY)

  for x = 0, 63 do
    local y = 5 + math.floor(math.sin(x * 0.18 + phase) * 2)
    rect(fb, x, y, 1, 3, C_SWIRL)
    if x % 2 == 0 then
      px(fb, x, y - 1, C_SWIRL_HI)
    end
  end
  for x = 0, 63 do
    local y = 10 + math.floor(math.sin(x * 0.14 - phase * 0.8) * 2)
    rect(fb, x, y, 1, 2, C_SWIRL_HI)
  end

  rect(fb, 45, 3, 8, 1, C_MOON)
  rect(fb, 43, 4, 12, 1, C_MOON)
  rect(fb, 42, 5, 14, 3, C_MOON)
  rect(fb, 43, 8, 12, 1, C_MOON)
  rect(fb, 45, 9, 8, 1, C_MOON)
  rect(fb, 46, 5, 5, 3, C_LIGHT)

  local stars = {{7,4},{15,7},{23,5},{31,3},{38,7},{56,5}}
  for i = 1, #stars do
    local s = stars[i]
    px(fb, s[1], s[2], (math.floor((state.anim_ms / 240) + i) % 2 == 0) and C_STAR or C_SWIRL_HI)
  end

  rect(fb, 0, 23, 64, 9, C_HILL)
  line(fb, 0, 23, 14, 21, C_HILL)
  line(fb, 14, 21, 26, 24, C_HILL)
  line(fb, 26, 24, 40, 22, C_HILL)
  line(fb, 40, 22, 63, 24, C_HILL)

  rect(fb, 4, 9, 2, 17, C_CYPRESS)
  rect(fb, 2, 12, 6, 11, C_CYPRESS)
  rect(fb, 1, 16, 8, 8, C_CYPRESS)
  rect(fb, 5, 7, 1, 3, C_CYPRESS)

  rect(fb, 14, 22, 5, 6, C_TOWN)
  rect(fb, 20, 23, 6, 5, C_TOWN)
  rect(fb, 28, 21, 7, 7, C_TOWN)
  rect(fb, 37, 22, 5, 6, C_TOWN)
  rect(fb, 43, 23, 6, 5, C_TOWN)
  rect(fb, 31, 17, 2, 11, C_TOWN)
  rect(fb, 30, 18, 4, 1, C_TOWN)
  rect(fb, 32, 15, 1, 2, C_TOWN)

  rect(fb, 16, 24, 1, 2, C_LIGHT)
  rect(fb, 22, 24, 1, 2, C_LIGHT)
  rect(fb, 30, 24, 1, 2, C_LIGHT)
  rect(fb, 40, 24, 1, 2, C_LIGHT)
  rect(fb, 45, 24, 1, 2, C_LIGHT)
end

return app
