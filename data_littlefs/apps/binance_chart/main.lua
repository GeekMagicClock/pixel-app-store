local app = {}

local symbols = {
  "BTCUSDT",
  "ETHUSDT",
  "BNBUSDT",
  "SOLUSDT",
  "XRPUSDT",
  "ADAUSDT",
  "DOGEUSDT",
  "TRXUSDT",
  "AVAXUSDT",
  "DOTUSDT",
}

local SYMBOL = tostring(data.get("binance_chart.symbol") or symbols[1])
local INTERVAL = tostring(data.get("binance_chart.interval") or "1m")
local KLIMIT = tonumber(data.get("binance_chart.limit") or 32) or 32
local CHART_TYPE = string.lower(tostring(data.get("binance_chart.chart_type") or "mountain"))
if CHART_TYPE ~= "line" and CHART_TYPE ~= "mountain" then
  CHART_TYPE = "line"
end
if KLIMIT < 16 then KLIMIT = 16 end
if KLIMIT > 64 then KLIMIT = 64 end

local C_BG = 0x0000
local C_TEXT = 0xFFFF
local C_MUTED = 0x9CF3
local C_UP = 0x07E0
local C_DOWN = 0xF800
local C_LINE = 0x07FF
local C_MTN = 0x0410

local FONT = "builtin:silkscreen_regular_8"

local HOSTS = {
  "http://data-api.binance.vision",
}

local state = {
  idx = 1,
  last_rotate_ms = 0,
  rotate_interval_ms = 10000,
  pending_symbol = nil,
  fetch_symbol = SYMBOL,
  last_req_start_ms = 0,
  req_id = nil,
  req_kind = nil, -- "ticker" | "klines"
  req_symbol = nil,
  req_host_idx = 1,
  cycle_due_ms = 0,
  last_ok_ms = 0,
  last_err = nil,
  backoff_ms = 0,
  backoff_until_ms = 0,
  next_kind = "ticker",

  symbol = SYMBOL,
  prices = {},
  price_texts = {},
  change_pcts = {},
  closes_map = {},
  active_klimit = KLIMIT,
  klines_pause_until_ms = 0,
  klines_fail_count = 0,
}

local function now_ms()
  return sys.now_ms()
end

local function current_symbol()
  return symbols[state.idx]
end

local function find_symbol_index(sym)
  for i = 1, #symbols do
    if symbols[i] == sym then return i end
  end
  return 1
end

local function has_symbol_data(sym)
  if not sym then return false end
  local p = state.price_texts[sym]
  local c = state.closes_map[sym]
  return (p ~= nil and p ~= "") and (c ~= nil and #c >= 2)
end

local function commit_switch(sym, now)
  state.idx = find_symbol_index(sym)
  state.symbol = sym
  state.pending_symbol = nil
  state.fetch_symbol = sym
  state.last_rotate_ms = now
  state.next_kind = "ticker"
  state.cycle_due_ms = 0
  state.last_err = nil
end

local function compact_symbol(sym)
  if string.sub(sym, -4) == "USDT" then
    return string.sub(sym, 1, #sym - 4)
  end
  return sym
end

local function pct_text(v)
  if v == nil then return "--" end
  if v > 0 then return string.format("+%.1f%%", v) end
  return string.format("%.1f%%", v)
end

local function pct_color(v)
  if v == nil then return C_MUTED end
  if v > 0 then return C_UP end
  if v < 0 then return C_DOWN end
  return C_MUTED
end

local function normalize_price_text(s)
  if not s or s == "" then return "--" end
  return tostring(s)
end

local function fit_price_text(s)
  if not s or s == "" then return "--" end
  local dot = string.find(s, "%.")
  if not dot then
    return string.sub(s, 1, 7)
  end

  local intp = string.sub(s, 1, dot - 1)
  local frac = string.sub(s, dot + 1)
  if frac == "" then return intp .. ".0" end

  while #frac > 0 and string.sub(frac, -1) == "0" do
    frac = string.sub(frac, 1, #frac - 1)
  end
  if frac == "" then frac = "0" end

  local max_chars = 7
  local room = max_chars - #intp - 1
  if room < 1 then room = 1 end
  if room > #frac then room = #frac end
  return intp .. "." .. string.sub(frac, 1, room)
end

local function ticker_url(sym, host_idx)
  local host = HOSTS[host_idx] or HOSTS[1]
  return string.format("%s/api/v3/ticker/24hr?symbol=%s", host, sym)
end

local function klines_url(sym, host_idx)
  local host = HOSTS[host_idx] or HOSTS[1]
  return string.format("%s/api/v3/klines?symbol=%s&interval=%s&limit=%d", host, sym, INTERVAL, state.active_klimit)
end

local function klines_buf_size(limit)
  if limit >= 64 then return 16384 end
  if limit >= 48 then return 8192 end
  if limit >= 32 then return 8192 end
  return 4096
end

local function interval_ms(interval)
  local s = tostring(interval or "1m")
  local n, unit = string.match(s, "^(%d+)([mhdwM])$")
  n = tonumber(n or "1") or 1
  if unit == "m" then return n * 60 * 1000 end
  if unit == "h" then return n * 60 * 60 * 1000 end
  if unit == "d" then return n * 24 * 60 * 60 * 1000 end
  if unit == "w" then return n * 7 * 24 * 60 * 60 * 1000 end
  if unit == "M" then return n * 30 * 24 * 60 * 60 * 1000 end
  return 60 * 1000
end

local function parse_ticker(sym, body)
  local ok_decode, obj_or_err, err2 = pcall(json.decode, body)
  local obj = nil
  local err = nil
  if ok_decode then
    obj = obj_or_err
    err = err2
  else
    return false, tostring(obj_or_err or "JSON ERR")
  end
  if not obj then return false, (err or "JSON ERR") end

  local s = tostring(obj.symbol or "")
  local p_str = tostring(obj.lastPrice or obj.price or "")
  local p_num = tonumber(obj.lastPrice or obj.price)
  local pct = tonumber(obj.priceChangePercent)

  if s ~= sym or p_str == "" or not p_num then
    return false, "BAD TICKER"
  end

  state.prices[sym] = p_num
  state.price_texts[sym] = normalize_price_text(p_str)
  state.change_pcts[sym] = pct
  return true, nil
end

local function parse_klines(sym, body)
  -- Low-memory fast path: extract close price directly from JSON text.
  local out = {}
  for s in string.gmatch(body or "", "%[%d+,%s*\"[^\"]+\",%s*\"[^\"]+\",%s*\"[^\"]+\",%s*\"([^\"]+)\"") do
    local c = tonumber(s)
    if c then out[#out + 1] = c end
  end

  -- Fallback parser for non-quoted close fields.
  if #out < 2 then
    for s in string.gmatch(body or "", "%[%d+,%s*[%d%.%-eE]+,%s*[%d%.%-eE]+,%s*[%d%.%-eE]+,%s*([%d%.%-eE]+)") do
      local c = tonumber(s)
      if c then out[#out + 1] = c end
    end
  end

  if #out < 2 then return false, "BAD CLOSE" end
  state.closes_map[sym] = out
  return true, nil
end

local function note_error(err, failed_kind_override)
  local failed_kind = failed_kind_override or state.req_kind or state.next_kind
  state.last_err = tostring(err or "ERR")
  local old_limit = state.active_klimit
  local lower_err = string.lower(state.last_err)
  local is_oom = string.find(lower_err, "oom", 1, true) or
                 string.find(lower_err, "memory", 1, true) or
                 string.find(lower_err, "not enough", 1, true)
  if is_oom and state.active_klimit > 24 then
    if state.active_klimit > 48 then
      state.active_klimit = 48
    elseif state.active_klimit > 32 then
      state.active_klimit = 32
    else
      state.active_klimit = 24
    end
  end
  if string.find(state.last_err, "body too large", 1, true) and state.active_klimit > 24 then
    if state.active_klimit > 32 then
      state.active_klimit = 32
    else
      state.active_klimit = 24
    end
  end
  if state.last_err == "HTTP 0" and state.active_klimit > 32 then
    state.active_klimit = 32
  end
  if state.active_klimit ~= old_limit then
    sys.log("binance_chart kline limit fallback " .. tostring(old_limit) .. " -> " .. tostring(state.active_klimit))
  end

  if failed_kind == "klines" then
    state.klines_fail_count = (state.klines_fail_count or 0) + 1
    if is_oom then
      state.active_klimit = 24
      state.klines_pause_until_ms = now_ms() + 180000 -- pause 3 min on OOM
      sys.log("binance_chart klines paused 180000ms due to OOM")
    elseif state.klines_fail_count >= 3 then
      state.klines_pause_until_ms = now_ms() + 60000 -- pause 1 min
      sys.log("binance_chart klines paused 60000ms after repeated failures")
    end
  end

  if state.backoff_ms <= 0 then state.backoff_ms = 5000 else state.backoff_ms = state.backoff_ms * 2 end
  if state.backoff_ms > 60000 then state.backoff_ms = 60000 end
  state.backoff_until_ms = now_ms() + state.backoff_ms
  if failed_kind == "klines" then
    state.next_kind = "ticker"
  end
  state.cycle_due_ms = state.backoff_until_ms
  sys.log("binance_chart req error kind=" .. tostring(failed_kind) .. " err=" .. state.last_err .. " backoff_ms=" .. tostring(state.backoff_ms))
  collectgarbage("collect")
end

local function note_success(kind)
  state.last_err = nil
  state.backoff_ms = 0
  state.backoff_until_ms = 0
  state.last_ok_ms = now_ms()
  if kind == "ticker" then
    state.next_kind = "klines"
    state.cycle_due_ms = now_ms() + 50
  else
    state.klines_fail_count = 0
    state.next_kind = "ticker"
    state.cycle_due_ms = now_ms() + 15000
  end
  collectgarbage("collect")
end

local function rotate_symbol(now)
  if #symbols <= 1 then return end
  if state.pending_symbol then
    if has_symbol_data(state.pending_symbol) then
      commit_switch(state.pending_symbol, now)
    end
    return
  end

  if now - (state.last_rotate_ms or 0) < (state.rotate_interval_ms or 10000) then return end
  local next_idx = (state.idx % #symbols) + 1
  local next_sym = symbols[next_idx]
  if has_symbol_data(next_sym) then
    commit_switch(next_sym, now)
  else
    state.pending_symbol = next_sym
    state.fetch_symbol = next_sym
    state.next_kind = "ticker"
    state.cycle_due_ms = 0
  end
end

local function start_next_request()
  if state.req_id then return end
  local now = now_ms()
  if now - (state.last_req_start_ms or 0) < 800 then return end
  if state.backoff_until_ms > now then return end
  if now < (state.cycle_due_ms or 0) then return end

  local kind = state.next_kind or "ticker"
  local sym = state.fetch_symbol or state.symbol
  if kind == "klines" and (state.klines_pause_until_ms or 0) > now then
    state.next_kind = "ticker"
    state.cycle_due_ms = now + 15000
    return
  end
  local host_idx = state.req_host_idx or 1
  local ttl_ms = 20 * 1000
  local url = nil
  local timeout_ms = 5000
  local max_body = 2048

  if kind == "ticker" then
    url = ticker_url(sym, host_idx)
    timeout_ms = 4000
    max_body = 1024
  else
    url = klines_url(sym, host_idx)
    timeout_ms = 7000
    max_body = klines_buf_size(state.active_klimit)
    local im = interval_ms(INTERVAL)
    if im < 30 * 1000 then
      ttl_ms = 20 * 1000
    else
      -- Avoid hammering API while still refreshing around each new candle.
      ttl_ms = im - 5000
    end
  end

  sys.log(string.format(
    "binance_chart req start kind=%s url=%s timeout=%d max_body=%d",
    tostring(kind), tostring(url), timeout_ms, max_body
  ))

  local ok_call, id, body, age_ms, err = pcall(net.cached_get, url, ttl_ms, timeout_ms, max_body)
  state.last_req_start_ms = now
  if not ok_call then
    note_error("cached_get call failed: " .. tostring(id), kind)
    return
  end
  if err then
    sys.log("binance_chart cached_get err: " .. tostring(err))
    note_error(err, kind)
    return
  end

  if id then
    state.req_id = id
    state.req_kind = kind
    state.req_symbol = state.symbol
  end

  if body then
    sys.log("binance_chart cached_get hit kind=" .. tostring(kind) .. " bytes=" .. tostring(#body))
    if age_ms then
      sys.log("binance_chart cached_get age_ms kind=" .. tostring(kind) .. " age=" .. tostring(age_ms))
    end
    local ok, perr
    if kind == "ticker" then
      ok, perr = parse_ticker(sym, body)
    else
      ok, perr = parse_klines(sym, body)
    end
    if ok then
      note_success(kind)
    else
      note_error(perr, kind)
    end
    return
  end

  if state.req_id then
    return
  end

  note_error("REQ FAIL", kind)
end

local function poll_request()
  if not state.req_id then return end

  local ok_call, done, status, body = pcall(net.cached_poll, state.req_id)
  if not ok_call then
    local e = tostring(done)
    state.req_id = nil
    state.req_kind = nil
    note_error("cached_poll call failed: " .. e, state.req_kind)
    return
  end
  if not done then return end

  local kind = state.req_kind
  local req_symbol = state.req_symbol
  state.req_id = nil
  state.req_kind = nil
  state.req_symbol = nil
  state.last_req_start_ms = now_ms()

  -- Symbol rotated while request was in flight: ignore stale response.
  if req_symbol and req_symbol ~= state.symbol then
    -- Still cache data for non-displayed symbol, but do not treat as active refresh.
  end

  local body_len = (body and #body) or 0
  if status ~= 200 then
    local detail = ""
    if body and body ~= "" then
      detail = " detail=" .. string.sub(tostring(body), 1, 80)
    end
    sys.log(string.format(
      "binance_chart req done kind=%s status=%s body_len=%d%s",
      tostring(kind), tostring(status), body_len, detail
    ))
  end

  if status == 429 then
    note_error("HTTP 429", kind)
    return
  end
  if status ~= 200 then
    if status == 0 and body and body ~= "" then
      note_error("HTTP 0 " .. tostring(body), kind)
    else
      note_error("HTTP " .. tostring(status), kind)
    end
    return
  end

  local ok, err
  if kind == "ticker" then
    ok, err = parse_ticker(req_symbol or state.fetch_symbol or state.symbol, body or "")
  else
    ok, err = parse_klines(req_symbol or state.fetch_symbol or state.symbol, body or "")
  end

  if ok then
    note_success(kind)
  else
    note_error(err, kind)
  end
end

local function draw_line(fb, x0, y0, x1, y1, c)
  local dx = math.abs(x1 - x0)
  local sx = (x0 < x1) and 1 or -1
  local dy = -math.abs(y1 - y0)
  local sy = (y0 < y1) and 1 or -1
  local err = dx + dy

  while true do
    fb:set_px(x0, y0, c)
    if x0 == x1 and y0 == y1 then break end
    local e2 = err * 2
    if e2 >= dy then
      err = err + dy
      x0 = x0 + sx
    end
    if e2 <= dx then
      err = err + dx
      y0 = y0 + sy
    end
  end
end

local function fill_column_to_bottom(fb, x, y_top, y_bottom, c)
  if y_top > y_bottom then
    local t = y_top
    y_top = y_bottom
    y_bottom = t
  end
  for y = y_top, y_bottom do
    fb:set_px(x, y, c)
  end
end

local function fill_mountain_segment(fb, x0, y0, x1, y1, y_bottom, c)
  if x0 == x1 then
    fill_column_to_bottom(fb, x0, y0, y_bottom, c)
    return
  end

  if x0 > x1 then
    local tx = x0
    local ty = y0
    x0 = x1
    y0 = y1
    x1 = tx
    y1 = ty
  end

  local dx = x1 - x0
  for x = x0, x1 do
    local t = (x - x0) / dx
    local y = math.floor(y0 + (y1 - y0) * t + 0.5)
    fill_column_to_bottom(fb, x, y, y_bottom, c)
  end
end

local function draw_chart(fb, closes)
  if not closes or #closes < 2 then
    fb:text_box(0, 20, 64, 8, "NO CHART", C_MUTED, FONT, 8, "center", true)
    return
  end

  local min_v = closes[1]
  local max_v = closes[1]
  for i = 2, #closes do
    local v = closes[i]
    if v < min_v then min_v = v end
    if v > max_v then max_v = v end
  end

  local x0 = 0
  local y0 = 16
  local w = 64
  local h = 16
  local n = #closes

  local range = max_v - min_v
  if range <= 0 then range = 1 end

  local prev_x = nil
  local prev_y = nil
  for i = 1, n do
    local px = x0 + math.floor((i - 1) * (w - 1) / (n - 1))
    local norm = (closes[i] - min_v) / range
    local py = y0 + (h - 1) - math.floor(norm * (h - 1))
    if CHART_TYPE == "mountain" then
      if prev_x ~= nil then
        fill_mountain_segment(fb, prev_x, prev_y, px, py, y0 + h - 1, C_MTN)
      else
        fill_column_to_bottom(fb, px, py, y0 + h - 1, C_MTN)
      end
    end

    if prev_x ~= nil then
      draw_line(fb, prev_x, prev_y, px, py, C_LINE)
    else
      fb:set_px(px, py, C_LINE)
    end
    prev_x = px
    prev_y = py
  end

end

function app.init(config)
  sys.log("binance_chart init")
  state.idx = 1
  for i = 1, #symbols do
    if symbols[i] == SYMBOL then
      state.idx = i
      break
    end
  end
  state.last_rotate_ms = now_ms()
  state.rotate_interval_ms = 10000
  state.pending_symbol = nil
  state.fetch_symbol = SYMBOL
  state.last_req_start_ms = 0
  state.req_id = nil
  state.req_kind = nil
  state.req_symbol = nil
  state.req_host_idx = 1
  state.cycle_due_ms = 0
  state.last_ok_ms = 0
  state.last_err = nil
  state.backoff_ms = 0
  state.backoff_until_ms = 0
  state.next_kind = "ticker"
  state.prices = {}
  state.price_texts = {}
  state.change_pcts = {}
  state.closes_map = {}
  state.active_klimit = KLIMIT
  state.klines_pause_until_ms = 0
  state.klines_fail_count = 0
  state.symbol = current_symbol()
  state.fetch_symbol = state.symbol

  start_next_request()
end

function app.tick(dt_ms)
  rotate_symbol(now_ms())
  poll_request()
  start_next_request()
end

function app.render_fb(fb)
  fb:fill(C_BG)

  local sym = compact_symbol(state.symbol)
  local sym = state.symbol
  local pct_v = state.change_pcts[sym]
  local pct = pct_text(pct_v)
  local pct_c = pct_color(pct_v)
  local price = fit_price_text(state.price_texts[sym])

  -- Row 1 (8px): symbol left, change% right
  fb:text_box(0, -3, 32, 8, sym, C_TEXT, FONT, 8, "left", true)
  fb:text_box(32, -3, 32, 8, pct, pct_c, FONT, 8, "right", true)

  -- Row 2 (8px): price
  fb:text_box(0, 5, 64, 8, price, C_TEXT, FONT, 8, "left", true)

  -- Row 3 (16px): chart
  draw_chart(fb, state.closes_map[sym])

  if state.last_err then
    fb:text_box(0, 24, 64, 8, tostring(state.last_err), C_DOWN, FONT, 8, "center", true)
  end
end

return app
