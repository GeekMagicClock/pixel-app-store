local app = {}

local state = { anim_ms = 0 }

local C_BG = 0x9E58
local C_TABLE = 0x6A80
local C_VASE = 0xB400
local C_VASE_HI = 0xDDE0
local C_STEM = 0x2C60
local C_PETAL = 0xFD20
local C_PETAL_HI = 0xFFE0
local C_CORE = 0x6A20

local function px(fb, x, y, c)
  if x < 0 or x >= 64 or y < 0 or y >= 32 then return end
  fb:set_px(x, y, c)
end

local function rect(fb, x, y, w, h, c)
  if w <= 0 or h <= 0 then return end
  fb:rect(x, y, w, h, c)
end

function app.init()
  state.anim_ms = 0
end

function app.tick(dt_ms)
  state.anim_ms = (state.anim_ms + (dt_ms or 0)) % 60000
end

function app.render_fb(fb)
  local glow = math.floor((state.anim_ms / 260) % 2)
  fb:fill(C_BG)
  rect(fb, 0, 24, 64, 8, C_TABLE)

  rect(fb, 24, 16, 16, 8, C_VASE)
  rect(fb, 26, 15, 12, 1, C_VASE_HI)
  rect(fb, 28, 13, 8, 3, C_VASE)
  rect(fb, 30, 12, 4, 1, C_VASE_HI)

  rect(fb, 31, 7, 1, 9, C_STEM)
  rect(fb, 27, 8, 1, 8, C_STEM)
  rect(fb, 35, 7, 1, 9, C_STEM)
  rect(fb, 23, 10, 1, 6, C_STEM)
  rect(fb, 39, 10, 1, 6, C_STEM)

  local flowers = {
    {31,6, glow},
    {27,7, 1 - glow},
    {35,7, glow},
    {23,10, 1 - glow},
    {39,10, glow},
  }
  for i = 1, #flowers do
    local f = flowers[i]
    local x, y, tw = f[1], f[2], f[3]
    rect(fb, x - 2, y, 5, 1, tw == 1 and C_PETAL_HI or C_PETAL)
    rect(fb, x - 1, y - 1, 3, 1, C_PETAL)
    rect(fb, x - 1, y + 1, 3, 1, C_PETAL)
    rect(fb, x, y - 2, 1, 5, C_PETAL_HI)
    rect(fb, x - 1, y - 1, 3, 3, C_CORE)
    px(fb, x, y, C_PETAL_HI)
  end
end

return app
