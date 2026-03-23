local app = {}

local C_SKY = 0x44FF
local C_SKY_DARK = 0x2418
local C_STRIPE_A = 0x7DFF
local C_STRIPE_B = 0x03FF
local C_RING = 0xFFE0
local C_RING_DARK = 0xD580
local C_DIGIT = 0xFFFF
local C_DIGIT_SH = 0x0197
local C_GRASS = 0x07E0
local C_DIRT = 0xA145
local C_DIRT_DARK = 0x79A0
local C_DASH = 0x001F
local C_DASH_HI = 0x7DFF
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

local function draw_ring(fb, x, y)
  rect_safe(fb, x + 1, y, 3, 1, C_RING)
  rect_safe(fb, x, y + 1, 5, 3, C_RING)
  rect_safe(fb, x + 1, y + 4, 3, 1, C_RING)
  rect_safe(fb, x + 2, y + 1, 1, 3, C_SKY)
  rect_safe(fb, x + 4, y + 1, 1, 3, C_RING_DARK)
end

function app.init()
  state.anim_ms = 0
end

function app.tick(dt_ms)
  state.anim_ms = (state.anim_ms + (dt_ms or 0)) % 60000
end

function app.render_fb(fb)
  fb:fill(C_SKY)
  rect_safe(fb, 0, 0, 64, 8, C_SKY_DARK)

  local t = get_local_time()
  if not t then
    return
  end

  local sec = tonumber(t.sec or 0) or 0
  local rings = math.floor((sec / 59) * 5 + 0.5)
  for i = 0, 4 do
    if i < rings then
      draw_ring(fb, 8 + i * 10, 1)
    else
      rect_safe(fb, 9 + i * 10, 2, 3, 3, C_SKY)
      rect_safe(fb, 8 + i * 10, 3, 5, 1, C_RING_DARK)
    end
  end

  local shift = math.floor(state.anim_ms / 90) % 14
  for i = 0, 4 do
    local x = (i * 16 + 4 - shift) % 64
    rect_safe(fb, x, 10 + i * 3, 14, 2, (i % 2 == 0) and C_STRIPE_A or C_STRIPE_B)
  end

  local digits = string.format("%02d%02d", tonumber(t.hour or 0) or 0, tonumber(t.min or 0) or 0)
  draw_digit(fb, string.sub(digits, 1, 1), 5, 9, 3)
  draw_digit(fb, string.sub(digits, 2, 2), 17, 9, 3)
  rect_safe(fb, 31, 14, 2, 2, C_RING)
  rect_safe(fb, 31, 20, 2, 2, C_RING)
  draw_digit(fb, string.sub(digits, 3, 3), 38, 9, 3)
  draw_digit(fb, string.sub(digits, 4, 4), 50, 9, 3)

  rect_safe(fb, 0, 26, 64, 1, C_GRASS)
  for x = 0, 60, 4 do
    rect_safe(fb, x, 27, 4, 2, ((math.floor(x / 4) % 2) == 0) and C_DIRT or C_DIRT_DARK)
    rect_safe(fb, x, 29, 4, 3, ((math.floor(x / 4) % 2) == 0) and C_DIRT_DARK or C_DIRT)
  end
  local dash_x = 2 + math.floor((sec / 59) * 52)
  rect_safe(fb, dash_x, 24, 6, 2, C_DASH)
  rect_safe(fb, dash_x + 1, 23, 4, 1, C_DASH_HI)
end

return app
