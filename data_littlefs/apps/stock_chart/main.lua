local app = {}

local DEFAULT_SYMBOLS = {
  "AMZN",
  "TSLA",
  "AAPL",
  "MSFT",
  "NVDA",
  "GOOGL",
  "META",
  "SONY",
  "SSNLF",
  "GC=F",
}

local function normalize_symbols(raw)
  local out = {}
  local seen = {}

  local function add_symbol(sym)
    local s = string.upper(tostring(sym or ""))
    s = string.gsub(s, "%s+", "")
    if s ~= "" and not seen[s] then
      out[#out + 1] = s
      seen[s] = true
    end
  end

  if type(raw) == "table" then
    for i = 1, #raw do
      add_symbol(raw[i])
    end
  else
    local text = tostring(raw or "")
    for token in string.gmatch(text, "[^,]+") do
      add_symbol(token)
    end
  end

  if #out == 0 then
    for i = 1, #DEFAULT_SYMBOLS do
      out[#out + 1] = DEFAULT_SYMBOLS[i]
    end
  end
  return out
end

local symbols = normalize_symbols(data.get("stock_chart.symbols"))

local SYMBOL = tostring(data.get("stock_chart.symbol") or symbols[1])
if SYMBOL == "" then
  SYMBOL = symbols[1]
end
local INTERVAL_DEFAULT = tostring(data.get("stock_chart.interval") or "1m")
if INTERVAL_DEFAULT == "" then INTERVAL_DEFAULT = "1m" end
local KLIMIT = 64
local CHART_TYPE = string.lower(tostring(data.get("stock_chart.chart_type") or "mountain"))
if CHART_TYPE ~= "line" and CHART_TYPE ~= "mountain" then
  CHART_TYPE = "line"
end
local ROTATE_INTERVAL_MS = tonumber(data.get("stock_chart.rotate_interval_ms") or 10000) or 10000
if ROTATE_INTERVAL_MS < 5000 then ROTATE_INTERVAL_MS = 5000 end
if ROTATE_INTERVAL_MS > 600000 then ROTATE_INTERVAL_MS = 600000 end

local C_BG = 0x0000
local C_TEXT = 0xFFFF
local C_MUTED = 0x9CF3
local C_UP = 0x07E0
local C_DOWN = 0xF800
local C_UP_FILL = 0x0400
local C_DOWN_FILL = 0x8000

local FONT = "builtin:silkscreen_regular_8"
local MIN_REFRESH_MS = tonumber(data.get("stock_chart.refresh_interval_ms") or 10000) or 10000
if MIN_REFRESH_MS < 10000 then MIN_REFRESH_MS = 10000 end
if MIN_REFRESH_MS > 600000 then MIN_REFRESH_MS = 600000 end
local CACHE_KEY = "stock_chart.cache.v1"

local YAHOO_UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/115.0.0.0 Safari/537.36"
local PROXY_BASE = tostring(data.get("proxy.market_data_base") or data.get("proxy.stock_base") or "")
local HOSTS = {
  "https://query1.finance.yahoo.com",
}

local state = {
  idx = 1,
  last_rotate_ms = 0,
  rotate_interval_ms = ROTATE_INTERVAL_MS,
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
  change_vals = {},
  closes_map = {},
  cache = {},
  ticker_req_ms = {},
  klines_req_ms = {},
  active_klimit = KLIMIT,
  klines_pause_until_ms = 0,
  klines_fail_count = 0,
  yahoo_cookie = nil,
  yahoo_cookie_prev = nil,
  yahoo_crumb = nil,
  yahoo_crumb_ms = 0,
  auth_pending_kind = nil,
  interval = INTERVAL_DEFAULT,
}

local function sanitize_interval(v)
  local s = string.lower(tostring(v or ""))
  if s == "1m" or s == "5m" or s == "15m" or s == "30m" or s == "60m" or s == "1d" or s == "1wk" then
    return s
  end
  return "1m"
end

local function current_interval()
  local raw = nil
  if data and type(data.get) == "function" then
    raw = data.get("stock_chart.interval")
  end
  state.interval = sanitize_interval(raw or state.interval or INTERVAL_DEFAULT)
  return state.interval
end

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

local function symbol_exists(sym)
  for i = 1, #symbols do
    if symbols[i] == sym then return true end
  end
  return false
end

local function unix_time_s()
  return tonumber(sys.unix_time() or 0) or 0
end

local function cache_age_ms(updated_at_s)
  local now_s = unix_time_s()
  local ts = tonumber(updated_at_s or 0) or 0
  if now_s <= 0 or ts <= 0 or ts > now_s then return MIN_REFRESH_MS end
  local age_ms = (now_s - ts) * 1000
  if age_ms < 0 then return MIN_REFRESH_MS end
  local max_age_ms = 7 * 24 * 60 * 60 * 1000
  if age_ms > max_age_ms then age_ms = max_age_ms end
  return age_ms
end

local function has_symbol_data(sym)
  if not sym then return false end
  local p = state.price_texts[sym]
  local c = state.closes_map[sym]
  return (p ~= nil and p ~= "") and (c ~= nil and #c >= 2)
end

local function clone_number_array(arr)
  if type(arr) ~= "table" then return nil end
  local out = {}
  for i = 1, #arr do
    local v = tonumber(arr[i])
    if v then out[#out + 1] = v end
  end
  if #out < 2 then return nil end
  return out
end

local function normalize_price_text(s)
  if not s or s == "" then return "--" end
  return tostring(s)
end

local function normalize_cache_entry(entry)
  if type(entry) ~= "table" then return nil end
  local out = {
    price = tonumber(entry.price),
    price_text = normalize_price_text(entry.price_text),
    change_pct = tonumber(entry.change_pct),
    change_val = tonumber(entry.change_val),
    ticker_updated_at_s = tonumber(entry.ticker_updated_at_s) or 0,
    chart_updated_at_s = tonumber(entry.chart_updated_at_s) or 0,
  }
  local entry_interval = tostring(entry.interval or "")
  local entry_limit = tonumber(entry.limit or 0) or 0
  if entry_interval == current_interval() and entry_limit == KLIMIT then
    out.closes = clone_number_array(entry.closes)
    out.interval = entry_interval
    out.limit = entry_limit
  end
  if (out.price == nil) and (out.price_text == "--") and (out.change_pct == nil) and (out.change_val == nil) and (out.closes == nil) then
    return nil
  end
  return out
end

local function load_cache()
  if not data or type(data.get) ~= "function" then
    return {}, false
  end
  local raw = data.get(CACHE_KEY)
  local cache = {}
  local changed = false
  if type(raw) == "table" then
    for sym, entry in pairs(raw) do
      if symbol_exists(sym) then
        local norm = normalize_cache_entry(entry)
        if norm then
          cache[sym] = norm
          if norm.closes == nil and type(entry) == "table" and entry.closes ~= nil then
            changed = true
          end
        else
          changed = true
        end
      else
        changed = true
      end
    end
  end
  return cache, changed
end

local function save_cache()
  if not data or type(data.set) ~= "function" then
    return
  end
  local ok = data.set(CACHE_KEY, state.cache)
  if ok == false then
    sys.log("stock_chart cache save failed")
  end
end

local function hydrate_from_cache()
  local now = now_ms()
  for sym, entry in pairs(state.cache) do
    if entry.price ~= nil then state.prices[sym] = entry.price end
    if entry.price_text and entry.price_text ~= "" then state.price_texts[sym] = entry.price_text end
    if entry.change_pct ~= nil then state.change_pcts[sym] = entry.change_pct end
    if entry.change_val ~= nil then state.change_vals[sym] = entry.change_val end
    if entry.closes then state.closes_map[sym] = entry.closes end
    state.ticker_req_ms[sym] = now - cache_age_ms(entry.ticker_updated_at_s)
    if entry.closes then
      state.klines_req_ms[sym] = now - cache_age_ms(entry.chart_updated_at_s)
    end
  end
end

local function persist_symbol_cache(sym)
  local entry = state.cache[sym] or {}
  entry.price = state.prices[sym]
  entry.price_text = state.price_texts[sym]
  entry.change_pct = state.change_pcts[sym]
  entry.change_val = state.change_vals[sym]
  entry.ticker_updated_at_s = tonumber(entry.ticker_updated_at_s) or 0
  if state.closes_map[sym] and #state.closes_map[sym] >= 2 then
    entry.closes = state.closes_map[sym]
    entry.interval = current_interval()
    entry.limit = KLIMIT
    entry.chart_updated_at_s = tonumber(entry.chart_updated_at_s) or 0
  else
    entry.closes = nil
    entry.interval = nil
    entry.limit = nil
    entry.chart_updated_at_s = nil
  end
  state.cache[sym] = entry
  save_cache()
end

local function kind_has_data(kind, sym)
  if kind == "ticker" then
    local p = state.price_texts[sym]
    return p ~= nil and p ~= ""
  end
  local c = state.closes_map[sym]
  return c ~= nil and #c >= 2
end

local function kind_due_in_ms(kind, sym, now)
  if not kind_has_data(kind, sym) then return 0 end
  local last = 0
  if kind == "ticker" then
    last = state.ticker_req_ms[sym] or 0
  else
    last = state.klines_req_ms[sym] or 0
  end
  if last <= 0 then return 0 end
  local wait_ms = MIN_REFRESH_MS - (now - last)
  if wait_ms < 0 then return 0 end
  return wait_ms
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
  sym = tostring(sym or "")
  local quote_suffixes = {
    "USDT",
    "USDC",
    "BUSD",
    "FDUSD",
    "TUSD",
    "EUR",
    "USD",
  }
  for i = 1, #quote_suffixes do
    local suffix = quote_suffixes[i]
    if #sym > #suffix and string.sub(sym, -#suffix) == suffix then
      return string.sub(sym, 1, #sym - #suffix)
    end
  end
  return sym
end

local function pct_text(v)
  if v == nil then return "--" end
  if v > 0 then return string.format("+%.1f%%", v) end
  return string.format("%.1f%%", v)
end

local function chg_text(v)
  if v == nil then return "--" end
  if v > 0 then return string.format("+%.1f", v) end
  return string.format("%.1f", v)
end

local function pct_color(v)
  if v == nil then return C_MUTED end
  if v > 0 then return C_UP end
  if v < 0 then return C_DOWN end
  return C_MUTED
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

local function url_escape(s)
  s = tostring(s or "")
  return (s:gsub("([^%w%-_%.~])", function(c)
    return string.format("%%%02X", string.byte(c))
  end))
end

local function extract_a3_cookie(raw)
  local s = tostring(raw or "")
  local a3 = string.match(s, "A3=([^;]+)")
  if not a3 or a3 == "" then return nil end
  return "A3=" .. a3
end

local function auth_is_fresh(require_crumb)
  local cookie_ok = tostring(state.yahoo_cookie or "") ~= ""
  local crumb_ok = tostring(state.yahoo_crumb or "") ~= ""
  local age = now_ms() - (state.yahoo_crumb_ms or 0)
  if require_crumb then
    return cookie_ok and crumb_ok and age <= (15 * 60 * 1000)
  end
  return cookie_ok
end

local function ticker_url(sym, host_idx)
  local esc_sym = url_escape(sym)
  if PROXY_BASE ~= "" then
    local base = PROXY_BASE
    if string.sub(base, -1) == "/" then base = string.sub(base, 1, #base - 1) end
    return string.format("%s/yahoo/quote?symbols=%s", base, esc_sym)
  end
  local host = HOSTS[host_idx] or HOSTS[1]
  return string.format("%s/v7/finance/quote?symbols=%s", host, esc_sym)
end

local function ticker_url_with_crumb(sym, host_idx)
  local esc_sym = url_escape(sym)
  local crumb = url_escape(state.yahoo_crumb or "")
  local host = HOSTS[host_idx] or HOSTS[1]
  return string.format(
    "%s/v7/finance/quote?symbols=%s&fields=currency,priceHint,regularMarketChange,regularMarketChangePercent,regularMarketPrice,shortName,symbol&crumb=%s",
    host, esc_sym, crumb
  )
end

local function klines_url(sym, host_idx)
  local function yahoo_range(interval)
    local s = string.lower(tostring(interval or "1m"))
    if s == "1m" then return "1d" end
    if s == "2m" or s == "5m" then return "5d" end
    if s == "15m" or s == "30m" or s == "60m" or s == "90m" then return "1mo" end
    if s == "1h" then return "3mo" end
    if s == "1d" or s == "5d" then return "3mo" end
    if s == "1wk" then return "2y" end
    if s == "1mo" then return "10y" end
    return "1mo"
  end

  local interval = current_interval()
  local range = yahoo_range(interval)
  local esc_sym = url_escape(sym)
  if PROXY_BASE ~= "" then
    local base = PROXY_BASE
    if string.sub(base, -1) == "/" then base = string.sub(base, 1, #base - 1) end
    return string.format("%s/yahoo/chart/%s?interval=%s&range=%s", base, esc_sym, interval, range)
  end
  local host = HOSTS[host_idx] or HOSTS[1]
  return string.format("%s/v8/finance/chart/%s?interval=%s&range=%s", host, esc_sym, interval, range)
end

local function klines_buf_size(limit)
  if limit >= 64 then return 65536 end
  if limit >= 48 then return 49152 end
  if limit >= 32 then return 32768 end
  return 16384
end

local function interval_ms(interval)
  local s = tostring(interval or "1m")
  local n, unit = string.match(s, "^(%d+)([a-zA-Z]+)$")
  n = tonumber(n or "1") or 1
  unit = string.lower(unit or "m")
  if unit == "m" then return n * 60 * 1000 end
  if unit == "h" then return n * 60 * 60 * 1000 end
  if unit == "d" then return n * 24 * 60 * 60 * 1000 end
  if unit == "wk" or unit == "w" then return n * 7 * 24 * 60 * 60 * 1000 end
  if unit == "mo" then return n * 30 * 24 * 60 * 60 * 1000 end
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

  local result = obj and obj.quoteResponse and obj.quoteResponse.result
  if type(result) ~= "table" or #result == 0 then return false, "NO QUOTE" end
  local pick = result[1]
  if type(pick) == "table" then
    for i = 1, #result do
      local r = result[i]
      if tostring(r and r.symbol or "") == tostring(sym or "") then
        pick = r
        break
      end
    end
  end
  local raw_symbol = pick and pick.symbol
  local s = tostring(raw_symbol or sym or "")
  local raw_price = pick and pick.regularMarketPrice
  if raw_price == nil or raw_price == "" then raw_price = pick and pick.postMarketPrice end
  local p_str = tostring(raw_price or "")
  local p_num = tonumber(raw_price)
  local pct = tonumber(pick and pick.regularMarketChangePercent)
  local chg = tonumber(pick and pick.regularMarketChange)

  if p_str == "" or not p_num then
    sys.log(string.format(
      "stock_chart bad ticker expect=%s resp_sym=%s raw_price=%s pct=%s body=%s",
      tostring(sym), tostring(s), tostring(raw_price), tostring(pick and pick.regularMarketChangePercent),
      string.sub(tostring(body or ""), 1, 160)
    ))
    return false, "BAD TICKER"
  end
  if raw_symbol ~= nil and s ~= sym then
    sys.log(string.format("stock_chart ticker symbol mismatch expect=%s got=%s", tostring(sym), tostring(s)))
  end

  state.prices[sym] = p_num
  state.price_texts[sym] = normalize_price_text(p_str)
  state.change_pcts[sym] = pct
  state.change_vals[sym] = chg
  state.ticker_req_ms[sym] = now_ms()
  local entry = state.cache[sym] or {}
  entry.ticker_updated_at_s = unix_time_s()
  state.cache[sym] = entry
  persist_symbol_cache(sym)
  return true, nil
end

local function parse_klines(sym, body)
  local obj, jerr = json.decode(body)
  if not obj then return false, jerr or "JSON ERR" end
  local chart_err = obj and obj.chart and obj.chart.error
  if type(chart_err) == "table" then
    local code = tostring(chart_err.code or "")
    local desc = tostring(chart_err.description or "")
    if code ~= "" or desc ~= "" then
      return false, "CHART " .. code .. " " .. desc
    end
  end
  local result = obj and obj.chart and obj.chart.result
  if type(result) ~= "table" or #result == 0 then return false, "NO CHART" end
  local pick = result[1]
  local indicators = pick and pick.indicators
  local quote = indicators and indicators.quote and indicators.quote[1]
  local adj = indicators and indicators.adjclose and indicators.adjclose[1]

  local function collect_series(arr)
    if type(arr) ~= "table" then return nil end
    local out = {}
    for i = 1, #arr do
      local c = tonumber(arr[i])
      if c then out[#out + 1] = c end
    end
    if #out < 2 then return nil end
    return out
  end

  local out = nil
  if quote then
    out = collect_series(quote.close)
    if not out then out = collect_series(quote.open) end
    if not out then out = collect_series(quote.high) end
    if not out then out = collect_series(quote.low) end
  end
  if not out and adj then
    out = collect_series(adj.adjclose)
  end

  -- Keep previous usable chart to avoid flashing NO CLOSE on transient API gaps.
  if not out and state.closes_map[sym] and #state.closes_map[sym] >= 2 then
    state.klines_req_ms[sym] = now_ms()
    return true, nil
  end

  -- Last fallback: synthesize a flat 2-point line from latest price.
  if not out then
    local p = tonumber(state.prices[sym])
    if p then out = { p, p } end
  end
  if not out then
    return false, "NO CLOSE"
  end
  if #out > state.active_klimit then
    local keep = {}
    local start = #out - state.active_klimit + 1
    for i = start, #out do keep[#keep + 1] = out[i] end
    out = keep
  end
  if #out < 2 then return false, "BAD CLOSE" end
  state.closes_map[sym] = out
  state.klines_req_ms[sym] = now_ms()
  local entry = state.cache[sym] or {}
  entry.chart_updated_at_s = unix_time_s()
  state.cache[sym] = entry
  persist_symbol_cache(sym)
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
    sys.log("stock_chart kline limit fallback " .. tostring(old_limit) .. " -> " .. tostring(state.active_klimit))
  end

  if failed_kind == "klines" then
    state.klines_fail_count = (state.klines_fail_count or 0) + 1
    if is_oom then
      state.active_klimit = 24
      state.klines_pause_until_ms = now_ms() + 180000 -- pause 3 min on OOM
      sys.log("stock_chart klines paused 180000ms due to OOM")
    elseif state.klines_fail_count >= 3 then
      state.klines_pause_until_ms = now_ms() + 60000 -- pause 1 min
      sys.log("stock_chart klines paused 60000ms after repeated failures")
    end
  end

  if state.backoff_ms <= 0 then state.backoff_ms = MIN_REFRESH_MS else state.backoff_ms = state.backoff_ms * 2 end
  if state.backoff_ms > 60000 then state.backoff_ms = 60000 end
  state.backoff_until_ms = now_ms() + state.backoff_ms
  if failed_kind == "klines" then
    state.next_kind = "ticker"
  end
  state.cycle_due_ms = state.backoff_until_ms
  sys.log("stock_chart req error kind=" .. tostring(failed_kind) .. " err=" .. state.last_err .. " backoff_ms=" .. tostring(state.backoff_ms))
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
    state.cycle_due_ms = now_ms() + MIN_REFRESH_MS
  end
  collectgarbage("collect")
end

local function start_fc_request(kind)
  if state.req_id then return false end
  local headers = { ["User-Agent"] = YAHOO_UA }
  local prev = tostring(state.yahoo_cookie or "")
  if prev ~= "" then headers["Cookie"] = prev end
  local id, err = net.http_get("https://fc.yahoo.com", 8000, 8192, headers)
  if not id then
    note_error(err or "FC REQ FAIL", kind)
    return false
  end
  state.req_id = id
  state.req_kind = "fc"
  state.auth_pending_kind = kind
  state.last_req_start_ms = now_ms()
  return true
end

local function start_crumb_request_get(kind)
  if state.req_id then return false end
  local cookie = tostring(state.yahoo_cookie or "")
  if cookie == "" then
    return start_fc_request(kind)
  end
  local headers = {
    ["User-Agent"] = YAHOO_UA,
    ["Cookie"] = cookie,
    ["Accept"] = "*/*",
  }
  sys.log("stock_chart crumb GET cookie_len=" .. tostring(#cookie))
  local id, err = net.http_get("https://query1.finance.yahoo.com/v1/test/getcrumb?ts=1", 8000, 8192, headers)
  if not id then
    note_error(err or "CRUMB GET REQ FAIL", kind)
    return false
  end
  state.req_id = id
  state.req_kind = "crumb_get"
  state.auth_pending_kind = kind
  state.last_req_start_ms = now_ms()
  return true
end

local function start_crumb_request(kind)
  if state.req_id then return false end
  local cookie = tostring(state.yahoo_cookie or "")
  if cookie == "" then
    return start_fc_request(kind)
  end
  local headers = {
    ["User-Agent"] = YAHOO_UA,
    ["Cookie"] = cookie,
    ["Accept"] = "*/*",
  }
  sys.log("stock_chart crumb POST cookie_len=" .. tostring(#cookie))
  local id, err = net.http_get("https://query1.finance.yahoo.com/v1/test/getcrumb", 8000, 8192, headers)
  if not id then
    note_error(err or "CRUMB REQ FAIL", kind)
    return false
  end
  state.req_id = id
  state.req_kind = "crumb"
  state.auth_pending_kind = kind
  state.last_req_start_ms = now_ms()
  return true
end

local function start_direct_request(kind, sym, host_idx)
  if state.req_id then return false end
  local timeout_ms = 8000
  local max_body = 8192
  local url = ""
  local headers = {
    ["User-Agent"] = YAHOO_UA,
    ["Accept"] = "application/json,text/plain,*/*",
    ["Accept-Language"] = "en-US,en;q=0.9",
    ["Referer"] = "https://finance.yahoo.com/",
  }
  if kind == "ticker" then
    if not auth_is_fresh(true) then
      if tostring(state.yahoo_cookie or "") == "" then return start_fc_request(kind) end
      return start_crumb_request(kind)
    end
    url = ticker_url_with_crumb(sym, host_idx)
    headers["Cookie"] = tostring(state.yahoo_cookie or "")
    max_body = 8192
  else
    url = klines_url(sym, host_idx)
    if not auth_is_fresh(false) then
      return start_fc_request(kind)
    end
    headers["Cookie"] = tostring(state.yahoo_cookie or "")
    timeout_ms = 9000
    max_body = klines_buf_size(state.active_klimit)
  end
  local id, err = net.http_get(url, timeout_ms, max_body, headers)
  if not id then
    note_error(err or "REQ FAIL", kind)
    return false
  end
  state.req_id = id
  state.req_kind = kind
  state.req_symbol = sym
  state.last_req_start_ms = now_ms()
  return true
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

  local sym = state.fetch_symbol or state.symbol
  local ticker_wait_ms = kind_due_in_ms("ticker", sym, now)
  local klines_wait_ms = kind_due_in_ms("klines", sym, now)
  local klines_paused = (state.klines_pause_until_ms or 0) > now
  local kind = state.next_kind or "ticker"

  if kind == "ticker" and ticker_wait_ms > 0 then
    if not klines_paused and klines_wait_ms == 0 then
      kind = "klines"
    else
      local next_wait_ms = ticker_wait_ms
      if not klines_paused and klines_wait_ms < next_wait_ms then
        next_wait_ms = klines_wait_ms
      end
      if klines_paused then
        local pause_wait_ms = state.klines_pause_until_ms - now
        if pause_wait_ms < next_wait_ms then
          next_wait_ms = pause_wait_ms
        end
      end
      state.cycle_due_ms = now + math.max(200, next_wait_ms)
      return
    end
  elseif kind == "klines" then
    if klines_paused or klines_wait_ms > 0 then
      if ticker_wait_ms == 0 then
        kind = "ticker"
      else
        local next_wait_ms = ticker_wait_ms
        if not klines_paused and klines_wait_ms < next_wait_ms then
          next_wait_ms = klines_wait_ms
        end
        if klines_paused then
          local pause_wait_ms = state.klines_pause_until_ms - now
          if pause_wait_ms < next_wait_ms then
            next_wait_ms = pause_wait_ms
          end
        end
        state.cycle_due_ms = now + math.max(200, next_wait_ms)
        return
      end
    end
  end
  local host_idx = state.req_host_idx or 1
  if PROXY_BASE == "" then
    start_direct_request(kind, sym, host_idx)
    return
  end

  local ttl_ms = MIN_REFRESH_MS
  local url = nil
  local timeout_ms = 5000
  local max_body = 2048

  if kind == "ticker" then
    url = ticker_url(sym, host_idx)
    timeout_ms = 8000
    max_body = 8192
  else
    url = klines_url(sym, host_idx)
    timeout_ms = 9000
    max_body = klines_buf_size(state.active_klimit)
  end

  sys.log(string.format(
    "stock_chart req start kind=%s url=%s timeout=%d max_body=%d",
    tostring(kind), tostring(url), timeout_ms, max_body
  ))

  local ok_call, id, body, age_ms, err = pcall(net.cached_get, url, ttl_ms, timeout_ms, max_body)
  state.last_req_start_ms = now
  if not ok_call then
    note_error("cached_get call failed: " .. tostring(id), kind)
    return
  end
  if err then
    sys.log("stock_chart cached_get err: " .. tostring(err))
    note_error(err, kind)
    return
  end

  if id then
    state.req_id = id
    state.req_kind = kind
    state.req_symbol = sym
  end

  if body then
    sys.log("stock_chart cached_get hit kind=" .. tostring(kind) .. " bytes=" .. tostring(#body))
    if age_ms then
      sys.log("stock_chart cached_get age_ms kind=" .. tostring(kind) .. " age=" .. tostring(age_ms))
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
  local kind = state.req_kind
  local req_symbol = state.req_symbol

  if PROXY_BASE == "" then
    local ok_call, done, status, body, headers = pcall(net.http_poll, state.req_id)
    if not ok_call then
      local e = tostring(done)
      state.req_id = nil
      state.req_kind = nil
      state.req_symbol = nil
      note_error("http_poll call failed: " .. e, kind)
      return
    end
    if not done then return end

    state.req_id = nil
    state.req_kind = nil
    state.req_symbol = nil
    state.last_req_start_ms = now_ms()

    if kind == "fc" then
      local set_cookie = nil
      if type(headers) == "table" then
        set_cookie = headers["set-cookie"] or headers["Set-Cookie"]
      end
      local a3 = extract_a3_cookie(set_cookie)
      sys.log("stock_chart fc status=" .. tostring(status) .. " set_cookie_len=" .. tostring(#tostring(set_cookie or "")) .. " a3=" .. tostring(a3 and "ok" or "nil"))
      if a3 and (status == 200 or status == 301 or status == 302 or status == 404) then
        state.yahoo_cookie_prev = state.yahoo_cookie
        state.yahoo_cookie = a3
        local pending = state.auth_pending_kind or "ticker"
        state.auth_pending_kind = nil
        if pending == "ticker" then
          start_crumb_request("ticker")
        else
          start_direct_request("klines", req_symbol or state.fetch_symbol or state.symbol, state.req_host_idx or 1)
        end
        return
      end
      state.auth_pending_kind = nil
      note_error("FC HTTP " .. tostring(status), kind)
      return
    end

    if kind == "crumb" then
      if status == 405 then
        start_crumb_request_get("ticker")
        return
      end
      if (status == 401 or status == 403) and tostring(state.yahoo_cookie_prev or "") ~= "" then
        state.yahoo_cookie = state.yahoo_cookie_prev
        state.yahoo_cookie_prev = nil
        start_crumb_request("ticker")
        return
      end
      state.yahoo_cookie_prev = nil
      if status == 200 then
        local crumb = tostring(body or ""):gsub("^%s+", ""):gsub("%s+$", "")
        if crumb ~= "" then
          state.yahoo_crumb = crumb
          state.yahoo_crumb_ms = now_ms()
          local pending = state.auth_pending_kind or "ticker"
          state.auth_pending_kind = nil
          start_direct_request(pending, req_symbol or state.fetch_symbol or state.symbol, state.req_host_idx or 1)
          return
        end
      end
      state.auth_pending_kind = nil
      note_error("CRUMB HTTP " .. tostring(status), "ticker")
      return
    end

    if kind == "crumb_get" then
      if status == 200 then
        local crumb = tostring(body or ""):gsub("^%s+", ""):gsub("%s+$", "")
        if crumb ~= "" then
          state.yahoo_crumb = crumb
          state.yahoo_crumb_ms = now_ms()
          state.yahoo_cookie_prev = nil
          local pending = state.auth_pending_kind or "ticker"
          state.auth_pending_kind = nil
          start_direct_request(pending, req_symbol or state.fetch_symbol or state.symbol, state.req_host_idx or 1)
          return
        end
      end
      state.yahoo_cookie_prev = nil
      state.auth_pending_kind = nil
      note_error("CRUMB HTTP " .. tostring(status), "ticker")
      return
    end

    if status == 401 or status == 403 or status == 429 then
      state.yahoo_cookie = nil
      state.yahoo_crumb = nil
      state.yahoo_crumb_ms = 0
    end

    if status ~= 200 then
      note_error("HTTP " .. tostring(status), kind)
      return
    end

    local ok, err
    if kind == "ticker" then
      ok, err = parse_ticker(req_symbol or state.fetch_symbol or state.symbol, body or "")
    else
      ok, err = parse_klines(req_symbol or state.fetch_symbol or state.symbol, body or "")
    end
    if ok then note_success(kind) else note_error(err, kind) end
    return
  end

  local ok_call, done, status, body = pcall(net.cached_poll, state.req_id)
  if not ok_call then
    local e = tostring(done)
    state.req_id = nil
    state.req_kind = nil
    state.req_symbol = nil
    note_error("cached_poll call failed: " .. e, kind)
    return
  end
  if not done then return end

  state.req_id = nil
  state.req_kind = nil
  state.req_symbol = nil
  state.last_req_start_ms = now_ms()

  local body_len = (body and #body) or 0
  if status ~= 200 then
    local detail = ""
    if body and body ~= "" then
      detail = " detail=" .. string.sub(tostring(body), 1, 80)
    end
    sys.log(string.format(
      "stock_chart req done kind=%s status=%s body_len=%d%s",
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

local function draw_chart(fb, closes, pct_v)
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

  local chart_up = nil
  if pct_v ~= nil then
    chart_up = pct_v >= 0
  else
    chart_up = closes[#closes] >= closes[1]
  end
  local chart_line = chart_up and C_UP or C_DOWN
  local chart_fill = chart_up and C_UP_FILL or C_DOWN_FILL

  local prev_x = nil
  local prev_y = nil
  for i = 1, n do
    local px = x0 + math.floor((i - 1) * (w - 1) / (n - 1))
    local norm = (closes[i] - min_v) / range
    local py = y0 + (h - 1) - math.floor(norm * (h - 1))
    if CHART_TYPE == "mountain" then
      if prev_x ~= nil then
        fill_mountain_segment(fb, prev_x, prev_y, px, py, y0 + h - 1, chart_fill)
      else
        fill_column_to_bottom(fb, px, py, y0 + h - 1, chart_fill)
      end
    end

    if prev_x ~= nil then
      draw_line(fb, prev_x, prev_y, px, py, chart_line)
    else
      fb:set_px(px, py, chart_line)
    end
    prev_x = px
    prev_y = py
  end

end

function app.init(config)
  sys.log("stock_chart init")
  sys.log("stock_chart interval=" .. tostring(current_interval()) .. " limit=64")
  state.idx = 1
  for i = 1, #symbols do
    if symbols[i] == SYMBOL then
      state.idx = i
      break
    end
  end
  state.last_rotate_ms = now_ms()
  state.rotate_interval_ms = ROTATE_INTERVAL_MS
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
  state.change_vals = {}
  state.closes_map = {}
  state.cache = {}
  state.ticker_req_ms = {}
  state.klines_req_ms = {}
  state.active_klimit = KLIMIT
  state.klines_pause_until_ms = 0
  state.klines_fail_count = 0
  state.yahoo_cookie = nil
  state.yahoo_crumb = nil
  state.yahoo_crumb_ms = 0
  state.auth_pending_kind = nil
  state.symbol = current_symbol()
  state.fetch_symbol = state.symbol

  local cache, changed = load_cache()
  state.cache = cache
  hydrate_from_cache()
  if changed then save_cache() end

  start_next_request()
end

function app.tick(dt_ms)
  rotate_symbol(now_ms())
  poll_request()
  start_next_request()
end

function app.render_fb(fb)
  fb:fill(C_BG)

  local raw_sym = state.symbol
  local display_sym = compact_symbol(raw_sym)
  local pct_v = state.change_pcts[raw_sym]
  local chg_v = state.change_vals[raw_sym]
  local pct = pct_text(pct_v)
  local chg = chg_text(chg_v)
  local pct_c = pct_color(pct_v)
  local price = fit_price_text(state.price_texts[raw_sym])

  -- Row 1 (8px): symbol left, change% right
  fb:text_box(0, -3, 40, 8, display_sym, C_TEXT, FONT, 8, "left", true)
  fb:text_box(40, -3, 24, 8, pct, pct_c, FONT, 8, "right", true)

  -- Row 2 (8px): price left, change value right
  fb:text_box(0, 5, 40, 8, price, C_TEXT, FONT, 8, "left", true)
  fb:text_box(40, 5, 24, 8, chg, pct_c, FONT, 8, "right", true)

  -- Row 3 (16px): chart
  draw_chart(fb, state.closes_map[raw_sym], pct_v)

  if state.last_err then
    fb:text_box(0, 24, 64, 8, "DATA ERR", C_DOWN, FONT, 8, "center", true)
  end
end


-- __GLOBAL_BOOT_SPLASH_WRAPPER_V1__
local __boot_now_ms = now_ms or (sys and sys.now_ms) or function() return 0 end
local __boot_started_ms = 0
local __boot_ms = tonumber(data.get("stock_chart.boot_splash_ms") or data.get("app.boot_splash_ms") or 5000) or 5000
if __boot_ms < 0 then __boot_ms = 0 end
local __boot_name = tostring(data.get("stock_chart.app_name") or "Stock Chart")

local function __boot_compact_text(s, limit)
  s = tostring(s or "")
  s = string.gsub(s, "%s+", " ")
  s = string.gsub(s, "^%s+", "")
  s = string.gsub(s, "%s+$", "")
  local n = tonumber(limit) or 16
  if #s > n then return string.sub(s, 1, n - 1) .. "…" end
  return s
end

local function __boot_split_title_lines(name)
  local text = tostring(name or "")
  text = string.gsub(text, "%s+", " ")
  text = string.gsub(text, "^%s+", "")
  text = string.gsub(text, "%s+$", "")
  if text == "" then return "APP", "" end
  local mid = math.floor(#text / 2)
  local cut = nil
  local best = 999
  for i = 1, #text do
    if string.sub(text, i, i) == " " then
      local d = math.abs(i - mid)
      if d < best then
        best = d
        cut = i
      end
    end
  end
  if not cut then return text, "" end
  local a = string.gsub(string.sub(text, 1, cut - 1), "%s+$", "")
  local b = string.gsub(string.sub(text, cut + 1), "^%s+", "")
  return a, b
end

local function __boot_is_active()
  if __boot_started_ms <= 0 then return false end
  return (__boot_now_ms() - __boot_started_ms) < __boot_ms
end

local __orig_init = app.init
app.init = function(...)
  __boot_started_ms = __boot_now_ms()
  if __orig_init then return __orig_init(...) end
end

local __orig_render_fb = app.render_fb
if __orig_render_fb then
  app.render_fb = function(...)
    local fb = select(1, ...)
    if __boot_is_active() and fb and fb.fill and fb.text_box then
      local t1, t2 = __boot_split_title_lines(__boot_name)
      fb:fill(0x0000)
      if t2 ~= "" then
        fb:text_box(0, 8, 64, 8, __boot_compact_text(t1, 14), 0x07FF, "builtin:silkscreen_regular_8", 8, "center", false)
        fb:text_box(0, 16, 64, 8, __boot_compact_text(t2, 14), 0x07FF, "builtin:silkscreen_regular_8", 8, "center", false)
      else
        fb:text_box(0, 12, 64, 8, __boot_compact_text(t1, 14), 0x07FF, "builtin:silkscreen_regular_8", 8, "center", false)
      end
      return true
    end
    return __orig_render_fb(...)
  end
end

local __orig_render = app.render
if __orig_render then
  app.render = function(...)
    if __boot_is_active() then
      local t1, t2 = __boot_split_title_lines(__boot_name)
      if t2 ~= "" then
        return {"", __boot_compact_text(t1, 16), __boot_compact_text(t2, 16), ""}
      end
      return {"", __boot_compact_text(t1, 16), "", ""}
    end
    return __orig_render(...)
  end
end

return app
