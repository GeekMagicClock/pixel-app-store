local app = {}

local FONT_UI = "builtin:silkscreen_regular_8"

local C_BG = 0x0000
local C_SCAN = 0x0020
local C_GLOW = 0x05E0
local C_GLOW_BRIGHT = 0x87F1
local C_GHOST = 0x02A8
local C_DIM = 0x03A9

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
  local _, month, day = civil_from_days(days)
  return {
    month = month,
    day = day,
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
  sys.log("crt_clock init")
  state.anim_ms = 0
end

function app.tick(dt_ms)
  state.anim_ms = (state.anim_ms + (dt_ms or 0)) % 60000
end

function app.render_fb(fb)
  fb:fill(C_BG)
  for y = 1, 31, 2 do
    rect_safe(fb, 0, y, 64, 1, C_SCAN)
  end

  local sweep = 6 + math.floor((state.anim_ms / 80) % 20)
  rect_safe(fb, 1, sweep, 62, 1, C_GHOST)

  local t = get_local_time()
  if not t then
    fb:text_box(2, -1, 60, 8, "SYS> TIME", C_GLOW, FONT_UI, 8, "left", true)
    fb:text_box(0, 12, 64, 8, "--:--", C_GLOW_BRIGHT, FONT_UI, 8, "center", true)
    fb:text_box(2, 24, 60, 8, "WAIT NTP_", C_GHOST, FONT_UI, 8, "left", true)
    return
  end

  fb:text_box(2, -1, 60, 8, "SYS> TIME", C_GLOW, FONT_UI, 8, "left", true)

  local s = string.format("%02d%02d", tonumber(t.hour or 0) or 0, tonumber(t.min or 0) or 0)
  local x = 10
  local y = 8
  draw_digit(fb, string.sub(s, 1, 1), x + 1, y + 1, 3, C_GHOST)
  draw_digit(fb, string.sub(s, 2, 2), x + 12, y + 1, 3, C_GHOST)
  draw_digit(fb, string.sub(s, 3, 3), x + 30, y + 1, 3, C_GHOST)
  draw_digit(fb, string.sub(s, 4, 4), x + 42, y + 1, 3, C_GHOST)
  draw_digit(fb, string.sub(s, 1, 1), x, y, 3, C_GLOW)
  draw_digit(fb, string.sub(s, 2, 2), x + 11, y, 3, C_GLOW)
  draw_digit(fb, string.sub(s, 3, 3), x + 28, y, 3, C_GLOW)
  draw_digit(fb, string.sub(s, 4, 4), x + 39, y, 3, C_GLOW)

  local colon_on = (tonumber(t.sec or 0) or 0) % 2 == 0
  rect_safe(fb, 33, 12, 2, 2, colon_on and C_GLOW_BRIGHT or C_DIM)
  rect_safe(fb, 33, 18, 2, 2, colon_on and C_GLOW_BRIGHT or C_DIM)

  local cursor = (triangle(1000, 0) > 0.45) and "_" or " "
  fb:text_box(2, 24, 60, 8, string.format("%02d-%02d READY%s", tonumber(t.month or 1) or 1, tonumber(t.day or 1) or 1, cursor), C_DIM, FONT_UI, 8, "left", true)
end

return app
