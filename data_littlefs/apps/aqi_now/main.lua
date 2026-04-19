local app = {}

local DEFAULT_CITY = "zhongshangang,cn"
local DEFAULT_LAT = tonumber(data.get("aqi.lat") or data.get("openmeteo.lat")) or 22.548994
local DEFAULT_LON = tonumber(data.get("aqi.lon") or data.get("openmeteo.lon")) or 113.459035

local font = "builtin:silkscreen_regular_8"

local C_BG = 0x0000
local C_TEXT = 0xFFFF
local C_MUTED = 0x9CF3
local C_GOOD = 0x07E0
local C_FAIR = 0xFFE0
local C_USG = 0xFD20
local C_BAD = 0xF800
local C_VBAD = 0xC81F
local C_PANEL = 0x0843
local C_FRAME = 0x18C6
local C_WARN = 0xF800
local C_ACCENT = 0x07FF
local BOOT_SPLASH_MS = 1200
local APP_NAME = tostring(data.get("aqi_now.app_name") or "AQI Now")

local DIGITS = {
  ["0"] = {"111", "101", "101", "101", "111"},
  ["1"] = {"010", "110", "010", "010", "111"},
  ["2"] = {"111", "001", "111", "100", "111"},
  ["3"] = {"111", "001", "111", "001", "111"},
  ["4"] = {"101", "101", "111", "001", "001"},
  ["5"] = {"111", "100", "111", "001", "111"},
  ["6"] = {"111", "100", "111", "101", "111"},
  ["7"] = {"111", "001", "001", "001", "001"},
  ["8"] = {"111", "101", "111", "101", "111"},
  ["9"] = {"111", "101", "111", "001", "111"},
  ["-"] = {"000", "000", "111", "000", "000"},
}

local state = {
  req_id = nil,
  req_kind = nil, -- "geo" | "aqi"
  last_req_ms = 0,
  last_ok_ms = 0,
  err = nil,
  anim_ms = 0,
  city = nil,
  lat = DEFAULT_LAT,
  lon = DEFAULT_LON,
  geo_ok = false,
  aqi = nil,
  pm25 = nil,
  pm10 = nil,
  no2 = nil,
  ozone = nil,
  dominant = "PM25",
  dominant_value = nil,
  country_code = "--",
  boot_started_ms = 0,
}

local function now_ms()
  return sys.now_ms()
end

local function trim(s)
  s = tostring(s or "")
  s = string.gsub(s, "^%s+", "")
  s = string.gsub(s, "%s+$", "")
  s = string.gsub(s, "%s+", " ")
  return s
end

local function url_encode(s)
  s = tostring(s or "")
  return (s:gsub("([^%w%-_%.~])", function(c)
    return string.format("%%%02X", string.byte(c))
  end))
end

local function cfg_city()
  local city = trim(data.get("aqi_now.city") or data.get("aqi.city") or DEFAULT_CITY)
  if city == "" then city = DEFAULT_CITY end
  return city
end

local function set_px_safe(fb, x, y, c)
  if x < 0 or x >= 64 or y < 0 or y >= 32 then return end
  fb:set_px(x, y, c)
end

local function hline(fb, x0, x1, y, c)
  if x0 > x1 then x0, x1 = x1, x0 end
  for x = x0, x1 do set_px_safe(fb, x, y, c) end
end

local function rect_fill(fb, x, y, w, h, c)
  for yy = 0, h - 1 do
    for xx = 0, w - 1 do
      set_px_safe(fb, x + xx, y + yy, c)
    end
  end
end

local function rect_outline(fb, x, y, w, h, c)
  hline(fb, x, x + w - 1, y, c)
  hline(fb, x, x + w - 1, y + h - 1, c)
  for yy = y, y + h - 1 do
    set_px_safe(fb, x, yy, c)
    set_px_safe(fb, x + w - 1, yy, c)
  end
end

local function draw_digit(fb, ch, x, y, scale, c)
  local pat = DIGITS[ch]
  if not pat then return end
  for row = 1, #pat do
    local line = pat[row]
    for col = 1, #line do
      if string.sub(line, col, col) == "1" then
        rect_fill(fb, x + (col - 1) * scale, y + (row - 1) * scale, scale, scale, c)
      end
    end
  end
end

local function draw_big_text(fb, txt, x, y, scale, c)
  local s = tostring(txt or "--")
  local cx = x
  for i = 1, #s do
    local ch = string.sub(s, i, i)
    draw_digit(fb, ch, cx, y, scale, c)
    cx = cx + 3 * scale + scale
  end
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

local function compact_text(s, limit)
  s = tostring(s or "")
  s = string.gsub(s, "%s+", " ")
  s = string.gsub(s, "^%s+", "")
  s = string.gsub(s, "%s+$", "")
  local n = tonumber(limit) or 16
  if #s > n then return string.sub(s, 1, n - 1) .. "…" end
  return s
end

local function split_title_lines(name)
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

local function aqi_color(v)
  local n = tonumber(v) or 0
  if n <= 50 then return C_GOOD end
  if n <= 100 then return C_FAIR end
  if n <= 150 then return C_USG end
  if n <= 200 then return C_BAD end
  return C_VBAD
end

local function aqi_label(v)
  local n = tonumber(v) or 0
  if n <= 50 then return "GOOD" end
  if n <= 100 then return "FAIR" end
  if n <= 150 then return "USG" end
  if n <= 200 then return "BAD" end
  return "VBAD"
end

local function dominant_pollutant(pm25, pm10, no2, ozone)
  local ratios = {
    { key = "PM25", value = pm25, ratio = (tonumber(pm25) or 0) / 35.0 },
    { key = "PM10", value = pm10, ratio = (tonumber(pm10) or 0) / 80.0 },
    { key = "NO2", value = no2, ratio = (tonumber(no2) or 0) / 100.0 },
    { key = "O3", value = ozone, ratio = (tonumber(ozone) or 0) / 120.0 },
  }
  local best = ratios[1]
  for i = 2, #ratios do
    if ratios[i].ratio > best.ratio then best = ratios[i] end
  end
  return best.key, best.value
end

local function fmt_int(v)
  if v == nil then return "--" end
  return string.format("%d", math.floor((tonumber(v) or 0) + 0.5))
end

local function draw_chip(fb, x, y, w, label, bg, fg, text_y)
  rect_fill(fb, x, y, w, 8, bg)
  rect_outline(fb, x, y, w, 8, C_FRAME)
  fb:text_box(x + 1, text_y or y, w - 2, 8, label, fg, font, 8, "center", false)
end

local function dominant_label(key)
  if key == "PM25" then return "PM2.5" end
  if key == "PM10" then return "PM10" end
  if key == "NO2" then return "NO2" end
  if key == "O3" then return "O3" end
  return tostring(key or "--")
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

  local idx = current_hour_index(hourly)
  state.aqi = hourly.us_aqi[idx]
  state.pm25 = hourly.pm2_5 and hourly.pm2_5[idx] or nil
  state.pm10 = hourly.pm10 and hourly.pm10[idx] or nil
  state.no2 = hourly.nitrogen_dioxide and hourly.nitrogen_dioxide[idx] or nil
  state.ozone = hourly.ozone and hourly.ozone[idx] or nil
  state.dominant, state.dominant_value = dominant_pollutant(state.pm25, state.pm10, state.no2, state.ozone)

  state.err = nil
  state.last_ok_ms = now_ms()
end

local function start_aqi_request()
  if state.req_id then return end
  local url = string.format(
    "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=%s&longitude=%s&hourly=us_aqi,pm2_5,pm10,nitrogen_dioxide,ozone&forecast_days=2&timezone=auto",
    tostring(state.lat), tostring(state.lon)
  )
  local id, body, age_ms, err = net.cached_get(url, 30 * 60 * 1000, 7000, 12288)
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
    state.req_kind = "aqi"
    state.last_req_ms = now_ms()
    return
  end
  state.err = "HTTP GET FAIL"
end

local function handle_geo_response(status, body)
  if status ~= 200 then
    state.geo_ok = false
    state.lat = DEFAULT_LAT
    state.lon = DEFAULT_LON
    state.country_code = "--"
    start_aqi_request()
    return
  end
  local obj, jerr = json.decode(body)
  if not obj then
    state.geo_ok = false
    state.lat = DEFAULT_LAT
    state.lon = DEFAULT_LON
    state.country_code = "--"
    start_aqi_request()
    return
  end
  local results = obj.results
  local item = (type(results) == "table" and #results > 0) and results[1] or nil
  local lat = item and tonumber(item.latitude)
  local lon = item and tonumber(item.longitude)
  local cc = item and tostring(item.country_code or "") or ""
  cc = string.upper(trim(cc))
  if #cc > 2 then cc = string.sub(cc, 1, 2) end
  if cc == "" then cc = "--" end
  state.country_code = cc
  if lat and lon then
    state.lat = lat
    state.lon = lon
    state.geo_ok = true
  else
    state.geo_ok = false
    state.lat = DEFAULT_LAT
    state.lon = DEFAULT_LON
  end
  start_aqi_request()
end

local function start_request()
  if state.req_id then return end
  local city = cfg_city()
  state.city = city
  local geo_url = string.format(
    "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=1&language=en&format=json",
    url_encode(city)
  )
  local id, body, age_ms, err = net.cached_get(geo_url, 24 * 60 * 60 * 1000, 7000, 6144)
  if err then
    state.geo_ok = false
    state.lat = DEFAULT_LAT
    state.lon = DEFAULT_LON
    state.country_code = "--"
    start_aqi_request()
    return
  end
  if body then
    handle_geo_response(200, body)
    state.last_req_ms = now_ms()
    return
  end
  if id then
    state.req_id = id
    state.req_kind = "geo"
    state.last_req_ms = now_ms()
    return
  end
  state.geo_ok = false
  state.lat = DEFAULT_LAT
  state.lon = DEFAULT_LON
  state.country_code = "--"
  start_aqi_request()
end

function app.init()
  state.req_id = nil
  state.req_kind = nil
  state.last_req_ms = 0
  state.last_ok_ms = 0
  state.err = nil
  state.anim_ms = 0
  state.city = cfg_city()
  state.lat = DEFAULT_LAT
  state.lon = DEFAULT_LON
  state.geo_ok = false
  state.country_code = "--"
  state.boot_started_ms = now_ms()
  start_request()
end

function app.tick(dt_ms)
  state.anim_ms = (state.anim_ms + (dt_ms or 0)) % 3600
  if state.req_id then
    local done, status, body = net.cached_poll(state.req_id)
    if done then
      local kind = state.req_kind
      state.req_id = nil
      state.req_kind = nil
      if status == 0 then
        if kind == "geo" then
          state.geo_ok = false
          state.lat = DEFAULT_LAT
          state.lon = DEFAULT_LON
          state.country_code = "--"
          start_aqi_request()
        else
          state.err = body or "HTTP ERR"
        end
      else
        if kind == "geo" then
          handle_geo_response(status, body or "")
        else
          handle_response(status, body or "")
        end
      end
    end
    return
  end
  local interval = state.err and 30000 or (30 * 60 * 1000)
  if now_ms() - state.last_req_ms >= interval then start_request() end
end

function app.render_fb(fb)
  fb:fill(C_BG)

  if state.boot_started_ms > 0 and (now_ms() - state.boot_started_ms) < BOOT_SPLASH_MS then
    local t1, t2 = split_title_lines(APP_NAME)
    if t2 ~= "" then
      fb:text_box(0, 8, 64, 8, compact_text(t1, 14), C_ACCENT, font, 8, "center", false)
      fb:text_box(0, 16, 64, 8, compact_text(t2, 14), C_ACCENT, font, 8, "center", false)
    else
      fb:text_box(0, 12, 64, 8, compact_text(t1, 14), C_ACCENT, font, 8, "center", false)
    end
    return
  end

  if state.err then
    fb:text_box(0, 8, 64, 8, "AQI NOW", C_WARN, font, 8, "center", true)
    fb:text_box(0, 18, 64, 8, "DATA ERR", C_MUTED, font, 8, "center", true)
    return
  end

  local aqi = tonumber(state.aqi) or 0
  local accent = aqi_color(aqi)
  local label = aqi_label(aqi)

  for i = 0, 7 do
    local px = (math.floor(state.anim_ms / 90) + i * 7) % 64
    local py = 2 + (i % 4) * 2
    set_px_safe(fb, px, py, accent)
  end

  draw_big_text(fb, fmt_int(aqi), 3, 8, 2, C_FRAME)
  draw_big_text(fb, fmt_int(aqi), 2, 7, 2, accent)
  fb:text_box(28, -1, 35, 8, "AQI NOW", C_TEXT, font, 8, "left", true)
  draw_chip(fb, 26, 10, 23, label, accent, C_BG, 9)
  draw_chip(fb, 51, 10, 13, state.country_code or "--", C_PANEL, C_MUTED, 9)
  draw_chip(fb, 25, 20, 26, dominant_label(state.dominant), C_PANEL, C_TEXT, 19)
  fb:text_box(50, 20, 12, 8, fmt_int(state.dominant_value), accent, font, 8, "right", false)

  rect_fill(fb, 2, 29, 60, 2, C_PANEL)
  rect_fill(fb, 2, 29, 10, 2, C_GOOD)
  rect_fill(fb, 12, 29, 10, 2, C_FAIR)
  rect_fill(fb, 22, 29, 10, 2, C_USG)
  rect_fill(fb, 32, 29, 10, 2, C_BAD)
  rect_fill(fb, 42, 29, 20, 2, C_VBAD)
  for x = 2, 62, 10 do
    set_px_safe(fb, x, 28, C_FRAME)
  end
  local pos = math.floor(math.min(aqi, 300) / 300 * 59 + 0.5)
  rect_fill(fb, 2 + pos, 28, 2, 4, C_TEXT)
end


-- __GLOBAL_BOOT_SPLASH_WRAPPER_V1__
local __boot_now_ms = now_ms or (sys and sys.now_ms) or function() return 0 end
local __boot_started_ms = 0
local __boot_ms = tonumber(data.get("aqi_now.boot_splash_ms") or data.get("app.boot_splash_ms") or 1200) or 1200
if __boot_ms < 0 then __boot_ms = 0 end
local __boot_name = tostring(data.get("aqi_now.app_name") or "AQI Now")

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
