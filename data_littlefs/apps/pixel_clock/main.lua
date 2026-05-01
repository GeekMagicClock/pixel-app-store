local app = {}

local FONT_UI = "builtin:silkscreen_regular_8"
local FONT_TITLE = "builtin:pressstart2p_regular_8"

local C_BG = 0x0023
local C_PANEL = 0x0866
local C_TEXT = 0xFFDF
local C_MUTED = 0x9CF3
local C_ACCENT = 0x07FF
local C_WARN = 0xFD20
local C_STAR_DIM = 0x318C
local C_STAR_MID = 0x8DD8
local C_STAR_BRIGHT = 0xFFDF
local C_STAR_BLUE_DIM = 0x224B
local C_STAR_BLUE_MID = 0x4CBF
local C_STAR_BLUE_BRIGHT = 0xA71F

local WEEKDAYS = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"}
local STARS = {
  {x = 4, y = 3, period = 7200, phase = 300, accent = false, cross = false},
  {x = 12, y = 7, period = 6100, phase = 900, accent = true, cross = false},
  {x = 30, y = 4, period = 8400, phase = 1800, accent = false, cross = true},
  {x = 58, y = 3, period = 6900, phase = 1400, accent = false, cross = false},
  {x = 61, y = 9, period = 7800, phase = 3500, accent = true, cross = false},
  {x = 6, y = 13, period = 7600, phase = 2400, accent = false, cross = true},
  {x = 56, y = 15, period = 5800, phase = 700, accent = false, cross = false},
  {x = 3, y = 21, period = 9100, phase = 4000, accent = true, cross = false},
  {x = 60, y = 22, period = 6400, phase = 1200, accent = false, cross = true},
  {x = 8, y = 26, period = 8700, phase = 5200, accent = false, cross = false},
  {x = 20, y = 24, period = 7300, phase = 1600, accent = true, cross = false},
  {x = 34, y = 26, period = 8000, phase = 2600, accent = false, cross = false},
  {x = 48, y = 24, period = 6600, phase = 4800, accent = true, cross = false},
  {x = 56, y = 27, period = 9500, phase = 6200, accent = false, cross = false},
}

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

local function twinkle_level(period_ms, phase_ms)
  local period = period_ms or 7000
  local phase = phase_ms or 0
  local t = (state.anim_ms + phase) % period
  local half = period / 2
  if t < half then
    return t / half
  end
  return 1 - ((t - half) / half)
end

local function draw_star(fb, star)
  local glow = twinkle_level(star.period, star.phase)
  local dim = star.accent and C_STAR_BLUE_DIM or C_STAR_DIM
  local mid = star.accent and C_STAR_BLUE_MID or C_STAR_MID
  local bright = star.accent and C_STAR_BLUE_BRIGHT or C_STAR_BRIGHT

  local center = dim
  if glow >= 0.72 then
    center = bright
  elseif glow >= 0.34 then
    center = mid
  end
  set_px_safe(fb, star.x, star.y, center)

  if glow >= 0.52 then
    set_px_safe(fb, star.x - 1, star.y, dim)
    set_px_safe(fb, star.x + 1, star.y, dim)
  end

  if star.cross and glow >= 0.84 then
    set_px_safe(fb, star.x, star.y - 1, mid)
    set_px_safe(fb, star.x, star.y + 1, mid)
  end
end

local function draw_bg(fb)
  fb:fill(C_BG)

  for i = 1, #STARS do
    draw_star(fb, STARS[i])
  end

  rect_safe(fb, 0, 0, 64, 1, C_PANEL)
  rect_safe(fb, 0, 31, 64, 1, C_PANEL)
end

local function draw_digit(fb, digit, x, y, scale, color)
  local pat = DIGITS[digit]
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

local function draw_time_digits(fb, hh, mm, sec)
  local s = string.format("%02d%02d", hh, mm)
  local scale = 3
  local digit_w = 3 * scale
  local digit_h = 5 * scale
  local gap = 2
  local colon_gap = 4
  local total_w = digit_w * 4 + gap * 2 + colon_gap
  local x = math.floor((64 - total_w) / 2)
  local y = 7

  draw_digit(fb, string.sub(s, 1, 1), x, y, scale, C_TEXT)
  x = x + digit_w + gap
  draw_digit(fb, string.sub(s, 2, 2), x, y, scale, C_TEXT)
  x = x + digit_w + 1

  local colon_on = (sec % 2) == 0
  local colon_color = colon_on and C_ACCENT or C_MUTED
  rect_safe(fb, x, y + 4, 2, 2, colon_color)
  rect_safe(fb, x, y + 10, 2, 2, colon_color)
  x = x + colon_gap - 1

  draw_digit(fb, string.sub(s, 3, 3), x, y, scale, C_TEXT)
  x = x + digit_w + gap
  draw_digit(fb, string.sub(s, 4, 4), x, y, scale, C_TEXT)

  if colon_on then
    rect_safe(fb, 4, 6, 2, 2, C_ACCENT)
  end

end

local function draw_seconds_bar(fb, sec)
  local x = 2
  local y = 29
  local w = 60
  rect_safe(fb, x, y, w, 2, C_PANEL)
  local fill_w = math.floor((sec / 59) * (w - 2) + 0.5)
  if sec <= 0 then
    fill_w = 0
  end
  if fill_w > 0 then
    rect_safe(fb, x + 1, y, fill_w, 2, C_ACCENT)
  end
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
  local wday = ((days + 4) % 7) + 1

  return {
    hour = math.floor(day_sec / 3600),
    min = math.floor((day_sec % 3600) / 60),
    sec = math.floor(day_sec % 60),
    wday = wday,
    year = 2024,
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

function app.init(config)
  sys.log("stars_clock init")
  state.anim_ms = 0
end

function app.tick(dt_ms)
  state.anim_ms = (state.anim_ms + (dt_ms or 0)) % 60000
end

function app.render_fb(fb)
  draw_bg(fb)

  local t = get_local_time()
  if not t then
    fb:text_box(0, 6, 64, 8, "STARS CLOCK", C_TEXT, FONT_TITLE, 8, "center", true)
    fb:text_box(0, 15, 64, 8, "--:--", C_TEXT, FONT_UI, 8, "center", true)
    fb:text_box(0, 24, 64, 8, "WAIT NTP", C_WARN, FONT_UI, 8, "center", true)
    return
  end

  local hh = tonumber(t.hour or 0) or 0
  local mm = tonumber(t.min or 0) or 0
  local ss = tonumber(t.sec or 0) or 0
  local wday = tonumber(t.wday or 1) or 1
  if wday < 1 or wday > 7 then wday = 1 end

  fb:text_box(3, 0, 28, 8, "TIME", C_MUTED, FONT_TITLE, 8, "left", true)
  fb:text_box(36, -1, 25, 8, WEEKDAYS[wday], C_MUTED, FONT_UI, 8, "right", true)

  draw_time_digits(fb, hh, mm, ss)
  draw_seconds_bar(fb, ss)
end


-- __GLOBAL_BOOT_SPLASH_WRAPPER_V1__
local __boot_now_ms = now_ms or (sys and sys.now_ms) or function() return 0 end
local __boot_started_ms = 0
local __boot_ms = tonumber(data.get("pixel_clock.boot_splash_ms") or data.get("app.boot_splash_ms") or 5000) or 5000
if __boot_ms < 0 then __boot_ms = 0 end
local __boot_name = tostring(data.get("pixel_clock.app_name") or "STARS CLOCK")

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
