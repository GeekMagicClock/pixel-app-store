local app = {}

local C_BG = 0xC618
local C_BOX = 0xFFFF
local C_BOX_LINE = 0x0000
local C_TEXT = 0x0000
local C_TEXT_DIM = 0x632C
local C_HP = 0x07E0
local C_HP_BG = 0x632C
local C_EXP = 0x001F
local C_EXP_TIP = 0x7DFF
local C_DIGIT = 0x0000
local C_DIGIT_SH = 0x8410
local C_MON = 0x2945
local C_MON_HI = 0x7BEF
local C_SPARK = 0xFFE0
local C_SPARK_HI = 0xFFFF
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

local MON_A = {
  "..111...",
  ".11111..",
  "1111111.",
  "11.11.11",
  ".111111.",
  "..1..1..",
  ".1....1.",
}
local MON_B = {
  "...11...",
  "..1111..",
  ".111111.",
  ".11..11.",
  "11111111",
  "..1..1..",
  ".1....1.",
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

local function triangle_wave(period_ms, phase_ms)
  local period = period_ms or 1000
  local t = (state.anim_ms + (phase_ms or 0)) % period
  local half = period / 2
  if t < half then
    return t / half
  end
  return 1 - ((t - half) / half)
end

local function cycle_progress(period_ms, phase_ms)
  local period = period_ms or 1000
  return ((state.anim_ms + (phase_ms or 0)) % period) / period
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

local function draw_mon(fb, pat, x, y, flash)
  for row = 1, #pat do
    local line = pat[row]
    for col = 1, #line do
      if string.sub(line, col, col) == "1" then
        set_px_safe(fb, x + col - 1, y + row - 1, C_MON)
      end
    end
  end
  if flash then
    rect_safe(fb, x + 2, y + 1, 2, 1, C_MON_HI)
  end
end

local function draw_attack_fx(fb, progress)
  local x = math.floor(27 + progress * 12)
  local y = 14 + math.floor((progress * 5) % 2)
  rect_safe(fb, x, y, 2, 1, C_SPARK)
  rect_safe(fb, x + 1, y - 1, 1, 3, C_SPARK_HI)
  rect_safe(fb, x - 1, y, 4, 1, C_SPARK)
end

local function draw_hp_shine(fb, x, y, width, p)
  local shine = x + math.floor(p * math.max(1, width - 2))
  rect_safe(fb, shine, y, 1, 2, C_MON_HI)
end

function app.init()
  state.anim_ms = 0
end

function app.tick(dt_ms)
  state.anim_ms = (state.anim_ms + (dt_ms or 0)) % 60000
end

function app.render_fb(fb)
  local player_bob = math.floor(triangle_wave(1400, 0) * 2)
  local enemy_bob = math.floor(triangle_wave(1100, 420) * 2)
  local attack_p = cycle_progress(1200, 0)
  local attack_on = attack_p > 0.20 and attack_p < 0.48
  local hp_shine_a = cycle_progress(1800, 250)
  local hp_shine_b = cycle_progress(1800, 980)
  fb:fill(C_BG)
  rect_safe(fb, 19, 2, 42, 9, C_BOX)
  rect_safe(fb, 20, 3, 40, 7, C_BG)
  rect_safe(fb, 8, 21, 49, 10, C_BOX)
  rect_safe(fb, 9, 22, 47, 8, C_BG)

  local t = get_local_time()
  if not t then
    return
  end

  draw_mon(fb, MON_A, 4, 3 + player_bob, attack_p < 0.10)
  draw_mon(fb, MON_B, 46, 12 + enemy_bob, attack_p > 0.55 and attack_p < 0.70)

  rect_safe(fb, 33, 5, 20, 2, C_HP_BG)
  rect_safe(fb, 33, 5, 13, 2, C_HP)
  rect_safe(fb, 11, 18, 20, 2, C_HP_BG)
  rect_safe(fb, 11, 18, 16, 2, C_HP)
  draw_hp_shine(fb, 33, 5, 13, hp_shine_a)
  draw_hp_shine(fb, 11, 18, 16, hp_shine_b)

  if attack_on then
    draw_attack_fx(fb, (attack_p - 0.20) / 0.28)
  end

  local digits = string.format("%02d%02d", tonumber(t.hour or 0) or 0, tonumber(t.min or 0) or 0)
  draw_digit(fb, string.sub(digits, 1, 1), 14, 23, 2)
  draw_digit(fb, string.sub(digits, 2, 2), 22, 23, 2)
  rect_safe(fb, 31, 26, 1, 1, C_TEXT)
  rect_safe(fb, 31, 29, 1, 1, C_TEXT)
  draw_digit(fb, string.sub(digits, 3, 3), 36, 23, 2)
  draw_digit(fb, string.sub(digits, 4, 4), 44, 23, 2)

  local sec = tonumber(t.sec or 0) or 0
  local exp = math.floor((sec / 59) * 44 + 0.5)
  rect_safe(fb, 10, 30, 44, 1, C_HP_BG)
  rect_safe(fb, 10, 30, exp, 1, C_EXP)
  if exp > 1 then
    rect_safe(fb, 9 + exp, 30, 1, 1, C_EXP_TIP)
  end
end

return app
