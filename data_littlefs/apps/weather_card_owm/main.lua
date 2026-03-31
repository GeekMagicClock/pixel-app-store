local app = {}

-- Config keys:
--   owm.api_key = "..."
--   owm.city    = "zhongshangang,cn"
--   owm.units   = "metric" | "imperial"
local OWM_API_KEY = data.get("owm.api_key") or "5ce216b488692ef60673d24f9583a873"
local CITY = data.get("owm.city") or "zhongshangang,cn"
local UNITS = data.get("owm.units") or "metric"
-- Debug: "clear" | "clouds" | "cycle"; empty means use real weather mapping.
local DEBUG_ICON = data.get("weather_card.debug_icon") or ""

local font = "builtin:silkscreen_regular_8"

local C_BG = 0x0000
local C_TEXT = 0xFFFF
local C_MUTED = 0x9CF3
local C_WARN = 0xF800

local ASSET_BASE = "S:/littlefs/apps/weather_card_owm/assets/"

local state = {
  req_id = nil,
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
    if obj.main.temp_min ~= nil then state.temp_min = obj.main.temp_min end
    if obj.main.temp_max ~= nil then state.temp_max = obj.main.temp_max end
    if obj.main.humidity ~= nil then state.humidity = tonumber(obj.main.humidity) end
  end
  if obj.weather and obj.weather[1] then
    state.weather_id = tonumber(obj.weather[1].id)
    state.weather_main = tostring(obj.weather[1].main or "")
  end

  state.err = nil
  state.last_ok_ms = now_ms()
end

local function start_request()
  if state.req_id then return end
  if OWM_API_KEY == "" then
    state.err = "NO KEY"
    return
  end

  local url = string.format(
    "https://api.openweathermap.org/data/2.5/weather?q=%s&units=%s&appid=%s",
    CITY, UNITS, OWM_API_KEY
  )

  local ttl_ms = 5 * 60 * 1000
  local id, body, age_ms, err = net.cached_get(url, ttl_ms, 5000, 3072)
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
  sys.log("weather_card_owm init")
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

  local interval = 5 * 60 * 1000
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

return app
