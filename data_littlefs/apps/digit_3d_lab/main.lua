local app = {}

local C_BG = 0x0001
local C_PANEL = 0x0824
local C_GRID = 0x18C7
local C_TEXT = 0xC638
local C_TEXT_DIM = 0x73AE
local C_SHADOW = 0x0000
local PALETTE_A = {front = 0xFE58, top = 0xFF9C, side = 0x8A66, edge = 0xFFFF}
local PALETTE_B = {front = 0x86FF, top = 0xC7FF, side = 0x22D3, edge = 0xEFFF}

local FONT = {
  ["0"] = {"111", "101", "101", "101", "111"},
  ["1"] = {"010", "110", "010", "010", "111"},
  ["2"] = {"111", "001", "111", "100", "111"},
  ["3"] = {"111", "001", "111", "001", "111"},
  ["4"] = {"101", "101", "111", "001", "001"},
  ["5"] = {"111", "100", "111", "001", "111"},
  ["6"] = {"111", "100", "111", "101", "111"},
  ["7"] = {"111", "001", "010", "010", "010"},
  ["8"] = {"111", "101", "111", "101", "111"},
  ["9"] = {"111", "101", "111", "001", "111"},
  ["A"] = {"010", "101", "111", "101", "101"},
  ["B"] = {"110", "101", "110", "101", "110"},
  ["D"] = {"110", "101", "101", "101", "110"},
  ["E"] = {"111", "100", "110", "100", "111"},
  ["G"] = {"011", "100", "101", "101", "011"},
  ["I"] = {"111", "010", "010", "010", "111"},
  ["L"] = {"100", "100", "100", "100", "111"},
  ["M"] = {"101", "111", "111", "101", "101"},
  ["O"] = {"111", "101", "101", "101", "111"},
  ["P"] = {"110", "101", "110", "100", "100"},
  ["R"] = {"110", "101", "110", "101", "101"},
  ["S"] = {"011", "100", "111", "001", "110"},
  ["T"] = {"111", "010", "010", "010", "010"},
  ["V"] = {"101", "101", "101", "101", "010"},
  ["X"] = {"101", "101", "010", "101", "101"},
  [" "] = {"000", "000", "000", "000", "000"},
}

local SEGMENTS = {
  ["0"] = {true, true, true, true, true, true, false},
  ["1"] = {false, true, true, false, false, false, false},
  ["2"] = {true, true, false, true, true, false, true},
  ["3"] = {true, true, true, true, false, false, true},
  ["4"] = {false, true, true, false, false, true, true},
  ["5"] = {true, false, true, true, false, true, true},
  ["6"] = {true, false, true, true, true, true, true},
  ["7"] = {true, true, true, false, false, false, false},
  ["8"] = {true, true, true, true, true, true, true},
  ["9"] = {true, true, true, true, false, true, true},
}

local state = {anim_ms = 0}

local function rect_safe(fb, x, y, w, h, c)
  if w <= 0 or h <= 0 then return end
  fb:rect(x, y, w, h, c)
end

local function set_px_safe(fb, x, y, c)
  if x < 0 or x >= 64 or y < 0 or y >= 32 then return end
  fb:set_px(x, y, c)
end

local function line_safe(fb, x0, y0, x1, y1, color)
  local dx = math.abs(x1 - x0)
  local sx = x0 < x1 and 1 or -1
  local dy = -math.abs(y1 - y0)
  local sy = y0 < y1 and 1 or -1
  local err = dx + dy
  while true do
    set_px_safe(fb, x0, y0, color)
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

local function draw_micro_text(fb, x, y, text, color, shadow)
  local cur = x
  local s = string.upper(tostring(text or ""))
  for i = 1, #s do
    local ch = string.sub(s, i, i)
    local glyph = FONT[ch] or FONT[" "]
    if shadow then
      for gy = 1, #glyph do
        local row = glyph[gy]
        for gx = 1, #row do
          if string.sub(row, gx, gx) == "1" then
            set_px_safe(fb, cur + gx, y + gy, shadow)
          end
        end
      end
    end
    for gy = 1, #glyph do
      local row = glyph[gy]
      for gx = 1, #row do
        if string.sub(row, gx, gx) == "1" then
          set_px_safe(fb, cur + gx - 1, y + gy - 1, color)
        end
      end
    end
    cur = cur + 4
  end
end

local function rotate_y(x, y, z, a)
  local ca = math.cos(a)
  local sa = math.sin(a)
  return x * ca + z * sa, y, -x * sa + z * ca
end

local function rotate_x(x, y, z, a)
  local ca = math.cos(a)
  local sa = math.sin(a)
  return x, y * ca - z * sa, y * sa + z * ca
end

local function project(x, y, z, cx, cy, scale, cam)
  local denom = cam - z
  if denom < 0.5 then denom = 0.5 end
  local k = scale / denom
  return cx + x * k, cy + y * k, z
end

local function lerp(a, b, t)
  return a + (b - a) * t
end

local function draw_quad_fill(fb, quad, color)
  for i = 0, 4 do
    local t = i / 4
    local ax = lerp(quad[1].x, quad[4].x, t)
    local ay = lerp(quad[1].y, quad[4].y, t)
    local bx = lerp(quad[2].x, quad[3].x, t)
    local by = lerp(quad[2].y, quad[3].y, t)
    line_safe(fb, math.floor(ax + 0.5), math.floor(ay + 0.5), math.floor(bx + 0.5), math.floor(by + 0.5), color)
  end
end

local function draw_quad_edges(fb, quad, color)
  for i = 1, 4 do
    local a = quad[i]
    local b = quad[(i % 4) + 1]
    line_safe(fb, math.floor(a.x + 0.5), math.floor(a.y + 0.5), math.floor(b.x + 0.5), math.floor(b.y + 0.5), color)
  end
end

local function make_prism(cx, cy, horizontal)
  local hx = horizontal and 1.9 or 0.45
  local hy = horizontal and 0.45 or 1.85
  local hz = 0.55
  return {
    {-hx + cx, -hy + cy, hz},
    { hx + cx, -hy + cy, hz},
    { hx + cx,  hy + cy, hz},
    {-hx + cx,  hy + cy, hz},
    {-hx + cx, -hy + cy, -hz},
    { hx + cx, -hy + cy, -hz},
    { hx + cx,  hy + cy, -hz},
    {-hx + cx,  hy + cy, -hz},
  }
end

local function transform_prism(prism, angle_y, angle_x, ox, oy)
  local pts = {}
  for i = 1, #prism do
    local p = prism[i]
    local x1, y1, z1 = rotate_y(p[1], p[2], p[3], angle_y)
    local x2, y2, z2 = rotate_x(x1, y1, z1, angle_x)
    local sx, sy, zf = project(x2, y2, z2, ox, oy, 22, 11)
    pts[i] = {x = sx, y = sy, z = zf}
  end
  return pts
end

local function segment_faces(pts, angle_y, angle_x)
  local faces = {
    {quad = {pts[1], pts[2], pts[3], pts[4]}, kind = "front", z = (pts[1].z + pts[2].z + pts[3].z + pts[4].z) / 4},
  }
  if angle_x < 0 then
    faces[#faces + 1] = {quad = {pts[5], pts[6], pts[2], pts[1]}, kind = "top", z = (pts[5].z + pts[6].z + pts[2].z + pts[1].z) / 4}
  else
    faces[#faces + 1] = {quad = {pts[4], pts[3], pts[7], pts[8]}, kind = "top", z = (pts[4].z + pts[3].z + pts[7].z + pts[8].z) / 4}
  end
  if angle_y >= 0 then
    faces[#faces + 1] = {quad = {pts[2], pts[6], pts[7], pts[3]}, kind = "side", z = (pts[2].z + pts[6].z + pts[7].z + pts[3].z) / 4}
  else
    faces[#faces + 1] = {quad = {pts[5], pts[1], pts[4], pts[8]}, kind = "side", z = (pts[5].z + pts[1].z + pts[4].z + pts[8].z) / 4}
  end
  return faces
end

local function render_seg_digit(fb, ch, ox, oy, angle_y, angle_x, palette)
  local seg = SEGMENTS[ch]
  if not seg then return end
  local specs = {
    {0, -4.2, true},
    {2.4, -2.1, false},
    {2.4, 2.1, false},
    {0, 4.2, true},
    {-2.4, 2.1, false},
    {-2.4, -2.1, false},
    {0, 0, true},
  }
  local faces = {}
  for i = 1, 7 do
    if seg[i] then
      local spec = specs[i]
      local prism = make_prism(spec[1], spec[2], spec[3])
      local pts = transform_prism(prism, angle_y, angle_x, ox, oy)
      local seg_faces = segment_faces(pts, angle_y, angle_x)
      for j = 1, #seg_faces do
        faces[#faces + 1] = seg_faces[j]
      end
    end
  end
  table.sort(faces, function(a, b) return a.z < b.z end)
  for i = 1, #faces do
    local face = faces[i]
    local color = palette.front
    if face.kind == "top" then color = palette.top end
    if face.kind == "side" then color = palette.side end
    draw_quad_fill(fb, face.quad, color)
    draw_quad_edges(fb, face.quad, palette.edge)
  end
  rect_safe(fb, ox - 6, oy + 9, 12, 1, C_SHADOW)
end

local function draw_floor(fb)
  for i = 0, 4 do
    local y = 21 + i * 2
    line_safe(fb, 9 - i * 2, y, 55 + i * 2, y, C_GRID)
  end
  for i = 0, 5 do
    local x = 12 + i * 8
    line_safe(fb, x, 21, 32 + (x - 32) * 2, 31, C_PANEL)
  end
end

function app.init()
  sys.log("digit_3d_lab init")
end

function app.tick(dt_ms)
  state.anim_ms = state.anim_ms + (tonumber(dt_ms) or 0)
end

function app.render_fb(fb)
  rect_safe(fb, 0, 0, 64, 32, C_BG)
  rect_safe(fb, 0, 0, 64, 8, C_PANEL)
  draw_floor(fb)

  local t = state.anim_ms * 0.001
  local left_y = math.sin(t * 1.15) * 0.8
  local right_y = -math.sin(t * 0.95 + 1.1) * 0.8
  local left_x = -0.34 + math.sin(t * 0.7) * 0.05
  local right_x = -0.28 + math.cos(t * 0.85 + 0.4) * 0.06

  render_seg_digit(fb, "2", 18, 14, left_y, left_x, PALETTE_A)
  render_seg_digit(fb, "8", 46, 14, right_y, right_x, PALETTE_B)

  draw_micro_text(fb, 3, 2, "PRISM 3D LAB", C_TEXT, C_SHADOW)
  draw_micro_text(fb, 3, 26, "7SEG ROTATE", C_TEXT_DIM, nil)
end

return app
