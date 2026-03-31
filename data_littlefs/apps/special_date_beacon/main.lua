local app = {}

local FONT_UI = "builtin:silkscreen_regular_8"
local FONT_TITLE = "builtin:pressstart2p_regular_8"

local C_BG = 0x0000
local C_TEXT = 0xFFFF
local C_MUTED = 0x9CF3
local C_PANEL = 0x0843
local C_HOLIDAY = 0xFFE0
local C_ANNIV = 0xFD20
local C_BILL = 0x07FF
local C_ALERT = 0xF800
local C_OK = 0x87F0

local function resolve_url()
  local direct = tostring(data.get("special_date_beacon.url") or "")
  if direct ~= "" then return direct end
  local base = tostring(data.get("calendar.api_base_url") or "")
  if base == "" then return "" end
  if string.sub(base, -1) == "/" then return base .. "special-date" end
  return base .. "/special-date"
end

local APP_URL = resolve_url()
local TTL_MS = tonumber(data.get("special_date_beacon.ttl_ms") or data.get("calendar.ttl_ms") or 60000) or 60000
local TIMEOUT_MS = tonumber(data.get("special_date_beacon.timeout_ms") or data.get("calendar.timeout_ms") or 6000) or 6000
local MAX_BODY = tonumber(data.get("special_date_beacon.max_body") or data.get("calendar.max_body") or 24576) or 24576

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

local function hline(fb, x0, x1, y, c)
  if x0 > x1 then x0, x1 = x1, x0 end
  for x = x0, x1 do set_px_safe(fb, x, y, c) end
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
  local n = tonumber(limit) or 16
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

local function item_accent(kind)
  local k = tostring(kind or "")
  if k == "holiday" then return C_HOLIDAY end
  if k == "anniversary" or k == "birthday" then return C_ANNIV end
  if k == "bill" or k == "payday" then return C_BILL end
  return C_OK
end

local function pick_special()
  if not state.payload then return nil end
  if type(state.payload.item) == "table" then return state.payload.item end
  return nil
end

local function model()
  local item = pick_special()
  if not item then
    return { mode = "word", word = "CLEAR", accent = C_OK, title = "No special date", detail = "No near holiday" }
  end

  local accent = item_accent(item.kind)
  local title = compact_text(item.title or item.kind, 16)
  local date_label = compact_text(item.date_label or item.date or "", 14)
  local kind_label = compact_text(tostring(item.label or string.upper(tostring(item.kind or "date"))), 14)
  local days_left = tonumber(item.days_left)

  if days_left and days_left <= 0 then
    return { mode = "word", word = "TODAY", accent = accent, title = title, detail = date_label ~= "" and date_label or kind_label }
  end
  if days_left then
    return { mode = "digits", value = tostring(math.min(99, math.max(1, math.floor(days_left + 0.5)))), unit = "DAYS", accent = accent, title = title, detail = date_label ~= "" and date_label or kind_label }
  end
  return { mode = "word", word = "SOON", accent = accent, title = title, detail = date_label ~= "" and date_label or kind_label }
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
  hline(fb, 0, 63, 8, C_PANEL)
  hline(fb, 0, 63, 24, C_PANEL)

  if state.err and not state.payload then
    fb:text_box(0, 2, 64, 8, "SPECIAL DATE", C_TEXT, FONT_UI, 8, "center", true)
    fb:text_box(0, 12, 64, 8, tostring(state.err), C_ALERT, FONT_UI, 8, "center", true)
    fb:text_box(0, 22, 64, 8, "special_date_beacon.url", C_MUTED, FONT_UI, 8, "center", true)
    return
  end

  local m = model()
  fb:text_box(1, 0, 62, 8, "SPECIAL DATE", C_MUTED, FONT_UI, 8, "left", false)
  if m.mode == "digits" then
    draw_big_value(fb, m.value, 2, 10, 2, m.accent)
    fb:text_box(22, 10, 40, 8, m.unit or "", m.accent, FONT_TITLE, 8, "left", false)
  else
    fb:text_box(2, 10, 60, 8, m.word or "--", m.accent, FONT_TITLE, 8, "left", false)
  end
  fb:text_box(2, 18, 60, 8, m.title or "", C_TEXT, FONT_UI, 8, "left", false)
  fb:text_box(2, 25, 60, 8, m.detail or "", C_MUTED, FONT_UI, 8, "left", false)
end

return app
