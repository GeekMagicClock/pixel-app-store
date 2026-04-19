local app = {}

local FONT_UI = "builtin:silkscreen_regular_8"

local C_BG = 0x1800
local C_PANEL = 0x3000
local C_TEXT = 0xFFFF
local C_TEXT_DIM = 0xAD55
local C_DIGIT = 0xFFE0
local C_DIGIT_SH = 0xA200
local C_BAR_BG = 0x4208
local C_BAR_A = 0x07E0
local C_BAR_B = 0xFD20
local C_BAR_C = 0xF800
local C_KO = 0xFD20

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
  local _, _, _ = civil_from_days(days)
  return {
    hour = math.floor(day_sec / 3600),
    min = math.floor((day_sec % 3600) / 60),
    sec = math.floor(day_sec % 60),
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
      end
    end
  end
end

local function draw_bar(fb, x, y, w, fill, reverse)
  rect_safe(fb, x, y, w, 3, C_BAR_BG)
  local f = fill
  if f < 0 then f = 0 end
  if f > w then f = w end
  if reverse then
    rect_safe(fb, x + w - f, y, f, 3, C_BAR_A)
    if f > 8 then rect_safe(fb, x + w - f, y, 8, 3, C_BAR_C) end
    if f > 4 then rect_safe(fb, x + w - f + 8, y, f - 8, 3, C_BAR_B) end
  else
    rect_safe(fb, x, y, f, 3, C_BAR_A)
    if f > 8 then rect_safe(fb, x + w - 8, y, 8, 3, C_BAR_C) end
    if f > 4 then rect_safe(fb, x, y, f - 8, 3, C_BAR_B) end
  end
end

function app.init()
  state.anim_ms = 0
end

function app.tick(dt_ms)
  state.anim_ms = (state.anim_ms + (dt_ms or 0)) % 60000
end

function app.render_fb(fb)
  local Y_OFF = -2
  fb:fill(C_BG)
  rect_safe(fb, 0, 8 + Y_OFF, 64, 18, C_PANEL)

  local t = get_local_time()
  if not t then
    return
  end

  local sec = tonumber(t.sec or 0) or 0
  local fill = 20 - math.floor((sec / 59) * 16)
  draw_bar(fb, 3, 3 + Y_OFF, 22, fill, false)
  draw_bar(fb, 39, 3 + Y_OFF, 22, fill, true)
  fb:text_box(23, 0 + Y_OFF, 18, 8, "KO", C_KO, FONT_UI, 8, "center", true)

  local digits = string.format("%02d%02d", tonumber(t.hour or 0) or 0, tonumber(t.min or 0) or 0)
  draw_digit(fb, string.sub(digits, 1, 1), 5, 11 + Y_OFF, 3)
  draw_digit(fb, string.sub(digits, 2, 2), 17, 11 + Y_OFF, 3)
  rect_safe(fb, 31, 16 + Y_OFF, 2, 2, C_KO)
  rect_safe(fb, 31, 22 + Y_OFF, 2, 2, C_KO)
  draw_digit(fb, string.sub(digits, 3, 3), 38, 11 + Y_OFF, 3)
  draw_digit(fb, string.sub(digits, 4, 4), 50, 11 + Y_OFF, 3)

  fb:text_box(2, 23, 12, 8, "P1", C_TEXT_DIM, FONT_UI, 8, "left", true)
  fb:text_box(50, 23, 12, 8, "P2", C_TEXT_DIM, FONT_UI, 8, "right", true)
  fb:text_box(16, 23, 32, 8, "ROUND1", C_TEXT, FONT_UI, 8, "center", true)
end


-- __GLOBAL_BOOT_SPLASH_WRAPPER_V1__
local __boot_now_ms = now_ms or (sys and sys.now_ms) or function() return 0 end
local __boot_started_ms = 0
local __boot_ms = tonumber(data.get("fighter_clock.boot_splash_ms") or data.get("app.boot_splash_ms") or 1200) or 1200
if __boot_ms < 0 then __boot_ms = 0 end
local __boot_name = tostring(data.get("fighter_clock.app_name") or "Fighter Clock")

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
