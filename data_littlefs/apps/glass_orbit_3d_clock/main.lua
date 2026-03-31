local app = {}

local C_BG = 0x0004
local C_HALO = 0x10A9
local C_PANEL = 0x0828
local C_PANEL_EDGE = 0x4A31
local C_FACE = 0xFF9E
local C_FACE_CORE = 0xF7FE
local C_TOP = 0xC7FF
local C_SIDE = 0x2B34
local C_FLOOR = 0x114C
local C_SHADOW = 0x0001
local C_TEXT = 0xCEFC
local C_TEXT_DIM = 0x74D7
local C_ACCENT = 0xFD2D

local WEEKDAYS = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"}
local MONTHS = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"}

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
  ["A"] = {"010", "101", "111", "101", "101"},
  ["C"] = {"011", "100", "100", "100", "011"},
  ["D"] = {"110", "101", "101", "101", "110"},
  ["E"] = {"111", "100", "110", "100", "111"},
  ["F"] = {"111", "100", "110", "100", "100"},
  ["G"] = {"011", "100", "101", "101", "011"},
  ["J"] = {"001", "001", "001", "101", "010"},
  ["M"] = {"101", "111", "111", "101", "101"},
  ["N"] = {"101", "111", "111", "111", "101"},
  ["O"] = {"111", "101", "101", "101", "111"},
  ["R"] = {"110", "101", "110", "101", "101"},
  ["S"] = {"011", "100", "111", "001", "110"},
  ["T"] = {"111", "010", "010", "010", "010"},
  ["U"] = {"101", "101", "101", "101", "111"},
  ["W"] = {"101", "101", "111", "111", "101"},
  [":"] = {"0", "1", "0", "1", "0"},
  [" "] = {"000", "000", "000", "000", "000"},
}

local function rect_safe(fb, x, y, w, h, c)
  if w <= 0 or h <= 0 then return end
  fb:rect(x, y, w, h, c)
end

local function draw_glyph(fb, ch, x, y, color, scale, gap)
  local pat = DIGITS[ch] or DIGITS[" "]
  local s = scale or 1
  local g = gap or 0
  for row = 1, #pat do
    local line = pat[row]
    for col = 1, #line do
      if string.sub(line, col, col) == "1" then
        rect_safe(fb, x + (col - 1) * (s + g), y + (row - 1) * (s + g), s, s, color)
      end
    end
  end
end

local function draw_text(fb, text, x, y, color, shadow)
  local cur = x
  for i = 1, #text do
    local ch = string.sub(text, i, i)
    if shadow then draw_glyph(fb, ch, cur + 1, y + 1, shadow, 1, 0) end
    draw_glyph(fb, ch, cur, y, color, 1, 0)
    cur = cur + ((ch == ":") and 2 or 4)
  end
end

local function draw_block_digit_3d(fb, ch, x, y)
  local pat = DIGITS[ch]
  if not pat then return end
  local scale = 3
  local depth = 2
  local step = scale + 1
  for row = 1, #pat do
    local line = pat[row]
    for col = 1, #line do
      if string.sub(line, col, col) == "1" then
        local px = x + (col - 1) * step
        local py = y + (row - 1) * step
        rect_safe(fb, px + depth + 1, py + depth + 1, scale, scale, C_SHADOW)
        rect_safe(fb, px + depth, py, 1, scale, C_TOP)
        rect_safe(fb, px, py + depth, scale, 1, C_TOP)
        rect_safe(fb, px + depth, py + depth, scale, scale, C_SIDE)
        rect_safe(fb, px, py, scale, scale, C_FACE)
        rect_safe(fb, px + 1, py + 1, scale - 1, scale - 1, C_FACE_CORE)
      end
    end
  end
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

local function get_local_time()
  if sys and sys.local_time then
    local t = sys.local_time()
    if t and tonumber(t.year or 0) >= 2024 then return t end
  end
  local unix = sys and sys.unix_time and tonumber(sys.unix_time()) or 0
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

function app.init()
  sys.log("glass_orbit_3d_clock init")
end

function app.tick(dt_ms)
end

function app.render_fb(fb)
  rect_safe(fb, 0, 0, 64, 32, C_BG)
  rect_safe(fb, 2, 3, 60, 26, C_PANEL)
  rect_safe(fb, 2, 3, 60, 1, C_PANEL_EDGE)
  rect_safe(fb, 2, 28, 60, 1, C_FLOOR)
  rect_safe(fb, 3, 4, 58, 2, C_HALO)
  rect_safe(fb, 5, 26, 54, 1, C_FLOOR)

  local t = get_local_time()
  if not t then
    draw_text(fb, "GLASS", 18, 10, C_TEXT, C_SHADOW)
    draw_text(fb, "WAIT", 22, 18, C_ACCENT, C_SHADOW)
    return
  end

  local s = string.format("%02d%02d", tonumber(t.hour or 0) or 0, tonumber(t.min or 0) or 0)
  draw_block_digit_3d(fb, string.sub(s, 1, 1), 4, 8)
  draw_block_digit_3d(fb, string.sub(s, 2, 2), 17, 8)
  draw_block_digit_3d(fb, string.sub(s, 3, 3), 35, 8)
  draw_block_digit_3d(fb, string.sub(s, 4, 4), 48, 8)

  local sec = tonumber(t.sec or 0) or 0
  rect_safe(fb, 31, 13, 2, 2, (sec % 2 == 0) and C_ACCENT or C_SIDE)
  rect_safe(fb, 31, 20, 2, 2, (sec % 2 == 0) and C_ACCENT or C_SIDE)

  local wday = WEEKDAYS[math.max(1, math.min(7, tonumber(t.wday or 1) or 1))]
  local month = MONTHS[math.max(1, math.min(12, tonumber(t.month or 1) or 1))]
  draw_text(fb, wday, 4, 1, C_TEXT_DIM, nil)
  draw_text(fb, string.format("%s %02d", month, tonumber(t.day or 1) or 1), 34, 1, C_TEXT, nil)
  rect_safe(fb, 5, 30, 54, 1, C_SIDE)
  rect_safe(fb, 5, 30, math.floor((sec / 59) * 54 + 0.5), 1, C_ACCENT)
end

return app
