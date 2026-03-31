local app = {}

local LAT = tonumber(data.get("aqi.lat") or data.get("openmeteo.lat")) or 22.548994
local LON = tonumber(data.get("aqi.lon") or data.get("openmeteo.lon")) or 113.459035

local font = "builtin:silkscreen_regular_8"

local C_BG = 0x0000
local C_TEXT = 0xFFFF
local C_MUTED = 0x9CF3
local C_GOOD = 0x07E0
local C_FAIR = 0xFFE0
local C_USG = 0xFD20
local C_BAD = 0xF800
local C_VBAD = 0xC81F
local C_GRID = 0x0843
local C_FRAME = 0x18C6
local C_WARN = 0xF800

local state = {
  req_id = nil,
  last_req_ms = 0,
  last_ok_ms = 0,
  err = nil,
  anim_ms = 0,
  current = nil,
  peak = nil,
  points = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
}

local function now_ms()
  return sys.now_ms()
end

local function set_px_safe(fb, x, y, c)
  if x < 0 or x >= 64 or y < 0 or y >= 32 then return end
  fb:set_px(x, y, c)
end

local function line(fb, x0, y0, x1, y1, c)
  local dx = x1 - x0
  local dy = y1 - y0
  local steps = math.max(math.abs(dx), math.abs(dy))
  if steps <= 0 then
    set_px_safe(fb, x0, y0, c)
    return
  end
  for i = 0, steps do
    local t = i / steps
    set_px_safe(fb, math.floor(x0 + dx * t + 0.5), math.floor(y0 + dy * t + 0.5), c)
  end
end

local function rect_fill(fb, x, y, w, h, c)
  for yy = 0, h - 1 do
    for xx = 0, w - 1 do
      set_px_safe(fb, x + xx, y + yy, c)
    end
  end
end

local function hline(fb, x0, x1, y, c)
  if x0 > x1 then x0, x1 = x1, x0 end
  for x = x0, x1 do set_px_safe(fb, x, y, c) end
end

local function rect_outline(fb, x, y, w, h, c)
  hline(fb, x, x + w - 1, y, c)
  hline(fb, x, x + w - 1, y + h - 1, c)
  for yy = y, y + h - 1 do
    set_px_safe(fb, x, yy, c)
    set_px_safe(fb, x + w - 1, yy, c)
  end
end

local function aqi_color(v)
  local n = tonumber(v) or 0
  if n <= 50 then return C_GOOD end
  if n <= 100 then return C_FAIR end
  if n <= 150 then return C_USG end
  if n <= 200 then return C_BAD end
  return C_VBAD
end

local function fmt_int(v)
  if v == nil then return "--" end
  return string.format("%d", math.floor((tonumber(v) or 0) + 0.5))
end

local function aqi_label(v)
  local n = tonumber(v) or 0
  if n <= 50 then return "GOOD" end
  if n <= 100 then return "FAIR" end
  if n <= 150 then return "USG" end
  if n <= 200 then return "BAD" end
  return "VBAD"
end

local function draw_chip(fb, x, y, w, txt, bg, fg)
  rect_fill(fb, x, y, w, 8, bg)
  rect_outline(fb, x, y, w, 8, C_FRAME)
  fb:text_box(x + 1, y, w - 2, 8, txt, fg, font, 8, "center", false)
end

local function draw_chip_right(fb, x, y, w, txt, bg, fg)
  rect_fill(fb, x, y, w, 8, bg)
  rect_outline(fb, x, y, w, 8, C_FRAME)
  fb:text_box(x + 2, y, w - 4, 8, txt, fg, font, 8, "right", false)
end

local function draw_chip_text(fb, x, y, w, txt, fg, align, text_y)
  fb:text_box(x + 1, text_y or y, w - 2, 8, txt, fg, font, 8, align or "center", false)
end

local function current_hour_index(hourly)
  local t = sys and sys.local_time and sys.local_time() or nil
  if t and tonumber(t.hour) ~= nil then
    local idx = tonumber(t.hour) + 1
    if hourly and hourly.us_aqi and idx >= 1 and idx <= #hourly.us_aqi then
      return idx
    end
  end
  return 1
end

local function handle_response(status, body)
  if status ~= 200 then
    state.err = "HTTP " .. tostring(status)
    return
  end

  local obj, jerr = json.decode(body)
  if not obj then
    state.err = jerr or "JSON ERR"
    return
  end
  local hourly = obj.hourly
  if type(hourly) ~= "table" or type(hourly.us_aqi) ~= "table" then
    state.err = "BAD DATA"
    return
  end

  local start_idx = current_hour_index(hourly)
  state.current = hourly.us_aqi[start_idx]
  state.peak = state.current or 0
  for i = 1, 12 do
    local idx = start_idx + (i - 1) * 2
    local val = hourly.us_aqi[idx] or state.points[i] or 0
    state.points[i] = tonumber(val) or 0
    if state.points[i] > state.peak then state.peak = state.points[i] end
  end

  state.err = nil
  state.last_ok_ms = now_ms()
end

local function start_request()
  if state.req_id then return end
  local url = string.format(
    "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=%s&longitude=%s&hourly=us_aqi&forecast_days=2&timezone=auto",
    tostring(LAT), tostring(LON)
  )
  local id, body, age_ms, err = net.cached_get(url, 30 * 60 * 1000, 7000, 8192)
  if err then
    state.err = err
    return
  end
  if body then
    handle_response(200, body)
    state.last_req_ms = now_ms()
    return
  end
  if id then
    state.req_id = id
    state.last_req_ms = now_ms()
    return
  end
  state.err = "HTTP GET FAIL"
end

function app.init()
  state.req_id = nil
  state.last_req_ms = 0
  state.last_ok_ms = 0
  state.err = nil
  state.anim_ms = 0
  start_request()
end

function app.tick(dt_ms)
  state.anim_ms = (state.anim_ms + (dt_ms or 0)) % 2400
  if state.req_id then
    local done, status, body = net.cached_poll(state.req_id)
    if done then
      state.req_id = nil
      if status == 0 then
        state.err = body or "HTTP ERR"
      else
        handle_response(status, body or "")
      end
    end
    return
  end
  local interval = state.err and 30000 or (30 * 60 * 1000)
  if now_ms() - state.last_req_ms >= interval then start_request() end
end

function app.render_fb(fb)
  fb:fill(C_BG)

  if state.err then
    fb:text_box(0, 8, 64, 8, "AQI TREND", C_WARN, font, 8, "center", true)
    fb:text_box(0, 18, 64, 8, tostring(state.err), C_MUTED, font, 8, "center", true)
    return
  end

  local current = tonumber(state.current) or 0
  local peak = tonumber(state.peak) or current
  local accent = aqi_color(current)

  for i = 0, 5 do
    local px = (math.floor(state.anim_ms / 130) + i * 11) % 64
    set_px_safe(fb, px, 1 + (i % 2), accent)
    set_px_safe(fb, px + 1, 1 + (i % 2), C_FRAME)
  end

  fb:text_box(1, 1, 14, 8, "AQI", C_TEXT, font, 8, "left", false)
  draw_chip(fb, 14, 1, 16, "24H", C_GRID, C_TEXT)
  fb:text_box(36, 1, 27, 8, fmt_int(current), accent, font, 8, "right", false)
  rect_fill(fb, 1, 9, 27, 8, accent)
  rect_outline(fb, 1, 9, 27, 8, C_FRAME)
  draw_chip_text(fb, 1, 9, 27, aqi_label(current), C_BG, "center", 8)
  rect_fill(fb, 31, 9, 32, 8, C_GRID)
  rect_outline(fb, 31, 9, 32, 8, C_FRAME)
  draw_chip_text(fb, 31, 9, 32, "PK " .. fmt_int(peak), aqi_color(peak), "right", 8)

  rect_outline(fb, 1, 17, 62, 14, C_FRAME)
  for y = 19, 29, 4 do
    for x = 2, 61 do
      if (x % 2) == 0 then set_px_safe(fb, x, y, C_GRID) end
    end
  end
  for x = 2, 57, 10 do
    line(fb, x, 18, x, 29, C_GRID)
  end

  local prev_x, prev_y = nil, nil
  local scan = math.floor(state.anim_ms / 200) % 12 + 1
  for i = 1, 12 do
    local value = tonumber(state.points[i]) or 0
    local x = 2 + (i - 1) * 5
    local bar_h = math.floor(math.min(value, 250) / 250 * 11 + 0.5)
    local top_y = 29 - bar_h
    local color = aqi_color(value)
    rect_fill(fb, x, top_y, 4, bar_h + 1, color)
    if i == scan then
      rect_fill(fb, x, top_y, 4, 1, C_TEXT)
    end
    local px = x + 1
    local py = top_y
    if prev_x then line(fb, prev_x, prev_y, px, py, C_TEXT) end
    prev_x, prev_y = px, py
    if i == 1 then
      rect_fill(fb, x, top_y - 1, 4, 1, C_TEXT)
    end
  end
  if prev_x and prev_y then
    rect_fill(fb, prev_x - 1, prev_y - 1, 3, 3, C_TEXT)
    rect_fill(fb, prev_x, prev_y, 1, 1, accent)
  end
end

return app
