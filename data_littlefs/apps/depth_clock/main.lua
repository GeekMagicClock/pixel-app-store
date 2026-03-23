local app = {}

local C_BG = 0x0004
local C_SKY_A = 0x08A7
local C_SKY_B = 0x0108
local C_HORIZON = 0x12B0
local C_GRID = 0x0394
local C_GRID_DIM = 0x018C
local C_TEXT = 0xBDF7
local C_TEXT_DIM = 0x6B4D
local C_SHADOW = 0x0000
local C_DIGIT_FRONT = 0x7E9F
local C_DIGIT_LIGHT = 0xEFFF
local C_DIGIT_DARK = 0x14D0
local C_DEPTH_1 = 0x0350
local C_DEPTH_2 = 0x022C
local C_DEPTH_3 = 0x0108
local C_COLON_FRONT = 0xFE58
local C_COLON_LIGHT = 0xFF7D
local C_COLON_DARK = 0xA9C5

local WEEKDAYS = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"}

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

local MICRO_GLYPHS = {
  ["A"] = {"010", "101", "111", "101", "101"},
  ["D"] = {"110", "101", "101", "101", "110"},
  ["E"] = {"111", "100", "110", "100", "111"},
  ["F"] = {"111", "100", "110", "100", "100"},
  ["H"] = {"101", "101", "111", "101", "101"},
  ["I"] = {"111", "010", "010", "010", "111"},
  ["M"] = {"101", "111", "111", "101", "101"},
  ["N"] = {"101", "111", "111", "111", "101"},
  ["O"] = {"111", "101", "101", "101", "111"},
  ["R"] = {"110", "101", "110", "101", "101"},
  ["S"] = {"111", "100", "111", "001", "111"},
  ["T"] = {"111", "010", "010", "010", "010"},
  ["U"] = {"101", "101", "101", "101", "111"},
  ["W"] = {"101", "101", "111", "111", "101"},
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
  ["-"] = {"000", "000", "111", "000", "000"},
  [":"] = {"000", "010", "000", "010", "000"},
  [" "] = {"000", "000", "000", "000", "000"},
}

local state = {
  anim_ms = 0,
}

local function set_px_safe(fb, x, y, color)
  if x < 0 or x >= 64 or y < 0 or y >= 32 then return end
  fb:set_px(x, y, color)
end

local function rect_safe(fb, x, y, w, h, color)
  if w <= 0 or h <= 0 then return end
  fb:rect(x, y, w, h, color)
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
    if t and tonumber(t.year or 0) >= 2024 then
      return t
    end
  end
  return fallback_local_time()
end

local function draw_micro_text(fb, x, y, text, color)
  local cx = x
  local s = string.upper(tostring(text or ""))
  for i = 1, #s do
    local ch = string.sub(s, i, i)
    local glyph = MICRO_GLYPHS[ch] or MICRO_GLYPHS[" "]
    for gy = 1, #glyph do
      local row = glyph[gy]
      for gx = 1, #row do
        if string.sub(row, gx, gx) == "1" then
          set_px_safe(fb, cx + gx - 1, y + gy - 1, color)
        end
      end
    end
    cx = cx + 4
  end
end

local function measure_micro_text(text)
  local len = #tostring(text or "")
  if len <= 0 then return 0 end
  return len * 4 - 1
end

local function draw_digit_layer(fb, ch, x, y, scale, color)
  local pattern = DIGITS[ch]
  if not pattern then return end
  for row = 1, #pattern do
    local row_bits = pattern[row]
    for col = 1, #row_bits do
      if string.sub(row_bits, col, col) == "1" then
        rect_safe(fb, x + (col - 1) * scale, y + (row - 1) * scale, scale, scale, color)
      end
    end
  end
end

local function draw_digit_3d(fb, ch, x, y)
  local scale = 3
  draw_digit_layer(fb, ch, x + 2, y + 2, scale, C_DEPTH_1)
  draw_digit_layer(fb, ch, x, y, scale, C_DIGIT_FRONT)
  draw_digit_layer(fb, ch, x, y, 1, C_DIGIT_LIGHT)
end

local function draw_colon(fb, x, y, sec)
  local pulse = triangle_wave(1200, 80)
  local on = (sec % 2) == 0 or pulse > 0.66
  local front = on and C_COLON_FRONT or C_DEPTH_1
  local shadow = on and C_COLON_DARK or C_DEPTH_3

  rect_safe(fb, x + 2, y + 6, 3, 3, shadow)
  rect_safe(fb, x + 2, y + 13, 3, 3, shadow)
  rect_safe(fb, x, y + 4, 3, 3, front)
  rect_safe(fb, x, y + 11, 3, 3, front)
  rect_safe(fb, x, y + 4, 3, 1, C_COLON_LIGHT)
  rect_safe(fb, x, y + 11, 3, 1, C_COLON_LIGHT)
end

local function draw_sky(fb)
  fb:fill(C_BG)
  for y = 0, 14 do
    rect_safe(fb, 0, y, 64, 1, C_SKY_A)
  end
  for y = 15, 31 do
    rect_safe(fb, 0, y, 64, 1, C_SKY_B)
  end

  local glow = 10 + math.floor(triangle_wave(2600, 300) * 14)
  rect_safe(fb, 32 - glow, 18, glow * 2, 1, C_HORIZON)
  rect_safe(fb, 16, 19, 32, 1, C_GRID_DIM)
end

local function draw_grid(fb, sec)
  local horizon_y = 19
  local horizon_x = 32

  local bottom_targets = {2, 10, 18, 26, 38, 46, 54, 62}
  for i = 1, #bottom_targets do
    local tx = bottom_targets[i]
    local topx = horizon_x + math.floor((tx - horizon_x) * 0.18)
    line_safe(fb, topx, horizon_y, tx, 31, C_GRID_DIM)
  end

  local rows = {
    {y = 21, half = 7},
    {y = 23, half = 12},
    {y = 25, half = 18},
    {y = 27, half = 24},
    {y = 29, half = 29},
    {y = 31, half = 31},
  }
  for i = 1, #rows do
    local row = rows[i]
    rect_safe(fb, horizon_x - row.half, row.y, row.half * 2, 1, i <= 2 and C_GRID or C_GRID_DIM)
  end

  local progress = math.floor((clamp(sec, 0, 59) / 59) * 23 + 0.5)
  rect_safe(fb, 9, 30, 46, 1, C_DEPTH_3)
  rect_safe(fb, 9, 30, progress, 1, C_GRID)
end

local function draw_shadow_band(fb)
  rect_safe(fb, 5, 22, 54, 3, C_SHADOW)
  rect_safe(fb, 8, 24, 48, 2, C_DEPTH_3)
end

function app.init(config)
  state.anim_ms = 0
  sys.log("depth_clock init")
end

function app.tick(dt_ms)
  state.anim_ms = (state.anim_ms + (dt_ms or 0)) % 60000
end

function app.render_fb(fb)
  draw_sky(fb)

  local t = get_local_time()
  if not t then
    draw_micro_text(fb, 20, 9, "NO TIME", C_TEXT)
    draw_micro_text(fb, 25, 18, "--:--", C_TEXT_DIM)
    return
  end

  local sec = tonumber(t.sec or 0) or 0
  draw_grid(fb, sec)
  draw_shadow_band(fb)

  local wday = clamp(tonumber(t.wday or 1) or 1, 1, 7)
  local date_text = string.format("%d-%02d", clamp(tonumber(t.month or 1) or 1, 1, 12), clamp(tonumber(t.day or 1) or 1, 1, 31))
  local wday_text = WEEKDAYS[wday] or "SUN"
  draw_micro_text(fb, 2, 2, date_text, C_TEXT_DIM)
  draw_micro_text(fb, 62 - measure_micro_text(wday_text), 2, wday_text, C_TEXT)

  local digits = string.format("%02d%02d", tonumber(t.hour or 0) or 0, tonumber(t.min or 0) or 0)
  local bob = math.floor(triangle_wave(3000, 0) * 2)
  local y = 6 + bob

  draw_digit_3d(fb, string.sub(digits, 1, 1), 4, y)
  draw_digit_3d(fb, string.sub(digits, 2, 2), 16, y)
  draw_colon(fb, 30, y, sec)
  draw_digit_3d(fb, string.sub(digits, 3, 3), 37, y)
  draw_digit_3d(fb, string.sub(digits, 4, 4), 49, y)
end

return app
