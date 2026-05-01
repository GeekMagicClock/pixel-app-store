local app = {}

local DIGITS = {
  ["0"] = {"111","101","101","101","111"},
  ["1"] = {"010","110","010","010","111"},
  ["2"] = {"111","001","111","100","111"},
  ["3"] = {"111","001","111","001","111"},
  ["4"] = {"101","101","111","001","001"},
  ["5"] = {"111","100","111","001","111"},
  ["6"] = {"111","100","111","101","111"},
  ["7"] = {"111","001","001","010","010"},
  ["8"] = {"111","101","111","101","111"},
  ["9"] = {"111","101","111","001","111"},
}

local W = 10
local H = 14
local CELL = 2
local ORGX = 3
local ORGY = 2
local DROP_MS = 220
local MOVE_MS = 150
local SPAWN_MS = 180
local FLASH_MS = 180

local C_BG = 0x0000
local C_FRAME = 0x7BEF
local C_WELL = 0x0008
local C_GRID = 0x18C3
local C_PANEL = 0x1082
local C_FLASH = 0xFFFF

local PIECES = {
  I = {color = 0x07FF, rots = {
    {{0,1},{1,1},{2,1},{3,1}},
    {{2,0},{2,1},{2,2},{2,3}},
  }},
  O = {color = 0xFFE0, rots = {
    {{1,0},{2,0},{1,1},{2,1}},
  }},
  T = {color = 0xC81F, rots = {
    {{1,0},{0,1},{1,1},{2,1}},
    {{1,0},{1,1},{2,1},{1,2}},
    {{0,1},{1,1},{2,1},{1,2}},
    {{1,0},{0,1},{1,1},{1,2}},
  }},
  S = {color = 0x07E0, rots = {
    {{1,0},{2,0},{0,1},{1,1}},
    {{1,0},{1,1},{2,1},{2,2}},
  }},
  Z = {color = 0xF800, rots = {
    {{0,0},{1,0},{1,1},{2,1}},
    {{2,0},{1,1},{2,1},{1,2}},
  }},
  L = {color = 0xFD20, rots = {
    {{0,0},{0,1},{1,1},{2,1}},
    {{1,0},{2,0},{1,1},{1,2}},
    {{0,1},{1,1},{2,1},{2,2}},
    {{1,0},{1,1},{0,2},{1,2}},
  }},
  J = {color = 0xFFFF, rots = {
    {{2,0},{0,1},{1,1},{2,1}},
    {{1,0},{1,1},{1,2},{2,2}},
    {{0,1},{1,1},{2,1},{0,2}},
    {{0,0},{1,0},{1,1},{1,2}},
  }},
}

local ORDER = {"I","T","L","O","S","Z","J"}

local state = {
  anim_ms = 0,
  board = {},
  current = nil,
  next_name = nil,
  queue_idx = 1,
  spawn_wait_ms = 0,
  drop_ms = 0,
  move_ms = 0,
  flash_ms = 0,
  flash_rows = nil,
  lines = 0,
  score = 0,
  reset_flash_ms = 0,
}

local function set_px_safe(fb, x, y, c)
  x = math.floor(x + 0.5)
  y = math.floor(y + 0.5)
  if x < 0 or x >= 64 or y < 0 or y >= 32 then return end
  fb:set_px(x, y, c)
end

local function rect_safe(fb, x, y, w, h, c)
  if w <= 0 or h <= 0 then return end
  fb:rect(math.floor(x + 0.5), math.floor(y + 0.5), math.floor(w + 0.5), math.floor(h + 0.5), c)
end

local function draw_digit(fb, ch, x, y, s, c, shadow)
  local pat = DIGITS[ch]
  if not pat then return end
  for row = 1, #pat do
    local line = pat[row]
    for col = 1, #line do
      if string.sub(line, col, col) == "1" then
        if shadow then
          rect_safe(fb, x + (col - 1) * s + 1, y + (row - 1) * s + 1, s, s, shadow)
        end
        rect_safe(fb, x + (col - 1) * s, y + (row - 1) * s, s, s, c)
      end
    end
  end
end

local function draw_number(fb, text, x, y, s, c, shadow, gap)
  gap = gap or s
  for i = 1, #text do
    draw_digit(fb, string.sub(text, i, i), x + (i - 1) * (3 * s + gap), y, s, c, shadow)
  end
end

local function board_cell(fb, gx, gy, color)
  local x = ORGX + gx * CELL
  local y = ORGY + gy * CELL
  rect_safe(fb, x, y, 2, 2, color)
  rect_safe(fb, x, y, 2, 1, 0xFFFF)
  rect_safe(fb, x, y, 1, 2, 0xFFFF)
end

local function piece_cells(name, rot)
  local rots = PIECES[name].rots
  local idx = ((rot - 1) % #rots) + 1
  return rots[idx]
end

local function rotation_bounds(name, rot)
  local min_x, max_x = 99, -99
  for _, cell in ipairs(piece_cells(name, rot)) do
    if cell[1] < min_x then min_x = cell[1] end
    if cell[1] > max_x then max_x = cell[1] end
  end
  return min_x, max_x
end

local function empty_board()
  local board = {}
  for y = 1, H do
    board[y] = {0,0,0,0,0,0,0,0,0,0}
  end
  return board
end

local function next_name()
  local name = ORDER[state.queue_idx]
  state.queue_idx = (state.queue_idx % #ORDER) + 1
  return name
end

local function can_place(board, name, rot, px, py)
  for _, cell in ipairs(piece_cells(name, rot)) do
    local x = px + cell[1]
    local y = py + cell[2]
    if x < 0 or x >= W or y >= H then
      return false
    end
    if y >= 0 and board[y + 1][x + 1] ~= 0 then
      return false
    end
  end
  return true
end

local function clone_board(src)
  local out = {}
  for y = 1, H do
    out[y] = {}
    for x = 1, W do
      out[y][x] = src[y][x]
    end
  end
  return out
end

local function clear_full_rows(board)
  local kept = {}
  local cleared = 0
  for y = H, 1, -1 do
    local full = true
    for x = 1, W do
      if board[y][x] == 0 then
        full = false
        break
      end
    end
    if full then
      cleared = cleared + 1
    else
      table.insert(kept, 1, board[y])
    end
  end
  while #kept < H do
    table.insert(kept, 1, {0,0,0,0,0,0,0,0,0,0})
  end
  return kept, cleared
end

local function score_board_state(board, cleared)
  local heights = {}
  local total_height = 0
  local holes = 0
  local bumpiness = 0
  for x = 1, W do
    local h = 0
    local filled_seen = false
    for y = 1, H do
      if board[y][x] ~= 0 then
        if not filled_seen then
          h = H - y + 1
          filled_seen = true
        end
      elseif filled_seen then
        holes = holes + 1
      end
    end
    heights[x] = h
    total_height = total_height + h
    if x > 1 then
      bumpiness = bumpiness + math.abs(h - heights[x - 1])
    end
  end
  return cleared * 220 - holes * 70 - total_height * 4 - bumpiness * 9
end

local function choose_target(name)
  local best = nil
  for rot = 1, #PIECES[name].rots do
    local min_x, max_x = rotation_bounds(name, rot)
    for px = -min_x, (W - 1) - max_x do
      local py = -4
      if can_place(state.board, name, rot, px, py) then
        while can_place(state.board, name, rot, px, py + 1) do
          py = py + 1
        end
        local test_board = clone_board(state.board)
        for _, cell in ipairs(piece_cells(name, rot)) do
          local x = px + cell[1]
          local y = py + cell[2]
          if y >= 0 and y < H and x >= 0 and x < W then
            test_board[y + 1][x + 1] = PIECES[name].color
          end
        end
        test_board, cleared = clear_full_rows(test_board)
        local score = score_board_state(test_board, cleared)
        score = score - math.abs(px - 3) * 2
        if not best or score > best.score then
          best = {rot = rot, x = px, score = score}
        end
      end
    end
  end
  return best or {rot = 1, x = 3}
end

local function lock_piece()
  local cur = state.current
  if not cur then return end
  for _, cell in ipairs(piece_cells(cur.name, cur.rot)) do
    local x = cur.x + cell[1]
    local y = cur.y + cell[2]
    if y >= 0 and y < H and x >= 0 and x < W then
      state.board[y + 1][x + 1] = cur.color
    end
  end

  local rows = {}
  for y = 1, H do
    local full = true
    for x = 1, W do
      if state.board[y][x] == 0 then
        full = false
        break
      end
    end
    if full then
      table.insert(rows, y - 1)
    end
  end
  state.board, cleared = clear_full_rows(state.board)
  if cleared > 0 then
    state.lines = state.lines + cleared
    local reward = ({100, 300, 500, 800})[math.min(4, cleared)] or (cleared * 200)
    state.score = state.score + reward
    state.flash_ms = FLASH_MS
    state.flash_rows = rows
  else
    state.score = state.score + 12
  end
  state.current = nil
  state.spawn_wait_ms = SPAWN_MS
  state.drop_ms = 0
  state.move_ms = 0
end

local function reset_run()
  state.board = empty_board()
  state.current = nil
  state.next_name = next_name()
  state.spawn_wait_ms = SPAWN_MS
  state.drop_ms = 0
  state.move_ms = 0
  state.flash_ms = 0
  state.flash_rows = nil
  state.lines = 0
  state.score = 0
  state.reset_flash_ms = 320
end

local function spawn_piece()
  local name = state.next_name or next_name()
  state.next_name = next_name()
  local cur = {
    name = name,
    rot = 1,
    x = 3,
    y = -3,
    color = PIECES[name].color,
  }
  if not can_place(state.board, cur.name, cur.rot, cur.x, cur.y) then
    reset_run()
    return
  end
  local target = choose_target(name)
  cur = {
    name = name,
    rot = 1,
    x = 3,
    y = -3,
    color = PIECES[name].color,
    target_x = target.x,
    target_rot = target.rot,
  }
  state.current = cur
  state.drop_ms = 0
  state.move_ms = 0
  state.spawn_wait_ms = 0
end

local function try_auto_align()
  local cur = state.current
  if not cur then return end
  if cur.rot ~= cur.target_rot then
    local next_rot = cur.rot + 1
    if next_rot > #PIECES[cur.name].rots then
      next_rot = 1
    end
    if can_place(state.board, cur.name, next_rot, cur.x, cur.y) then
      cur.rot = next_rot
    elseif can_place(state.board, cur.name, next_rot, cur.x - 1, cur.y) then
      cur.x = cur.x - 1
      cur.rot = next_rot
    elseif can_place(state.board, cur.name, next_rot, cur.x + 1, cur.y) then
      cur.x = cur.x + 1
      cur.rot = next_rot
    end
    return
  end

  if cur.x < cur.target_x then
    if can_place(state.board, cur.name, cur.rot, cur.x + 1, cur.y) then
      cur.x = cur.x + 1
    end
    return
  end

  if cur.x > cur.target_x then
    if can_place(state.board, cur.name, cur.rot, cur.x - 1, cur.y) then
      cur.x = cur.x - 1
    end
    return
  end
end

function app.init()
  state.anim_ms = 0
  state.board = empty_board()
  state.current = nil
  state.next_name = next_name()
  state.queue_idx = 2
  state.spawn_wait_ms = 0
  state.drop_ms = 0
  state.move_ms = 0
  state.flash_ms = 0
  state.flash_rows = nil
  state.lines = 0
  state.score = 0
  state.reset_flash_ms = 0
  spawn_piece()
end

function app.tick(dt_ms)
  dt_ms = dt_ms or 0
  state.anim_ms = (state.anim_ms + dt_ms) % 600000
  state.flash_ms = math.max(0, state.flash_ms - dt_ms)
  state.reset_flash_ms = math.max(0, state.reset_flash_ms - dt_ms)
  if state.flash_ms == 0 then
    state.flash_rows = nil
  end

  if not state.current then
    state.spawn_wait_ms = state.spawn_wait_ms - dt_ms
    if state.spawn_wait_ms <= 0 then
      spawn_piece()
    end
    return
  end

  state.move_ms = state.move_ms + dt_ms
  while state.move_ms >= MOVE_MS do
    state.move_ms = state.move_ms - MOVE_MS
    try_auto_align()
  end

  state.drop_ms = state.drop_ms + dt_ms
  while state.drop_ms >= DROP_MS do
    state.drop_ms = state.drop_ms - DROP_MS
    local cur = state.current
    if cur and can_place(state.board, cur.name, cur.rot, cur.x, cur.y + 1) then
      cur.y = cur.y + 1
    else
      lock_piece()
      break
    end
  end
end

local function render_preview(fb, name, ox, oy)
  if not name then return end
  for _, cell in ipairs(piece_cells(name, 1)) do
    rect_safe(fb, ox + cell[1] * 2, oy + cell[2] * 2, 2, 2, PIECES[name].color)
  end
end

function app.render_fb(fb)
  fb:fill(state.reset_flash_ms > 0 and 0x2000 or C_BG)
  rect_safe(fb, 2, 0, 22, 30, C_FRAME)
  rect_safe(fb, 3, 1, 20, 28, C_WELL)

  for gy = 0, H - 1 do
    for gx = 0, W - 1 do
      rect_safe(fb, ORGX + gx * CELL, ORGY + gy * CELL, 1, 1, C_GRID)
      local c = state.board[gy + 1][gx + 1]
      if c ~= 0 then
        board_cell(fb, gx, gy, c)
      end
    end
  end

  if state.current then
    for _, cell in ipairs(piece_cells(state.current.name, state.current.rot)) do
      local x = state.current.x + cell[1]
      local y = state.current.y + cell[2]
      if y >= 0 then
        board_cell(fb, x, y, state.current.color)
      end
    end
  end

  if state.flash_ms > 0 and state.flash_rows then
    for _, row in ipairs(state.flash_rows) do
      rect_safe(fb, ORGX, ORGY + row * CELL, W * CELL, CELL, C_FLASH)
    end
  end

  rect_safe(fb, 26, 2, 14, 10, C_PANEL)
  rect_safe(fb, 27, 3, 12, 8, C_BG)
  render_preview(fb, state.next_name, 28, 4)

  rect_safe(fb, 42, 2, 20, 16, C_PANEL)
  rect_safe(fb, 43, 3, 18, 14, C_BG)
  draw_number(fb, string.format("%02d", math.min(99, state.lines)), 45, 4, 1, 0xFFFF, 0x2104, 1)
  draw_number(fb, string.format("%04d", state.score % 10000), 45, 11, 1, 0xFFE0, 0x2104, 1)

  rect_safe(fb, 26, 18, 36, 10, C_PANEL)
  rect_safe(fb, 27, 19, 34, 8, C_BG)
  for i = 0, 9 do
    local h = 0
    for y = 1, H do
      if state.board[y][i + 1] ~= 0 then
        h = H - y + 1
        break
      end
    end
    local bar_h = math.max(1, math.floor(h * 6 / H))
    rect_safe(fb, 29 + i * 3, 26 - bar_h, 2, bar_h, (i % 2 == 0) and 0x07FF or 0xFD20)
  end
  rect_safe(fb, 28, 25, math.min(32, state.lines * 2), 1, 0x07E0)
end


-- __GLOBAL_BOOT_SPLASH_WRAPPER_V1__
local __boot_now_ms = now_ms or (sys and sys.now_ms) or function() return 0 end
local __boot_started_ms = 0
local __boot_ms = tonumber(data.get("tetris_sprint.boot_splash_ms") or data.get("app.boot_splash_ms") or 5000) or 5000
if __boot_ms < 0 then __boot_ms = 0 end
local __boot_name = tostring(data.get("tetris_sprint.app_name") or "Tetris Sprint")

local function __boot_compact_text(s, limit)
  s = tostring(s or "")
  s = string.gsub(s, "%s+", " ")
  s = string.gsub(s, "^%s+", "")
  s = string.gsub(s, "%s+$", "")
  local n = tonumber(limit) or 16
  if #s > n then return string.sub(s, 1, n - 1) .. "…" end
  return s
end

local function __boot_split_title_lines(name)
  local text = tostring(name or "")
  text = string.gsub(text, "%s+", " ")
  text = string.gsub(text, "^%s+", "")
  text = string.gsub(text, "%s+$", "")
  if text == "" then return "APP", "" end
  local mid = math.floor(#text / 2)
  local cut = nil
  local best = 999
  for i = 1, #text do
    if string.sub(text, i, i) == " " then
      local d = math.abs(i - mid)
      if d < best then
        best = d
        cut = i
      end
    end
  end
  if not cut then return text, "" end
  local a = string.gsub(string.sub(text, 1, cut - 1), "%s+$", "")
  local b = string.gsub(string.sub(text, cut + 1), "^%s+", "")
  return a, b
end

local function __boot_is_active()
  if __boot_started_ms <= 0 then return false end
  return (__boot_now_ms() - __boot_started_ms) < __boot_ms
end

local __orig_init = app.init
app.init = function(...)
  __boot_started_ms = __boot_now_ms()
  if __orig_init then return __orig_init(...) end
end

local __orig_render_fb = app.render_fb
if __orig_render_fb then
  app.render_fb = function(...)
    local fb = select(1, ...)
    if __boot_is_active() and fb and fb.fill and fb.text_box then
      local t1, t2 = __boot_split_title_lines(__boot_name)
      fb:fill(0x0000)
      if t2 ~= "" then
        fb:text_box(0, 8, 64, 8, __boot_compact_text(t1, 14), 0x07FF, "builtin:silkscreen_regular_8", 8, "center", false)
        fb:text_box(0, 16, 64, 8, __boot_compact_text(t2, 14), 0x07FF, "builtin:silkscreen_regular_8", 8, "center", false)
      else
        fb:text_box(0, 12, 64, 8, __boot_compact_text(t1, 14), 0x07FF, "builtin:silkscreen_regular_8", 8, "center", false)
      end
      return true
    end
    return __orig_render_fb(...)
  end
end

local __orig_render = app.render
if __orig_render then
  app.render = function(...)
    if __boot_is_active() then
      local t1, t2 = __boot_split_title_lines(__boot_name)
      if t2 ~= "" then
        return {"", __boot_compact_text(t1, 16), __boot_compact_text(t2, 16), ""}
      end
      return {"", __boot_compact_text(t1, 16), "", ""}
    end
    return __orig_render(...)
  end
end

return app
