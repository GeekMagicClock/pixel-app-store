local app = {}

local C_BG = 0x0842
local C_FRAME = 0xBDF7
local C_FRAME_INNER = 0x5ACB
local C_PANEL = 0x10A2
local C_GRID = 0x18E3
local C_TICK = 0x7BEF
local C_TEXT = 0xFFDF
local C_TEXT_DIM = 0xBDF7
local C_TEXT_SHADOW = 0x0000
local C_HOUR = 0xFEF7
local C_MINUTE = 0x4F7F
local C_SECOND = 0xF9C6
local C_CENTER = 0xFD20
local C_ACCENT = 0xFFE0

local WEEKDAYS = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"}
local MONTHS = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"}

local FONT = {
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
  ["B"] = {"110", "101", "110", "101", "110"},
  ["C"] = {"011", "100", "100", "100", "011"},
  ["D"] = {"110", "101", "101", "101", "110"},
  ["E"] = {"111", "100", "110", "100", "111"},
  ["F"] = {"111", "100", "110", "100", "100"},
  ["G"] = {"011", "100", "101", "101", "011"},
  ["H"] = {"101", "101", "111", "101", "101"},
  ["I"] = {"111", "010", "010", "010", "111"},
  ["L"] = {"100", "100", "100", "100", "111"},
  ["M"] = {"101", "111", "111", "101", "101"},
  ["N"] = {"101", "111", "111", "111", "101"},
  ["O"] = {"111", "101", "101", "101", "111"},
  ["P"] = {"110", "101", "110", "100", "100"},
  ["R"] = {"110", "101", "110", "101", "101"},
  ["T"] = {"111", "010", "010", "010", "010"},
  ["U"] = {"101", "101", "101", "101", "111"},
  ["V"] = {"101", "101", "101", "101", "010"},
  ["W"] = {"101", "101", "111", "111", "101"},
  ["Y"] = {"101", "101", "010", "010", "010"},
  [":"] = {"000", "010", "000", "010", "000"},
  [" "] = {"000", "000", "000", "000", "000"},
}

local function set_px_safe(fb, x, y, c)
  if x < 0 or x >= 64 or y < 0 or y >= 32 then return end
  fb:set_px(x, y, c)
end

local function rect_safe(fb, x, y, w, h, c)
  if w <= 0 or h <= 0 then return end
  fb:rect(x, y, w, h, c)
end

local function line_safe(fb, x0, y0, x1, y1, c)
  local dx = math.abs(x1 - x0)
  local sx = x0 < x1 and 1 or -1
  local dy = -math.abs(y1 - y0)
  local sy = y0 < y1 and 1 or -1
  local err = dx + dy
  while true do
    set_px_safe(fb, x0, y0, c)
    if x0 == x1 and y0 == y1 then break end
    local e2 = err * 2
    if e2 >= dy then err = err + dy; x0 = x0 + sx end
    if e2 <= dx then err = err + dx; y0 = y0 + sy end
  end
end

local function draw_glyph(fb, ch, x, y, color, scale)
  local pat = FONT[ch] or FONT[" "]
  local s = scale or 1
  for row = 1, #pat do
    local line = pat[row]
    for col = 1, #line do
      if string.sub(line, col, col) == "1" then
        rect_safe(fb, x + (col - 1) * s, y + (row - 1) * s, s, s, color)
      end
    end
  end
end

local function draw_text(fb, text, x, y, color, shadow, spacing)
  local cur = x
  local gap = spacing or 1
  for i = 1, #text do
    local ch = string.sub(text, i, i)
    draw_glyph(fb, ch, cur + 1, y + 1, shadow or C_TEXT_SHADOW, 1)
    draw_glyph(fb, ch, cur, y, color, 1)
    cur = cur + 3 + gap
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

local function endpoint(cx, cy, radius, angle_deg)
  local rad = math.rad(angle_deg - 90)
  return math.floor(cx + math.cos(rad) * radius + 0.5), math.floor(cy + math.sin(rad) * radius + 0.5)
end

local function draw_hand(fb, cx, cy, angle, radius, color, width)
  local x1, y1 = endpoint(cx, cy, radius, angle)
  line_safe(fb, cx, cy, x1, y1, color)
  if width and width > 1 then
    local ox, oy = endpoint(0, 0, math.floor(width / 2), angle + 90)
    line_safe(fb, cx + ox, cy + oy, x1 + ox, y1 + oy, color)
    line_safe(fb, cx - ox, cy - oy, x1 - ox, y1 - oy, color)
  end
end

local function draw_rect_face(fb)
  rect_safe(fb, 0, 0, 64, 32, C_BG)
  rect_safe(fb, 0, 0, 64, 1, C_FRAME)
  rect_safe(fb, 0, 31, 64, 1, C_FRAME)
  rect_safe(fb, 0, 0, 1, 32, C_FRAME)
  rect_safe(fb, 63, 0, 1, 32, C_FRAME)

  rect_safe(fb, 1, 1, 62, 1, C_FRAME_INNER)
  rect_safe(fb, 1, 30, 62, 1, C_FRAME_INNER)
  rect_safe(fb, 1, 1, 1, 30, C_FRAME_INNER)
  rect_safe(fb, 62, 1, 1, 30, C_FRAME_INNER)

  rect_safe(fb, 4, 4, 56, 24, C_PANEL)
  for x = 8, 56, 8 do rect_safe(fb, x, 5, 1, 22, C_GRID) end
  for y = 8, 24, 8 do rect_safe(fb, 5, y, 54, 1, C_GRID) end
  for x = 4, 60, 14 do rect_safe(fb, x, 3, 4, 1, C_TICK) end
  for x = 4, 60, 14 do rect_safe(fb, x, 28, 4, 1, C_TICK) end
  for y = 4, 28, 8 do rect_safe(fb, 3, y, 1, 4, C_TICK) end
  for y = 4, 28, 8 do rect_safe(fb, 60, y, 1, 4, C_TICK) end
end

local function draw_info(fb, t)
  local wday = tonumber(t.wday or 1) or 1
  if wday < 1 or wday > 7 then wday = 1 end
  local month = tonumber(t.month or 1) or 1
  if month < 1 or month > 12 then month = 1 end
  draw_text(fb, WEEKDAYS[wday], 5, 5, C_TEXT_DIM, C_TEXT_SHADOW, 1)
  draw_text(fb, string.format("%02d:%02d", tonumber(t.hour or 0) or 0, tonumber(t.min or 0) or 0), 39, 5, C_TEXT, C_TEXT_SHADOW, 1)
  draw_text(fb, MONTHS[month], 5, 23, C_TEXT_DIM, C_TEXT_SHADOW, 0)
  draw_text(fb, string.format("%02d", tonumber(t.day or 1) or 1), 53, 23, C_TEXT, C_TEXT_SHADOW, 0)
  rect_safe(fb, 26, 26, 12, 1, C_FRAME_INNER)
  rect_safe(fb, 26, 26, math.floor(((tonumber(t.sec or 0) or 0) / 59) * 12 + 0.5), 1, C_ACCENT)
end

function app.init(config)
  sys.log("frame_rect_analog_clock init")
end

function app.tick(dt_ms)
end

function app.render_fb(fb)
  local t = get_local_time()
  if not t then
    rect_safe(fb, 0, 0, 64, 32, C_BG)
    draw_text(fb, "RECT", 20, 10, C_TEXT, C_TEXT_SHADOW, 1)
    draw_text(fb, "WAIT", 20, 18, C_ACCENT, C_TEXT_SHADOW, 1)
    return
  end

  draw_rect_face(fb)
  local cx = 32
  local cy = 16
  local hour = tonumber(t.hour or 0) or 0
  local minute = tonumber(t.min or 0) or 0
  local second = tonumber(t.sec or 0) or 0
  local hour_angle = ((hour % 12) + minute / 60 + second / 3600) * 30
  local minute_angle = (minute + second / 60) * 6
  local second_angle = second * 6
  draw_hand(fb, cx, cy, minute_angle, 10, C_MINUTE, 1)
  draw_hand(fb, cx, cy, hour_angle, 7, C_HOUR, 1)
  draw_hand(fb, cx, cy, second_angle, 11, C_SECOND, 1)
  rect_safe(fb, cx - 1, cy - 1, 3, 3, C_CENTER)
  draw_info(fb, t)
end


-- __GLOBAL_BOOT_SPLASH_WRAPPER_V1__
local __boot_now_ms = now_ms or (sys and sys.now_ms) or function() return 0 end
local __boot_started_ms = 0
local __boot_ms = tonumber(data.get("frame_rect_analog_clock.boot_splash_ms") or data.get("app.boot_splash_ms") or 5000) or 5000
if __boot_ms < 0 then __boot_ms = 0 end
local __boot_name = tostring(data.get("frame_rect_analog_clock.app_name") or "Frame Rect Analog Clock")

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
