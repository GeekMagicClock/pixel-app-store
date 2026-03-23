local app = {}

local FONT_UI = "builtin:silkscreen_regular_8"

local C_BG = 0x0001
local C_GRID = 0x1082
local C_GRID_DIM = 0x0821
local C_TEXT = 0xFFFF
local C_TEXT_DIM = 0x94B2
local C_CYAN = 0x4E7F
local C_CYAN_HI = 0xB7FF
local C_CYAN_SH = 0x0450
local C_YEL = 0xFE40
local C_YEL_HI = 0xFFE0
local C_YEL_SH = 0xB3A0
local C_PUR = 0xC1DF
local C_PUR_HI = 0xF3FF
local C_PUR_SH = 0x600B
local C_RED = 0xF965
local C_RED_HI = 0xFD8E
local C_RED_SH = 0x9800
local C_BAR = 0x07E0

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

local TETROMINOES = {
  {{0,0},{1,0},{2,0},{3,0}},
  {{0,0},{0,1},{1,1},{2,1}},
  {{2,0},{0,1},{1,1},{2,1}},
  {{0,0},{1,0},{0,1},{1,1}},
  {{1,0},{2,0},{0,1},{1,1}},
  {{1,0},{0,1},{1,1},{2,1}},
  {{0,0},{1,0},{1,1},{2,1}},
}

local state = { anim_ms = 0 }

local function rect_safe(fb, x, y, w, h, c)
  if w <= 0 or h <= 0 then return end
  fb:rect(x, y, w, h, c)
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
  local _, _, _ = civil_from_days(days)
  return {
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

local function draw_block(fb, x, y, s, c, hi, sh)
  rect_safe(fb, x, y, s, s, c)
  rect_safe(fb, x, y, s, 1, hi)
  rect_safe(fb, x, y, 1, s, hi)
  rect_safe(fb, x + s - 1, y + 1, 1, s - 1, sh)
  rect_safe(fb, x + 1, y + s - 1, s - 1, 1, sh)
end

local function draw_digit(fb, ch, x, y, s, c, hi, sh)
  local pat = DIGITS[ch]
  if not pat then return end
  for row = 1, #pat do
    local line = pat[row]
    for col = 1, #line do
      if string.sub(line, col, col) == "1" then
        draw_block(fb, x + (col - 1) * s, y + (row - 1) * s, s, c, hi, sh)
      end
    end
  end
end

local function draw_piece(fb, piece, x, y, s, c, hi, sh)
  for i = 1, #piece do
    local cell = piece[i]
    draw_block(fb, x + cell[1] * s, y + cell[2] * s, s, c, hi, sh)
  end
end

function app.init()
  state.anim_ms = 0
end

function app.tick(dt_ms)
  state.anim_ms = (state.anim_ms + (dt_ms or 0)) % 60000
end

function app.render_fb(fb)
  fb:fill(C_BG)
  for x = 1, 62, 6 do
    rect_safe(fb, x, 8, 1, 20, C_GRID_DIM)
  end
  for y = 8, 28, 4 do
    rect_safe(fb, 0, y, 64, 1, C_GRID_DIM)
  end

  local t = get_local_time()
  if not t then
    fb:text_box(0, 10, 64, 8, "TETRIS", C_TEXT, FONT_UI, 8, "center", true)
    fb:text_box(0, 19, 64, 8, "--:--", C_TEXT, FONT_UI, 8, "center", true)
    return
  end

  local wday = WEEKDAYS[tonumber(t.wday or 1) or 1] or "SUN"
  fb:text_box(2, 0, 38, 8, "TETRIS", C_TEXT, FONT_UI, 8, "left", true)
  fb:text_box(42, 0, 20, 8, wday, C_TEXT_DIM, FONT_UI, 8, "right", true)

  local digits = string.format("%02d%02d", tonumber(t.hour or 0) or 0, tonumber(t.min or 0) or 0)
  draw_digit(fb, string.sub(digits, 1, 1), 4, 9, 3, C_CYAN, C_CYAN_HI, C_CYAN_SH)
  draw_digit(fb, string.sub(digits, 2, 2), 16, 9, 3, C_YEL, C_YEL_HI, C_YEL_SH)
  draw_block(fb, 30, 14, 3, C_RED, C_RED_HI, C_RED_SH)
  draw_block(fb, 30, 20, 3, C_RED, C_RED_HI, C_RED_SH)
  draw_digit(fb, string.sub(digits, 3, 3), 37, 9, 3, C_PUR, C_PUR_HI, C_PUR_SH)
  draw_digit(fb, string.sub(digits, 4, 4), 49, 9, 3, C_RED, C_RED_HI, C_RED_SH)

  local sec = tonumber(t.sec or 0) or 0
  local piece = TETROMINOES[(math.floor(sec / 9) % #TETROMINOES) + 1]
  local drop = math.floor((sec % 9) * 2)
  draw_piece(fb, piece, 53, 4 + drop, 2, C_CYAN, C_CYAN_HI, C_CYAN_SH)

  local fill = math.floor((sec / 59) * 10 + 0.5)
  for i = 0, 9 do
    local x = 2 + i * 6
    if i < fill then
      draw_block(fb, x, 29, 4, C_BAR, 0x7FEF, 0x03E0)
    else
      rect_safe(fb, x, 29, 4, 2, C_GRID)
    end
  end
end

return app
