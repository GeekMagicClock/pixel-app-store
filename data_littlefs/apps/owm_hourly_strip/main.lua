local app = {}

local OWM_API_KEY = data.get("owm.api_key") or "5ce216b488692ef60673d24f9583a873"
local CITY = data.get("hourly_weather_strip.city") or "zhongshangang,cn"
local TEMP_UNIT = (data.get("owm.units") == "imperial") and "fahrenheit" or "celsius"
local TZ_NAME = data.get("weather.timezone") or "Asia/Shanghai"
local TZ_OFFSET_HOURS = tonumber(data.get("clock.utc_offset_hours") or 8) or 8
local DEBUG_HTTP = tostring(data.get("hourly_weather_strip.debug_http") or "0") == "1"

local font = "builtin:silkscreen_regular_8"

local C_BG = 0x0000
local C_PANEL = 0x0843
local C_TEXT = 0xFFFF
local C_MUTED = 0x9CF3
local C_WARM = 0xFD20
local C_COLD = 0x051F
local C_CYAN = 0x07FF
local C_BLUE = 0x03D7
local C_CLOUD = 0xC638
local C_CLOUD_HI = 0xEF7D
local C_SUN = 0xFD20
local C_SUN_HI = 0xFFE0
local C_RAIN = 0x051F
local C_SNOW = 0xE73C
local C_STORM = 0xFFE0
local C_WARN = 0xF800

local state = {
  req_id = nil,
  req_kind = nil,
  last_req_ms = 0,
  last_ok_ms = 0,
  err = nil,
  anim_ms = 0,
  lat = nil,
  lon = nil,
  slots = {
    { hour = "--", icon = "03d", temp = nil, pop = 0, rain = 0 },
    { hour = "--", icon = "03d", temp = nil, pop = 0, rain = 0 },
    { hour = "--", icon = "03d", temp = nil, pop = 0, rain = 0 },
    { hour = "--", icon = "03d", temp = nil, pop = 0, rain = 0 },
  },
}

local function now_ms()
  return sys.now_ms()
end

local function debug_log(msg)
  if not DEBUG_HTTP then return end
  if sys and sys.log then
    sys.log("[owm_hourly_strip] " .. tostring(msg or ""))
  end
end

local function preview_text(s, limit)
  local text = tostring(s or "")
  text = string.gsub(text, "[\r\n\t]+", " ")
  local max_len = tonumber(limit) or 120
  if #text > max_len then
    return string.sub(text, 1, max_len) .. "..."
  end
  return text
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

local function draw_pattern(fb, x, y, pattern, palette)
  for row = 1, #pattern do
    local line = pattern[row]
    for col = 1, #line do
      local ch = string.sub(line, col, col)
      local c = palette[ch]
      if c then set_px_safe(fb, x + col - 1, y + row - 1, c) end
    end
  end
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
  draw_pattern(fb, x, y + 7, {
    ".r..r..r.",
    "..r..r...",
  }, { r = C_RAIN })
end

local function draw_snow_icon(fb, x, y)
  draw_cloud_icon(fb, x, y)
  draw_pattern(fb, x + 1, y + 7, {
    ".*...*...",
    "..*.*....",
  }, { ["*"] = C_SNOW })
end

local function draw_storm_icon(fb, x, y)
  draw_cloud_icon(fb, x, y)
  draw_pattern(fb, x + 3, y + 6, {
    "..t.",
    ".tt.",
    ".t..",
    ".tt.",
  }, { t = C_STORM })
end

local function draw_fog_icon(fb, x, y)
  draw_pattern(fb, x, y + 1, {
    "...hhh....",
    "..hcccch..",
    ".ccccccccc",
    "cccccccccc.",
  }, { h = C_CLOUD_HI, c = C_CLOUD })
  draw_pattern(fb, x + 1, y + 6, {
    "mmmmmmmmm",
    ".mmmmmmm.",
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

local function draw_icon(fb, icon, cx, cy)
  local key = tostring(icon or "03d")
  local x = cx - 5
  local y = cy - 4
  if key == "01d" or key == "01n" then
    draw_sun_icon(fb, x + 1, y)
  elseif key == "02d" or key == "02n" then
    draw_partly_icon(fb, x, y)
  elseif key == "03d" or key == "03n" or key == "04d" or key == "04n" then
    draw_cloud_icon(fb, x, y + 1)
  elseif key == "09d" or key == "09n" or key == "10d" or key == "10n" then
    draw_rain_icon(fb, x, y)
  elseif key == "11d" or key == "11n" then
    draw_storm_icon(fb, x, y)
  elseif key == "13d" or key == "13n" then
    draw_snow_icon(fb, x, y)
  elseif key == "50d" or key == "50n" then
    draw_fog_icon(fb, x, y)
  else
    draw_cloud_icon(fb, x, y + 1)
  end
end

local function map_code_to_icon(code)
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

local function fmt_temp(v)
  if v == nil then return "--" end
  local n = tonumber(v) or 0
  if n >= 0 then return string.format("%d", math.floor(n + 0.5)) end
  return string.format("-%d", math.abs(math.ceil(n - 0.5)))
end

local function hour_from_iso(iso)
  local hh = string.match(tostring(iso or ""), "T(%d%d):")
  if not hh then hh = string.match(tostring(iso or ""), " (%d%d):") end
  return hh or "--"
end

local function accent_color(temp)
  local t = tonumber(temp) or 0
  if t <= 5 then return C_COLD end
  if t >= 24 then return C_WARM end
  return C_CYAN
end

local function slot_kind(icon)
  local key = tostring(icon or "03d")
  if key == "11d" or key == "11n" then return "storm" end
  if key == "13d" or key == "13n" then return "snow" end
  if key == "09d" or key == "09n" or key == "10d" or key == "10n" then return "rain" end
  if key == "01d" or key == "01n" then return "sun" end
  return "cloud"
end

local function kind_marker_color(icon)
  local kind = slot_kind(icon)
  if kind == "storm" then return C_STORM end
  if kind == "snow" then return C_SNOW end
  if kind == "rain" then return C_BLUE end
  if kind == "sun" then return C_SUN_HI end
  return C_CLOUD_HI
end

local function draw_drop(fb, x, y, c)
  set_px_safe(fb, x + 1, y, c)
  set_px_safe(fb, x, y + 1, c)
  set_px_safe(fb, x + 1, y + 1, c)
  set_px_safe(fb, x + 2, y + 1, c)
  set_px_safe(fb, x + 1, y + 2, c)
end

local function clamp(v, lo, hi)
  if v < lo then return lo end
  if v > hi then return hi end
  return v
end

local function hex2(n)
  local chars = "0123456789ABCDEF"
  n = clamp(math.floor(tonumber(n) or 0), 0, 255)
  local hi = math.floor(n / 16) + 1
  local lo = (n % 16) + 1
  return string.sub(chars, hi, hi) .. string.sub(chars, lo, lo)
end

local function url_encode(s)
  s = tostring(s or "")
  local out = {}
  for i = 1, #s do
    local ch = string.sub(s, i, i)
    if string.match(ch, "[A-Za-z0-9%-_%.~]") then
      out[#out + 1] = ch
    else
      out[#out + 1] = "%" .. hex2(string.byte(ch) or 0)
    end
  end
  return table.concat(out)
end

local function now_hour_key()
  local t = sys and sys.local_time and sys.local_time() or nil
  if not t then return nil end
  local y = tonumber(t.year)
  local m = tonumber(t.month)
  local d = tonumber(t.day)
  local h = tonumber(t.hour)
  if not y or not m or not d or h == nil then return nil end
  return string.format("%04d-%02d-%02dT%02d:00", y, m, d, h)
end

local function find_start_index(times)
  if type(times) ~= "table" or #times == 0 then return 1 end
  local key = now_hour_key()
  if key then
    for i = 1, #times do
      if tostring(times[i]) == key then return i end
    end
    for i = 1, #times do
      if tostring(times[i] or "") >= key then return i end
    end
  end
  return 1
end

local function set_error(msg)
  state.err = tostring(msg or "ERR")
  debug_log("error=" .. tostring(state.err))
end

local function parse_geo(status, body)
  if status ~= 200 then
    set_error("GEO " .. tostring(status))
    return false
  end
  local arr, jerr = json.decode(body)
  if not arr or type(arr) ~= "table" or not arr[1] then
    set_error(jerr or "NO CITY")
    return false
  end
  local pick = arr[1]
  for i = 1, #arr do
    local it = arr[i]
    if type(it) == "table" then
      local name = string.lower(tostring(it.name or ""))
      local country = string.upper(tostring(it.country or ""))
      if country == "CN" and name == "zhongshan" then
        pick = it
        break
      end
    end
  end
  local lat = tonumber(pick.lat)
  local lon = tonumber(pick.lon)
  if not lat or not lon then
    set_error("NO GEO")
    return false
  end
  state.lat = lat
  state.lon = lon
  return true
end

local function parse_hourly(status, body)
  if status ~= 200 then
    set_error("WX " .. tostring(status))
    return false
  end
  local obj, jerr = json.decode(body)
  if not obj then
    set_error(jerr or "JSON ERR")
    return false
  end
  local h = obj.hourly
  if type(h) ~= "table" or type(h.time) ~= "table" then
    set_error("BAD DATA")
    return false
  end

  local times = h.time
  local temps = h.temperature_2m or {}
  local pops = h.precipitation_probability or {}
  local rains = h.precipitation or {}
  local codes = h.weather_code or {}
  local start_idx = find_start_index(times)

  for i = 1, 4 do
    local idx = start_idx + i - 1
    state.slots[i] = {
      hour = hour_from_iso(times[idx]),
      icon = map_code_to_icon(codes[idx]),
      temp = temps[idx],
      pop = clamp((tonumber(pops[idx]) or 0) / 100.0, 0, 1),
      rain = tonumber(rains[idx]) or 0,
    }
  end

  state.err = nil
  state.last_ok_ms = now_ms()
  return true
end

local function process_response(kind, status, body)
  debug_log(string.format("response kind=%s status=%s body=%s", tostring(kind), tostring(status), preview_text(body, 140)))
  if kind == "geo" then
    if parse_geo(status, body) then
      local wx_url = string.format(
        "https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s&hourly=temperature_2m,precipitation_probability,precipitation,weather_code&forecast_days=2&timezone=%s&temperature_unit=%s",
        tostring(state.lat), tostring(state.lon), url_encode(TZ_NAME), TEMP_UNIT
      )
      debug_log("wx_url=" .. wx_url)
      local id, body2, age_ms, err = net.cached_get(wx_url, 5 * 60 * 1000, 6000, 16384)
      if err then
        set_error(err)
        return
      end
      if body2 then
        debug_log(string.format("wx cache-hit age_ms=%s body=%s", tostring(age_ms), preview_text(body2, 140)))
        parse_hourly(200, body2)
        state.last_req_ms = now_ms()
        return
      end
      if id then
        debug_log("wx request queued id=" .. tostring(id))
        state.req_id = id
        state.req_kind = "wx"
        state.last_req_ms = now_ms()
        return
      end
      set_error("WX GET FAIL")
    end
    return
  end

  if kind == "wx" then
    parse_hourly(status, body)
  end
end

local function start_request()
  if state.req_id then return end
  if OWM_API_KEY == "" then
    set_error("NO KEY")
    return
  end

  local geo_url = string.format(
    "https://api.openweathermap.org/geo/1.0/direct?q=%s&limit=1&appid=%s",
    url_encode(CITY), OWM_API_KEY
  )
  debug_log("geo_url=" .. geo_url)

  local id, body, age_ms, err = net.cached_get(geo_url, 12 * 60 * 60 * 1000, 6000, 4096)
  if err then
    set_error(err)
    return
  end
  if body then
    debug_log(string.format("geo cache-hit age_ms=%s body=%s", tostring(age_ms), preview_text(body, 140)))
    process_response("geo", 200, body)
    state.last_req_ms = now_ms()
    return
  end
  if id then
    debug_log("geo request queued id=" .. tostring(id))
    state.req_id = id
    state.req_kind = "geo"
    state.last_req_ms = now_ms()
    return
  end
  set_error("GEO GET FAIL")
end

function app.init()
  state.req_id = nil
  state.req_kind = nil
  state.last_req_ms = 0
  state.last_ok_ms = 0
  state.err = nil
  state.anim_ms = 0
  state.lat = nil
  state.lon = nil
  start_request()
end

function app.tick(dt_ms)
  state.anim_ms = (state.anim_ms + (dt_ms or 0)) % 4000
  if state.req_id then
    local done, status, body = net.cached_poll(state.req_id)
    if done then
      local kind = state.req_kind
      debug_log(string.format("poll done kind=%s status=%s body=%s", tostring(kind), tostring(status), preview_text(body, 140)))
      state.req_id = nil
      state.req_kind = nil
      if status == 0 then
        set_error(body or "HTTP ERR")
      else
        process_response(kind, status, body or "")
      end
    end
    return
  end

  local interval = state.err and 30000 or (10 * 60 * 1000)
  if now_ms() - state.last_req_ms >= interval then start_request() end
end

function app.render_fb(fb)
  fb:fill(C_BG)

  for x = 0, 63 do
    if (x % 4) ~= 3 then set_px_safe(fb, x, 30, C_PANEL) end
  end
  for x = 1, 62, 6 do
    set_px_safe(fb, x, 2 + (math.floor(x / 6) % 2), C_PANEL)
  end

  if state.err then
    fb:text_box(0, 8, 64, 8, "HOURLY WEATHER", C_WARN, font, 8, "center", true)
    fb:text_box(0, 18, 64, 8, tostring(state.err), C_MUTED, font, 8, "center", true)
    return
  end

  fb:text_box(1, -2, 40, 8, "NEXT 4H", C_TEXT, font, 8, "left", true)
  local active = math.floor(state.anim_ms / 1000) % 4 + 1
  for i = 1, 4 do
    local slot = state.slots[i]
    local x = (i - 1) * 16
    local accent = accent_color(slot.temp)
    local marker = kind_marker_color(slot.icon)
    rect_outline(fb, x + 1, 8, 14, 21, C_PANEL)
    if i == active then
      rect_fill(fb, x + 1, 8, 14, 21, C_PANEL)
      rect_outline(fb, x + 1, 8, 14, 21, accent)
    end
    fb:text_box(x, 6, 16, 8, slot.hour, C_MUTED, font, 8, "center", true)
    draw_icon(fb, slot.icon, x + 8, 19)
    fb:text_box(x, 23, 16, 8, fmt_temp(slot.temp), C_TEXT, font, 8, "center", true)
    set_px_safe(fb, x + 2, 6, marker)
    set_px_safe(fb, x + 13, 6, marker)

    local pop_w = math.floor((tonumber(slot.pop) or 0) * 12 + 0.5)
    hline(fb, x + 2, x + 13, 28, C_PANEL)
    if pop_w > 0 then
      hline(fb, x + 2, x + 1 + pop_w, 28, C_BLUE)
      set_px_safe(fb, x + 1 + pop_w, 27, C_CYAN)
      if (tonumber(slot.rain) or 0) > 0.05 then
        draw_drop(fb, x + 6, 24, C_CYAN)
      end
    end
  end
end

return app
