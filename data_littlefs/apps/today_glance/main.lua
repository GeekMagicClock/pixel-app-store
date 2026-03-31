local app = {}

-- Remote config:
--   today_glance.url = "https://calendar.example.com/calendar/v1/today-glance"
-- Optional fallback:
--   calendar.api_base_url = "https://calendar.example.com/calendar/v1"
-- Optional transport tuning:
--   today_glance.ttl_ms
--   today_glance.meta_poll_ms
--   today_glance.meta_ttl_ms
--   today_glance.timeout_ms
--   today_glance.max_body
-- Fallback transport tuning:
--   calendar.ttl_ms
--   calendar.timeout_ms
--   calendar.max_body

local FONT_UI = "builtin:silkscreen_regular_8"
local FONT_TITLE = "builtin:pressstart2p_regular_8"

local C_BG = 0x0000
local C_PANEL = 0x0843
local C_TEXT = 0xFFFF
local C_MUTED = 0x9CF3
local C_CYAN = 0x07FF
local C_WARM = 0xFD20
local C_GREEN = 0x87F0
local C_RED = 0xF800
local C_YELLOW = 0xFFE0
local C_STALE = 0x8410

local function resolve_url()
  local direct = tostring(data.get("today_glance.url") or "")
  if direct ~= "" then return direct end
  local base = tostring(data.get("calendar.api_base_url") or "")
  if base == "" then return "" end
  if string.sub(base, -1) == "/" then return base .. "today-glance" end
  return base .. "/today-glance"
end

local function resolve_meta_url()
  local direct = tostring(data.get("today_glance.meta_url") or "")
  if direct ~= "" then return direct end
  local base = tostring(data.get("calendar.api_base_url") or "")
  if base == "" then return "" end
  if string.sub(base, -1) == "/" then return base .. "today-glance-meta" end
  return base .. "/today-glance-meta"
end

local APP_URL = resolve_url()
local META_URL = resolve_meta_url()
local TTL_MS = tonumber(data.get("today_glance.ttl_ms") or data.get("calendar.ttl_ms") or 60000) or 60000
local META_POLL_MS = tonumber(data.get("today_glance.meta_poll_ms") or 5000) or 5000
local META_TTL_MS = tonumber(data.get("today_glance.meta_ttl_ms") or 1000) or 1000
local TIMEOUT_MS = tonumber(data.get("today_glance.timeout_ms") or data.get("calendar.timeout_ms") or 6000) or 6000
local MAX_BODY = tonumber(data.get("today_glance.max_body") or data.get("calendar.max_body") or 24576) or 24576

local state = {
  req_id = nil,
  req_kind = nil,
  last_req_ms = 0,
  last_ok_ms = 0,
  last_meta_req_ms = 0,
  err = nil,
  payload = nil,
  meta = nil,
  revision = "",
  wanted_revision = "",
  anim_ms = 0,
  last_err_ms = 0,
  ok_streak = 0,
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
  local n = tonumber(limit) or 18
  if #s > n then
    return string.sub(s, 1, n - 1) .. "…"
  end
  return s
end

local function fallback_today()
  local t = sys.local_time and sys.local_time() or {}
  local weekdays = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"}
  local months = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"}
  local wday = tonumber(t.wday or 1) or 1
  local month = tonumber(t.month or 1) or 1
  if wday < 1 or wday > 7 then wday = 1 end
  if month < 1 or month > 12 then month = 1 end
  return {
    weekday = weekdays[wday],
    month_label = months[month],
    day = tonumber(t.day or 1) or 1,
  }
end

local function today_data()
  local today = state.payload and state.payload.today or nil
  if type(today) ~= "table" then return fallback_today() end
  return {
    weekday = tostring(today.weekday or fallback_today().weekday),
    month_label = tostring(today.month_label or fallback_today().month_label),
    day = tonumber(today.day or fallback_today().day) or fallback_today().day,
    holiday = tostring(today.holiday or ""),
  }
end

local function summary_data()
  local s = state.payload and state.payload.summary or nil
  if type(s) ~= "table" then return {} end
  return s
end

local function next_item()
  local item = state.payload and state.payload["next"] or nil
  if type(item) == "table" then return item end
  return nil
end

local function primary_title()
  local item = next_item()
  if item and tostring(item.title or "") ~= "" then
    return compact_text(item.title, 18)
  end
  local summary = summary_data()
  if tonumber(summary.tasks_due_today or 0) > 0 then
    return compact_text(tostring(summary.tasks_due_today) .. " task due today", 18)
  end
  local primary = state.payload and state.payload.primary or nil
  if type(primary) == "table" and tostring(primary.title or "") ~= "" then
    return compact_text(primary.title, 18)
  end
  return "No urgent item"
end

local function footer_text()
  local secondary = state.payload and state.payload.secondary or nil
  if type(secondary) == "table" and tostring(secondary.text or "") ~= "" then
    return compact_text(secondary.text, 19)
  end
  local item = next_item()
  if item and item.source_label and tostring(item.source_label) ~= "" then
    return compact_text(tostring(item.source_label), 19)
  end
  return "Today glance"
end

local function has_source_errors()
  local health = state.payload and state.payload.health or nil
  return type(health) == "table" and (tonumber(health.sources_failed or 0) or 0) > 0
end

local function source_error_label()
  local health = state.payload and state.payload.health or nil
  if type(health) ~= "table" then return "" end
  local failed = tonumber(health.sources_failed or 0) or 0
  if failed <= 0 then return "" end
  return tostring(failed) .. " SRC ERR"
end

local function payload_age_minutes()
  if state.last_ok_ms <= 0 then return 0 end
  local age_ms = now_ms() - state.last_ok_ms
  if age_ms <= 0 then return 0 end
  return math.floor(age_ms / 60000)
end

local function live_minutes_left(item)
  if not item or not sys or not sys.unix_time then return nil end
  local target_unix = tonumber(item.target_unix)
  local now_unix = tonumber(sys.unix_time())
  if not target_unix or not now_unix or now_unix <= 0 then return nil end
  local diff_sec = target_unix - now_unix
  if diff_sec <= 0 then return 0 end
  return math.ceil(diff_sec / 60)
end

local function effective_minutes_left(item)
  if not item then return nil end
  local live = live_minutes_left(item)
  if live ~= nil then return live end
  local base = tonumber(item.minutes_left)
  if base == nil then return nil end
  return base - payload_age_minutes()
end

local function status_line()
  local item = next_item()
  if item then
    local minutes_left = effective_minutes_left(item)
    if minutes_left then
      if minutes_left <= 0 then return "NOW", C_RED end
      if minutes_left < 60 then
        return "IN " .. tostring(minutes_left) .. "M", C_WARM
      end
      if minutes_left < 1440 then
        local hh = tostring(item.time_label or "")
        if hh then return hh, C_CYAN end
        return tostring(math.floor((minutes_left + 59) / 60)) .. "H", C_CYAN
      end
      return "D-" .. tostring(math.floor((minutes_left + 1439) / 1440)), C_GREEN
    end
    if tostring(item.kind or "") == "task" or tostring(item.kind or "") == "deadline" then
      return "TASK", C_YELLOW
    end
    if item.all_day then return "TODAY", C_GREEN end
  end

  local summary = summary_data()
  if tonumber(summary.tasks_due_today or 0) > 0 then
    return "TASK DUE", C_YELLOW
  end
  return "FREE", C_GREEN
end

local function counters_text()
  local summary = summary_data()
  local events = tonumber(summary.events_total or summary.events or 0) or 0
  local tasks = tonumber(summary.tasks_due_today or 0) or 0
  return tostring(events) .. "E " .. tostring(tasks) .. "T"
end

local function is_stale()
  if state.payload and state.payload.stale then return true end
  if state.meta and state.meta.stale then return true end
  if state.last_ok_ms <= 0 then return false end
  return (now_ms() - state.last_ok_ms) > math.max(TTL_MS * 6, 5 * 60 * 1000)
end

local function handle_response(status, body)
  if status ~= 200 then
    state.err = "HTTP " .. tostring(status)
    state.last_err_ms = now_ms()
    state.ok_streak = 0
    return
  end
  local obj, jerr = json.decode(body)
  if not obj or type(obj) ~= "table" then
    state.err = jerr or "JSON ERR"
    state.last_err_ms = now_ms()
    state.ok_streak = 0
    return
  end
  if type(obj.today) ~= "table" or type(obj.summary) ~= "table" or type(obj["next"]) ~= "table" then
    state.err = "BAD SHAPE"
    state.last_err_ms = now_ms()
    state.ok_streak = 0
    return
  end
  state.payload = obj
  state.revision = tostring(obj.revision or state.wanted_revision or state.revision or "")
  state.err = nil
  state.last_ok_ms = now_ms()
  state.ok_streak = state.ok_streak + 1
end

local function handle_meta_response(status, body)
  if status ~= 200 then
    state.err = "META " .. tostring(status)
    state.last_err_ms = now_ms()
    return
  end
  local obj, jerr = json.decode(body)
  if not obj or type(obj) ~= "table" then
    state.err = jerr or "META JSON"
    state.last_err_ms = now_ms()
    return
  end
  state.meta = obj
  local incoming_revision = tostring(obj.revision or "")
  if incoming_revision ~= "" and incoming_revision ~= state.revision then
    state.wanted_revision = incoming_revision
  end
end

local function refresh_interval_ms()
  local item = next_item()
  local minutes_left = effective_minutes_left(item)
  if state.err then return 10000 end
  if has_source_errors() then return 10000 end
  if minutes_left and minutes_left <= 15 then return 5000 end
  if minutes_left and minutes_left <= 120 then return 10000 end
  return math.max(TTL_MS, 30000)
end

local function build_full_url(revision)
  local rev = tostring(revision or "")
  if rev == "" then return APP_URL end
  if string.find(APP_URL, "?", 1, true) then
    return APP_URL .. "&rev=" .. rev
  end
  return APP_URL .. "?rev=" .. rev
end

local function start_full_request(revision)
  if state.req_id then return end
  if APP_URL == "" then
    state.err = "SET URL"
    state.last_err_ms = now_ms()
    return
  end
  local fetch_url = build_full_url(revision)
  local id, body, age_ms, err = net.cached_get(fetch_url, TTL_MS, TIMEOUT_MS, MAX_BODY)
  if err then
    state.err = tostring(err)
    state.last_err_ms = now_ms()
    return
  end
  if body then
    state.wanted_revision = tostring(revision or state.wanted_revision or "")
    handle_response(200, body)
    state.last_req_ms = now_ms()
    return
  end
  if id then
    state.req_id = id
    state.req_kind = "full"
    state.wanted_revision = tostring(revision or state.wanted_revision or "")
    state.last_req_ms = now_ms()
    return
  end
  state.err = "HTTP FAIL"
  state.last_err_ms = now_ms()
end

local function start_meta_request()
  if state.req_id then return end
  if META_URL == "" then
    start_full_request(state.revision)
    return
  end
  local id, body, age_ms, err = net.cached_get(META_URL, META_TTL_MS, TIMEOUT_MS, 2048)
  if err then
    state.err = tostring(err)
    state.last_err_ms = now_ms()
    return
  end
  if body then
    handle_meta_response(200, body)
    state.last_meta_req_ms = now_ms()
    return
  end
  if id then
    state.req_id = id
    state.req_kind = "meta"
    state.last_meta_req_ms = now_ms()
    return
  end
  state.err = "META FAIL"
  state.last_err_ms = now_ms()
end

function app.init()
  state.req_id = nil
  state.req_kind = nil
  state.last_req_ms = 0
  state.last_ok_ms = 0
  state.last_meta_req_ms = 0
  state.err = nil
  state.payload = nil
  state.meta = nil
  state.revision = ""
  state.wanted_revision = ""
  state.anim_ms = 0
  state.last_err_ms = 0
  state.ok_streak = 0
  start_full_request("")
end

function app.tick(dt_ms)
  state.anim_ms = (state.anim_ms + (dt_ms or 0)) % 4000
  if state.req_id then
    local done, status, body = net.cached_poll(state.req_id)
    if done then
      local kind = state.req_kind
      state.req_id = nil
      state.req_kind = nil
      if status == 0 then
        state.err = body or "HTTP ERR"
        state.last_err_ms = now_ms()
      else
        if kind == "meta" then
          handle_meta_response(status, body or "")
        else
          handle_response(status, body or "")
        end
      end
    end
    return
  end

  if state.wanted_revision ~= "" and state.wanted_revision ~= state.revision then
    start_full_request(state.wanted_revision)
    return
  end

  if state.payload and now_ms() - state.last_meta_req_ms >= META_POLL_MS then
    start_meta_request()
    return
  end

  local interval = refresh_interval_ms()
  if now_ms() - state.last_req_ms >= interval then
    start_full_request(state.revision)
  end
end

function app.render_fb(fb)
  fb:fill(C_BG)
  local yoff = -2
  hline(fb, 0, 63, 8 + yoff, C_PANEL)
  hline(fb, 0, 63, 24 + yoff, C_PANEL)

  if state.err and not state.payload then
    fb:text_box(0, 2 + yoff, 64, 8, "TODAY GLANCE", C_TEXT, FONT_UI, 8, "center", true)
    fb:text_box(0, 12 + yoff, 64, 8, compact_text(tostring(state.err), 16), C_RED, FONT_UI, 8, "center", true)
    fb:text_box(0, 22 + yoff, 64, 8, "today_glance.url", C_MUTED, FONT_UI, 8, "center", true)
    return
  end

  local today = today_data()
  local status, accent = status_line()
  fb:text_box(1, 0 + yoff, 38, 8, tostring(today.weekday) .. " " .. tostring(today.day) .. " " .. tostring(today.month_label), C_TEXT, FONT_UI, 8, "left", false)
  fb:text_box(41, 0 + yoff, 22, 8, counters_text(), C_MUTED, FONT_UI, 8, "right", false)
  fb:text_box(1, 10 + yoff, 62, 8, status, accent, FONT_TITLE, 8, "left", false)
  fb:text_box(1, 17 + yoff, 62, 8, primary_title(), C_TEXT, FONT_UI, 8, "left", false)
  fb:text_box(1, 25 + yoff, 62, 8, footer_text(), C_MUTED, FONT_UI, 8, "left", false)

  if has_source_errors() then
    fb:text_box(44, 25 + yoff, 19, 8, source_error_label(), C_YELLOW, FONT_UI, 8, "right", false)
  end

  if is_stale() then
    rect_fill(fb, 60, 29, 3, 3, C_STALE)
  end
end

return app
