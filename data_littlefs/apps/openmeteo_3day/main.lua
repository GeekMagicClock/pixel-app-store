local app = {}

local LAT = tonumber(data.get("openmeteo.lat")) or 37.5665
local LON = tonumber(data.get("openmeteo.lon")) or 126.9780
local TEMP_UNIT = data.get("openmeteo.temp_unit") or "celsius" -- fahrenheit/celsius

local font = "builtin:silkscreen_regular_8"

local C_BG = 0x0000
local C_TEXT = 0xFFFF
local C_MUTED = 0x9CF3
local C_CYAN = 0x07FF
local C_HOT = 0xF800
local C_SUN = 0xFD20
local C_CLOUD = 0xC638
local C_RAIN = 0x051F
local C_SNOW = 0xE73C
local C_STORM = 0xFFE0
local C_WARN = 0xFD20

local state = {
  req_id = nil,
  last_req_ms = 0,
  last_ok_ms = 0,
  err = nil,
  days = {
    {label = "---", code = nil, temp = nil, icon = "03d"},
    {label = "---", code = nil, temp = nil, icon = "03d"},
    {label = "---", code = nil, temp = nil, icon = "03d"},
  },
}

local function now_ms()
  return sys.now_ms()
end

local function set_px_safe(fb, x, y, c)
  if x < 0 or x >= 64 or y < 0 or y >= 32 then return end
  fb:set_px(x, y, c)
end

local function hline(fb, x0, x1, y, c)
  if x0 > x1 then x0, x1 = x1, x0 end
  for x = x0, x1 do set_px_safe(fb, x, y, c) end
end

local function vline(fb, x, y0, y1, c)
  if y0 > y1 then y0, y1 = y1, y0 end
  for y = y0, y1 do set_px_safe(fb, x, y, c) end
end

local function fill_circle(fb, cx, cy, r, c)
  for dy = -r, r do
    for dx = -r, r do
      if dx * dx + dy * dy <= r * r then
        set_px_safe(fb, cx + dx, cy + dy, c)
      end
    end
  end
end

-- Open-Meteo weather_code -> OWM icon key.
local function code_to_owm_icon(code)
  local c = tonumber(code) or -1
  if c == 0 then return "01d" end
  if c == 1 or c == 2 then return "02d" end
  if c == 3 then return "03d" end
  if c == 45 or c == 48 then return "50d" end
  if c == 51 or c == 53 or c == 55 or c == 56 or c == 57 then return "09d" end
  if c == 61 or c == 63 or c == 65 or c == 66 or c == 67 then return "10d" end
  if c == 71 or c == 73 or c == 75 or c == 77 or c == 85 or c == 86 then return "13d" end
  if c == 80 or c == 81 or c == 82 then return "09d" end
  if c == 95 or c == 96 or c == 99 then return "11d" end
  return "03d"
end

-- Zeller-based weekday from YYYY-MM-DD, returns MON/TUE/...
local function weekday_label(ymd)
  local y, m, d = string.match(ymd or "", "^(%d%d%d%d)%-(%d%d)%-(%d%d)$")
  y = tonumber(y)
  m = tonumber(m)
  d = tonumber(d)
  if not y or not m or not d then return "---" end
  if m < 3 then
    m = m + 12
    y = y - 1
  end
  local K = y % 100
  local J = math.floor(y / 100)
  local h = (d + math.floor((13 * (m + 1)) / 5) + K + math.floor(K / 4) + math.floor(J / 4) + 5 * J) % 7
  local zeller = {"SAT", "SUN", "MON", "TUE", "WED", "THU", "FRI"}
  return zeller[h + 1] or "---"
end

local function draw_cloud(fb, cx, cy)
  fill_circle(fb, cx - 3, cy + 1, 2, C_CLOUD)
  fill_circle(fb, cx, cy, 3, C_CLOUD)
  fill_circle(fb, cx + 3, cy + 1, 2, C_CLOUD)
  hline(fb, cx - 5, cx + 5, cy + 3, C_CLOUD)
  hline(fb, cx - 4, cx + 4, cy + 4, C_CLOUD)
end

local function draw_sun(fb, cx, cy)
  fill_circle(fb, cx, cy, 3, C_SUN)
  set_px_safe(fb, cx, cy - 5, C_SUN)
  set_px_safe(fb, cx, cy + 5, C_SUN)
  set_px_safe(fb, cx - 5, cy, C_SUN)
  set_px_safe(fb, cx + 5, cy, C_SUN)
  set_px_safe(fb, cx - 4, cy - 4, C_SUN)
  set_px_safe(fb, cx + 4, cy - 4, C_SUN)
  set_px_safe(fb, cx - 4, cy + 4, C_SUN)
  set_px_safe(fb, cx + 4, cy + 4, C_SUN)
end

local function draw_rain(fb, cx, cy)
  vline(fb, cx - 3, cy + 4, cy + 6, C_RAIN)
  vline(fb, cx, cy + 5, cy + 7, C_RAIN)
  vline(fb, cx + 3, cy + 4, cy + 6, C_RAIN)
end

local function draw_snow(fb, cx, cy)
  -- Extra-large single snowflake.
  local sx = cx
  local sy = cy + 1
  -- center core (thicker)
  set_px_safe(fb, sx, sy, C_SNOW)
  set_px_safe(fb, sx - 1, sy, C_SNOW)
  set_px_safe(fb, sx + 1, sy, C_SNOW)
  set_px_safe(fb, sx, sy - 1, C_SNOW)
  set_px_safe(fb, sx, sy + 1, C_SNOW)
  -- long cross arms
  set_px_safe(fb, sx - 3, sy, C_SNOW)
  set_px_safe(fb, sx - 2, sy, C_SNOW)
  set_px_safe(fb, sx + 2, sy, C_SNOW)
  set_px_safe(fb, sx + 3, sy, C_SNOW)
  set_px_safe(fb, sx, sy - 3, C_SNOW)
  set_px_safe(fb, sx, sy - 2, C_SNOW)
  set_px_safe(fb, sx, sy + 2, C_SNOW)
  set_px_safe(fb, sx, sy + 3, C_SNOW)
  -- stronger diagonals
  set_px_safe(fb, sx - 2, sy - 2, C_SNOW)
  set_px_safe(fb, sx + 2, sy - 2, C_SNOW)
  set_px_safe(fb, sx - 2, sy + 2, C_SNOW)
  set_px_safe(fb, sx + 2, sy + 2, C_SNOW)
  set_px_safe(fb, sx - 1, sy - 1, C_SNOW)
  set_px_safe(fb, sx + 1, sy - 1, C_SNOW)
  set_px_safe(fb, sx - 1, sy + 1, C_SNOW)
  set_px_safe(fb, sx + 1, sy + 1, C_SNOW)
end

local function draw_storm(fb, cx, cy)
  -- A bigger zig-zag bolt so it stays visible on 64x32.
  set_px_safe(fb, cx + 1, cy + 3, C_STORM)
  set_px_safe(fb, cx + 0, cy + 4, C_STORM)
  set_px_safe(fb, cx + 1, cy + 4, C_STORM)
  set_px_safe(fb, cx + 2, cy + 4, C_STORM)
  set_px_safe(fb, cx + 0, cy + 5, C_STORM)
  set_px_safe(fb, cx + 1, cy + 5, C_STORM)
  set_px_safe(fb, cx + 1, cy + 6, C_STORM)
  set_px_safe(fb, cx + 2, cy + 6, C_STORM)
  set_px_safe(fb, cx + 1, cy + 7, C_STORM)
end

local function draw_fog(fb, cx, cy)
  -- Lift mist by 2px to align with other icons.
  hline(fb, cx - 5, cx + 5, cy + 0, C_MUTED)
  hline(fb, cx - 4, cx + 4, cy + 2, C_MUTED)
  hline(fb, cx - 5, cx + 5, cy + 4, C_MUTED)
end

local function draw_owm_icon(fb, icon, cx, cy)
  if icon == "01d" then
    draw_sun(fb, cx, cy)
    return
  end
  if icon == "02d" then
    draw_sun(fb, cx - 2, cy - 1)
    draw_cloud(fb, cx + 1, cy + 1)
    return
  end
  if icon == "03d" or icon == "04d" then
    draw_cloud(fb, cx, cy)
    return
  end
  if icon == "09d" then
    draw_cloud(fb, cx, cy)
    draw_rain(fb, cx, cy)
    return
  end
  if icon == "10d" then
    draw_sun(fb, cx - 2, cy - 1)
    draw_cloud(fb, cx + 1, cy + 1)
    draw_rain(fb, cx + 1, cy + 1)
    return
  end
  if icon == "11d" then
    draw_cloud(fb, cx, cy)
    draw_storm(fb, cx, cy)
    return
  end
  if icon == "13d" then
    draw_snow(fb, cx, cy)
    return
  end
  if icon == "50d" then
    draw_fog(fb, cx, cy)
    return
  end
  draw_cloud(fb, cx, cy)
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

  local d = obj.daily
  if not d or not d.time or not d.weather_code or not d.temperature_2m_max then
    state.err = "BAD DATA"
    return
  end

  for i = 1, 3 do
    local day = state.days[i]
    local ymd = d.time[i]
    local code = d.weather_code[i]
    local temp = d.temperature_2m_max[i]
    day.label = weekday_label(ymd)
    day.code = tonumber(code)
    day.temp = tonumber(temp)
    day.icon = code_to_owm_icon(code)
  end

  state.err = nil
  state.last_ok_ms = now_ms()
end

local function start_request()
  if state.req_id then return end

  local url = string.format(
    "https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s&daily=weather_code,temperature_2m_max&forecast_days=3&timezone=auto&temperature_unit=%s",
    tostring(LAT),
    tostring(LON),
    tostring(TEMP_UNIT)
  )

  local ttl_ms = 10 * 60 * 1000
  local id, body, age_ms, err = net.cached_get(url, ttl_ms, 6000, 4096)
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

function app.init(config)
  sys.log("openmeteo_3day init")
  state.req_id = nil
  state.last_req_ms = 0
  state.last_ok_ms = 0
  state.err = nil
  start_request()
end

function app.tick(dt_ms)
  local now = now_ms()

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

  local interval = 15 * 60 * 1000
  if state.err then interval = 60 * 1000 end
  if now - state.last_req_ms >= interval then
    start_request()
  end
end

local function format_temp(v)
  if v == nil then return "--" end
  return string.format("%d", math.floor((tonumber(v) or 0) + 0.5))
end

local function temp_color(v)
  local t = tonumber(v)
  if not t then return C_TEXT end
  if t <= 10 then return C_CYAN end
  if t > 28 then return C_HOT end
  return C_TEXT
end

local function draw_degree_mark(fb, x, y, c)
  -- Tiny 3x3 hollow degree symbol.
  set_px_safe(fb, x + 1, y + 0, c)
  set_px_safe(fb, x + 0, y + 1, c)
  set_px_safe(fb, x + 2, y + 1, c)
  set_px_safe(fb, x + 1, y + 2, c)
end

function app.render_fb(fb)
  fb:fill(C_BG)
  local yoff = -2

  if state.err then
    fb:text_box(0, 0 + yoff, 64, 8, "OPENMETEO", C_TEXT, font, 8, "center", true)
    fb:text_box(0, 10 + yoff, 64, 8, "ERROR", C_WARN, font, 8, "center", true)
    fb:text_box(0, 20 + yoff, 64, 8, tostring(state.err), C_MUTED, font, 8, "center", true)
    return
  end

  local centers = {10, 32, 54}

  for i = 1, 3 do
    local d = state.days[i]
    local cx = centers[i]
    local temp_txt = format_temp(d.temp)
    local tc = temp_color(d.temp)
    fb:text_box(cx - 10, 0 + yoff, 20, 8, d.label or "---", C_TEXT, font, 8, "center", true)
    draw_owm_icon(fb, d.icon or "03d", cx, 16 + yoff)
    fb:text_box(cx - 10, 25 + yoff, 20, 8, temp_txt, tc, font, 8, "center", true)
    draw_degree_mark(fb, cx + 4, 25 + yoff, tc)
  end
end

return app
