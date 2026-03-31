local app = {}

local FONT_UI = "builtin:silkscreen_regular_8"

local C_BG = 0x0842
local C_FRAME = 0x18C6
local C_CARD_TOP = 0x4208
local C_CARD_BOT = 0x20E6
local C_SPLIT = 0x0000
local C_TEXT = 0xFFDF
local C_TEXT_DIM = 0xB5B6
local C_ACCENT = 0xFFE0
local C_GHOST = 0x5AEB
local C_SPECIAL = 0xFD20

local function resolve_url()
  local direct = tostring(data.get("daily_flip_calendar.url") or "")
  if direct ~= "" then return direct end
  local base = tostring(data.get("calendar.api_base_url") or "")
  if base == "" then return "" end
  if string.sub(base, -1) == "/" then return base .. "daily-flip" end
  return base .. "/daily-flip"
end

local APP_URL = resolve_url()
local TTL_MS = tonumber(data.get("daily_flip_calendar.ttl_ms") or data.get("calendar.ttl_ms") or 60000) or 60000
local TIMEOUT_MS = tonumber(data.get("daily_flip_calendar.timeout_ms") or data.get("calendar.timeout_ms") or 6000) or 6000
local MAX_BODY = tonumber(data.get("daily_flip_calendar.max_body") or data.get("calendar.max_body") or 24576) or 24576

local WEEKDAYS = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"}
local MONTHS = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"}
local DIGITS = {
  ["0"] = {"111", "101", "101", "101", "111"},
  ["1"] = {"010", "110", "010", "010", "111"},
  ["2"] = {"111", "001", "111", "100", "111"},
  ["3"] = {"111", "001", "111", "001", "111"},
  ["4"] = {"101", "101", "111", "001", "001"},
  ["5"] = {"111", "100", "111", "001", "111"},
  ["6"] = {"111", "100", "111", "101", "111"},
  ["7"] = {"111", "001", "010", "010", "010"},
  ["8"] = {"111", "101", "111", "101", "111"},
  ["9"] = {"111", "101", "111", "001", "111"},
}

local state = {
  req_id = nil,
  last_req_ms = 0,
  last_ok_ms = 0,
  err = nil,
  payload = nil,
  flip_ms = 900,
  last_day_key = "",
}

local function now_ms()
  return sys.now_ms()
end

local function rect_safe(fb, x, y, w, h, c)
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

local function draw_digit(fb, ch, x, y, scale, color)
  local pat = DIGITS[ch]
  if not pat then return end
  for row = 1, #pat do
    local line = pat[row]
    for col = 1, #line do
      if string.sub(line, col, col) == "1" then
        rect_safe(fb, x + (col - 1) * scale, y + (row - 1) * scale, scale, scale, color)
      end
    end
  end
end

local function draw_tile(fb, x, y, ch, phase)
  rect_safe(fb, x, y, 13, 19, C_CARD_TOP)
  rect_safe(fb, x, y + 9, 13, 10, C_CARD_BOT)
  rect_safe(fb, x, y + 9, 13, 1, C_SPLIT)
  rect_safe(fb, x, y, 13, 1, C_FRAME)
  rect_safe(fb, x, y + 18, 13, 1, C_FRAME)
  rect_safe(fb, x, y, 1, 19, C_FRAME)
  rect_safe(fb, x + 12, y, 1, 19, C_FRAME)
  draw_digit(fb, ch, x + 2, y + 2, 3, C_TEXT)
  if phase > 0 then
    local sweep = math.floor((phase / 900) * 19)
    rect_safe(fb, x, y + sweep, 13, 1, C_ACCENT)
  end
end

local function local_today()
  local t = sys.local_time and sys.local_time() or {}
  local wday = tonumber(t.wday or 1) or 1
  local month = tonumber(t.month or 1) or 1
  if wday < 1 or wday > 7 then wday = 1 end
  if month < 1 or month > 12 then month = 1 end
  return {
    year = tonumber(t.year or 2026) or 2026,
    month = month,
    day = tonumber(t.day or 1) or 1,
    weekday = WEEKDAYS[wday],
    month_label = MONTHS[month],
    holiday = "",
  }
end

local function snapshot_today()
  local fallback = local_today()
  local today = state.payload and state.payload.today or nil
  if type(today) ~= "table" then return fallback end
  return {
    year = tonumber(today.year or fallback.year) or fallback.year,
    month = tonumber(today.month or fallback.month) or fallback.month,
    day = tonumber(today.day or fallback.day) or fallback.day,
    weekday = tostring(today.weekday or fallback.weekday),
    month_label = tostring(today.month_label or fallback.month_label),
    holiday = tostring(today.holiday or ""),
  }
end

local function footer_text()
  local footer = state.payload and state.payload.footer or nil
  if type(footer) == "table" and tostring(footer.text or "") ~= "" then
    local tone = tostring(footer.tone or "")
    local color = C_TEXT_DIM
    if tone == "special" then color = C_SPECIAL end
    if tone == "accent" then color = C_ACCENT end
    return compact_text(footer.text, 16), color
  end
  return "Calendar day", C_TEXT_DIM
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
  state.flip_ms = 900
  state.last_day_key = ""
  start_request()
end

function app.tick(dt_ms)
  state.flip_ms = math.max(0, state.flip_ms - (dt_ms or 0))
  local today = snapshot_today()
  local key = tostring(today.year) .. "-" .. tostring(today.month) .. "-" .. tostring(today.day)
  if state.last_day_key == "" then
    state.last_day_key = key
  elseif state.last_day_key ~= key then
    state.last_day_key = key
    state.flip_ms = 900
  end

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
  rect_safe(fb, 0, 0, 64, 1, C_FRAME)
  rect_safe(fb, 0, 31, 64, 1, C_FRAME)

  local today = snapshot_today()
  local footer, footer_color = footer_text()
  local day = string.format("%02d", tonumber(today.day or 1) or 1)

  fb:text_box(2, 0, 34, 8, tostring(today.month_label) .. " " .. tostring(today.year), C_TEXT_DIM, FONT_UI, 8, "left", false)
  fb:text_box(40, 0, 22, 8, tostring(today.weekday), C_ACCENT, FONT_UI, 8, "right", false)
  draw_tile(fb, 17, 7, string.sub(day, 1, 1), state.flip_ms)
  draw_tile(fb, 34, 7, string.sub(day, 2, 2), state.flip_ms)
  fb:text_box(2, 25, 60, 8, footer, footer_color, FONT_UI, 8, "center", false)
end

return app
