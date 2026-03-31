local app = {}

local FONT_UI = "builtin:silkscreen_regular_8"
local FONT_TITLE = "builtin:pressstart2p_regular_8"

local C_BG = 0x0000
local C_TEXT = 0xFFFF
local C_MUTED = 0x9CF3
local C_PANEL = 0x0843
local C_BUSY1 = 0x18A3
local C_BUSY2 = 0x07FF
local C_BUSY3 = 0xFD20
local C_TODAY = 0xFFE0

local function resolve_url()
  local direct = tostring(data.get("calendar_month_card.url") or "")
  if direct ~= "" then return direct end
  local base = tostring(data.get("calendar.api_base_url") or "")
  if base == "" then return "" end
  if string.sub(base, -1) == "/" then return base .. "month-card" end
  return base .. "/month-card"
end

local APP_URL = resolve_url()
local TTL_MS = tonumber(data.get("calendar_month_card.ttl_ms") or data.get("calendar.ttl_ms") or 60000) or 60000
local TIMEOUT_MS = tonumber(data.get("calendar_month_card.timeout_ms") or data.get("calendar.timeout_ms") or 6000) or 6000
local MAX_BODY = tonumber(data.get("calendar_month_card.max_body") or data.get("calendar.max_body") or 24576) or 24576

local MONTHS = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"}
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
}

local state = {
  req_id = nil,
  last_req_ms = 0,
  last_ok_ms = 0,
  err = nil,
  payload = nil,
}

local function now_ms()
  return sys.now_ms()
end

local function set_px_safe(fb, x, y, c)
  if x < 0 or x >= 64 or y < 0 or y >= 32 then return end
  fb:set_px(x, y, c)
end

local function rect_fill(fb, x, y, w, h, c)
  if w <= 0 or h <= 0 then return end
  fb:rect(x, y, w, h, c)
end

local function compact_text(s, limit)
  s = tostring(s or "")
  s = string.gsub(s, "[\r\n\t]", " ")
  s = string.gsub(s, "%s+", " ")
  s = string.gsub(s, "^%s+", "")
  s = string.gsub(s, "%s+$", "")
  local n = tonumber(limit) or 18
  if #s > n then return string.sub(s, 1, n - 1) .. "…" end
  return s
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

local function draw_big_value(fb, value, x, y, scale, c)
  local s = tostring(value or "--")
  local cx = x
  for i = 1, #s do
    draw_digit(fb, string.sub(s, i, i), cx, y, scale, c)
    cx = cx + 3 * scale + scale
  end
end

local function fallback_month()
  local t = sys.local_time and sys.local_time() or {}
  local month = tonumber(t.month or 1) or 1
  return {
    year = tonumber(t.year or 2026) or 2026,
    month = month,
    month_label = MONTHS[month],
    first_weekday = 0,
    days = 31,
    today = tonumber(t.day or 1) or 1,
    busy_map = {},
  }
end

local function month_data()
  local m = state.payload and state.payload.month or nil
  if type(m) ~= "table" then return fallback_month() end
  local fallback = fallback_month()
  return {
    year = tonumber(m.year or fallback.year) or fallback.year,
    month = tonumber(m.month or fallback.month) or fallback.month,
    month_label = tostring(m.month_label or fallback.month_label),
    first_weekday = tonumber(m.first_weekday or fallback.first_weekday) or fallback.first_weekday,
    days = tonumber(m.days or fallback.days) or fallback.days,
    today = tonumber(m.today or fallback.today) or fallback.today,
    busy_map = type(m.busy_map) == "table" and m.busy_map or {},
  }
end

local function footer_text()
  local footer = state.payload and state.payload.footer or nil
  if type(footer) == "table" and tostring(footer.text or "") ~= "" then
    return compact_text(footer.text, 18)
  end
  return "Month overview"
end

local function busy_color(level)
  local n = tonumber(level or 0) or 0
  if n <= 0 then return C_PANEL end
  if n == 1 then return C_BUSY1 end
  if n == 2 then return C_BUSY2 end
  return C_BUSY3
end

local function draw_month_grid(fb, month)
  local cell_w = 4
  local cell_h = 3
  local origin_x = 1
  local origin_y = 10
  local first = tonumber(month.first_weekday or 0) or 0
  local days = tonumber(month.days or 31) or 31
  for day = 1, days do
    local idx = first + day - 1
    local col = idx % 7
    local row = math.floor(idx / 7)
    local x = origin_x + col * cell_w
    local y = origin_y + row * cell_h
    rect_fill(fb, x, y, 3, 2, busy_color(month.busy_map[day]))
    if day == month.today then
      rect_fill(fb, x - 1, y - 1, 1, 4, C_TODAY)
      rect_fill(fb, x + 3, y - 1, 1, 4, C_TODAY)
      rect_fill(fb, x - 1, y - 1, 5, 1, C_TODAY)
      rect_fill(fb, x - 1, y + 2, 5, 1, C_TODAY)
    end
  end
end

local function handle_response(status, body)
  if status ~= 200 then
    state.err = "HTTP " .. tostring(status)
    return
  end
  local obj, jerr = json.decode(body)
  if not obj or type(obj) ~= "table" then
    state.err = jerr or "JSON ERR"
    return
  end
  state.payload = obj
  state.err = nil
  state.last_ok_ms = now_ms()
end

local function start_request()
  if state.req_id then return end
  if APP_URL == "" then
    state.err = "SET URL"
    return
  end
  local id, body, age_ms, err = net.cached_get(APP_URL, TTL_MS, TIMEOUT_MS, MAX_BODY)
  if err then
    state.err = tostring(err)
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
  state.err = "HTTP FAIL"
end

function app.init()
  state.req_id = nil
  state.last_req_ms = 0
  state.last_ok_ms = 0
  state.err = nil
  state.payload = nil
  start_request()
end

function app.tick(dt_ms)
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
  local interval = state.err and 30000 or TTL_MS
  if now_ms() - state.last_req_ms >= interval then start_request() end
end

function app.render_fb(fb)
  fb:fill(C_BG)
  if state.err and not state.payload then
    fb:text_box(0, 2, 64, 8, "MONTH CARD", C_TEXT, FONT_UI, 8, "center", true)
    fb:text_box(0, 12, 64, 8, tostring(state.err), C_BUSY3, FONT_UI, 8, "center", true)
    fb:text_box(0, 22, 64, 8, "calendar_month_card.url", C_MUTED, FONT_UI, 8, "center", true)
    return
  end

  local month = month_data()
  draw_month_grid(fb, month)
  fb:text_box(1, 0, 28, 8, tostring(month.month_label), C_TEXT, FONT_UI, 8, "left", false)
  fb:text_box(32, 0, 30, 8, tostring(month.year), C_MUTED, FONT_UI, 8, "right", false)
  draw_big_value(fb, string.format("%02d", tonumber(month.today or 1) or 1), 34, 10, 2, C_TODAY)
  fb:text_box(31, 22, 32, 8, "TODAY", C_MUTED, FONT_TITLE, 8, "center", false)
  fb:text_box(1, 25, 62, 8, footer_text(), C_MUTED, FONT_UI, 8, "center", false)
end

return app
