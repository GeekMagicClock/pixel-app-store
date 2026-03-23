local app = {}

local FONT_UI = "builtin:silkscreen_regular_8"
local FONT_TITLE = "builtin:pressstart2p_regular_8"

local C_BG = 0x0842
local C_PANEL = 0x18A4
local C_RIBBON_A = 0x07FF
local C_RIBBON_B = 0xF81F
local C_RIBBON_C = 0xFFE0
local C_TEXT = 0xFFDF
local C_TEXT_DIM = 0xB5B6
local C_GHOST = 0x52AA

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
}

local state = { anim_ms = 0 }

local function rect_safe(fb, x, y, w, h, c)
  if w <= 0 or h <= 0 then return end
  fb:rect(x, y, w, h, c)
end

local function triangle(period_ms, phase_ms)
  local t = (state.anim_ms + (phase_ms or 0)) % period_ms
  local half = period_ms / 2
  if t < half then return t / half end
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
  local _, month, day = civil_from_days(days)
  return {
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

local function draw_digit(fb, ch, x, y, scale, color)
  local pat = DIGITS[ch]
  if not pat then return end
  for row = 1, #pat do
    local line = pat[row]
    for col = 1, #line do
      if string.sub(line, col, col) == "1" then
        rect_safe(fb, x + (col - 1) * scale, y + (row - 1) * scale, scale, scale, color)
      end
    end
  end
end

function app.init(config)
  sys.log("motion_clock init")
  state.anim_ms = 0
end

function app.tick(dt_ms)
  state.anim_ms = (state.anim_ms + (dt_ms or 0)) % 60000
end

function app.render_fb(fb)
  fb:fill(C_BG)
  rect_safe(fb, 0, 0, 64, 1, C_PANEL)
  rect_safe(fb, 0, 31, 64, 1, C_PANEL)

  for i = 0, 4 do
    local x = (i * 14 + math.floor((triangle(2600, i * 300) * 10))) % 64
    rect_safe(fb, x - 10, 5 + i * 5, 18, 3, (i % 3 == 0) and C_RIBBON_A or ((i % 3 == 1) and C_RIBBON_B or C_RIBBON_C))
  end

  local t = get_local_time()
  if not t then
    fb:text_box(0, 6, 64, 8, "MOTION CLOCK", C_TEXT, FONT_TITLE, 8, "center", true)
    fb:text_box(0, 15, 64, 8, "--:--", C_TEXT, FONT_UI, 8, "center", true)
    return
  end

  local wday = tonumber(t.wday or 1) or 1
  if wday < 1 or wday > 7 then wday = 1 end
  local month = tonumber(t.month or 1) or 1
  if month < 1 or month > 12 then month = 1 end

  fb:text_box(2, 0, 34, 8, "MOTION", C_TEXT, FONT_TITLE, 8, "left", true)
  fb:text_box(42, 0, 20, 8, WEEKDAYS[wday], C_TEXT_DIM, FONT_UI, 8, "right", true)

  local s = string.format("%02d%02d", tonumber(t.hour or 0) or 0, tonumber(t.min or 0) or 0)
  draw_digit(fb, string.sub(s, 1, 1), 10, 8, 3, C_TEXT)
  draw_digit(fb, string.sub(s, 2, 2), 21, 8, 3, C_TEXT)
  rect_safe(fb, 33, 12, 2, 2, triangle(1000, 0) > 0.4 and C_RIBBON_C or C_GHOST)
  rect_safe(fb, 33, 18, 2, 2, triangle(1000, 0) > 0.4 and C_RIBBON_C or C_GHOST)
  draw_digit(fb, string.sub(s, 3, 3), 38, 8, 3, C_TEXT)
  draw_digit(fb, string.sub(s, 4, 4), 49, 8, 3, C_TEXT)

  local sec = tonumber(t.sec or 0) or 0
  for i = 0, 7 do
    local h = 1 + math.floor(triangle(1200 + i * 90, i * 120) * 5)
    rect_safe(fb, 4 + i * 7, 29 - h, 4, h, (i % 2 == 0) and C_RIBBON_A or C_RIBBON_B)
  end
  fb:text_box(2, 23, 30, 8, string.format("%s %02d", MONTHS[month], tonumber(t.day or 1) or 1), C_TEXT_DIM, FONT_UI, 8, "left", true)
  fb:text_box(38, 23, 24, 8, string.format("%02dS", sec), C_TEXT_DIM, FONT_UI, 8, "right", true)
end

return app
