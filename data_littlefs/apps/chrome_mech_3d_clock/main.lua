local app = {}

local C_BG = 0x0000
local C_CASE = 0x2104
local C_CASE_HI = 0x7BEF
local C_CASE_LO = 0x0842
local C_PANEL = 0x1082
local C_RIVET = 0xDEDB
local C_FACE = 0xE71C
local C_FACE_CORE = 0xFFFF
local C_TOP = 0xFF9F
local C_SIDE = 0x6B0E
local C_SEG_BG = 0x18C3
local C_SEG_DIM = 0x39C7
local C_TEXT = 0xDEBA
local C_TEXT_DIM = 0x94F5
local C_ACCENT = 0xFD20
local C_SHADOW = 0x0000

local WEEKDAYS = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"}

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
  ["J"] = {"001", "001", "001", "101", "010"},
  ["K"] = {"101", "101", "110", "101", "101"},
  ["L"] = {"100", "100", "100", "100", "111"},
  ["M"] = {"101", "111", "111", "101", "101"},
  ["N"] = {"101", "111", "111", "111", "101"},
  ["O"] = {"111", "101", "101", "101", "111"},
  ["P"] = {"110", "101", "110", "100", "100"},
  ["Q"] = {"111", "101", "101", "111", "001"},
  ["R"] = {"110", "101", "110", "101", "101"},
  ["S"] = {"011", "100", "111", "001", "110"},
  ["T"] = {"111", "010", "010", "010", "010"},
  ["U"] = {"101", "101", "101", "101", "111"},
  ["V"] = {"101", "101", "101", "101", "010"},
  ["W"] = {"101", "101", "111", "111", "101"},
  ["X"] = {"101", "101", "010", "101", "101"},
  ["Y"] = {"101", "101", "010", "010", "010"},
  ["Z"] = {"111", "001", "010", "100", "111"},
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

local function rect_safe(fb, x, y, w, h, c)
  if w <= 0 or h <= 0 then return end
  fb:rect(x, y, w, h, c)
end

local function draw_glyph(fb, ch, x, y, color)
  local pat = FONT[ch] or FONT[" "]
  for row = 1, #pat do
    local line = pat[row]
    for col = 1, #line do
      if string.sub(line, col, col) == "1" then
        rect_safe(fb, x + col - 1, y + row - 1, 1, 1, color)
      end
    end
  end
end

local function draw_text(fb, text, x, y, color, shadow)
  local cur = x
  for i = 1, #text do
    local ch = string.sub(text, i, i)
    if shadow then draw_glyph(fb, ch, cur + 1, y + 1, shadow) end
    draw_glyph(fb, ch, cur, y, color)
    cur = cur + 4
  end
end

local function draw_seg_h(fb, x, y, active)
  rect_safe(fb, x + 2, y + 2, 8, 2, C_SHADOW)
  rect_safe(fb, x + 1, y + 1, 8, 2, C_SIDE)
  rect_safe(fb, x, y, 8, 2, active and C_FACE or C_SEG_DIM)
  if active then rect_safe(fb, x + 1, y, 6, 1, C_TOP) end
end

local function draw_seg_v(fb, x, y, active)
  rect_safe(fb, x + 2, y + 2, 2, 6, C_SHADOW)
  rect_safe(fb, x + 1, y + 1, 2, 6, C_SIDE)
  rect_safe(fb, x, y, 2, 6, active and C_FACE or C_SEG_DIM)
  if active then rect_safe(fb, x, y + 1, 1, 4, C_TOP) end
end

local function draw_digit_3d(fb, ch, x, y)
  local seg = SEGMENTS[ch]
  if not seg then return end
  rect_safe(fb, x + 1, y + 1, 14, 20, C_SEG_BG)
  draw_seg_h(fb, x + 4, y + 1, seg[1])
  draw_seg_v(fb, x + 12, y + 3, seg[2])
  draw_seg_v(fb, x + 12, y + 11, seg[3])
  draw_seg_h(fb, x + 4, y + 17, seg[4])
  draw_seg_v(fb, x + 2, y + 11, seg[5])
  draw_seg_v(fb, x + 2, y + 3, seg[6])
  draw_seg_h(fb, x + 4, y + 9, seg[7])
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
  sys.log("chrome_mech_3d_clock init")
end

function app.tick(dt_ms)
end

function app.render_fb(fb)
  rect_safe(fb, 0, 0, 64, 32, C_BG)
  rect_safe(fb, 1, 2, 62, 28, C_CASE)
  rect_safe(fb, 2, 3, 60, 26, C_PANEL)
  rect_safe(fb, 2, 3, 60, 1, C_CASE_HI)
  rect_safe(fb, 2, 28, 60, 1, C_CASE_LO)
  rect_safe(fb, 2, 3, 1, 26, C_CASE_HI)
  rect_safe(fb, 61, 3, 1, 26, C_CASE_LO)
  rect_safe(fb, 4, 5, 2, 2, C_RIVET)
  rect_safe(fb, 58, 5, 2, 2, C_RIVET)
  rect_safe(fb, 4, 25, 2, 2, C_RIVET)
  rect_safe(fb, 58, 25, 2, 2, C_RIVET)

  local t = get_local_time()
  if not t then
    draw_text(fb, "CHROME", 18, 10, C_TEXT, C_SHADOW)
    draw_text(fb, "WAIT", 23, 18, C_ACCENT, C_SHADOW)
    return
  end

  local s = string.format("%02d%02d", tonumber(t.hour or 0) or 0, tonumber(t.min or 0) or 0)
  draw_digit_3d(fb, string.sub(s, 1, 1), 1, 6)
  draw_digit_3d(fb, string.sub(s, 2, 2), 15, 6)
  draw_digit_3d(fb, string.sub(s, 3, 3), 34, 6)
  draw_digit_3d(fb, string.sub(s, 4, 4), 48, 6)

  local sec = tonumber(t.sec or 0) or 0
  rect_safe(fb, 30, 12, 3, 3, (sec % 2 == 0) and C_ACCENT or C_SEG_DIM)
  rect_safe(fb, 30, 18, 3, 3, (sec % 2 == 0) and C_ACCENT or C_SEG_DIM)

  draw_text(fb, "IRON", 4, 1, C_TEXT_DIM, nil)
  draw_text(fb, WEEKDAYS[math.max(1, math.min(7, tonumber(t.wday or 1) or 1))], 48, 1, C_TEXT, nil)
  draw_text(fb, string.format("%02d", tonumber(t.day or 1) or 1), 53, 25, C_ACCENT, C_SHADOW)
  rect_safe(fb, 5, 27, 44, 1, C_SEG_DIM)
  rect_safe(fb, 5, 27, math.floor((sec / 59) * 44 + 0.5), 1, C_ACCENT)
end


-- __GLOBAL_BOOT_SPLASH_WRAPPER_V1__
local __boot_now_ms = now_ms or (sys and sys.now_ms) or function() return 0 end
local __boot_started_ms = 0
local __boot_ms = tonumber(data.get("chrome_mech_3d_clock.boot_splash_ms") or data.get("app.boot_splash_ms") or 5000) or 5000
if __boot_ms < 0 then __boot_ms = 0 end
local __boot_name = tostring(data.get("chrome_mech_3d_clock.app_name") or "Chrome Mech 3D Clock")

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
