local app = {}

local FONT_UI = "builtin:silkscreen_regular_8"
local C_BG = 0x0842
local C_FRAME = 0x18C4
local C_CARD_TOP = 0x4208
local C_CARD_BOT = 0x20E6
local C_SPLIT = 0x0000
local C_TEXT = 0xFFDF
local C_TEXT_DIM = 0xB5B6
local C_ACCENT = 0xFFE0
local C_GHOST = 0x5AEB

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

local function draw_tile(fb, x, y, ch)
  rect_safe(fb, x, y, 13, 19, C_CARD_TOP)
  rect_safe(fb, x, y + 9, 13, 10, C_CARD_BOT)
  rect_safe(fb, x, y + 9, 13, 1, C_SPLIT)
  rect_safe(fb, x, y, 13, 1, C_FRAME)
  rect_safe(fb, x, y + 18, 13, 1, C_FRAME)
  rect_safe(fb, x, y, 1, 19, C_FRAME)
  rect_safe(fb, x + 12, y, 1, 19, C_FRAME)
  draw_digit(fb, ch, x + 2, y + 2, 3, C_TEXT)
end

function app.init(config)
  sys.log("flip_clock init")
end

function app.tick(dt_ms)
end

function app.render_fb(fb)
  fb:fill(C_BG)
  rect_safe(fb, 0, 0, 64, 1, C_FRAME)
  rect_safe(fb, 0, 31, 64, 1, C_FRAME)

  local t = get_local_time()
  if not t then
    fb:text_box(0, 7, 64, 8, "FLIP CLOCK", C_TEXT, FONT_UI, 8, "center", true)
    fb:text_box(0, 17, 64, 8, "--:--", C_TEXT, FONT_UI, 8, "center", true)
    return
  end

  local wday = tonumber(t.wday or 1) or 1
  if wday < 1 or wday > 7 then wday = 1 end
  local month = tonumber(t.month or 1) or 1
  if month < 1 or month > 12 then month = 1 end

  fb:text_box(2, 0, 22, 8, "PLT 2", C_TEXT_DIM, FONT_UI, 8, "left", true)
  fb:text_box(42, 0, 20, 8, WEEKDAYS[wday], C_ACCENT, FONT_UI, 8, "right", true)

  local s = string.format("%02d%02d", tonumber(t.hour or 0) or 0, tonumber(t.min or 0) or 0)
  draw_tile(fb, 3, 7, string.sub(s, 1, 1))
  draw_tile(fb, 18, 7, string.sub(s, 2, 2))
  draw_tile(fb, 33, 7, string.sub(s, 3, 3))
  draw_tile(fb, 48, 7, string.sub(s, 4, 4))

  local sec = tonumber(t.sec or 0) or 0
  rect_safe(fb, 30, 12, 3, 3, (sec % 2 == 0) and C_ACCENT or C_GHOST)
  rect_safe(fb, 30, 18, 3, 3, (sec % 2 == 0) and C_ACCENT or C_GHOST)

  fb:text_box(2, 24, 34, 8, string.format("%s %02d", MONTHS[month], tonumber(t.day or 1) or 1), C_TEXT_DIM, FONT_UI, 8, "left", true)
  rect_safe(fb, 40, 27, 20, 2, C_GHOST)
  rect_safe(fb, 41, 27, math.floor((sec / 59) * 18 + 0.5), 2, C_ACCENT)
end


-- __GLOBAL_BOOT_SPLASH_WRAPPER_V1__
local __boot_now_ms = now_ms or (sys and sys.now_ms) or function() return 0 end
local __boot_started_ms = 0
local __boot_ms = tonumber(data.get("flip_clock.boot_splash_ms") or data.get("app.boot_splash_ms") or 1200) or 1200
if __boot_ms < 0 then __boot_ms = 0 end
local __boot_name = tostring(data.get("flip_clock.app_name") or "Flip Station Clock")

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
