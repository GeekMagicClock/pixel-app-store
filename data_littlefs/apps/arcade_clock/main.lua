local app = {}

local FONT_UI = "builtin:silkscreen_regular_8"
local FONT_TITLE = "builtin:pressstart2p_regular_8"

local C_BG = 0x0000
local C_FRAME = 0x0842
local C_TEXT = 0xFFDF
local C_SCORE = 0xFFE0
local C_ACCENT = 0x07FF
local C_RED = 0xF920
local C_BLUE = 0x34BF
local C_GHOST = 0x318C

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

local state = {
  anim_ms = 0,
}

local function set_px_safe(fb, x, y, c)
  if x < 0 or x >= 64 or y < 0 or y >= 32 then return end
  fb:set_px(x, y, c)
end

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

local function draw_bg(fb)
  fb:fill(C_BG)
  rect_safe(fb, 0, 0, 64, 1, C_FRAME)
  rect_safe(fb, 0, 31, 64, 1, C_FRAME)

  local stars = {
    {3, 4}, {11, 6}, {19, 3}, {52, 4}, {60, 7},
    {6, 18}, {57, 17}, {4, 27}, {15, 28}, {49, 28}, {60, 25},
  }
  for i = 1, #stars do
    local s = stars[i]
    local c = (i % 3 == 0 and triangle(1800, i * 70) > 0.5) and C_TEXT or C_GHOST
    set_px_safe(fb, s[1], s[2], c)
  end
end

local function draw_digit(fb, ch, x, y, scale, on, glow)
  local pat = DIGITS[ch]
  if not pat then return end
  for row = 1, #pat do
    local line = pat[row]
    for col = 1, #line do
      if string.sub(line, col, col) == "1" then
        rect_safe(fb, x + (col - 1) * scale, y + (row - 1) * scale, scale, scale, on)
        if glow then
          set_px_safe(fb, x + (col - 1) * scale, y + (row - 1) * scale, glow)
        end
      end
    end
  end
end

local function draw_time(fb, hh, mm, sec)
  local s = string.format("%02d%02d", hh, mm)
  local scale = 3
  local x = 10
  local y = 8
  draw_digit(fb, string.sub(s, 1, 1), x, y, scale, C_SCORE, C_TEXT)
  draw_digit(fb, string.sub(s, 2, 2), x + 11, y, scale, C_SCORE, C_TEXT)

  local colon_on = (sec % 2) == 0
  rect_safe(fb, x + 23, y + 4, 2, 2, colon_on and C_RED or C_GHOST)
  rect_safe(fb, x + 23, y + 10, 2, 2, colon_on and C_RED or C_GHOST)

  draw_digit(fb, string.sub(s, 3, 3), x + 28, y, scale, C_SCORE, C_TEXT)
  draw_digit(fb, string.sub(s, 4, 4), x + 39, y, scale, C_SCORE, C_TEXT)
end

function app.init(config)
  sys.log("arcade_clock init")
  state.anim_ms = 0
end

function app.tick(dt_ms)
  state.anim_ms = (state.anim_ms + (dt_ms or 0)) % 60000
end

function app.render_fb(fb)
  draw_bg(fb)

  local t = get_local_time()
  if not t then
    fb:text_box(0, 6, 64, 8, "ARCADE CLOCK", C_SCORE, FONT_TITLE, 8, "center", true)
    fb:text_box(0, 15, 64, 8, "--:--", C_TEXT, FONT_UI, 8, "center", true)
    fb:text_box(0, 24, 64, 8, "WAIT NTP", C_RED, FONT_UI, 8, "center", true)
    return
  end

  local wday = tonumber(t.wday or 1) or 1
  if wday < 1 or wday > 7 then wday = 1 end
  local month = tonumber(t.month or 1) or 1
  if month < 1 or month > 12 then month = 1 end

  fb:text_box(2, 0, 18, 8, "1UP", C_RED, FONT_TITLE, 8, "left", true)
  fb:text_box(25, 0, 14, 8, "HI", C_SCORE, FONT_TITLE, 8, "center", true)
  fb:text_box(41, 0, 21, 8, WEEKDAYS[wday], C_ACCENT, FONT_UI, 8, "right", true)

  draw_time(fb, tonumber(t.hour or 0) or 0, tonumber(t.min or 0) or 0, tonumber(t.sec or 0) or 0)

  fb:text_box(2, 24, 32, 8, "CRED 01", C_RED, FONT_UI, 8, "left", true)
  fb:text_box(34, 24, 28, 8, string.format("%s%02d", MONTHS[month], tonumber(t.day or 1) or 1), C_BLUE, FONT_UI, 8, "right", true)
end


-- __GLOBAL_BOOT_SPLASH_WRAPPER_V1__
local __boot_now_ms = now_ms or (sys and sys.now_ms) or function() return 0 end
local __boot_started_ms = 0
local __boot_ms = tonumber(data.get("arcade_clock.boot_splash_ms") or data.get("app.boot_splash_ms") or 1200) or 1200
if __boot_ms < 0 then __boot_ms = 0 end
local __boot_name = tostring(data.get("arcade_clock.app_name") or "Arcade Clock")

local function __boot_compact_text(s, limit)
  s = tostring(s or "")
  s = string.gsub(s, "%s+", " ")
  s = string.gsub(s, "^%s+", "")
  s = string.gsub(s, "%s+$", "")
  local n = tonumber(limit) or 16
  if #s > n then return string.sub(s, 1, n) end
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
