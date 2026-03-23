local app = {}

local FONT_UI = "builtin:silkscreen_regular_8"

local C_SKY = 0x44FF
local C_SKY_DARK = 0x2C1A
local C_CLOUD = 0xFFFF
local C_HUD = 0xFFE0
local C_TEXT = 0xFFFF
local C_TEXT_DIM = 0xBDF7
local C_BLOCK = 0xFD20
local C_BLOCK_LIGHT = 0xFFE2
local C_BLOCK_DARK = 0xB2A0
local C_BLOCK_GLOW = 0xFEA0
local C_DIGIT = 0xFFFF
local C_DIGIT_SHADOW = 0x2800
local C_GROUND = 0xA145
local C_GROUND_DARK = 0x6924
local C_GRASS = 0x07E0
local C_GRASS_DARK = 0x05A0
local C_BUSH = 0x05E0
local C_BUSH_DARK = 0x03A0
local C_PIPE = 0x07E0
local C_PIPE_DARK = 0x05A0
local C_PIPE_LIGHT = 0x4FEA
local C_COIN = 0xFFE0
local C_COIN_DARK = 0xD5A0
local C_MARIO_RED = 0xF800
local C_MARIO_BLUE = 0x01DF
local C_MARIO_SKIN = 0xFDB8
local C_MARIO_BROWN = 0x79E0
local C_WARN = 0xF920

local WEEKDAYS = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"}
local BLOCK_X = {5, 17, 37, 49}
local BLOCK_Y = 8
local BLOCK_W = 10
local BLOCK_H = 9
local HERO_X = {6, 18, 38, 50}
local HERO_STAND_Y = 20
local HERO_JUMP_MS = 880
local IDLE_BOB_MS = 340

local DIGITS = {
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
}

local MARIO_STAND = {
  "..RRR...",
  ".RRRRR..",
  ".BSSS...",
  "..SUS...",
  ".RUUUR..",
  ".UUUU...",
  "..B.B...",
  ".BB.BB..",
}

local MARIO_WALK = {
  "..RRR...",
  ".RRRRR..",
  ".BSSS...",
  "..SUS...",
  ".RUUUR..",
  ".UUUU...",
  ".B.B....",
  "BB..BB..",
}

local state = {
  anim_ms = 0,
  hero_x = HERO_X[4],
  hero_target_x = HERO_X[4],
  current_digits = "0000",
  last_minute_key = nil,
  jump_start_ms = -100000,
  hit_slot = 4,
}

local function set_px_safe(fb, x, y, c)
  if x < 0 or x >= 64 or y < 0 or y >= 32 then return end
  fb:set_px(x, y, c)
end

local function rect_safe(fb, x, y, w, h, c)
  if w <= 0 or h <= 0 then return end
  fb:rect(x, y, w, h, c)
end

local function clamp(v, lo, hi)
  if v < lo then return lo end
  if v > hi then return hi end
  return v
end

local function civil_from_days(days)
  local z = days + 719468
  local era = math.floor(z / 146097)
  local doe = z - era * 146097
  local yoe = math.floor((doe - math.floor(doe / 1460) + math.floor(doe / 36524) - math.floor(doe / 146096)) / 365)
  local y = yoe + era * 400
  local doy = doe - (365 * yoe + math.floor(yoe / 4) - math.floor(yoe / 100))
  local mp = math.floor((5 * doy + 2) / 153)
  local d = doy - math.floor((153 * mp + 2) / 5) + 1
  local m = mp + (mp < 10 and 3 or -9)
  if m <= 2 then y = y + 1 end
  return y, m, d
end

local function fallback_local_time()
  local unix = 0
  if sys and sys.unix_time then unix = tonumber(sys.unix_time()) or 0 end
  if unix < 1600000000 then return nil end
  local offset_hours = tonumber(data.get("clock.utc_offset_hours") or 8) or 8
  local local_unix = unix + math.floor(offset_hours * 3600)
  local day_sec = ((local_unix % 86400) + 86400) % 86400
  local days = math.floor(local_unix / 86400)
  local year, month, day = civil_from_days(days)
  return {
    year = year,
    month = month,
    day = day,
    hour = math.floor(day_sec / 3600),
    min = math.floor((day_sec % 3600) / 60),
    sec = math.floor(day_sec % 60),
    wday = ((days + 4) % 7) + 1,
  }
end

local function get_local_time()
  if sys and sys.local_time then
    local t = sys.local_time()
    if t and tonumber(t.year or 0) >= 2024 then return t end
  end
  return fallback_local_time()
end

local function draw_cloud(fb, x, y)
  rect_safe(fb, x + 2, y, 8, 3, C_CLOUD)
  rect_safe(fb, x, y + 2, 14, 4, C_CLOUD)
  rect_safe(fb, x + 3, y + 5, 8, 1, C_SKY_DARK)
end

local function draw_bush(fb, x, y)
  rect_safe(fb, x + 1, y + 2, 11, 3, C_BUSH)
  rect_safe(fb, x + 3, y, 6, 5, C_BUSH)
  rect_safe(fb, x, y + 4, 13, 2, C_BUSH_DARK)
end

local function draw_pipe(fb, x, y)
  rect_safe(fb, x + 1, y, 6, 2, C_PIPE_LIGHT)
  rect_safe(fb, x, y + 2, 8, 6, C_PIPE)
  rect_safe(fb, x, y + 2, 1, 6, C_PIPE_DARK)
  rect_safe(fb, x + 6, y + 2, 1, 6, C_PIPE_LIGHT)
end

local function draw_ground(fb)
  rect_safe(fb, 0, 27, 64, 1, C_GRASS)
  rect_safe(fb, 0, 28, 64, 4, C_GROUND)
  for y = 28, 31, 4 do
    rect_safe(fb, 0, y, 64, 1, C_GROUND_DARK)
  end
  for x = 0, 63, 8 do
    rect_safe(fb, x, 28, 1, 4, C_GROUND_DARK)
  end
  rect_safe(fb, 3, 27, 4, 1, C_GRASS_DARK)
  rect_safe(fb, 22, 27, 4, 1, C_GRASS_DARK)
  rect_safe(fb, 43, 27, 5, 1, C_GRASS_DARK)
end

local function draw_digit(fb, ch, x, y, scale, c)
  local pat = DIGITS[ch]
  if not pat then return end
  local s = scale or 1
  for row = 1, #pat do
    local line = pat[row]
    for col = 1, #line do
      if string.sub(line, col, col) == "1" then
        rect_safe(fb, x + (col - 1) * s, y + (row - 1) * s, s, s, c)
      end
    end
  end
end

local function draw_digit_with_shadow(fb, ch, x, y)
  draw_digit(fb, ch, x + 1, y, 1, C_DIGIT_SHADOW)
  draw_digit(fb, ch, x, y + 1, 1, C_DIGIT_SHADOW)
  draw_digit(fb, ch, x + 1, y + 1, 1, C_DIGIT_SHADOW)
  draw_digit(fb, ch, x, y, 1, C_DIGIT)
end

local function jump_progress()
  local dt = state.anim_ms - state.jump_start_ms
  if dt < 0 or dt > HERO_JUMP_MS then return nil end
  return dt / HERO_JUMP_MS
end

local function block_bump(slot)
  local p = jump_progress()
  if not p or slot ~= state.hit_slot then return 0 end
  if p < 0.42 or p > 0.74 then return 0 end
  local local_p = (p - 0.42) / 0.32
  if local_p <= 0.5 then
    return math.floor(local_p * 4 + 0.5)
  end
  return math.floor((1 - local_p) * 4 + 0.5)
end

local function draw_block(fb, slot, ch)
  local x = BLOCK_X[slot]
  local y = BLOCK_Y - block_bump(slot)
  local glow = slot == state.hit_slot and jump_progress() ~= nil
  local fill = glow and C_BLOCK_GLOW or C_BLOCK

  rect_safe(fb, x, y, BLOCK_W, BLOCK_H, fill)
  rect_safe(fb, x, y, BLOCK_W, 1, C_BLOCK_LIGHT)
  rect_safe(fb, x, y, 1, BLOCK_H, C_BLOCK_LIGHT)
  rect_safe(fb, x + BLOCK_W - 1, y, 1, BLOCK_H, C_BLOCK_DARK)
  rect_safe(fb, x, y + BLOCK_H - 1, BLOCK_W, 1, C_BLOCK_DARK)

  set_px_safe(fb, x + 2, y + 2, C_BLOCK_LIGHT)
  set_px_safe(fb, x + 7, y + 2, C_BLOCK_LIGHT)
  set_px_safe(fb, x + 2, y + 6, C_BLOCK_DARK)
  set_px_safe(fb, x + 7, y + 6, C_BLOCK_DARK)

  draw_digit_with_shadow(fb, ch, x + 3, y + 1)
end

local function draw_colon(fb, sec)
  local c = (sec % 2 == 0) and C_COIN or C_TEXT_DIM
  rect_safe(fb, 32, 12, 2, 2, C_DIGIT_SHADOW)
  rect_safe(fb, 32, 16, 2, 2, C_DIGIT_SHADOW)
  rect_safe(fb, 31, 11, 2, 2, c)
  rect_safe(fb, 31, 15, 2, 2, c)
end

local function hero_y()
  local p = jump_progress()
  if p then
    local arc = 4 * p * (1 - p)
    return HERO_STAND_Y - math.floor(arc * 11 + 0.5)
  end
  local bob = state.anim_ms % IDLE_BOB_MS
  if bob > (IDLE_BOB_MS / 2) then return HERO_STAND_Y + 1 end
  return HERO_STAND_Y
end

local function hero_sprite()
  if jump_progress() then return MARIO_STAND end
  local frame = math.floor((state.anim_ms / 220) % 2)
  if frame == 0 then return MARIO_STAND end
  return MARIO_WALK
end

local function hero_color(ch)
  if ch == "R" then return C_MARIO_RED end
  if ch == "U" then return C_MARIO_BLUE end
  if ch == "S" then return C_MARIO_SKIN end
  if ch == "B" then return C_MARIO_BROWN end
  return nil
end

local function draw_mario(fb)
  local sprite = hero_sprite()
  local x0 = math.floor(state.hero_x + 0.5)
  local y0 = hero_y()
  for row = 1, #sprite do
    local line = sprite[row]
    for col = 1, #line do
      local ch = string.sub(line, col, col)
      local c = hero_color(ch)
      if c then
        set_px_safe(fb, x0 + col - 1, y0 + row - 1, c)
      end
    end
  end
end

local function draw_coin(fb)
  local p = jump_progress()
  if not p then return end
  if p < 0.46 or p > 0.88 then return end
  local local_p = (p - 0.46) / 0.42
  local x = BLOCK_X[state.hit_slot] + 4
  local y = BLOCK_Y - 6 - math.floor(local_p * 8 + 0.5)
  rect_safe(fb, x, y, 3, 5, C_COIN)
  rect_safe(fb, x + 1, y + 1, 1, 3, C_COIN_DARK)
end

local function update_digits_from_time(t)
  local hh = tonumber(t.hour or 0) or 0
  local mm = tonumber(t.min or 0) or 0
  local digits = string.format("%02d%02d", hh, mm)
  local minute_key = hh * 60 + mm

  if state.last_minute_key == nil then
    state.last_minute_key = minute_key
    state.current_digits = digits
    return
  end

  if minute_key ~= state.last_minute_key then
    local prev = state.current_digits or digits
    local hit_slot = 4
    for i = 4, 1, -1 do
      if string.sub(prev, i, i) ~= string.sub(digits, i, i) then
        hit_slot = i
        break
      end
    end
    state.current_digits = digits
    state.last_minute_key = minute_key
    state.hit_slot = hit_slot
    state.hero_target_x = HERO_X[hit_slot]
    state.jump_start_ms = state.anim_ms
    return
  end

  state.current_digits = digits
end

function app.init(config)
  sys.log("mario_clock init")
  state.anim_ms = 0
  state.hero_x = HERO_X[4]
  state.hero_target_x = HERO_X[4]
  state.jump_start_ms = -100000
  state.hit_slot = 4

  local t = get_local_time()
  if t then
    state.current_digits = string.format("%02d%02d", tonumber(t.hour or 0) or 0, tonumber(t.min or 0) or 0)
    state.last_minute_key = (tonumber(t.hour or 0) or 0) * 60 + (tonumber(t.min or 0) or 0)
  else
    state.current_digits = "0000"
    state.last_minute_key = nil
  end
end

function app.tick(dt_ms)
  local dt = dt_ms or 0
  state.anim_ms = (state.anim_ms + dt) % 600000

  local t = get_local_time()
  if t then update_digits_from_time(t) end

  local max_step = dt * 0.03
  if max_step < 0.5 then max_step = 0.5 end
  local dx = state.hero_target_x - state.hero_x
  state.hero_x = state.hero_x + clamp(dx, -max_step, max_step)
end

function app.render_fb(fb)
  fb:fill(C_SKY)

  local cloud_shift = math.floor((state.anim_ms / 1800) % 5)
  draw_cloud(fb, 3 + cloud_shift, 4)
  draw_cloud(fb, 41 - cloud_shift, 5)

  draw_bush(fb, 1, 22)
  draw_pipe(fb, 56, 20)
  draw_ground(fb)

  local t = get_local_time()
  if not t then
    fb:text_box(0, 4, 64, 8, "MARIO CLOCK", C_HUD, FONT_UI, 8, "center", true)
    fb:text_box(0, 13, 64, 8, "--:--", C_TEXT, FONT_UI, 8, "center", true)
    fb:text_box(0, 22, 64, 8, "WAIT NTP", C_WARN, FONT_UI, 8, "center", true)
    return
  end

  local wday = tonumber(t.wday or 1) or 1
  if wday < 1 or wday > 7 then wday = 1 end
  local month = tonumber(t.month or 1) or 1
  if month < 1 or month > 12 then month = 1 end
  local day = tonumber(t.day or 1) or 1
  if day < 1 then day = 1 end
  local sec = tonumber(t.sec or 0) or 0
  local digits = state.current_digits or string.format("%02d%02d", tonumber(t.hour or 0) or 0, tonumber(t.min or 0) or 0)

  local date_txt = string.format("%d-%02d", month, day)
  fb:text_box(3, -1, 22, 8, date_txt, C_GROUND_DARK, FONT_UI, 8, "left", true)
  fb:text_box(2, -2, 22, 8, date_txt, C_HUD, FONT_UI, 8, "left", true)
  fb:text_box(42, -2, 20, 8, WEEKDAYS[wday], C_GROUND_DARK, FONT_UI, 8, "right", true)
  fb:text_box(41, -3, 21, 8, WEEKDAYS[wday], C_TEXT, FONT_UI, 8, "right", true)

  for i = 1, 4 do
    draw_block(fb, i, string.sub(digits, i, i))
  end
  draw_colon(fb, sec)
  draw_coin(fb)
  draw_mario(fb)
end

return app
