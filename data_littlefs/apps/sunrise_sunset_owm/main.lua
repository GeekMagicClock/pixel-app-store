local app = {}

-- Same config source pattern as weather_owm.
local OWM_API_KEY = data.get("owm.api_key") or "5ce216b488692ef60673d24f9583a873"
local CITY = data.get("owm.city") or "Seoul,KR"

local font_label = "builtin:silkscreen_regular_8"
local font_time = "builtin:silkscreen_regular_8"

local C_BG = 0x0000
local C_TEXT = 0xFFFF
local C_MUTED = 0x9CF3
local C_WARN = 0xF800
local C_BLUE = 0x051F
local C_YELLOW = 0xFFE0
local C_ORANGE = 0xFD20
local C_RED = 0xF800

local state = {
  req_id = nil,
  last_req_ms = 0,
  last_ok_ms = 0,
  tz_shift_sec = 0,
  sunrise_ts = nil,
  sunset_ts = nil,
  err = nil,
  anim_ms = 0,
}

local function now_ms()
  return sys.now_ms()
end

local function set_px_safe(fb, x, y, c)
  if x < 0 or x >= 64 or y < 0 or y >= 32 then return end
  fb:set_px(x, y, c)
end

local function vline(fb, x, y0, y1, c)
  if x < 0 or x >= 64 then return end
  if y0 > y1 then y0, y1 = y1, y0 end
  for y = y0, y1 do set_px_safe(fb, x, y, c) end
end

local function fill_circle_half(fb, cx, cy, r, upper_half, c)
  for dy = -r, r do
    for dx = -r, r do
      if dx * dx + dy * dy <= r * r then
        if upper_half and dy <= 0 then
          set_px_safe(fb, cx + dx, cy + dy, c)
        elseif (not upper_half) and dy >= 0 then
          set_px_safe(fb, cx + dx, cy + dy, c)
        end
      end
    end
  end
end

local function draw_dotted_horizon(fb, x, y, w, c)
  for i = 0, w - 1 do
    if (i % 2) == 0 then set_px_safe(fb, x + i, y, c) end
  end
end

local function tri01(t)
  local x = t - math.floor(t)
  if x < 0.5 then return x * 2 end
  return (1 - x) * 2
end

local function draw_sunrise_icon(fb, x, y, phase)
  -- icon area ~22x14
  local hx = x + 1
  local hy = y + 9
  local hw = 20
  local w = tri01(phase)
  local sun_shift = math.floor(w * 2 + 0.5)      -- 0..2 px
  local ray_on = (((phase * 1000) % 500) < 250)  -- blink 2Hz

  draw_dotted_horizon(fb, hx, hy, hw, C_BLUE)
  fill_circle_half(fb, x + 10, hy - sun_shift, 4, true, C_YELLOW)

  if ray_on then
    -- rays
    set_px_safe(fb, x + 10, hy - 6 - sun_shift, C_YELLOW)
    set_px_safe(fb, x + 7, hy - 5 - sun_shift, C_YELLOW)
    set_px_safe(fb, x + 13, hy - 5 - sun_shift, C_YELLOW)
    set_px_safe(fb, x + 5, hy - 3 - sun_shift, C_YELLOW)
    set_px_safe(fb, x + 15, hy - 3 - sun_shift, C_YELLOW)
  end

  -- up arrow
  set_px_safe(fb, x + 18, hy - 4, C_TEXT)
  set_px_safe(fb, x + 17, hy - 3, C_TEXT)
  set_px_safe(fb, x + 19, hy - 3, C_TEXT)
  vline(fb, x + 18, hy - 2, hy + 1, C_TEXT)
end

local function draw_sunset_icon(fb, x, y, phase)
  local hx = x + 1
  local hy = y + 9
  local hw = 20
  local w = tri01(phase)
  local sun_shift = math.floor(w * 2 + 0.5)      -- 0..2 px
  local ray_on = (((phase * 1000 + 250) % 500) < 250)

  draw_dotted_horizon(fb, hx, hy, hw, C_BLUE)
  fill_circle_half(fb, x + 10, hy + sun_shift, 4, false, C_ORANGE)

  if ray_on then
    -- rays below
    set_px_safe(fb, x + 10, hy + 6 + sun_shift, C_ORANGE)
    set_px_safe(fb, x + 7, hy + 5 + sun_shift, C_ORANGE)
    set_px_safe(fb, x + 13, hy + 5 + sun_shift, C_ORANGE)
    set_px_safe(fb, x + 5, hy + 3 + sun_shift, C_ORANGE)
    set_px_safe(fb, x + 15, hy + 3 + sun_shift, C_ORANGE)
  end

  -- down arrow
  vline(fb, x + 18, hy - 1, hy + 2, C_TEXT)
  set_px_safe(fb, x + 17, hy + 3, C_TEXT)
  set_px_safe(fb, x + 19, hy + 3, C_TEXT)
  set_px_safe(fb, x + 18, hy + 4, C_TEXT)
end

local function format_local_time(ts_utc, tz_shift_sec)
  if not ts_utc then return "--:--" end
  local sec = math.floor(tonumber(ts_utc) or 0) + math.floor(tonumber(tz_shift_sec) or 0)
  sec = sec % 86400
  if sec < 0 then sec = sec + 86400 end

  local h = math.floor(sec / 3600)
  local m = math.floor((sec % 3600) / 60)
  local ap = "AM"
  if h >= 12 then ap = "PM" end
  local h12 = h % 12
  if h12 == 0 then h12 = 12 end
  return string.format("%d:%02d %s", h12, m, ap)
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
  if obj.sys then
    if obj.sys.sunrise ~= nil then state.sunrise_ts = tonumber(obj.sys.sunrise) end
    if obj.sys.sunset ~= nil then state.sunset_ts = tonumber(obj.sys.sunset) end
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
    -- Use plain HTTP to match weather_owm behavior and reduce TLS memory pressure.
    "http://api.openweathermap.org/data/2.5/weather?q=%s&appid=%s",
    CITY,
    OWM_API_KEY
  )

  local ttl_ms = 5 * 60 * 1000
  local id, body, age_ms, err = net.cached_get(url, ttl_ms, 5000, 2048)
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
  sys.log("sunrise_sunset_owm init")
  state.req_id = nil
  state.last_req_ms = 0
  state.last_ok_ms = 0
  state.tz_shift_sec = 0
  state.sunrise_ts = nil
  state.sunset_ts = nil
  state.err = nil
  state.anim_ms = 0
  start_request()
end

function app.tick(dt_ms)
  state.anim_ms = (state.anim_ms + (dt_ms or 0)) % 2000
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

  -- Data changes slowly; keep refresh moderate.
  local interval = 5 * 60 * 1000
  if state.err then interval = 30 * 1000 end
  if now - state.last_req_ms >= interval then
    start_request()
  end
end

function app.render_fb(fb)
  fb:fill(C_BG)

  -- Center red dotted separator for visual hierarchy.
  for x = 0, 63 do
    if (x % 2) == 0 then set_px_safe(fb, x, 15, C_RED) end
  end

  local sunrise_txt = format_local_time(state.sunrise_ts, state.tz_shift_sec)
  local sunset_txt = format_local_time(state.sunset_ts, state.tz_shift_sec)
  local top_time_y = 3
  local bottom_time_y = 19
  local phase = state.anim_ms / 2000.0

  -- Top row: sunrise
  draw_sunrise_icon(fb, 0, 1, phase)
  if state.err then
    fb:text_box(24, top_time_y, 40, 8, "ERR", C_WARN, font_time, 8, "center", true)
  else
    fb:text_box(24, top_time_y, 40, 8, sunrise_txt, C_TEXT, font_time, 8, "center", true)
  end

  -- Bottom row: sunset
  draw_sunset_icon(fb, 0, 13, phase)
  if state.err then
    fb:text_box(24, bottom_time_y, 40, 8, "NO DATA", C_WARN, font_time, 8, "center", true)
  else
    fb:text_box(24, bottom_time_y, 40, 8, sunset_txt, C_TEXT, font_time, 8, "center", true)
  end
end

return app
