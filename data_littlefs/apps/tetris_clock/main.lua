local app = {}

-- Port of the falling-block timing/layout from witnessmenow/WiFi-Tetris-Clock
-- and the underlying TetrisAnimation instruction tables, adapted to this repo's
-- Framebuffer app runtime.

local FONT_UI = "builtin:silkscreen_regular_8"

local C_BG = 0x0000
local C_WHITE = 0xFFFF
local C_RED = 0xF800
local C_GREEN = 0x07E0
local C_BLUE = 0x325F
local C_YELLOW = 0xFFE0
local C_CYAN = 0x07FF
local C_MAGENTA = 0xF81F
local C_ORANGE = 0xFB00

local TETRIS_DISTANCE_BETWEEN_DIGITS = 7
local TETRIS_Y_DROP_DEFAULT = 16
local DIGIT_SCALE = 2
local DIGIT_Y_FINISH = 26
local DIGIT_X_24H = 2
local DIGIT_X_12H = -6
local LETTER_X = 56
local LETTER_TOP_Y_FINISH = 15
local LETTER_BOTTOM_Y_FINISH = 25

local TETRIS_COLORS = {
  [0] = C_RED,
  [1] = C_GREEN,
  [2] = C_BLUE,
  [3] = C_WHITE,
  [4] = C_YELLOW,
  [5] = C_CYAN,
  [6] = C_MAGENTA,
  [7] = C_ORANGE,
  [8] = C_BG,
}

local SHAPE_CELLS = {
  [0] = {
    {{0, 0}, {1, 0}, {0, -1}, {1, -1}},
    {{0, 0}, {1, 0}, {0, -1}, {1, -1}},
    {{0, 0}, {1, 0}, {0, -1}, {1, -1}},
    {{0, 0}, {1, 0}, {0, -1}, {1, -1}},
  },
  [1] = {
    {{0, 0}, {1, 0}, {0, -1}, {0, -2}},
    {{0, 0}, {0, -1}, {1, -1}, {2, -1}},
    {{1, 0}, {1, -1}, {1, -2}, {0, -2}},
    {{0, 0}, {1, 0}, {2, 0}, {2, -1}},
  },
  [2] = {
    {{0, 0}, {1, 0}, {1, -1}, {1, -2}},
    {{0, 0}, {1, 0}, {2, 0}, {0, -1}},
    {{0, 0}, {0, -1}, {0, -2}, {1, -2}},
    {{0, -1}, {1, -1}, {2, -1}, {2, 0}},
  },
  [3] = {
    {{0, 0}, {1, 0}, {2, 0}, {3, 0}},
    {{0, 0}, {0, -1}, {0, -2}, {0, -3}},
    {{0, 0}, {1, 0}, {2, 0}, {3, 0}},
    {{0, 0}, {0, -1}, {0, -2}, {0, -3}},
  },
  [4] = {
    {{1, 0}, {0, -1}, {1, -1}, {0, -2}},
    {{0, 0}, {1, 0}, {1, -1}, {2, -1}},
    {{1, 0}, {0, -1}, {1, -1}, {0, -2}},
    {{0, 0}, {1, 0}, {1, -1}, {2, -1}},
  },
  [5] = {
    {{0, 0}, {0, -1}, {1, -1}, {1, -2}},
    {{1, 0}, {2, 0}, {0, -1}, {1, -1}},
    {{0, 0}, {0, -1}, {1, -1}, {1, -2}},
    {{1, 0}, {2, 0}, {0, -1}, {1, -1}},
  },
  [6] = {
    {{0, 0}, {1, 0}, {2, 0}, {1, -1}},
    {{0, 0}, {0, -1}, {0, -2}, {1, -1}},
    {{1, 0}, {0, -1}, {1, -1}, {2, -1}},
    {{1, 0}, {0, -1}, {1, -1}, {1, -2}},
  },
  [7] = {
    {{0, 0}, {1, 0}, {0, -1}},
    {{0, 0}, {0, -1}, {1, -1}},
    {{1, 0}, {1, -1}, {0, -1}},
    {{0, 0}, {1, 0}, {1, -1}},
  },
}

local DIGIT_INSTRUCTIONS = {
  [0] = {
    {2, 5, 4, 16, 0}, {4, 7, 2, 16, 1}, {3, 4, 0, 16, 1}, {6, 6, 1, 16, 1},
    {5, 1, 4, 14, 0}, {6, 6, 0, 13, 3}, {5, 1, 4, 12, 0}, {5, 1, 0, 11, 0},
    {6, 6, 4, 10, 1}, {6, 6, 0, 9, 1}, {5, 1, 1, 8, 1}, {2, 5, 3, 8, 3},
  },
  [1] = {
    {2, 5, 4, 16, 0}, {3, 4, 4, 15, 1}, {3, 4, 5, 13, 3}, {2, 5, 4, 11, 2},
    {0, 0, 4, 8, 0},
  },
  [2] = {
    {0, 0, 4, 16, 0}, {3, 4, 0, 16, 1}, {1, 2, 1, 16, 3}, {1, 2, 1, 15, 0},
    {3, 4, 1, 12, 2}, {1, 2, 0, 12, 1}, {2, 5, 3, 12, 3}, {0, 0, 4, 10, 0},
    {3, 4, 1, 8, 0}, {2, 5, 3, 8, 3}, {1, 2, 0, 8, 1},
  },
  [3] = {
    {1, 2, 3, 16, 3}, {2, 5, 0, 16, 1}, {3, 4, 1, 15, 2}, {0, 0, 4, 14, 0},
    {3, 4, 1, 12, 2}, {1, 2, 0, 12, 1}, {3, 4, 5, 12, 3}, {2, 5, 3, 11, 0},
    {3, 4, 1, 8, 0}, {1, 2, 0, 8, 1}, {2, 5, 3, 8, 3},
  },
  [4] = {
    {0, 0, 4, 16, 0}, {0, 0, 4, 14, 0}, {3, 4, 1, 12, 0}, {1, 2, 0, 12, 1},
    {2, 5, 0, 10, 0}, {2, 5, 3, 12, 3}, {3, 4, 4, 10, 3}, {2, 5, 0, 9, 2},
    {3, 4, 5, 10, 1},
  },
  [5] = {
    {0, 0, 0, 16, 0}, {2, 5, 2, 16, 1}, {2, 5, 3, 15, 0}, {3, 4, 5, 16, 1},
    {3, 4, 1, 12, 0}, {1, 2, 0, 12, 1}, {2, 5, 3, 12, 3}, {0, 0, 0, 10, 0},
    {3, 4, 1, 8, 2}, {1, 2, 0, 8, 1}, {2, 5, 3, 8, 3},
  },
  [6] = {
    {2, 5, 0, 16, 1}, {5, 1, 2, 16, 1}, {6, 6, 0, 15, 3}, {6, 6, 4, 16, 3},
    {5, 1, 4, 14, 0}, {3, 4, 1, 12, 2}, {2, 5, 0, 13, 2}, {3, 4, 2, 11, 0},
    {0, 0, 0, 10, 0}, {3, 4, 1, 8, 0}, {1, 2, 0, 8, 1}, {2, 5, 3, 8, 3},
  },
  [7] = {
    {0, 0, 4, 16, 0}, {1, 2, 4, 14, 0}, {3, 4, 5, 13, 1}, {2, 5, 4, 11, 2},
    {3, 4, 1, 8, 2}, {2, 5, 3, 8, 3}, {1, 2, 0, 8, 1},
  },
  [8] = {
    {3, 4, 1, 16, 0}, {6, 6, 0, 16, 1}, {3, 4, 5, 16, 1}, {1, 2, 2, 15, 3},
    {4, 7, 0, 14, 0}, {1, 2, 1, 12, 3}, {6, 6, 4, 13, 1}, {2, 5, 0, 11, 1},
    {4, 7, 0, 10, 0}, {4, 7, 4, 11, 0}, {5, 1, 0, 8, 1}, {5, 1, 2, 8, 1},
    {1, 2, 4, 9, 2},
  },
  [9] = {
    {0, 0, 0, 16, 0}, {3, 4, 2, 16, 0}, {1, 2, 2, 15, 3}, {1, 2, 4, 15, 2},
    {3, 4, 1, 12, 2}, {3, 4, 5, 12, 3}, {5, 1, 0, 12, 0}, {1, 2, 2, 11, 3},
    {5, 1, 4, 9, 0}, {6, 6, 0, 10, 1}, {5, 1, 0, 8, 1}, {6, 6, 2, 8, 2},
  },
}

local LETTER_INSTRUCTIONS = {
  [string.byte("A")] = {
    {0, 2, 0, 16, 0}, {0, 1, 4, 16, 0}, {5, 5, 3, 14, 1}, {4, 2, 0, 14, 1},
    {4, 1, 4, 13, 0}, {5, 0, 0, 13, 0}, {4, 3, 4, 11, 0}, {5, 2, 0, 11, 0},
    {0, 1, 2, 10, 0},
  },
  [string.byte("M")] = {
    {1, 0, 0, 16, 0}, {2, 1, 4, 16, 0}, {1, 3, 0, 15, 2}, {2, 3, 4, 15, 2},
    {0, 5, 2, 13, 0}, {4, 6, 0, 12, 1}, {5, 2, 3, 12, 3}, {6, 2, 4, 11, 3},
    {6, 1, 0, 11, 1},
  },
  [string.byte("P")] = {
    {0, 6, 0, 16, 0}, {4, 2, 0, 14, 1}, {5, 4, 2, 13, 3}, {7, 5, 4, 12, 0},
    {6, 0, 0, 13, 1}, {4, 1, 0, 11, 0}, {7, 2, 4, 11, 2}, {3, 4, 1, 9, 0},
  },
}

local state = {
  anim_ms = 0,
  drop_step_ms = 100,
  twelve_hour = true,
  force_refresh = true,
  minute_anim_mode = "all",
  time_ready = false,
  last_time = "",
  last_ampm = "",
  show_colon = true,
  digits = nil,
  ampm_top = nil,
  ampm_bottom = nil,
}

local function rect_safe(fb, x, y, w, h, c)
  if w <= 0 or h <= 0 then return end
  fb:rect(x, y, w, h, c)
end

local function draw_block(fb, x, y, size, color)
  rect_safe(fb, x, y, size, size, color)
end

local function draw_shape(fb, blocktype, color, x_pos, y_pos, rotation, scale)
  local variants = SHAPE_CELLS[blocktype]
  if not variants then return end
  local cells = variants[(rotation % 4) + 1] or variants[1]
  if not cells then return end
  for i = 1, #cells do
    local cell = cells[i]
    draw_block(fb, x_pos + cell[1] * scale, y_pos + cell[2] * scale, scale, color)
  end
end

local function read_setting(key)
  if data and data.get then return data.get(key) end
  return nil
end

local function read_bool(key, default)
  local raw = read_setting(key)
  if raw == nil then return default end
  local value = string.lower(tostring(raw))
  if value == "1" or value == "true" or value == "yes" or value == "on" then return true end
  if value == "0" or value == "false" or value == "no" or value == "off" then return false end
  return default
end

local function read_int(key, default)
  local raw = tonumber(read_setting(key))
  if raw == nil then return default end
  raw = math.floor(raw)
  if raw <= 0 then return default end
  return raw
end

local function clamp_int(v, lo, hi)
  local n = math.floor(tonumber(v) or 0)
  if n < lo then n = lo end
  if n > hi then n = hi end
  return n
end

local function read_minute_anim_mode()
  local mode = string.lower(tostring(read_setting("tetris_clock.minute_anim_mode") or ""))
  if mode == "changed" or mode == "changed_only" or mode == "delta" then return "changed" end
  if mode == "all" or mode == "full" then return "all" end
  -- Backward compatibility with previous boolean key.
  return read_bool("tetris_clock.force_refresh", true) and "all" or "changed"
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
  local offset_hours = tonumber(read_setting("clock.utc_offset_hours") or 8) or 8
  local local_unix = unix + math.floor(offset_hours * 3600)
  local day_sec = ((local_unix % 86400) + 86400) % 86400
  local days = math.floor(local_unix / 86400)
  local y, m, d = civil_from_days(days)
  return {
    year = y,
    month = m,
    day = d,
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

local function new_item()
  return { value = -1, block_index = 1, fall_index = 0, x_shift = 0 }
end

local function new_sequence(kind, scale, count)
  local seq = { kind = kind, scale = scale, items = {} }
  for i = 1, count do
    seq.items[i] = new_item()
  end
  return seq
end

local function reset_item(item, value, x_shift)
  item.value = value
  item.block_index = 1
  item.fall_index = 0
  item.x_shift = x_shift or 0
end

local function get_instructions(kind, value)
  if value == nil or value < 0 then return nil end
  if kind == "digit" then return DIGIT_INSTRUCTIONS[value] end
  if kind == "letter" then return LETTER_INSTRUCTIONS[value] end
  return nil
end

local function digit_x_shift(index, scale)
  local x_shift = (index - 1) * TETRIS_DISTANCE_BETWEEN_DIGITS * scale
  if index >= 3 then x_shift = x_shift + (3 * scale) end
  return x_shift
end

local function queue_values(seq, values, force_refresh, x_shift_fn)
  for i = 1, #seq.items do
    local value = values[i] or -1
    local x_shift = x_shift_fn(i, seq.scale)
    local item = seq.items[i]
    if force_refresh or item.value ~= value then
      reset_item(item, value, x_shift)
    else
      item.x_shift = x_shift
    end
  end
end

local function format_time_target(t, twelve_hour)
  local hour = tonumber(t.hour or 0) or 0
  local minute = tonumber(t.min or 0) or 0
  if twelve_hour then
    local ap = hour >= 12 and "PM" or "AM"
    local hour12 = hour % 12
    if hour12 == 0 then hour12 = 12 end
    local hour_str = tostring(hour12)
    if #hour_str == 1 then hour_str = " " .. hour_str end
    return hour_str .. string.format("%02d", minute), ap
  end
  return string.format("%02d%02d", hour, minute), nil
end

local function parse_digit_chars(text)
  local values = {}
  for i = 1, #text do
    local ch = string.sub(text, i, i)
    if ch == " " then
      values[i] = -1
    else
      values[i] = tonumber(ch) or -1
    end
  end
  return values
end

local function update_targets(t)
  local full_refresh = (state.minute_anim_mode == "all")
  local digits_text, ap = format_time_target(t, state.twelve_hour)
  if digits_text ~= state.last_time then
    state.last_time = digits_text
    queue_values(state.digits, parse_digit_chars(digits_text), full_refresh, digit_x_shift)
  end

  if state.twelve_hour then
    if ap ~= state.last_ampm then
      state.last_ampm = ap
      queue_values(state.ampm_top, { string.byte(string.sub(ap, 1, 1)) }, true, function() return 0 end)
      queue_values(state.ampm_bottom, { string.byte("M") }, true, function() return 0 end)
    end
  else
    state.last_ampm = ""
    queue_values(state.ampm_top, { -1 }, false, function() return 0 end)
    queue_values(state.ampm_bottom, { -1 }, false, function() return 0 end)
  end
end

local function animated_rotation(base_rotation, fall_index, y_stop)
  local rotation = base_rotation or 0
  if rotation == 1 then
    if fall_index < math.floor(y_stop / 2) then rotation = 0 end
  elseif rotation == 2 then
    if fall_index < math.floor(y_stop / 3) then
      rotation = 0
    elseif fall_index < math.floor((y_stop / 3) * 2) then
      rotation = 1
    end
  elseif rotation == 3 then
    if fall_index < math.floor(y_stop / 4) then
      rotation = 0
    elseif fall_index < math.floor((y_stop / 4) * 2) then
      rotation = 1
    elseif fall_index < math.floor((y_stop / 4) * 3) then
      rotation = 2
    end
  end
  return rotation
end

local function step_sequence(seq)
  for i = 1, #seq.items do
    local item = seq.items[i]
    local instructions = get_instructions(seq.kind, item.value)
    if instructions and item.block_index <= #instructions then
      local current = instructions[item.block_index]
      local y_stop = current[4]
      if item.fall_index >= y_stop then
        item.fall_index = 0
        item.block_index = item.block_index + 1
      else
        item.fall_index = item.fall_index + 1
      end
    end
  end
end

local function draw_sequence(fb, seq, x, y_finish)
  local finished = true
  local scaled_y_offset = seq.scale > 1 and seq.scale or 1
  local y = y_finish - (TETRIS_Y_DROP_DEFAULT * seq.scale)

  for i = 1, #seq.items do
    local item = seq.items[i]
    local instructions = get_instructions(seq.kind, item.value)
    if instructions then
      local settled = item.block_index - 1
      if settled > #instructions then settled = #instructions end

      for idx = 1, settled do
        local instr = instructions[idx]
        draw_shape(
          fb,
          instr[1],
          TETRIS_COLORS[instr[2]] or C_WHITE,
          x + (instr[3] * seq.scale) + item.x_shift,
          y + (instr[4] * scaled_y_offset) - scaled_y_offset,
          instr[5],
          seq.scale
        )
      end

      if item.block_index <= #instructions then
        finished = false
        local current = instructions[item.block_index]
        draw_shape(
          fb,
          current[1],
          TETRIS_COLORS[current[2]] or C_WHITE,
          x + (current[3] * seq.scale) + item.x_shift,
          y + (item.fall_index * scaled_y_offset) - scaled_y_offset,
          animated_rotation(current[5], item.fall_index, current[4]),
          seq.scale
        )
      end
    end
  end

  return finished
end

local function draw_colon(fb, x, y, scale, color)
  local colon_size = 2 * scale
  local x_pos = x + (TETRIS_DISTANCE_BETWEEN_DIGITS * 2 * scale)
  rect_safe(fb, x_pos, y + (12 * scale), colon_size, colon_size, color)
  rect_safe(fb, x_pos, y + (8 * scale), colon_size, colon_size, color)
end

local function digits_origin_x()
  return state.twelve_hour and DIGIT_X_12H or DIGIT_X_24H
end

function app.init()
  state.anim_ms = 0
  local drop_ms = read_int("tetris_clock.drop_step_ms", read_int("tetris_clock.step_ms", 100))
  state.drop_step_ms = clamp_int(drop_ms, 30, 500)
  state.twelve_hour = read_bool("tetris_clock.twelve_hour", true)
  state.minute_anim_mode = read_minute_anim_mode()
  state.force_refresh = (state.minute_anim_mode == "all")
  if sys and sys.log then
    sys.log(string.format("tetris_clock settings: drop_step_ms=%d minute_anim_mode=%s twelve_hour=%s",
      state.drop_step_ms, state.minute_anim_mode, state.twelve_hour and "1" or "0"))
  end
  state.time_ready = false
  state.last_time = ""
  state.last_ampm = ""
  state.show_colon = true
  state.digits = new_sequence("digit", DIGIT_SCALE, 4)
  state.ampm_top = new_sequence("letter", 1, 1)
  state.ampm_bottom = new_sequence("letter", 1, 1)
end

function app.tick(dt_ms)
  state.anim_ms = state.anim_ms + (dt_ms or 0)
  local current_mode = read_minute_anim_mode()
  if current_mode ~= state.minute_anim_mode then
    state.minute_anim_mode = current_mode
    state.force_refresh = (current_mode == "all")
    if sys and sys.log then
      sys.log("tetris_clock minute_anim_mode changed -> " .. tostring(current_mode))
    end
  end

  local t = get_local_time()
  if not t then
    state.time_ready = false
    return
  end

  state.time_ready = true
  state.show_colon = ((tonumber(t.sec or 0) or 0) % 2) == 0
  update_targets(t)

  while state.anim_ms >= state.drop_step_ms do
    state.anim_ms = state.anim_ms - state.drop_step_ms
    step_sequence(state.digits)
    if state.twelve_hour then
      step_sequence(state.ampm_top)
      step_sequence(state.ampm_bottom)
    end
  end
end

function app.render_fb(fb)
  fb:fill(C_BG)

  if not state.time_ready then
    fb:text_box(0, 8, 64, 8, "TETRIS", C_WHITE, FONT_UI, 8, "center", true)
    fb:text_box(0, 18, 64, 8, "WAIT NTP", C_YELLOW, FONT_UI, 8, "center", true)
    return
  end

  local digit_x = digits_origin_x()
  local digit_y = DIGIT_Y_FINISH - (TETRIS_Y_DROP_DEFAULT * DIGIT_SCALE)

  draw_sequence(fb, state.digits, digit_x, DIGIT_Y_FINISH)
  if state.twelve_hour then
    draw_sequence(fb, state.ampm_bottom, LETTER_X, LETTER_BOTTOM_Y_FINISH)
    draw_sequence(fb, state.ampm_top, LETTER_X, LETTER_TOP_Y_FINISH)
  end

  if state.show_colon then
    draw_colon(fb, digit_x, digit_y, DIGIT_SCALE, C_WHITE)
  end
end


-- __GLOBAL_BOOT_SPLASH_WRAPPER_V1__
local __boot_now_ms = now_ms or (sys and sys.now_ms) or function() return 0 end
local __boot_started_ms = 0
local __boot_ms = tonumber(data.get("tetris_clock.boot_splash_ms") or data.get("app.boot_splash_ms") or 5000) or 5000
if __boot_ms < 0 then __boot_ms = 0 end
local __boot_name = tostring(data.get("tetris_clock.app_name") or "Tetris Clock")

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
