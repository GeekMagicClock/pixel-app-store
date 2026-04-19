local app = {}

-- Config keys:
--   owm.api_key = "..."
--   weather_card_owm.city    = "kuala lumpur,my"   (fallback: owm.city)
--   weather_card_owm.units   = "metric" | "imperial" (fallback: owm.units)
--   weather_card_owm.refresh_ms = 300000            (default: 5 minutes)
local OWM_API_KEY = data.get("owm.api_key") or "5ce216b488692ef60673d24f9583a873"
local DEFAULT_CITY = "zhongshangang,cn"
-- Debug: "clear" | "clouds" | "cycle"; empty means use real weather mapping.
local DEBUG_ICON = data.get("weather_card.debug_icon") or ""
local DEFAULT_REFRESH_MS = 5 * 60 * 1000

local function cfg_city()
  local city = tostring(data.get("weather_card_owm.city") or data.get("owm.city") or DEFAULT_CITY)
  city = string.gsub(city, "%s+", " ")
  city = string.gsub(city, "^%s+", "")
  city = string.gsub(city, "%s+$", "")
  if city == "" then city = DEFAULT_CITY end
  return city
end

local function cfg_units()
  local u = string.lower(tostring(data.get("weather_card_owm.units") or data.get("owm.units") or "metric"))
  if u == "imperial" or u == "fahrenheit" or u == "f" then return "imperial" end
  return "metric"
end

local function cfg_refresh_ms()
  local n = tonumber(data.get("weather_card_owm.refresh_ms") or data.get("weather_card_owm.refresh_interval_ms") or DEFAULT_REFRESH_MS) or DEFAULT_REFRESH_MS
  if n < 15000 then n = 15000 end
  if n > 3600000 then n = 3600000 end
  return math.floor(n)
end

local font = "builtin:silkscreen_regular_8"

local C_BG = 0x0000
local C_TEXT = 0xFFFF
local C_MUTED = 0x9CF3
local C_WARN = 0xF800

local ASSET_BASE = "S:/littlefs/apps/weather_card_owm/assets/"

local state = {
  req_id = nil,
  om_req_id = nil,
  last_req_ms = 0,
  last_ok_ms = 0,
  err = nil,
  tz_shift_sec = 0,
  now_temp = nil,
  temp_min = nil,
  temp_max = nil,
  humidity = nil,
  weather_id = nil,
  weather_main = nil,
  lon = nil,
  lat = nil,
}

local function now_ms()
  return sys.now_ms()
end

local function set_px_safe(fb, x, y, c)
  if x < 0 or x >= 64 or y < 0 or y >= 32 then return end
  fb:set_px(x, y, c)
end

local function draw_degree_mark(fb, x, y, c)
  -- 3x3 ring-like degree mark
  set_px_safe(fb, x + 1, y, c)
  set_px_safe(fb, x, y + 1, c)
  set_px_safe(fb, x + 2, y + 1, c)
  set_px_safe(fb, x + 1, y + 2, c)
end

local function draw_icon_at_origin(fb, path)
  if fb.image_native then
    fb:image_native(0, 0, path)
  else
    fb:image(0, 0, 28, 24, path)
  end
end

local function draw_clouds_mood_icon(fb)
  draw_icon_at_origin(fb, ASSET_BASE .. "wx_clouds.png")
end

local function debug_force_cloud()
  local s = string.lower(tostring(DEBUG_ICON or ""))
  return s == "cloud" or s == "clouds" or s == "cloudy"
end

local function debug_force_clear()
  local s = string.lower(tostring(DEBUG_ICON or ""))
  return s == "clear" or s == "sun" or s == "sunny"
end

local function debug_cycle_mode()
  local s = string.lower(tostring(DEBUG_ICON or ""))
  return s == "cycle" or s == "all" or s == "icons"
end

local function debug_cycle_icon_path()
  local seq = {
    ASSET_BASE .. "wx_clear.png",
    ASSET_BASE .. "wx_clouds.png",
    ASSET_BASE .. "wx_rain.png",
    ASSET_BASE .. "wx_thunder.png",
    ASSET_BASE .. "wx_snow.png",
    ASSET_BASE .. "wx_mist.png",
  }
  local idx = math.floor((now_ms() or 0) / 5000) % #seq + 1
  return seq[idx]
end

local function day3_from_unix(ts, tz_shift_sec)
  local t = tonumber(ts) or 0
  if t <= 0 then return "---" end
  local sec = t + (tonumber(tz_shift_sec) or 0)
  local days = math.floor(sec / 86400)
  local idx = ((days + 4) % 7) + 1 -- 1970-01-01 was Thursday
  local names = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" }
  return names[idx] or "---"
end

local function url_encode(s)
  s = tostring(s or "")
  return (s:gsub("([^%w%-_%.~,])", function(c)
    return string.format("%%%02X", string.byte(c))
  end))
end

local function fmt_temp(v)
  if v == nil then return "--" end
  local n = tonumber(v) or 0
  if n >= 0 then return string.format("%d", math.floor(n + 0.5)) end
  return string.format("%d", math.ceil(n - 0.5))
end

local function scene_path(id, main)
  local wid = tonumber(id) or -1
  local m = tostring(main or "")
  if wid >= 200 and wid < 300 then return ASSET_BASE .. "wx_thunder.png" end
  if wid >= 300 and wid < 600 then return ASSET_BASE .. "wx_rain.png" end
  if wid >= 600 and wid < 700 then return ASSET_BASE .. "wx_snow.png" end
  if wid >= 700 and wid < 800 then return ASSET_BASE .. "wx_mist.png" end
  if wid == 800 or m == "Clear" then return ASSET_BASE .. "wx_clear.png" end
  return ASSET_BASE .. "wx_clouds.png"
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

  if obj.timezone ~= nil then state.tz_shift_sec = tonumber(obj.timezone) or 0 end
  if obj.main then
    if obj.main.temp ~= nil then state.now_temp = obj.main.temp end
    if obj.main.humidity ~= nil then state.humidity = tonumber(obj.main.humidity) end
  end
  if obj.coord then
    state.lon = tonumber(obj.coord.lon)
    state.lat = tonumber(obj.coord.lat)
  end
  if obj.weather and obj.weather[1] then
    state.weather_id = tonumber(obj.weather[1].id)
    state.weather_main = tostring(obj.weather[1].main or "")
  end

  state.err = nil
  state.last_ok_ms = now_ms()
end

local function handle_open_meteo_response(status, body)
  if status ~= 200 then
    sys.log("weather_card_owm: open-meteo HTTP " .. tostring(status))
    return
  end
  local obj, jerr = json.decode(body)
  if not obj then
    sys.log("weather_card_owm: open-meteo JSON ERR " .. tostring(jerr or "unknown"))
    return
  end
  local daily = obj.daily
  if type(daily) ~= "table" then return end
  local mins = daily.temperature_2m_min
  local maxs = daily.temperature_2m_max
  if type(mins) == "table" and mins[1] ~= nil then
    state.temp_min = mins[1]
  end
  if type(maxs) == "table" and maxs[1] ~= nil then
    state.temp_max = maxs[1]
  end
end

local function start_open_meteo_request(lat, lon)
  local lat_n = tonumber(lat)
  local lon_n = tonumber(lon)
  if not lat_n or not lon_n then return end
  if state.om_req_id then return end

  local unit_q = ""
  if cfg_units() == "imperial" then
    unit_q = "&temperature_unit=fahrenheit"
  end
  local url = string.format(
    "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&daily=temperature_2m_max,temperature_2m_min&forecast_days=1&timezone=auto%s",
    lat_n, lon_n, unit_q
  )

  local ttl_ms = cfg_refresh_ms()
  local id, body, age_ms, err = net.cached_get(url, ttl_ms, 5000, 3072)
  if err then
    sys.log("weather_card_owm: open-meteo request err " .. tostring(err))
    return
  end
  if body then
    handle_open_meteo_response(200, body)
    return
  end
  if id then
    state.om_req_id = id
    return
  end
  sys.log("weather_card_owm: open-meteo HTTP GET FAIL")
end

local function start_request()
  if state.req_id then return end
  if OWM_API_KEY == "" then
    state.err = "NO KEY"
    return
  end

  local url = string.format(
    "https://api.openweathermap.org/data/2.5/weather?q=%s&units=%s&appid=%s",
    url_encode(cfg_city()), cfg_units(), OWM_API_KEY
  )

  local ttl_ms = cfg_refresh_ms()
  local id, body, age_ms, err = net.cached_get(url, ttl_ms, 5000, 3072)
  if err then
    state.err = err
    return
  end

  if body then
    handle_response(200, body)
    -- High/Low must come from Open-Meteo (not OWM main.temp_min/max).
    state.temp_min = nil
    state.temp_max = nil
    start_open_meteo_request(state.lat, state.lon)
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
  sys.log("weather_card_owm init")
  state.req_id = nil
  state.om_req_id = nil
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
        state.temp_min = nil
        state.temp_max = nil
        start_open_meteo_request(state.lat, state.lon)
      end
    end
  end

  if state.om_req_id then
    local done, status, body = net.cached_poll(state.om_req_id)
    if done then
      state.om_req_id = nil
      if status == 0 then
        sys.log("weather_card_owm: open-meteo poll err " .. tostring(body or "HTTP ERR"))
      else
        handle_open_meteo_response(status, body or "")
      end
    end
  end

  if state.req_id or state.om_req_id then
    return
  end

  local interval = cfg_refresh_ms()
  if state.err then interval = 30 * 1000 end
  if now - state.last_req_ms >= interval then
    start_request()
  end
end

function app.render_fb(fb)
  fb:fill(C_BG)

  if state.err then
    fb:text_box(0, 8, 64, 8, "OWM ERROR", C_WARN, font, 8, "center", true)
    fb:text_box(0, 18, 64, 8, tostring(state.err), C_MUTED, font, 8, "center", true)
    return
  end

  local wid = tonumber(state.weather_id) or -1
  local m = tostring(state.weather_main or "")
  local is_clouds = (wid > 800 and wid < 900) or (m == "Clouds")
  if debug_cycle_mode() then
    draw_icon_at_origin(fb, debug_cycle_icon_path())
  elseif debug_force_clear() then
    draw_icon_at_origin(fb, ASSET_BASE .. "wx_clear.png")
  elseif debug_force_cloud() or is_clouds then
    draw_icon_at_origin(fb, ASSET_BASE .. "wx_clouds.png")
  elseif wid == 800 or m == "Clear" then
    draw_icon_at_origin(fb, ASSET_BASE .. "wx_clear.png")
  else
    draw_icon_at_origin(fb, scene_path(state.weather_id, state.weather_main))
  end
  local day3 = day3_from_unix(sys.unix_time and sys.unix_time() or 0, state.tz_shift_sec)
  fb:text_box(1, 24, 26, 8, day3, C_TEXT, font, 8, "left", true)

  local now_txt = fmt_temp(state.now_temp)
  local hi_lo_txt = string.format("%s/%s", fmt_temp(state.temp_min), fmt_temp(state.temp_max))
  local hum_txt = state.humidity and string.format("%d%%", state.humidity) or "--"

  fb:text_box(28, 3, 33, 8, now_txt, C_TEXT, font, 8, "right", true)
  draw_degree_mark(fb, 61, 3, C_TEXT)
  fb:text_box(28, 13, 33, 8, hi_lo_txt, C_MUTED, font, 8, "right", true)
  draw_degree_mark(fb, 61, 13, C_MUTED)
  fb:text_box(28, 24, 36, 8, hum_txt, C_TEXT, font, 8, "right", true)
end


-- __GLOBAL_BOOT_SPLASH_WRAPPER_V1__
local __boot_now_ms = now_ms or (sys and sys.now_ms) or function() return 0 end
local __boot_started_ms = 0
local __boot_ms = tonumber(data.get("weather_card_owm.boot_splash_ms") or data.get("app.boot_splash_ms") or 1200) or 1200
if __boot_ms < 0 then __boot_ms = 0 end
local __boot_name = tostring(data.get("weather_card_owm.app_name") or "Weather Card")

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
