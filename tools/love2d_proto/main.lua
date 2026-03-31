local t = 0
local mode = 1

local palettes = {
  {
    name = "Glass Ember",
    bg = {0.05, 0.06, 0.10},
    bg2 = {0.12, 0.08, 0.08},
    face = {1.00, 0.84, 0.76},
    top = {1.00, 0.96, 0.92},
    side = {0.52, 0.26, 0.18},
    edge = {1.00, 0.98, 0.96},
    glow = {1.00, 0.36, 0.22, 0.18},
    text = {0.95, 0.94, 0.96},
    sub = {0.72, 0.76, 0.82},
  },
  {
    name = "Cyan Alloy",
    bg = {0.02, 0.05, 0.07},
    bg2 = {0.04, 0.09, 0.12},
    face = {0.58, 0.88, 1.00},
    top = {0.84, 0.98, 1.00},
    side = {0.10, 0.34, 0.43},
    edge = {0.93, 1.00, 1.00},
    glow = {0.18, 0.78, 1.00, 0.18},
    text = {0.92, 0.98, 1.00},
    sub = {0.64, 0.82, 0.88},
  }
}

local function drawShadow(x, y, w, h)
  love.graphics.setColor(0, 0, 0, 0.18)
  love.graphics.ellipse("fill", x + w * 0.55, y + h + 28, w * 0.52, 18)
end

local function drawPrismDigit(x, y, w, h, depth, text, p, tilt)
  drawShadow(x, y, w, h)

  love.graphics.setColor(p.glow)
  love.graphics.rectangle("fill", x - 10, y - 10, w + depth + 20, h + 20, 18, 18)

  love.graphics.setColor(p.side)
  love.graphics.polygon("fill",
    x + w, y,
    x + w + depth, y - depth * 0.75,
    x + w + depth, y + h - depth * 0.3,
    x + w, y + h
  )
  love.graphics.setColor(p.top)
  love.graphics.polygon("fill",
    x, y,
    x + depth, y - depth * 0.75,
    x + w + depth, y - depth * 0.75,
    x + w, y
  )
  love.graphics.setColor(p.face)
  love.graphics.rectangle("fill", x, y, w, h, 18, 18)

  love.graphics.setColor(p.edge)
  love.graphics.setLineWidth(3)
  love.graphics.line(x, y, x + w, y)
  love.graphics.line(x + w, y, x + w + depth, y - depth * 0.75)
  love.graphics.line(x, y, x + depth, y - depth * 0.75)

  love.graphics.push()
  love.graphics.translate(x + w / 2, y + h / 2)
  love.graphics.shear(tilt, 0)
  love.graphics.setColor(0.06, 0.07, 0.10, 0.92)
  love.graphics.printf(text, -w / 2, -h / 2 + 12, w, "center")
  love.graphics.pop()
end

local function drawScene(p)
  love.graphics.clear(p.bg)

  local gradSteps = 18
  for i = 0, gradSteps - 1 do
    local t2 = i / (gradSteps - 1)
    local r = p.bg[1] * (1 - t2) + p.bg2[1] * t2
    local g = p.bg[2] * (1 - t2) + p.bg2[2] * t2
    local b = p.bg[3] * (1 - t2) + p.bg2[3] * t2
    love.graphics.setColor(r, g, b)
    love.graphics.rectangle("fill", 0, i * 18, 640, 20)
  end

  love.graphics.setColor(1, 1, 1, 0.03)
  for i = 0, 8 do
    local yy = 60 + i * 26
    love.graphics.rectangle("fill", 0, yy, 640, 1)
  end

  local hh = os.date("%H")
  local mm = os.date("%M")
  local colonOn = (math.floor(t * 2) % 2) == 0

  local wobbleA = math.sin(t * 1.15) * 8
  local wobbleB = math.cos(t * 0.92) * 8

  drawPrismDigit(64, 92 + wobbleA, 118, 128, 24, hh, p, -0.10)
  drawPrismDigit(326, 92 + wobbleB, 118, 128, 24, mm, p, 0.10)

  if colonOn then
    love.graphics.setColor(p.edge)
  else
    love.graphics.setColor(p.sub[1], p.sub[2], p.sub[3], 0.5)
  end
  love.graphics.rectangle("fill", 286, 128, 16, 16, 6, 6)
  love.graphics.rectangle("fill", 286, 170, 16, 16, 6, 6)

  love.graphics.setColor(p.text)
  love.graphics.setFont(love.graphics.newFont(22))
  love.graphics.print("Love2D 3D digit exploration", 22, 18)

  love.graphics.setColor(p.sub)
  love.graphics.setFont(love.graphics.newFont(16))
  love.graphics.print("Space: switch material preset", 22, 44)
  love.graphics.print("Current: " .. p.name, 22, 66)

  love.graphics.setColor(p.sub[1], p.sub[2], p.sub[3], 0.9)
  love.graphics.print("Desktop-only look dev before 64x32 porting", 22, 284)
end

function love.load()
  love.window.setTitle("Pixel 3D Digit Proto")
  love.window.setMode(640, 320, {resizable = false})
end

function love.update(dt)
  t = t + dt
end

function love.keypressed(key)
  if key == "space" then
    mode = (mode % #palettes) + 1
  end
end

function love.draw()
  drawScene(palettes[mode])
end
