local app = {}

local C_BG = 0x0000
local C_DIGIT = 0x07FF
local C_COLON = C_DIGIT
local C_DIM = 0x0210

local SEG_A = 1
local SEG_B = 2
local SEG_C = 3
local SEG_D = 4
local SEG_E = 5
local SEG_F = 6
local SEG_G = 7

local DIGIT_SEGMENTS = {
  [0] = {[SEG_A] = true, [SEG_B] = true, [SEG_C] = true, [SEG_D] = true, [SEG_E] = true, [SEG_F] = true},
  [1] = {[SEG_B] = true, [SEG_C] = true},
  [2] = {[SEG_A] = true, [SEG_B] = true, [SEG_D] = true, [SEG_E] = true, [SEG_G] = true},
  [3] = {[SEG_A] = true, [SEG_B] = true, [SEG_C] = true, [SEG_D] = true, [SEG_G] = true},
  [4] = {[SEG_B] = true, [SEG_C] = true, [SEG_F] = true, [SEG_G] = true},
  [5] = {[SEG_A] = true, [SEG_C] = true, [SEG_D] = true, [SEG_F] = true, [SEG_G] = true},
  [6] = {[SEG_A] = true, [SEG_C] = true, [SEG_D] = true, [SEG_E] = true, [SEG_F] = true, [SEG_G] = true},
  [7] = {[SEG_A] = true, [SEG_B] = true, [SEG_C] = true},
  [8] = {[SEG_A] = true, [SEG_B] = true, [SEG_C] = true, [SEG_D] = true, [SEG_E] = true, [SEG_F] = true, [SEG_G] = true},
  [9] = {[SEG_A] = true, [SEG_B] = true, [SEG_C] = true, [SEG_D] = true, [SEG_F] = true, [SEG_G] = true},
}

local state = {
  now_ms = 0,
  digits = {},
  boot_epoch = 0,
}

local MORPH_MS = tonumber(data.get("morphing_clock.morph_ms") or 220) or 220

-- Geometry tuned for 64x32 panel.
local SEG_H = 6
local SEG_W = SEG_H
local DIGIT_H = SEG_H * 2 + 3
local DIGIT_Y = 8
local DIGIT_X = {2, 11, 23, 32, 44, 53} -- HH:MM:SS

local function clamp(v, lo, hi)
  if v < lo then return lo end
  if v > hi then return hi end
  return v
end

local function rect_safe(fb, x, y, w, h, c)
  if w <= 0 or h <= 0 then return end
  if x >= 64 or y >= 32 then return end
  if x + w <= 0 or y + h <= 0 then return end

  local rx = x
  local ry = y
  local rw = w
  local rh = h
  if rx < 0 then
    rw = rw + rx
    rx = 0
  end
  if ry < 0 then
    rh = rh + ry
    ry = 0
  end
  if rx + rw > 64 then rw = 64 - rx end
  if ry + rh > 32 then rh = 32 - ry end
  if rw > 0 and rh > 0 then
    fb:rect(rx, ry, rw, rh, c)
  end
end

local function set_px_safe(fb, x, y, c)
  if x < 0 or y < 0 or x >= 64 or y >= 32 then return end
  fb:set_px(x, y, c)
end

local function segment_points(seg)
  local pts = {}
  if seg == SEG_A then
    for i = 0, SEG_W - 1 do pts[#pts + 1] = {SEG_W - i, SEG_H * 2 + 2} end
  elseif seg == SEG_B then
    for i = 0, SEG_H - 1 do pts[#pts + 1] = {SEG_W + 1, SEG_H * 2 + 1 - i} end
  elseif seg == SEG_C then
    for i = 0, SEG_H - 1 do pts[#pts + 1] = {SEG_W + 1, SEG_H - i} end
  elseif seg == SEG_D then
    for i = 0, SEG_W - 1 do pts[#pts + 1] = {SEG_W - i, 0} end
  elseif seg == SEG_E then
    for i = 0, SEG_H - 1 do pts[#pts + 1] = {0, SEG_H - i} end
  elseif seg == SEG_F then
    for i = 0, SEG_H - 1 do pts[#pts + 1] = {0, SEG_H * 2 + 1 - i} end
  elseif seg == SEG_G then
    for i = 0, SEG_W - 1 do pts[#pts + 1] = {SEG_W - i, SEG_H + 1} end
  end
  return pts
end

local SEGMENT_POINTS = {
  [SEG_A] = segment_points(SEG_A),
  [SEG_B] = segment_points(SEG_B),
  [SEG_C] = segment_points(SEG_C),
  [SEG_D] = segment_points(SEG_D),
  [SEG_E] = segment_points(SEG_E),
  [SEG_F] = segment_points(SEG_F),
  [SEG_G] = segment_points(SEG_G),
}

local function draw_segment_progress(fb, ox, oy, seg, progress, color)
  local pts = SEGMENT_POINTS[seg]
  if not pts then return end
  local p = clamp(progress, 0, 1)
  local n = math.floor(#pts * p + 0.5)
  if n < 1 then return end
  if n > #pts then n = #pts end
  for i = 1, n do
    local pt = pts[i]
    -- Original HariFun segment points are authored in a bottom-up coordinate system.
    -- Our framebuffer is top-down, so flip Y inside digit bounds.
    set_px_safe(fb, ox + pt[1], oy + (DIGIT_H - 1 - pt[2]), color)
  end
end

local function draw_digit_morph(fb, x, y, from_v, to_v, progress)
  local old = DIGIT_SEGMENTS[from_v] or {}
  local new = DIGIT_SEGMENTS[to_v] or {}
  for seg = SEG_A, SEG_G do
    local old_on = old[seg] == true
    local new_on = new[seg] == true
    if old_on and new_on then
      draw_segment_progress(fb, x, y, seg, 1, C_DIGIT)
    elseif old_on and not new_on then
      draw_segment_progress(fb, x, y, seg, 1 - progress, C_DIGIT)
    elseif (not old_on) and new_on then
      draw_segment_progress(fb, x, y, seg, progress, C_DIGIT)
    end
  end
end

local function normalize_hms(h, m, s)
  local hh = tonumber(h or 0) or 0
  local mm = tonumber(m or 0) or 0
  local ss = tonumber(s or 0) or 0
  hh = ((math.floor(hh) % 24) + 24) % 24
  mm = ((math.floor(mm) % 60) + 60) % 60
  ss = ((math.floor(ss) % 60) + 60) % 60
  return hh, mm, ss
end

local function time_from_epoch(epoch)
  local sec = math.floor(epoch)
  local day_sec = ((sec % 86400) + 86400) % 86400
  local h = math.floor(day_sec / 3600)
  local m = math.floor((day_sec % 3600) / 60)
  local s = math.floor(day_sec % 60)
  return normalize_hms(h, m, s)
end

local function get_clock_hms()
  if sys and sys.local_time then
    local t = sys.local_time()
    if t then
      local h = tonumber(t.hour)
      local m = tonumber(t.min)
      local s = tonumber(t.sec)
      if h and m and s then
        return normalize_hms(h, m, s)
      end
    end
  end

  if sys and sys.unix_time then
    local u = tonumber(sys.unix_time()) or 0
    if u > 0 then
      local offset_hours = tonumber(data.get("clock.utc_offset_hours") or 8) or 8
      return time_from_epoch(u + math.floor(offset_hours * 3600))
    end
  end

  local fallback_epoch = state.boot_epoch + math.floor(state.now_ms / 1000)
  return time_from_epoch(fallback_epoch)
end

local function set_digits(h, m, s, startup)
  local vals = {
    math.floor(h / 10),
    h % 10,
    math.floor(m / 10),
    m % 10,
    math.floor(s / 10),
    s % 10,
  }
  for i = 1, 6 do
    local d = state.digits[i]
    local v = vals[i]
    if startup or not d then
      state.digits[i] = {
        value = v,
        from = v,
        to = v,
        start_ms = state.now_ms,
        anim = false,
      }
    elseif d.value ~= v then
      d.from = d.value
      d.to = v
      d.start_ms = state.now_ms
      d.anim = true
      d.value = v
    end
  end
end

local function draw_colons(fb, s)
  local on = (s % 2) == 0
  if not on then return end
  rect_safe(fb, 19, DIGIT_Y + 4, 2, 2, C_COLON)
  rect_safe(fb, 19, DIGIT_Y + 9, 2, 2, C_COLON)
  rect_safe(fb, 40, DIGIT_Y + 4, 2, 2, C_COLON)
  rect_safe(fb, 40, DIGIT_Y + 9, 2, 2, C_COLON)
end

function app.init()
  state.now_ms = 0
  state.digits = {}
  if sys and sys.unix_time then
    state.boot_epoch = tonumber(sys.unix_time()) or 0
  else
    state.boot_epoch = 0
  end
  local h, m, s = get_clock_hms()
  set_digits(h, m, s, true)
  if sys and sys.log then
    sys.log("morphing_clock init h=" .. tostring(h) .. " m=" .. tostring(m) .. " s=" .. tostring(s))
  end
end

function app.tick(dt_ms)
  state.now_ms = state.now_ms + (dt_ms or 0)
  local h, m, s = get_clock_hms()
  set_digits(h, m, s, false)
end

function app.render_fb(fb)
  fb:fill(C_BG)
  local h, m, s = get_clock_hms()

  for i = 1, 6 do
    local d = state.digits[i]
    if d then
      local p = 1
      if d.anim then
        p = (state.now_ms - d.start_ms) / MORPH_MS
        if p >= 1 then
          p = 1
          d.anim = false
          d.from = d.to
        end
      end
      draw_digit_morph(fb, DIGIT_X[i], DIGIT_Y, d.from or d.value, d.to or d.value, p)
    end
  end

  draw_colons(fb, s)
end

return app
