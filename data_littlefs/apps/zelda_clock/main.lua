local app = {}

local FONT_UI = "builtin:silkscreen_regular_8"

local C_BG = 0x0180
local C_PANEL = 0xA201
local C_PANEL_FILL = 0xFF17
local C_PANEL_SH = 0x58C1
local C_TEXT = 0x0000
local C_TEXT_DIM = 0x632C
local C_DIGIT = 0x03C0
local C_DIGIT_HI = 0xA7F5
local C_DIGIT_SH = 0x0140
local C_HEART = 0xF800
local C_HEART_HI = 0xFC68
local C_RUPEE = 0x07E0
local C_KEY = 0xFFE0
local C_TRI = 0xFD20

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

local function draw_digit(fb, ch, x, y, s)
  local pat = DIGITS[ch]
  if not pat then return end
  for row = 1, #pat do
    local line = pat[row]
    for col = 1, #line do
      if string.sub(line, col, col) == "1" then
        rect_safe(fb, x + (col - 1) * s + 1, y + (row - 1) * s + 1, s, s, C_DIGIT_SH)
        rect_safe(fb, x + (col - 1) * s, y + (row - 1) * s, s, s, C_DIGIT)
        rect_safe(fb, x + (col - 1) * s, y + (row - 1) * s, s, 1, C_DIGIT_HI)
      end
    end
  end
end

local function draw_heart(fb, x, y)
  rect_safe(fb, x + 1, y, 2, 2, C_HEART_HI)
  rect_safe(fb, x + 4, y, 2, 2, C_HEART_HI)
  rect_safe(fb, x, y + 1, 7, 3, C_HEART)
  rect_safe(fb, x + 1, y + 4, 5, 2, C_HEART)
  rect_safe(fb, x + 2, y + 6, 3, 1, C_HEART)
end

local function draw_rupee(fb, x, y)
  rect_safe(fb, x + 2, y, 2, 1, C_RUPEE)
  rect_safe(fb, x + 1, y + 1, 4, 1, C_RUPEE)
  rect_safe(fb, x, y + 2, 6, 2, C_RUPEE)
  rect_safe(fb, x + 1, y + 4, 4, 1, C_RUPEE)
  rect_safe(fb, x + 2, y + 5, 2, 1, C_RUPEE)
end

local function draw_key(fb, x, y)
  rect_safe(fb, x, y + 1, 3, 3, C_KEY)
  rect_safe(fb, x + 2, y + 2, 4, 1, C_KEY)
  rect_safe(fb, x + 4, y + 1, 1, 3, C_KEY)
  rect_safe(fb, x + 5, y + 3, 1, 2, C_KEY)
end

function app.init()
  state.anim_ms = 0
end

function app.tick(dt_ms)
  state.anim_ms = (state.anim_ms + (dt_ms or 0)) % 60000
end

function app.render_fb(fb)
  fb:fill(C_BG)
  rect_safe(fb, 4, 8, 56, 17, C_PANEL)
  rect_safe(fb, 6, 10, 52, 13, C_PANEL_FILL)
  rect_safe(fb, 5, 24, 54, 1, C_PANEL_SH)

  local t = get_local_time()
  if not t then
    fb:text_box(0, 10, 64, 8, "ZELDA", C_PANEL_FILL, FONT_UI, 8, "center", true)
    fb:text_box(0, 19, 64, 8, "--:--", C_PANEL_FILL, FONT_UI, 8, "center", true)
    return
  end

  draw_heart(fb, 2, 1)
  draw_heart(fb, 11, 1)
  draw_heart(fb, 20, 1)
  draw_rupee(fb, 49, 1)
  draw_key(fb, 57, 1)

  local digits = string.format("%02d%02d", tonumber(t.hour or 0) or 0, tonumber(t.min or 0) or 0)
  draw_digit(fb, string.sub(digits, 1, 1), 9, 11, 3)
  draw_digit(fb, string.sub(digits, 2, 2), 21, 11, 3)
  rect_safe(fb, 33, 16, 2, 2, C_TRI)
  rect_safe(fb, 33, 22, 2, 2, C_TRI)
  draw_digit(fb, string.sub(digits, 3, 3), 40, 11, 3)
  draw_digit(fb, string.sub(digits, 4, 4), 52, 11, 3)

  local date_text = string.format("%d-%02d", tonumber(t.month or 1) or 1, tonumber(t.day or 1) or 1)
  local wday = WEEKDAYS[tonumber(t.wday or 1) or 1] or "SUN"
  fb:text_box(2, 25, 28, 8, date_text, C_PANEL_FILL, FONT_UI, 8, "left", true)
  fb:text_box(36, 25, 26, 8, wday, C_PANEL_FILL, FONT_UI, 8, "right", true)
end


-- __GLOBAL_BOOT_SPLASH_WRAPPER_V1__
local __boot_now_ms = now_ms or (sys and sys.now_ms) or function() return 0 end
local __boot_started_ms = 0
local __boot_ms = tonumber(data.get("zelda_clock.boot_splash_ms") or data.get("app.boot_splash_ms") or 1200) or 1200
if __boot_ms < 0 then __boot_ms = 0 end
local __boot_name = tostring(data.get("zelda_clock.app_name") or "Zelda HUD Clock")

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
