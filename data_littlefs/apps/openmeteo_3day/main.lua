local app = {}

local OWM_API_KEY = data.get("owm.api_key") or "5ce216b488692ef60673d24f9583a873"
local DEFAULT_CITY = "zhongshangang,cn"
local DEFAULT_REFRESH_MS = 5 * 60 * 1000

local function cfg_city()
  local city = tostring(data.get("openmeteo_3day.city") or data.get("owm.city") or DEFAULT_CITY)
  city = string.gsub(city, "%s+", " ")
  city = string.gsub(city, "^%s+", "")
  city = string.gsub(city, "%s+$", "")
  if city == "" then city = DEFAULT_CITY end
  return city
end

local function cfg_temp_unit()
  local u = string.lower(tostring(data.get("openmeteo_3day.units") or data.get("openmeteo_3day.temp_unit") or data.get("openmeteo.temp_unit") or data.get("owm.units") or "celsius"))
  if u == "imperial" or u == "fahrenheit" or u == "f" then return "fahrenheit" end
  return "celsius"
end

local function cfg_refresh_ms()
  local n = tonumber(data.get("openmeteo_3day.refresh_ms") or data.get("openmeteo_3day.refresh_interval_ms") or DEFAULT_REFRESH_MS) or DEFAULT_REFRESH_MS
  if n < 15000 then n = 15000 end
  if n > 3600000 then n = 3600000 end
  return math.floor(n)
end

local function cfg_coords()
  local lat = tonumber(data.get("openmeteo_3day.lat") or data.get("openmeteo.lat"))
  local lon = tonumber(data.get("openmeteo_3day.lon") or data.get("openmeteo.lon"))
  if lat and lon then return lat, lon end
  return nil, nil
end

local function url_encode(s)
  s = tostring(s or "")
  return (s:gsub("([^%w%-_%.~,])", function(c)
    return string.format("%%%02X", string.byte(c))
  end))
end

local font = "builtin:silkscreen_regular_8"

local C_BG = 0x0000
local C_TEXT = 0xFFFF
local C_MUTED = 0x9CF3
local C_CYAN = 0x07FF
local C_HOT = 0xF800
local C_SUN = 0xFD20
local C_SUN_HI = 0xFFE0
local C_CLOUD = 0xC638
local C_CLOUD_HI = 0xEF7D
local C_RAIN = 0x051F
local C_SNOW = 0xE73C
local C_STORM = 0xFFE0
local C_WARN = 0xFD20

local state = {
  req_id = nil,
  req_kind = nil,
  last_req_ms = 0,
  last_ok_ms = 0,
  err = nil,
  lat = nil,
  lon = nil,
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

local function draw_pattern(fb, x, y, pattern, palette)
  for row = 1, #pattern do
    local line = pattern[row]
    for col = 1, #line do
      local key = string.sub(line, col, col)
      local c = palette[key]
      if c then
        set_px_safe(fb, x + col - 1, y + row - 1, c)
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

local function draw_sun_icon(fb, x, y)
  draw_pattern(fb, x, y, {
    "..s..s..",
    "...sh...",
    ".sshhhss",
    "..hhhhh.",
    "shhhhhhs",
    "..hhhhh.",
    ".sshhhss",
    "...sh...",
    "..s..s..",
  }, { s = C_SUN, h = C_SUN_HI })
end

local function draw_cloud_icon(fb, x, y)
  draw_pattern(fb, x, y, {
    "...hhh....",
    "..hcccch..",
    ".hccccccch",
    "hccccccccch",
    "ccccccccccc",
    ".ccccccccc.",
    "..ccccccc..",
  }, { h = C_CLOUD_HI, c = C_CLOUD })
end

local function draw_rain_icon(fb, x, y)
  draw_cloud_icon(fb, x, y)
  draw_pattern(fb, x, y + 6, {
    ".r..r..r.",
    "..r..r...",
    ".r..r..r.",
  }, { r = C_RAIN })
end

local function draw_snow_icon(fb, x, y)
  draw_cloud_icon(fb, x, y)
  draw_pattern(fb, x + 1, y + 6, {
    ".*...*...",
    "..*.*....",
    ".*...*...",
  }, { ["*"] = C_SNOW })
end

local function draw_storm_icon(fb, x, y)
  draw_cloud_icon(fb, x, y)
  draw_pattern(fb, x + 3, y + 6, {
    "..t.",
    ".tt.",
    ".t..",
    ".tt.",
    "..t.",
  }, { t = C_STORM })
end

local function draw_fog_icon(fb, x, y)
  draw_pattern(fb, x, y + 1, {
    "...hhh....",
    "..hcccch..",
    ".ccccccccc",
    "cccccccccc.",
  }, { h = C_CLOUD_HI, c = C_CLOUD })
  draw_pattern(fb, x + 1, y + 5, {
    "mmmmmmmmm",
    ".mmmmmmm.",
    "mmmmmmmmm",
  }, { m = C_MUTED })
end

local function draw_partly_icon(fb, x, y)
  draw_pattern(fb, x, y, {
    ".s..s...",
    "..sh....",
    "sshhhss.",
    "..hhhhh.",
    ".hccccch",
    "cccccccc",
    ".cccccc.",
  }, { s = C_SUN, h = C_SUN_HI, c = C_CLOUD })
end

local function draw_owm_icon(fb, icon, cx, cy)
  local x = cx - 5
  local y = cy - 4
  if icon == "01d" then
    draw_sun_icon(fb, x + 1, y)
    return
  end
  if icon == "02d" then
    draw_partly_icon(fb, x, y)
    return
  end
  if icon == "03d" or icon == "04d" then
    draw_cloud_icon(fb, x, y + 1)
    return
  end
  if icon == "09d" then
    draw_rain_icon(fb, x, y)
    return
  end
  if icon == "10d" then
    draw_partly_icon(fb, x, y)
    draw_pattern(fb, x + 2, y + 7, {
      "r..r..r",
      ".r..r..",
    }, { r = C_RAIN })
    return
  end
  if icon == "11d" then
    draw_storm_icon(fb, x, y)
    return
  end
  if icon == "13d" then
    draw_snow_icon(fb, x, y)
    return
  end
  if icon == "50d" then
    draw_fog_icon(fb, x, y)
    return
  end
  draw_cloud_icon(fb, x, y + 1)
end

local function handle_response(status, body)
  sys.log(string.format(
    "openmeteo_3day handle_response status=%s body_len=%d",
    tostring(status), body and #body or 0
  ))
  if status ~= 200 then
    state.err = "HTTP " .. tostring(status)
    sys.log("openmeteo_3day response error=" .. tostring(state.err))
    return
  end

  local obj, jerr = json.decode(body)
  if not obj then
    state.err = jerr or "JSON ERR"
    sys.log("openmeteo_3day json error=" .. tostring(state.err))
    return
  end

  local d = obj.daily
  if not d or not d.time or not d.weather_code or not d.temperature_2m_max then
    state.err = "BAD DATA"
    sys.log("openmeteo_3day data error=BAD DATA")
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
  sys.log(string.format(
    "openmeteo_3day updated d1=%s/%s d2=%s/%s d3=%s/%s",
    tostring(state.days[1].label), tostring(state.days[1].temp),
    tostring(state.days[2].label), tostring(state.days[2].temp),
    tostring(state.days[3].label), tostring(state.days[3].temp)
  ))
end

local function parse_geo(status, body)
  if status ~= 200 then
    state.err = "GEO " .. tostring(status)
    return false
  end
  local arr, jerr = json.decode(body)
  if not arr or type(arr) ~= "table" or not arr[1] then
    state.err = jerr or "NO CITY"
    return false
  end
  local pick = arr[1]
  local lat = tonumber(pick.lat)
  local lon = tonumber(pick.lon)
  if not lat or not lon then
    state.err = "NO GEO"
    return false
  end
  state.lat = lat
  state.lon = lon
  return true
end

local function start_forecast_request()
  if state.req_id then return end

  local url = string.format(
    "http://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s&daily=weather_code,temperature_2m_max&forecast_days=3&timezone=auto&temperature_unit=%s",
    tostring(state.lat),
    tostring(state.lon),
    tostring(cfg_temp_unit())
  )

  local ttl_ms = cfg_refresh_ms()
  sys.log(string.format(
    "openmeteo_3day req start url=%s ttl=%d timeout=%d max_body=%d",
    tostring(url), ttl_ms, 6000, 4096
  ))
  local id, body, age_ms, err = net.cached_get(url, ttl_ms, 6000, 4096)
  if err then
    state.err = err
    sys.log("openmeteo_3day cached_get err=" .. tostring(err))
    return
  end

  if body then
    sys.log(string.format(
      "openmeteo_3day cached_get hit bytes=%d age_ms=%s",
      #body, tostring(age_ms)
    ))
    handle_response(200, body)
    state.last_req_ms = now_ms()
    return
  end

  if id then
    state.req_id = id
    state.req_kind = "wx"
    state.last_req_ms = now_ms()
    sys.log("openmeteo_3day req inflight id=" .. tostring(id))
    return
  end

  state.err = "HTTP GET FAIL"
  sys.log("openmeteo_3day req error=HTTP GET FAIL")
end

local function start_request()
  if state.req_id then return end

  local lat, lon = cfg_coords()
  if lat and lon then
    state.lat = lat
    state.lon = lon
    start_forecast_request()
    return
  end

  if OWM_API_KEY == "" then
    state.err = "NO KEY"
    return
  end

  local geo_url = string.format(
    "http://api.openweathermap.org/geo/1.0/direct?q=%s&limit=1&appid=%s",
    url_encode(cfg_city()), OWM_API_KEY
  )
  local id, body, age_ms, err = net.cached_get(geo_url, 12 * 60 * 60 * 1000, 6000, 4096)
  if err then
    state.err = err
    return
  end
  if body then
    if parse_geo(200, body) then
      start_forecast_request()
      state.last_req_ms = now_ms()
    end
    return
  end
  if id then
    state.req_id = id
    state.req_kind = "geo"
    state.last_req_ms = now_ms()
    return
  end
  state.err = "GEO GET FAIL"
end

function app.init(config)
  sys.log(string.format(
    "openmeteo_3day init city=%s unit=%s",
    tostring(cfg_city()), tostring(cfg_temp_unit())
  ))
  state.req_id = nil
  state.req_kind = nil
  state.last_req_ms = 0
  state.last_ok_ms = 0
  state.err = nil
  state.lat = nil
  state.lon = nil
  start_request()
end

function app.tick(dt_ms)
  local now = now_ms()

  if state.req_id then
    local done, status, body = net.cached_poll(state.req_id)
    if done then
      sys.log(string.format(
        "openmeteo_3day req done status=%s body_len=%d",
        tostring(status), body and #body or 0
      ))
      local kind = state.req_kind
      state.req_id = nil
      state.req_kind = nil
      if status == 0 then
        state.err = body or "HTTP ERR"
        sys.log("openmeteo_3day req transport err=" .. tostring(state.err))
      else
        if kind == "geo" then
          if parse_geo(status, body or "") then
            start_forecast_request()
          end
        else
          handle_response(status, body or "")
        end
      end
    end
    return
  end

  local interval = cfg_refresh_ms()
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


-- __GLOBAL_BOOT_SPLASH_WRAPPER_V1__
local __boot_now_ms = now_ms or (sys and sys.now_ms) or function() return 0 end
local __boot_started_ms = 0
local __boot_ms = tonumber(data.get("openmeteo_3day.boot_splash_ms") or data.get("app.boot_splash_ms") or 5000) or 5000
if __boot_ms < 0 then __boot_ms = 0 end
local __boot_name = tostring(data.get("openmeteo_3day.app_name") or "3-Day Forecast")

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
