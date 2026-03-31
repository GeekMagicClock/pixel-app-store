local app = {}

local FONT_UI = "builtin:silkscreen_regular_8"

local CELL = 4
local HUD_H = 8
local COLS = 16
local ROWS = 6

local C_BG = 0x0020
local C_HUD = 0x0844
local C_BOARD = 0x0148
local C_BOARD_ALT = 0x0126
local C_GRID = 0x0862
local C_BORDER = 0x1D27
local C_TEXT = 0xDF7B
local C_MUTED = 0x84F1
local C_SNAKE_HEAD = 0x2FE8
local C_SNAKE_BODY = 0x16A6
local C_SNAKE_TAIL = 0x0D44
local C_FOOD = 0xFD20
local C_FOOD_GLOW = 0xFFE0
local C_DEAD = 0x8000
local C_DEAD_ACCENT = 0xF8C4
local C_TRAIL = 0x0B83
local C_ROUTE = 0x6DF7

local DIRS = {
  {x = 1, y = 0},
  {x = 0, y = 1},
  {x = -1, y = 0},
  {x = 0, y = -1},
}

local state = {
  snake = {},
  dir = {x = 1, y = 0},
  food = {x = 0, y = 0},
  grow = 0,
  dead = false,
  restart_ms = 0,
  step_accum_ms = 0,
  run_ms = 0,
  crash_after_ms = 0,
}

local function rect_safe(fb, x, y, w, h, c)
  if w <= 0 or h <= 0 then return end
  fb:rect(x, y, w, h, c)
end

local function set_px_safe(fb, x, y, c)
  if x < 0 or x >= 64 or y < 0 or y >= 32 then return end
  fb:set_px(x, y, c)
end

local function seed_rng()
  local now = (sys and sys.now_ms and sys.now_ms()) or 1
  local unix = (sys and sys.unix_time and sys.unix_time()) or 1
  math.randomseed((now % 65535) + unix)
  math.random()
  math.random()
  math.random()
end

local function random_between(a, b)
  return a + math.random(0, b - a)
end

local function restart_delay_ms()
  return random_between(900, 1900)
end

local function random_crash_after_ms()
  return random_between(12000, 32000)
end

local function step_interval_ms()
  local len = #state.snake
  local speed = 210 - math.min(90, math.floor(len / 2) * 8)
  return math.max(95, speed)
end

local function same_cell(a, x, y)
  return a and a.x == x and a.y == y
end

local function snake_occupies(x, y, ignore_tail)
  local tail_index = ignore_tail and #state.snake or -1
  for i = 1, #state.snake do
    if i ~= tail_index then
      local part = state.snake[i]
      if part.x == x and part.y == y then
        return true
      end
    end
  end
  return false
end

local function inside_board(x, y)
  return x >= 0 and x < COLS and y >= 0 and y < ROWS
end

local function spawn_food()
  local free = {}
  for y = 0, ROWS - 1 do
    for x = 0, COLS - 1 do
      if not snake_occupies(x, y, false) then
        free[#free + 1] = {x = x, y = y}
      end
    end
  end
  if #free == 0 then
    state.food = {x = -1, y = -1}
    return
  end
  state.food = free[math.random(1, #free)]
end

local function reset_run()
  state.snake = {
    {x = 3, y = 2},
    {x = 2, y = 2},
    {x = 1, y = 2},
  }
  state.dir = {x = 1, y = 0}
  state.grow = 0
  state.dead = false
  state.restart_ms = 0
  state.step_accum_ms = 0
  state.run_ms = 0
  state.crash_after_ms = random_crash_after_ms()
  spawn_food()
end

local function trigger_death()
  state.dead = true
  state.restart_ms = restart_delay_ms()
end

local function direction_score(dir)
  local head = state.snake[1]
  local nx = head.x + dir.x
  local ny = head.y + dir.y
  if not inside_board(nx, ny) then
    return nil
  end

  local will_grow = same_cell(state.food, nx, ny)
  local ignore_tail = not will_grow
  if snake_occupies(nx, ny, ignore_tail) then
    return nil
  end

  local dist = math.abs(state.food.x - nx) + math.abs(state.food.y - ny)
  local wall_penalty = 0
  if nx == 0 or nx == COLS - 1 then wall_penalty = wall_penalty + 1 end
  if ny == 0 or ny == ROWS - 1 then wall_penalty = wall_penalty + 1 end
  local straight_bonus = (dir.x == state.dir.x and dir.y == state.dir.y) and -0.2 or 0
  local jitter = math.random() * 0.15
  return dist + wall_penalty + straight_bonus + jitter
end

local function choose_direction()
  local options = {}
  for i = 1, #DIRS do
    local dir = DIRS[i]
    if #state.snake <= 1 or not (dir.x == -state.dir.x and dir.y == -state.dir.y) then
      local score = direction_score(dir)
      if score then
        options[#options + 1] = {dir = dir, score = score}
      end
    end
  end

  if #options == 0 then
    return nil
  end

  table.sort(options, function(a, b) return a.score < b.score end)
  if #options > 1 and math.random() < 0.12 then
    return options[math.random(1, math.min(2, #options))].dir
  end
  return options[1].dir
end

local function advance_snake()
  if state.dead then return end

  if state.run_ms >= state.crash_after_ms then
    trigger_death()
    return
  end

  local next_dir = choose_direction()
  if not next_dir then
    trigger_death()
    return
  end
  state.dir = {x = next_dir.x, y = next_dir.y}

  local head = state.snake[1]
  local nx = head.x + state.dir.x
  local ny = head.y + state.dir.y
  if not inside_board(nx, ny) then
    trigger_death()
    return
  end

  local ate = same_cell(state.food, nx, ny)
  if snake_occupies(nx, ny, not ate) then
    trigger_death()
    return
  end

  table.insert(state.snake, 1, {x = nx, y = ny})
  if ate then
    state.grow = state.grow + 2
    spawn_food()
  end

  if state.grow > 0 then
    state.grow = state.grow - 1
  else
    table.remove(state.snake)
  end
end

local function draw_food(fb)
  local x = state.food.x * CELL
  local y = HUD_H + state.food.y * CELL
  rect_safe(fb, x + 1, y + 1, 2, 2, C_FOOD)
  set_px_safe(fb, x + 2, y, 0x07E0)
  if math.floor(state.run_ms / 250) % 2 == 0 then
    set_px_safe(fb, x + 1, y, C_FOOD_GLOW)
    set_px_safe(fb, x + 2, y, C_FOOD_GLOW)
    set_px_safe(fb, x + 1, y + 3, C_FOOD_GLOW)
    set_px_safe(fb, x + 2, y + 3, C_FOOD_GLOW)
    set_px_safe(fb, x, y + 1, C_FOOD_GLOW)
    set_px_safe(fb, x + 3, y + 1, C_FOOD_GLOW)
    set_px_safe(fb, x, y + 2, C_FOOD_GLOW)
    set_px_safe(fb, x + 3, y + 2, C_FOOD_GLOW)
  end
  if #state.snake > 0 then
    local head = state.snake[1]
    local hx = head.x * CELL + 2
    local hy = HUD_H + head.y * CELL + 2
    if math.abs(head.x - state.food.x) + math.abs(head.y - state.food.y) < 7 then
      local fx = x + 2
      local fy = y + 2
      local dx = fx > hx and 1 or -1
      for px = hx, fx, dx do
        if ((px + math.floor(state.run_ms / 80)) % 3) == 0 then
          set_px_safe(fb, px, hy, C_ROUTE)
        end
      end
      local dy = fy > hy and 1 or -1
      for py = hy, fy, dy do
        if ((py + math.floor(state.run_ms / 80)) % 3) == 0 then
          set_px_safe(fb, fx, py, C_ROUTE)
        end
      end
    end
  end
end

local function draw_snake(fb)
  for i = #state.snake, 1, -1 do
    local part = state.snake[i]
    local x = part.x * CELL
    local y = HUD_H + part.y * CELL
    local color = C_SNAKE_BODY
    if i == 1 then
      color = C_SNAKE_HEAD
    elseif i == #state.snake then
      color = C_SNAKE_TAIL
    end
    rect_safe(fb, x, y, CELL, CELL, color)
    rect_safe(fb, x + 1, y + 1, CELL - 2, CELL - 2, color)
    if i > 1 and i < #state.snake then
      set_px_safe(fb, x, y + 3, C_TRAIL)
      set_px_safe(fb, x + 3, y, C_TRAIL)
    end
    if i == 1 then
      if state.dir.x > 0 then
        set_px_safe(fb, x + 2, y + 1, 0xFFFF)
        set_px_safe(fb, x + 2, y + 2, 0xFFFF)
      elseif state.dir.x < 0 then
        set_px_safe(fb, x + 1, y + 1, 0xFFFF)
        set_px_safe(fb, x + 1, y + 2, 0xFFFF)
      elseif state.dir.y > 0 then
        set_px_safe(fb, x + 1, y + 2, 0xFFFF)
        set_px_safe(fb, x + 2, y + 2, 0xFFFF)
      else
        set_px_safe(fb, x + 1, y + 1, 0xFFFF)
        set_px_safe(fb, x + 2, y + 1, 0xFFFF)
      end
    end
  end
end

local function draw_board(fb)
  rect_safe(fb, 0, HUD_H, 64, 24, C_BORDER)
  for row = 0, ROWS - 1 do
    for col = 0, COLS - 1 do
      local x = col * CELL
      local y = HUD_H + row * CELL
      rect_safe(fb, x, y, CELL, CELL, ((col + row) % 2 == 0) and C_BOARD or C_BOARD_ALT)
      set_px_safe(fb, x, y, C_GRID)
    end
  end
end

local function draw_hud(fb)
  rect_safe(fb, 0, 0, 64, HUD_H, C_HUD)
  local left = state.dead and "CRASH" or "AUTO"
  local elapsed_s = math.floor(state.run_ms / 1000)
  local right = string.format("%02dS", elapsed_s)
  fb:text_box(1, 0, 34, 8, left, state.dead and C_DEAD_ACCENT or C_TEXT, FONT_UI, 8, "left", true)
  fb:text_box(36, 0, 27, 8, right, C_TEXT, FONT_UI, 8, "right", true)
  if not state.dead then
    fb:text_box(18, 0, 20, 8, string.format("%02d", #state.snake), C_MUTED, FONT_UI, 8, "center", true)
    rect_safe(fb, 0, 7, math.min(64, #state.snake * 3), 1, C_SNAKE_HEAD)
  end
end

local function draw_dead_overlay(fb)
  if not state.dead then return end
  local band = 11 + (math.floor(state.restart_ms / 120) % 2)
  rect_safe(fb, 0, band, 64, 6, C_DEAD)
  fb:text_box(0, band - 1, 64, 8, "RETRY", C_DEAD_ACCENT, FONT_UI, 8, "center", true)
end

function app.init(config)
  seed_rng()
  sys.log("auto_snake init")
  reset_run()
end

function app.tick(dt_ms)
  local dt = dt_ms or 0
  if dt < 0 then dt = 0 end

  if state.dead then
    state.restart_ms = state.restart_ms - dt
    if state.restart_ms <= 0 then
      reset_run()
    end
    return
  end

  state.run_ms = state.run_ms + dt
  state.step_accum_ms = state.step_accum_ms + dt

  local interval = step_interval_ms()
  while state.step_accum_ms >= interval and not state.dead do
    state.step_accum_ms = state.step_accum_ms - interval
    advance_snake()
    interval = step_interval_ms()
  end
end

function app.render_fb(fb)
  fb:fill(C_BG)
  draw_hud(fb)
  draw_board(fb)
  draw_food(fb)
  draw_snake(fb)
  draw_dead_overlay(fb)
end

return app
