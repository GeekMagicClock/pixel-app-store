local app = {}

local OWM_API_KEY = data.get("owm.api_key") or "5ce216b488692ef60673d24f9583a873"
local CITY = data.get("hourly_rain_watch.city") or "zhongshangang,cn"
local TZ_NAME = data.get("weather.timezone") or "Asia/Shanghai"
local TZ_OFFSET_HOURS = tonumber(data.get("clock.utc_offset_hours") or 8) or 8

local font = "builtin:silkscreen_regular_8"

local C_BG = 0x0000
local C_PANEL = 0x0843
local C_TEXT = 0xFFFF
local C_MUTED = 0x9CF3
local C_BLUE = 0x0410
local C_CYAN = 0x07FF
local C_CLOUD = 0xC638
local C_CLOUD_HI = 0xEF7D
local C_RAIN = 0x051F
local C_HEAVY = 0x03FF
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
  peak_pop = 0,
  total_rain = 0,
  summary_kind = "rain",
  slots = {
    { hour = "--", pop = 0, rain = 0, icon = "03d" },
    { hour = "--", pop = 0, rain = 0, icon = "03d" },
    { hour = "--", pop = 0, rain = 0, icon = "03d" },
    { hour = "--", pop = 0, rain = 0, icon = "03d" },
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

local function rect_fill(fb, x, y, w, h, c)
  for yy = 0, h - 1 do
    for xx = 0, w - 1 do
      set_px_safe(fb, x + xx, y + yy, c)
    end
  end
end

local function fmt_mm(v)
  local n = tonumber(v) or 0
  return string.format("%.1f", n)
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

local function slot_kind(icon)
  local key = tostring(icon or "03d")
  if key == "11d" or key == "11n" then return "storm" end
  if key == "13d" or key == "13n" then return "snow" end
  if key == "09d" or key == "09n" or key == "10d" or key == "10n" then return "rain" end
  return "cloud"
end

local function bar_color(slot)
  local pop = tonumber(slot.pop) or 0
  local kind = slot_kind(slot.icon)
  if kind == "storm" then return C_HEAVY end
  if pop >= 0.65 then return C_HEAVY end
  if pop >= 0.35 then return C_CYAN end
  return C_RAIN
end

local function draw_header_icon(fb, x, y, kind, phase)
  draw_pattern(fb, x, y, {
    "...hhh.....",
    "..hcccch...",
    ".hccccccch.",
    "hccccccccch",
    "ccccccccccc",
    ".ccccccccc.",
  }, { h = C_CLOUD_HI, c = C_CLOUD })

  if kind == "storm" then
    draw_pattern(fb, x + 4, y + 5, {
      "..t.",
      ".tt.",
      ".t..",
      ".tt.",
    }, { t = C_STORM })
  else
    local off = math.floor((phase or 0) * 2 + 0.5)
    draw_pattern(fb, x + 1, y + 6 + off, {
      "r..r..r..",
      ".r..r..r.",
    }, { r = C_CYAN })
    draw_pattern(fb, x + 3, y + 7 + ((off + 1) % 2), {
      "r..r..r",
    }, { r = C_RAIN })
  end
end

local function clamp(v, lo, hi)
  if v < lo then return lo end
  if v > hi then return hi end
  return v
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

local function hour_from_iso(iso)
  local hh = string.match(tostring(iso or ""), "T(%d%d):")
  if not hh then hh = string.match(tostring(iso or ""), " (%d%d):") end
  return hh or "--"
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

local function set_error(msg)
  state.err = tostring(msg or "ERR")
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
  local pops = h.precipitation_probability or {}
  local rains = h.precipitation or {}
  local codes = h.weather_code or {}
  local start_idx = find_start_index(times)

  state.peak_pop = 0
  state.total_rain = 0
  state.summary_kind = "cloud"

  for i = 1, 4 do
    local idx = start_idx + i - 1
    local pop = clamp((tonumber(pops[idx]) or 0) / 100.0, 0, 1)
    local rain = tonumber(rains[idx]) or 0
    local icon = map_code_to_icon(codes[idx])

    state.slots[i] = {
      hour = hour_from_iso(times[idx]),
      pop = pop,
      rain = rain,
      icon = icon,
    }

    local kind = slot_kind(icon)
    if kind == "storm" then
      state.summary_kind = "storm"
    elseif state.summary_kind ~= "storm" and (kind == "rain" or rain > 0.1 or pop >= 0.35) then
      state.summary_kind = "rain"
    end
    if pop > state.peak_pop then state.peak_pop = pop end
    state.total_rain = state.total_rain + rain
  end

  state.err = nil
  state.last_ok_ms = now_ms()
  return true
end

local function process_response(kind, status, body)
  if kind == "geo" then
    if parse_geo(status, body) then
      local wx_url = string.format(
        "https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s&hourly=precipitation_probability,precipitation,weather_code&forecast_days=2&timezone=%s",
        tostring(state.lat), tostring(state.lon), url_encode(TZ_NAME)
      )
      local id, body2, age_ms, err = net.cached_get(wx_url, 5 * 60 * 1000, 6000, 16384)
      if err then
        set_error(err)
        return
      end
      if body2 then
        parse_hourly(200, body2)
        state.last_req_ms = now_ms()
        return
      end
      if id then
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

  local id, body, age_ms, err = net.cached_get(geo_url, 12 * 60 * 60 * 1000, 6000, 4096)
  if err then
    set_error(err)
    return
  end
  if body then
    process_response("geo", 200, body)
    state.last_req_ms = now_ms()
    return
  end
  if id then
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
  state.peak_pop = 0
  state.total_rain = 0
  state.summary_kind = "rain"
  state.lat = nil
  state.lon = nil
  start_request()
end

function app.tick(dt_ms)
  state.anim_ms = (state.anim_ms + (dt_ms or 0)) % 2400
  if state.req_id then
    local done, status, body = net.cached_poll(state.req_id)
    if done then
      local kind = state.req_kind
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
  for x = 0, 63, 2 do
    set_px_safe(fb, x, 0, C_BLUE)
  end
  for x = 0, 63 do
    if (x % 5) ~= 0 then set_px_safe(fb, x, 31, C_PANEL) end
  end

  if state.err then
    fb:text_box(0, 8, 64, 8, "HOURLY RAIN", C_WARN, font, 8, "center", true)
    fb:text_box(0, 18, 64, 8, tostring(state.err), C_MUTED, font, 8, "center", true)
    return
  end

  local phase = (state.anim_ms % 1000) / 1000.0
  draw_header_icon(fb, 1, 1, state.summary_kind, phase)
  fb:text_box(16, 1, 26, 8, "RAIN 4H", C_TEXT, font, 8, "left", true)
  fb:text_box(41, 1, 22, 8, string.format("%d%%", math.floor(state.peak_pop * 100 + 0.5)), C_CYAN, font, 8, "right", true)
  fb:text_box(16, 8, 17, 8, "SUM", C_MUTED, font, 8, "left", true)
  fb:text_box(34, 8, 29, 8, fmt_mm(state.total_rain) .. "MM", C_TEXT, font, 8, "right", true)

  local scan = math.floor(state.anim_ms / 500) % 4 + 1
  for i = 1, 4 do
    local slot = state.slots[i]
    local slot_left = (i - 1) * 16
    local x = slot_left + 4
    local base_y = 31
    local bar_h = math.floor((slot.pop or 0) * 11 + 0.5)
    local rain_h = math.floor(math.min((slot.rain or 0) * 3, 10) + 0.5)
    local top_y = base_y - bar_h
    local tone = bar_color(slot)

    fb:text_box(slot_left, 14, 16, 8, slot.hour, C_MUTED, font, 8, "center", true)
    hline(fb, x, x + 8, base_y, C_PANEL)
    if bar_h > 0 then
      rect_fill(fb, x, top_y, 9, bar_h + 1, tone)
      vline(fb, x + 8, top_y, base_y, C_CYAN)
      if rain_h > 0 then
        rect_fill(fb, x + 3, base_y - rain_h + 1, 3, rain_h, C_HEAVY)
      end
      if i == scan then
        hline(fb, x, x + 8, top_y, C_TEXT)
      end
      if slot.icon == "11d" or slot.icon == "11n" then
        set_px_safe(fb, x + 4, top_y - 1, C_STORM)
      elseif slot_kind(slot.icon) == "rain" then
        set_px_safe(fb, x + 3, top_y - 1, C_CLOUD)
        set_px_safe(fb, x + 4, top_y - 2, C_CLOUD_HI)
        set_px_safe(fb, x + 5, top_y - 1, C_CLOUD)
      end
    end
  end

  for k = 0, 2 do
    local x = (math.floor(state.anim_ms / 180) + k * 21) % 64
    local y = 5 + (k % 2)
    set_px_safe(fb, x, y, C_CYAN)
    set_px_safe(fb, x - 1, y + 1, C_BLUE)
  end
end

return app
