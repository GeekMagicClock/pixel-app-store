local app = {}

local OWM_API_KEY = data.get("owm.api_key") or "5ce216b488692ef60673d24f9583a873"
local CITY = data.get("owm.city") or "Seoul,KR"
local UNITS = data.get("owm.units") or "metric"

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
local C_FRAME = 0x18C6
local C_WARN = 0xF800

local state = {
  req_id = nil,
  last_req_ms = 0,
  last_ok_ms = 0,
  err = nil,
  anim_ms = 0,
  slots = {
    { hour = "--", icon = "03d", temp = nil, pop = 0 },
    { hour = "--", icon = "03d", temp = nil, pop = 0 },
    { hour = "--", icon = "03d", temp = nil, pop = 0 },
    { hour = "--", icon = "03d", temp = nil, pop = 0 },
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

local function fmt_temp(v)
  if v == nil then return "--" end
  local n = tonumber(v) or 0
  if n >= 0 then return string.format("%d", math.floor(n + 0.5)) end
  return string.format("%d", math.ceil(n - 0.5))
end

local function time_label(dt_txt)
  local hh = string.match(tostring(dt_txt or ""), " (%d%d):")
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

  local list = obj.list
  if type(list) ~= "table" then
    state.err = "BAD DATA"
    return
  end

  for i = 1, 4 do
    local item = list[i] or {}
    local weather = (item.weather and item.weather[1]) or {}
    state.slots[i] = {
      hour = time_label(item.dt_txt),
      icon = tostring(weather.icon or "03d"),
      temp = item.main and item.main.temp or nil,
      pop = tonumber(item.pop) or 0,
    }
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
    "http://api.openweathermap.org/data/2.5/forecast?q=%s&units=%s&cnt=4&appid=%s",
    CITY, UNITS, OWM_API_KEY
  )
  local id, body, age_ms, err = net.cached_get(url, 10 * 60 * 1000, 6000, 8192)
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
  state.anim_ms = (state.anim_ms + (dt_ms or 0)) % 4000
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
    fb:text_box(0, 8, 64, 8, "HOURLY STRIP", C_WARN, font, 8, "center", true)
    fb:text_box(0, 18, 64, 8, tostring(state.err), C_MUTED, font, 8, "center", true)
    return
  end

  fb:text_box(1, 0, 24, 8, "NEXT 12H", C_TEXT, font, 8, "left", true)
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
    fb:text_box(x, 9, 16, 8, slot.hour, C_MUTED, font, 8, "center", true)
    draw_icon(fb, slot.icon, x + 8, 18)
    fb:text_box(x, 23, 16, 8, fmt_temp(slot.temp), accent, font, 8, "center", true)
    set_px_safe(fb, x + 2, 9, marker)
    set_px_safe(fb, x + 13, 9, marker)

    local pop_w = math.floor((tonumber(slot.pop) or 0) * 12 + 0.5)
    hline(fb, x + 2, x + 13, 28, C_PANEL)
    if pop_w > 0 then
      hline(fb, x + 2, x + 1 + pop_w, 28, C_BLUE)
      set_px_safe(fb, x + 1 + pop_w, 27, C_CYAN)
      if pop_w >= 5 then
        draw_drop(fb, x + 6, 24, C_CYAN)
      end
    end
  end
end

return app
