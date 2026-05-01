local app = {}

local FONT_UI = "builtin:silkscreen_regular_8"
local FONT_TITLE = "builtin:pressstart2p_regular_8"

local C_BG = 0x0001
local C_PANEL = 0x0844
local C_GLASS = 0x01C8
local C_GLASS_ALT = 0x0146
local C_FRAME = 0x10A4
local C_TEXT = 0xB7FE
local C_TEXT_BRIGHT = 0xDFFF
local C_TEXT_DIM = 0x10A4
local C_TEXT_GHOST = 0x0842
local C_WARN = 0xFD20

local WEEKDAYS = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"}
local MONTHS = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"}

local MICRO_GLYPHS = {
  ["A"] = {"010", "101", "111", "101", "101"},
  ["B"] = {"110", "101", "110", "101", "110"},
  ["C"] = {"111", "100", "100", "100", "111"},
  ["D"] = {"110", "101", "101", "101", "110"},
  ["E"] = {"111", "100", "110", "100", "111"},
  ["F"] = {"111", "100", "110", "100", "100"},
  ["G"] = {"111", "100", "101", "101", "111"},
  ["H"] = {"101", "101", "111", "101", "101"},
  ["I"] = {"111", "010", "010", "010", "111"},
  ["J"] = {"111", "001", "001", "101", "111"},
  ["L"] = {"100", "100", "100", "100", "111"},
  ["M"] = {"101", "111", "111", "101", "101"},
  ["N"] = {"101", "111", "111", "111", "101"},
  ["O"] = {"111", "101", "101", "101", "111"},
  ["P"] = {"110", "101", "110", "100", "100"},
  ["R"] = {"110", "101", "110", "101", "101"},
  ["S"] = {"111", "100", "111", "001", "111"},
  ["T"] = {"111", "010", "010", "010", "010"},
  ["U"] = {"101", "101", "101", "101", "111"},
  ["V"] = {"101", "101", "101", "101", "010"},
  ["Y"] = {"101", "101", "010", "010", "010"},
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
  [" "] = {"000", "000", "000", "000", "000"},
}

local SEGMENTS = {
  ["0"] = {true, true, true, true, true, true, false},
  ["1"] = {false, true, true, false, false, false, false},
  ["2"] = {true, true, false, true, true, false, true},
  ["3"] = {true, true, true, true, false, false, true},
  ["4"] = {false, true, true, false, false, true, true},
  ["5"] = {true, false, true, true, false, true, true},
  ["6"] = {true, false, true, true, true, true, true},
  ["7"] = {true, true, true, false, false, false, false},
  ["8"] = {true, true, true, true, true, true, true},
  ["9"] = {true, true, true, true, false, true, true},
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

local function draw_micro_text(fb, x, y, txt, color)
  local cx = x
  local s = string.upper(tostring(txt or ""))
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

local function triangle_wave(period_ms, phase_ms)
  local period = period_ms or 2400
  local phase = phase_ms or 0
  local t = (state.anim_ms + phase) % period
  local half = period / 2
  if t < half then
    return t / half
  end
  return 1 - ((t - half) / half)
end

local function panel_pulse()
  return triangle_wave(2200, 180)
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
  if m <= 2 then
    y = y + 1
  end
  return y, m, d
end

local function fallback_local_time()
  local unix = 0
  if sys and sys.unix_time then
    unix = tonumber(sys.unix_time()) or 0
  end
  if unix < 1600000000 then
    return nil
  end

  local offset_hours = tonumber(data.get("clock.utc_offset_hours") or 8) or 8
  local local_unix = unix + math.floor(offset_hours * 3600)
  local day_sec = ((local_unix % 86400) + 86400) % 86400
  local days = math.floor(local_unix / 86400)
  local year, month, day = civil_from_days(days)
  local wday = ((days + 4) % 7) + 1

  return {
    year = year,
    month = month,
    day = day,
    hour = math.floor(day_sec / 3600),
    min = math.floor((day_sec % 3600) / 60),
    sec = math.floor(day_sec % 60),
    wday = wday,
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

local function draw_bg(fb)
  fb:fill(C_BG)
  rect_safe(fb, 1, 1, 62, 30, C_PANEL)
  rect_safe(fb, 2, 2, 60, 28, C_GLASS)

  for y = 3, 29 do
    if (y % 3) == 0 then
      rect_safe(fb, 2, y, 60, 1, C_GLASS_ALT)
    end
  end

  rect_safe(fb, 0, 0, 64, 1, C_FRAME)
  rect_safe(fb, 0, 31, 64, 1, C_FRAME)
  rect_safe(fb, 0, 0, 1, 32, C_FRAME)
  rect_safe(fb, 63, 0, 1, 32, C_FRAME)

  local sweep_y = 10 + math.floor(((state.anim_ms / 90) % 14))
  rect_safe(fb, 3, sweep_y, 58, 1, panel_pulse() > 0.5 and C_TEXT_GHOST or C_GLASS_ALT)
end

local function draw_seg_h(fb, x, y, active)
  local base = active and C_TEXT or C_TEXT_GHOST
  rect_safe(fb, x, y, 8, 2, base)
  if active then
    rect_safe(fb, x + 1, y, 6, 1, C_TEXT_BRIGHT)
  else
    rect_safe(fb, x + 1, y, 6, 1, C_TEXT_DIM)
  end
end

local function draw_seg_v(fb, x, y, active)
  local base = active and C_TEXT or C_TEXT_GHOST
  rect_safe(fb, x, y, 2, 6, base)
  if active then
    rect_safe(fb, x, y + 1, 1, 4, C_TEXT_BRIGHT)
  else
    rect_safe(fb, x, y + 1, 1, 4, C_TEXT_DIM)
  end
end

local function draw_digit(fb, ch, x, y)
  local seg = SEGMENTS[ch]
  if not seg then return end
  draw_seg_h(fb, x + 2, y, seg[1])
  draw_seg_v(fb, x + 10, y + 2, seg[2])
  draw_seg_v(fb, x + 10, y + 10, seg[3])
  draw_seg_h(fb, x + 2, y + 16, seg[4])
  draw_seg_v(fb, x, y + 10, seg[5])
  draw_seg_v(fb, x, y + 2, seg[6])
  draw_seg_h(fb, x + 2, y + 8, seg[7])
end

local function draw_colon(fb, x, y, sec)
  local pulse = triangle_wave(1800, 500)
  local on = (sec % 2) == 0
  local bright = (on or pulse > 0.65) and C_TEXT_BRIGHT or C_TEXT_DIM
  local base = on and C_TEXT or C_TEXT_GHOST
  rect_safe(fb, x, y + 5, 2, 2, base)
  rect_safe(fb, x, y + 11, 2, 2, base)
  set_px_safe(fb, x, y + 5, bright)
  set_px_safe(fb, x, y + 11, bright)
end

local function draw_time_digits(fb, hh, mm, sec)
  local s = string.format("%02d%02d", hh, mm)
  local x = 5
  local y = 7
  draw_digit(fb, string.sub(s, 1, 1), x, y)
  x = x + 13
  draw_digit(fb, string.sub(s, 2, 2), x, y)
  x = x + 14
  draw_colon(fb, x, y, sec)
  x = x + 4
  draw_digit(fb, string.sub(s, 3, 3), x, y)
  x = x + 13
  draw_digit(fb, string.sub(s, 4, 4), x, y)
end

local function draw_status(fb, t)
  local wday = tonumber(t.wday or 1) or 1
  if wday < 1 or wday > 7 then wday = 1 end
  draw_micro_text(fb, 4, 2, "VFD", C_TEXT)
  rect_safe(fb, 47, 1, 13, 7, C_PANEL)
  rect_safe(fb, 47, 1, 13, 1, C_FRAME)
  draw_micro_text(fb, 49, 2, WEEKDAYS[wday], C_TEXT_GHOST)
  draw_micro_text(fb, 49, 1, WEEKDAYS[wday], C_TEXT_BRIGHT)
  rect_safe(fb, 19, 3, 2, 2, panel_pulse() > 0.42 and C_TEXT_BRIGHT or C_TEXT)
end

local function draw_footer(fb, t)
  local month = tonumber(t.month or 1) or 1
  if month < 1 or month > 12 then month = 1 end
  local day = tonumber(t.day or 1) or 1
  local sec = tonumber(t.sec or 0) or 0

  draw_micro_text(fb, 4, 24, string.format("%s %02d", MONTHS[month], day), C_TEXT)

  local bar_x = 42
  local bar_y = 28
  local bar_w = 18
  rect_safe(fb, bar_x, bar_y, bar_w, 2, C_TEXT_GHOST)
  local fill_w = math.floor((sec / 59) * (bar_w - 2) + 0.5)
  if sec <= 0 then fill_w = 0 end
  if fill_w > 0 then
    rect_safe(fb, bar_x + 1, bar_y, fill_w, 2, panel_pulse() > 0.45 and C_TEXT_BRIGHT or C_TEXT)
  end
end

function app.init(config)
  sys.log("vfd_clock init")
  state.anim_ms = 0
end

function app.tick(dt_ms)
  state.anim_ms = (state.anim_ms + (dt_ms or 0)) % 60000
end

function app.render_fb(fb)
  draw_bg(fb)

  local t = get_local_time()
  if not t then
    fb:text_box(0, 6, 64, 8, "VFD CLOCK", C_TEXT, FONT_TITLE, 8, "center", true)
    fb:text_box(0, 15, 64, 8, "--:--", C_TEXT_BRIGHT, FONT_UI, 8, "center", true)
    fb:text_box(0, 24, 64, 8, "WAIT NTP", C_WARN, FONT_UI, 8, "center", true)
    return
  end

  local hh = tonumber(t.hour or 0) or 0
  local mm = tonumber(t.min or 0) or 0
  local ss = tonumber(t.sec or 0) or 0

  draw_status(fb, t)
  draw_time_digits(fb, hh, mm, ss)
  draw_footer(fb, t)
end


-- __GLOBAL_BOOT_SPLASH_WRAPPER_V1__
local __boot_now_ms = now_ms or (sys and sys.now_ms) or function() return 0 end
local __boot_started_ms = 0
local __boot_ms = tonumber(data.get("vfd_clock.boot_splash_ms") or data.get("app.boot_splash_ms") or 5000) or 5000
if __boot_ms < 0 then __boot_ms = 0 end
local __boot_name = tostring(data.get("vfd_clock.app_name") or "VFD Clock")

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
