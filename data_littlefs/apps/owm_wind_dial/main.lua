local app = {}

local OWM_API_KEY = data.get("owm.api_key") or "5ce216b488692ef60673d24f9583a873"
local CITY = data.get("owm.city") or "Seoul,KR"
local UNITS = data.get("owm.units") or "metric"

local font = "builtin:silkscreen_regular_8"

local C_BG = 0x0000
local C_RING = 0x0C73
local C_TEXT = 0xFFFF
local C_MUTED = 0x9CF3
local C_CYAN = 0x07FF
local C_TEAL = 0x055B
local C_BLUE = 0x051F
local C_PANEL = 0x0843
local C_SUN = 0xFD20
local C_SUN_HI = 0xFFE0
local C_CLOUD = 0xC638
local C_CLOUD_HI = 0xEF7D
local C_RAIN = 0x051F
local C_WARN = 0xF800

local state = {
  req_id = nil,
  last_req_ms = 0,
  last_ok_ms = 0,
  err = nil,
  anim_ms = 0,
  wind_speed = nil,
  wind_gust = nil,
  wind_deg = 0,
  humidity = nil,
  feels_like = nil,
  icon = "03d",
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
    local x = math.floor(x0 + dx * t + 0.5)
    local y = math.floor(y0 + dy * t + 0.5)
    set_px_safe(fb, x, y, c)
  end
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
    local s = pattern[row]
    for col = 1, #s do
      local ch = string.sub(s, col, col)
      local c = palette[ch]
      if c then set_px_safe(fb, x + col - 1, y + row - 1, c) end
    end
  end
end

local GLYPH_W = {
  ["%"] = 4,
  ["/"] = 3,
  ["M"] = 5,
  ["S"] = 4,
}

local function glyph_w(ch)
  return GLYPH_W[ch] or 4
end

local function draw_tight_text(fb, x, y, text, color, gap)
  local s = tostring(text or "")
  local cx = x
  local sp = gap or 0
  for i = 1, #s do
    local ch = string.sub(s, i, i)
    fb:text(cx, y, ch, color, font, 8)
    if i < #s then
      cx = cx + glyph_w(ch) + sp
    end
  end
end

local function draw_weather_icon(fb, icon, x, y)
  local key = tostring(icon or "03d")
  if key == "01d" or key == "01n" then
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
  elseif key == "02d" or key == "02n" then
    draw_pattern(fb, x, y, {
      ".s..s...",
      "..sh....",
      "sshhhss.",
      "..hhhhh.",
      ".hccccch",
      "cccccccc",
      ".cccccc.",
    }, { s = C_SUN, h = C_SUN_HI, c = C_CLOUD })
  elseif key == "09d" or key == "09n" or key == "10d" or key == "10n" then
    draw_pattern(fb, x, y, {
      "...hhh....",
      "..hcccch..",
      ".hccccccch",
      "hccccccccch",
      "ccccccccccc",
      ".ccccccccc.",
      "..ccccccc..",
      ".r..r..r..",
      "..r..r....",
    }, { h = C_CLOUD_HI, c = C_CLOUD, r = C_RAIN })
  else
    draw_pattern(fb, x, y + 1, {
      "...hhh....",
      "..hcccch..",
      ".hccccccch",
      "hccccccccch",
      "ccccccccccc",
      ".ccccccccc.",
      "..ccccccc..",
    }, { h = C_CLOUD_HI, c = C_CLOUD })
  end
end

local function fmt_num(v, digits)
  if v == nil then return "--" end
  local n = tonumber(v) or 0
  if digits == 1 then return string.format("%.1f", n) end
  return string.format("%d", math.floor(n + 0.5))
end

local function dir_label(deg)
  local dirs = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" }
  local d = (tonumber(deg) or 0) % 360
  local idx = math.floor((d + 22.5) / 45) % 8 + 1
  return dirs[idx]
end

local function wind_unit()
  if UNITS == "imperial" then return "MPH" end
  return "M/S"
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

  local weather = (obj.weather and obj.weather[1]) or {}
  state.wind_speed = obj.wind and tonumber(obj.wind.speed) or nil
  state.wind_gust = obj.wind and tonumber(obj.wind.gust) or nil
  state.wind_deg = obj.wind and tonumber(obj.wind.deg) or 0
  state.humidity = obj.main and tonumber(obj.main.humidity) or nil
  state.feels_like = obj.main and tonumber(obj.main.feels_like) or nil
  state.icon = tostring(weather.icon or "03d")
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
    "http://api.openweathermap.org/data/2.5/weather?q=%s&units=%s&appid=%s",
    CITY, UNITS, OWM_API_KEY
  )
  local id, body, age_ms, err = net.cached_get(url, 10 * 60 * 1000, 6000, 4096)
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
  state.anim_ms = (state.anim_ms + (dt_ms or 0)) % 3600
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
  local cx, cy, r = 14, 16, 11

  if state.err then
    fb:text_box(0, 8, 64, 8, "WIND DIAL", C_WARN, font, 8, "center", true)
    fb:text_box(0, 18, 64, 8, tostring(state.err), C_MUTED, font, 8, "center", true)
    return
  end

  for i = 0, 3 do
    local trail = (math.floor(state.anim_ms / 120) + i * 12) % 64
    local yy = 3 + i * 2
    set_px_safe(fb, trail, yy, C_TEAL)
    set_px_safe(fb, trail + 1, yy, C_CYAN)
    set_px_safe(fb, trail + 2, yy, C_TEAL)
  end

  for a = 0, 330, 30 do
    local rad = math.rad(a - 90)
    local major = (a % 90) == 0
    local inner = r - (major and 3 or 2)
    local x0 = math.floor(cx + math.cos(rad) * inner + 0.5)
    local y0 = math.floor(cy + math.sin(rad) * inner + 0.5)
    local x1 = math.floor(cx + math.cos(rad) * (r + 1) + 0.5)
    local y1 = math.floor(cy + math.sin(rad) * (r + 1) + 0.5)
    line(fb, x0, y0, x1, y1, C_RING)
  end
  for rr = 8, 10 do
    for a = 0, 345, 15 do
      local rad = math.rad(a)
      set_px_safe(fb, math.floor(cx + math.cos(rad) * rr + 0.5), math.floor(cy + math.sin(rad) * rr + 0.5), C_RING)
    end
  end
  line(fb, cx - 8, cy, cx + 8, cy, C_PANEL)
  line(fb, cx, cy - 8, cx, cy + 8, C_PANEL)
  set_px_safe(fb, cx, cy - 9, C_TEXT)
  set_px_safe(fb, cx, cy + 9, C_MUTED)
  set_px_safe(fb, cx - 9, cy, C_MUTED)
  set_px_safe(fb, cx + 9, cy, C_MUTED)

  local deg = tonumber(state.wind_deg) or 0
  local rad = math.rad(deg - 90)
  local pulse = (math.sin((state.anim_ms / 3600) * math.pi * 2) + 1) * 0.5
  local reach = 7 + pulse
  local tip_x = math.floor(cx + math.cos(rad) * reach + 0.5)
  local tip_y = math.floor(cy + math.sin(rad) * reach + 0.5)
  line(fb, cx, cy, tip_x, tip_y, C_CYAN)
  line(fb, cx, cy, math.floor(cx - math.cos(rad) * 2 + 0.5), math.floor(cy - math.sin(rad) * 2 + 0.5), C_TEAL)
  local wing1_x = math.floor(tip_x + math.cos(rad + math.rad(145)) * 3 + 0.5)
  local wing1_y = math.floor(tip_y + math.sin(rad + math.rad(145)) * 3 + 0.5)
  local wing2_x = math.floor(tip_x + math.cos(rad - math.rad(145)) * 3 + 0.5)
  local wing2_y = math.floor(tip_y + math.sin(rad - math.rad(145)) * 3 + 0.5)
  line(fb, tip_x, tip_y, wing1_x, wing1_y, C_TEXT)
  line(fb, tip_x, tip_y, wing2_x, wing2_y, C_TEXT)
  fill_circle(fb, cx, cy, 1, C_TEXT)
  set_px_safe(fb, tip_x, tip_y, C_TEXT)

  local tail_dx = math.cos(rad) * 3
  local tail_dy = math.sin(rad) * 3
  for i = 1, 3 do
    local bx = math.floor(cx - tail_dx - i * 2 + 0.5)
    local by = math.floor(cy - tail_dy + ((i % 2 == 0) and 1 or 0) + 0.5)
    set_px_safe(fb, bx, by, C_TEAL)
  end

  fb:text_box(30, -1, 14, 8, "DIR", C_TEXT, font, 8, "left", true)
  rect_outline(fb, 45, 0, 17, 8, C_CYAN)
  fb:text_box(46, -2, 15, 8, dir_label(deg), C_TEXT, font, 8, "center", true)

  fb:text_box(30, 6, 18, 8, fmt_num(state.wind_speed, 1), C_CYAN, font, 8, "left", true)
  draw_tight_text(fb, 48, 6, wind_unit(), C_MUTED, 0)
  fb:text_box(30, 13, 10, 8, fmt_num(state.humidity, 0), C_TEXT, font, 8, "left", true)
  fb:text_box(40, 13, 6, 8, "%", C_MUTED, font, 8, "left", true)
  fb:text_box(49, 13, 8, 8, "FL", C_MUTED, font, 8, "left", true)
  fb:text_box(56, 13, 6, 8, fmt_num(state.feels_like, 0), C_TEXT, font, 8, "right", true)

  local gust = tonumber(state.wind_gust or state.wind_speed) or 0
  fb:text_box(30, 19, 15, 8, "GST", C_MUTED, font, 8, "left", true)
  hline(fb, 36, 62, 30, C_PANEL)
  for x = 36, 62, 5 do
    set_px_safe(fb, x, 29, C_RING)
  end
  local fill_w = math.floor(math.min(gust, 15) / 15 * 26 + 0.5)
  if fill_w > 0 then
    rect_fill(fb, 36, 29, fill_w, 2, C_CYAN)
    set_px_safe(fb, 36 + fill_w, 28, C_TEXT)
  end
  fb:text_box(46, 19, 16, 8, fmt_num(gust, 0), C_TEXT, font, 8, "right", true)
end

return app
