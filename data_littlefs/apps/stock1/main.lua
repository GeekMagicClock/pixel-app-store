local app = {}

local DEFAULT_SYMBOLS = {
  "AMZN", "TSLA", "AAPL", "MSFT", "NVDA",
  "GOOGL", "META", "SONY", "SSNLF", "GC=F",
}

local YAHOO_UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/115.0.0.0 Safari/537.36"
local DEV_PROXY_BASE = "http://192.168.3.156:8787"
local proxy_base = tostring(data.get("proxy.market_data_base") or data.get("proxy.stock_base") or DEV_PROXY_BASE)
local ROTATE_INTERVAL_MS = tonumber(data.get("stock_theme1.rotate_interval_ms") or 5000) or 5000
local REFRESH_INTERVAL_MS = tonumber(data.get("stock_theme1.refresh_interval_ms") or 60000) or 60000
if ROTATE_INTERVAL_MS < 2000 then ROTATE_INTERVAL_MS = 2000 end
if ROTATE_INTERVAL_MS > 600000 then ROTATE_INTERVAL_MS = 600000 end
if REFRESH_INTERVAL_MS < 10000 then REFRESH_INTERVAL_MS = 10000 end
if REFRESH_INTERVAL_MS > 600000 then REFRESH_INTERVAL_MS = 600000 end

local function normalize_symbol(raw)
  local s = string.upper(tostring(raw or ""))
  s = string.gsub(s, "%s+", "")
  if s == "" then return nil end
  if #s > 16 then s = string.sub(s, 1, 16) end
  return s
end

local function load_symbols()
  local out = {}
  local seen = {}
  for i = 1, 10 do
    local key = "stock_theme1.symbol_" .. tostring(i)
    local sym = normalize_symbol(data.get(key))
    if sym and not seen[sym] then
      out[#out + 1] = sym
      seen[sym] = true
    end
  end
  if #out == 0 then
    for i = 1, #DEFAULT_SYMBOLS do out[#out + 1] = DEFAULT_SYMBOLS[i] end
  end
  return out
end

local symbols = load_symbols()
local state = {
  idx = 1,
  last_rotate_ms = 0,
  req_id = nil,
  req_kind = nil,
  last_req_ms = 0,
  last_ok_ms = 0,
  last_err = nil,
  rows = {},
  yahoo_cookie = nil,
  yahoo_crumb = nil,
  yahoo_crumb_ms = 0,
}

local function now_ms()
  return sys.now_ms()
end

local function quote_url()
  local esc = {}
  for i = 1, #symbols do
    esc[#esc + 1] = tostring(symbols[i]):gsub("([^%w%-_%.~])", function(c)
      return string.format("%%%02X", string.byte(c))
    end)
  end
  local csv = table.concat(esc, "%2C")
  if proxy_base ~= "" then
    local base = proxy_base
    if string.sub(base, -1) == "/" then base = string.sub(base, 1, #base - 1) end
    return base .. "/yahoo/quote?symbols=" .. csv
  end
  return "https://query1.finance.yahoo.com/v7/finance/quote?symbols=" .. csv
end

local function quote_url_with_crumb()
  local esc = {}
  for i = 1, #symbols do
    esc[#esc + 1] = tostring(symbols[i]):gsub("([^%w%-_%.~])", function(c)
      return string.format("%%%02X", string.byte(c))
    end)
  end
  local csv = table.concat(esc, "%2C")
  local crumb = tostring(state.yahoo_crumb or ""):gsub("([^%w%-_%.~])", function(c)
    return string.format("%%%02X", string.byte(c))
  end)
  return "https://query1.finance.yahoo.com/v7/finance/quote?symbols="
    .. csv
    .. "&fields=currency,priceHint,regularMarketChange,regularMarketChangePercent,regularMarketPrice,shortName,symbol"
    .. "&crumb=" .. crumb
end

local function trim(s)
  s = tostring(s or "")
  s = string.gsub(s, "^%s+", "")
  s = string.gsub(s, "%s+$", "")
  return s
end

local function extract_a3_cookie(raw)
  local s = tostring(raw or "")
  local a3 = string.match(s, "A3=([^;]+)")
  if not a3 or a3 == "" then return nil end
  return "A3=" .. a3
end

local function fmt_pct(pct)
  local n = tonumber(pct)
  if not n then return "--" end
  if n >= 0 then
    return string.format("+%.2f%%", n)
  end
  return string.format("%.2f%%", n)
end

local function fmt_price(v)
  local n = tonumber(v)
  if not n then return "--" end
  if n >= 1000 then
    return string.format("$%.0f", n)
  end
  if n >= 100 then
    return string.format("$%.1f", n)
  end
  return string.format("$%.2f", n)
end

local function fmt_change(v)
  local n = tonumber(v)
  if not n then return "--" end
  if n >= 0 then
    return string.format("+%.2f", n)
  end
  return string.format("%.2f", n)
end

local function parse_body(body)
  local obj, jerr = json.decode(body)
  if not obj then return false, jerr or "JSON ERR" end
  local result = obj and obj.quoteResponse and obj.quoteResponse.result
  if type(result) ~= "table" or #result == 0 then return false, "NO DATA" end

  local rows = {}
  for i = 1, #result do
    local row = result[i]
    local sym = normalize_symbol(row and row.symbol)
    if sym then
      local price = tonumber(row and row.regularMarketPrice)
      if not price then price = tonumber(row and row.postMarketPrice) end
      if not price then price = tonumber(row and row.preMarketPrice) end

      local pct = tonumber(row and row.regularMarketChangePercent)
      local chg = tonumber(row and row.regularMarketChange)
      if not pct then
        if chg and price and price ~= 0 then
          pct = (chg / price) * 100
        end
      end

      rows[sym] = {
        symbol = sym,
        name = tostring((row and row.shortName) or sym),
        price = price,
        pct = pct,
        chg = chg,
      }
    end
  end
  state.rows = rows
  state.last_ok_ms = now_ms()
  state.last_err = nil
  return true, nil
end

local function start_proxy_request()
  if state.req_id then return end
  local url = quote_url()
  local id, body, age_ms, err = net.cached_get(url, REFRESH_INTERVAL_MS, 8000, 16384)
  if err then
    state.last_err = tostring(err)
    return
  end
  if body then
    local ok, perr = parse_body(body)
    if not ok then state.last_err = tostring(perr) end
    state.last_req_ms = now_ms()
    return
  end
  if id then
    state.req_id = id
    state.last_req_ms = now_ms()
    return
  end
  state.last_err = "REQ FAIL"
end

local function start_fc_request()
  if state.req_id then return end
  local headers = { ["User-Agent"] = YAHOO_UA }
  local id, err = net.http_get("https://fc.yahoo.com", 8000, 4096, headers)
  if not id then
    state.last_err = tostring(err or "FC REQ FAIL")
    return
  end
  state.req_id = id
  state.req_kind = "fc"
  state.last_req_ms = now_ms()
end

local function start_crumb_request()
  if state.req_id then return end
  local cookie = tostring(state.yahoo_cookie or "")
  if cookie == "" then
    state.last_err = "NO COOKIE"
    return
  end
  local headers = {
    ["Cookie"] = cookie,
    ["Referer"] = "https://finance.yahoo.com/",
    ["Accept-Language"] = "en-US,en;q=0.9",
    ["User-Agent"] = YAHOO_UA,
  }
  local id, err = net.http_get("https://query1.finance.yahoo.com/v1/test/getcrumb", 8000, 4096, headers)
  if not id then
    state.last_err = tostring(err or "CRUMB REQ FAIL")
    return
  end
  state.req_id = id
  state.req_kind = "crumb"
  state.last_req_ms = now_ms()
end

local function start_quote_request_with_crumb()
  if state.req_id then return end
  local cookie = tostring(state.yahoo_cookie or "")
  local crumb = tostring(state.yahoo_crumb or "")
  if cookie == "" or crumb == "" then
    start_fc_request()
    return
  end
  local headers = {
    ["Cookie"] = cookie,
    ["Referer"] = "https://finance.yahoo.com/",
    ["Accept"] = "application/json,text/plain,*/*",
    ["Accept-Language"] = "en-US,en;q=0.9",
    ["User-Agent"] = YAHOO_UA,
  }
  local id, err = net.http_get(quote_url_with_crumb(), 8000, 16384, headers)
  if not id then
    state.last_err = tostring(err or "QUOTE REQ FAIL")
    return
  end
  state.req_id = id
  state.req_kind = "quote"
  state.last_req_ms = now_ms()
end

local function start_request()
  if proxy_base ~= "" then
    start_proxy_request()
    return
  end
  local age = now_ms() - (state.yahoo_crumb_ms or 0)
  if tostring(state.yahoo_cookie or "") == "" or tostring(state.yahoo_crumb or "") == "" or age > (15 * 60 * 1000) then
    start_fc_request()
  else
    start_quote_request_with_crumb()
  end
end

local function poll_request()
  if not state.req_id then return end
  if proxy_base ~= "" then
    local done, status, body = net.cached_poll(state.req_id)
    if not done then return end
    state.req_id = nil
    if status ~= 200 then
      state.last_err = "HTTP " .. tostring(status)
      return
    end
    local ok, perr = parse_body(body or "")
    if not ok then state.last_err = tostring(perr) end
    return
  end

  local done, status, body, headers = net.http_poll(state.req_id)
  if not done then return end
  local kind = state.req_kind
  state.req_id = nil
  state.req_kind = nil

  if kind == "fc" then
    local set_cookie = nil
    if type(headers) == "table" then
      set_cookie = headers["set-cookie"] or headers["Set-Cookie"]
    end
    local a3 = extract_a3_cookie(set_cookie)
    if a3 and (status == 200 or status == 301 or status == 302 or status == 404) then
      state.yahoo_cookie = a3
      start_crumb_request()
      return
    end
    state.last_err = "FC HTTP " .. tostring(status)
    return
  end

  if kind == "crumb" then
    if status == 200 then
      local crumb = trim(body or "")
      if crumb ~= "" then
        state.yahoo_crumb = crumb
        state.yahoo_crumb_ms = now_ms()
        start_quote_request_with_crumb()
        return
      end
    end
    state.last_err = "CRUMB HTTP " .. tostring(status)
    return
  end

  if kind == "quote" then
    if status == 200 then
      local ok, perr = parse_body(body or "")
      if ok then
        return
      end
      state.last_err = tostring(perr)
      return
    end
    if status == 401 or status == 403 then
      state.yahoo_cookie = nil
      state.yahoo_crumb = nil
      state.yahoo_crumb_ms = 0
    end
    state.last_err = "HTTP " .. tostring(status)
    return
  end

  state.last_err = "REQ KIND ERR"
end

local function current_symbol()
  if #symbols == 0 then return "AAPL" end
  if state.idx < 1 then state.idx = 1 end
  if state.idx > #symbols then state.idx = 1 end
  return symbols[state.idx]
end

function app.init(config)
  sys.log("stock_theme1 init")
  state.idx = 1
  state.last_rotate_ms = now_ms()
  state.req_id = nil
  state.req_kind = nil
  state.last_req_ms = 0
  state.last_ok_ms = 0
  state.last_err = nil
  state.rows = {}
  state.yahoo_cookie = nil
  state.yahoo_crumb = nil
  state.yahoo_crumb_ms = 0
  start_request()
end

function app.tick(dt_ms)
  poll_request()
  local now = now_ms()
  if now - state.last_rotate_ms >= ROTATE_INTERVAL_MS then
    state.idx = (state.idx % math.max(#symbols, 1)) + 1
    state.last_rotate_ms = now
  end
  if not state.req_id and (now - state.last_req_ms >= REFRESH_INTERVAL_MS) then
    start_request()
  end
end

function app.render_fb(fb)
  fb:fill(0x0000)
  local sym = current_symbol()
  local row = state.rows[sym]
  local name = row and row.name or ""
  local name_upper = string.upper(string.sub(name or "", 1, 12))
  local show_name = (name_upper ~= "" and name_upper ~= sym)
  local price = row and row.price or nil
  local pct = row and row.pct or nil
  local chg = row and row.chg or nil

  local chg_color = 0x9CF3
  if pct and pct > 0.0001 then chg_color = 0x07E0 end
  if pct and pct < -0.0001 then chg_color = 0xF800 end

  local font = "builtin:silkscreen_regular_8"
  fb:text_box(1, -2, 32, 8, sym, 0xFFFF, font, 8, "left", true)
  fb:text_box(33, -2, 30, 8, fmt_pct(pct), chg_color, font, 8, "right", true)
  fb:text_box(1, 7, 62, 8, fmt_price(price), 0xFFFF, font, 8, "left", true)
  fb:text_box(1, 16, 62, 8, fmt_change(chg), chg_color, font, 8, "left", true)
  if show_name then
    fb:text_box(1, 24, 62, 8, name_upper, 0x9CF3, font, 8, "left", true)
  else
    fb:text_box(1, 24, 62, 8, "YAHOO", 0x9CF3, font, 8, "left", true)
  end

  if state.last_err then
    fb:text_box(36, 0, 27, 8, "ERR", 0xF800, font, 8, "right", true)
  end
end

return app
