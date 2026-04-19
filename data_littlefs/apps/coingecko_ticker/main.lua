local app = {}

local DEFAULT_SYMBOLS = {
  "BTC",
  "ETH",
  "BNB",
  "SOL",
  "XRP",
  "ADA",
  "DOGE",
  "TRX",
  "AVAX",
  "DOT",
}

local SUPPORTED_SYMBOLS = {}
for i = 1, #DEFAULT_SYMBOLS do
  SUPPORTED_SYMBOLS[DEFAULT_SYMBOLS[i]] = true
end

local function normalize_symbols(raw)
  local out = {}
  local seen = {}

  local function add_symbol(sym)
    local s = string.upper(tostring(sym or ""))
    s = string.gsub(s, "%s+", "")
    if s ~= "" and SUPPORTED_SYMBOLS[s] and not seen[s] then
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

local symbols = normalize_symbols(data.get("coingecko_ticker.symbols"))

local cg_id_by_symbol = {
  BTC = "bitcoin",
  ETH = "ethereum",
  BNB = "binancecoin",
  SOL = "solana",
  XRP = "ripple",
  ADA = "cardano",
  DOGE = "dogecoin",
  TRX = "tron",
  AVAX = "avalanche-2",
  DOT = "polkadot",
}

local symbol_by_cg_id = {
  ["bitcoin"] = "BTC",
  ["ethereum"] = "ETH",
  ["binancecoin"] = "BNB",
  ["solana"] = "SOL",
  ["ripple"] = "XRP",
  ["cardano"] = "ADA",
  ["dogecoin"] = "DOGE",
  ["tron"] = "TRX",
  ["avalanche-2"] = "AVAX",
  ["polkadot"] = "DOT",
}

local icon_map = {
  BTC = "S:/littlefs/apps/coingecko_ticker/icons/btc-24.png",
  ETH = "S:/littlefs/apps/coingecko_ticker/icons/eth-24.png",
  BNB = "S:/littlefs/apps/coingecko_ticker/icons/bnb-24.png",
  SOL = "S:/littlefs/apps/coingecko_ticker/icons/sol-24.png",
  XRP = "S:/littlefs/apps/coingecko_ticker/icons/xrp-24.png",
  ADA = "S:/littlefs/apps/coingecko_ticker/icons/ada-24.png",
  DOGE = "S:/littlefs/apps/coingecko_ticker/icons/doge-24.png",
  TRX = "S:/littlefs/apps/coingecko_ticker/icons/trx-24.png",
  AVAX = "S:/littlefs/apps/coingecko_ticker/icons/avax-24.png",
  DOT = "S:/littlefs/apps/coingecko_ticker/icons/dot-24.png",
}

local DEV_PROXY_BASE = "http://192.168.3.156:8787"
local gateway_base = data.get("proxy.coingecko_base") or data.get("proxy.market_data_base") or DEV_PROXY_BASE

local base_hosts = {
  "https://api.coingecko.com",
}

local ROTATE_INTERVAL_MS = tonumber(data.get("coingecko_ticker.rotate_interval_ms") or 5000) or 5000
if ROTATE_INTERVAL_MS < 5000 then ROTATE_INTERVAL_MS = 5000 end
local MIN_REFRESH_MS = tonumber(data.get("coingecko_ticker.refresh_interval_ms") or 60000) or 60000
if MIN_REFRESH_MS < 10000 then MIN_REFRESH_MS = 10000 end
if MIN_REFRESH_MS > 10 * 60 * 1000 then MIN_REFRESH_MS = 10 * 60 * 1000 end
local HTTP_TIMEOUT_MS = tonumber(data.get("coingecko_ticker.timeout_ms") or 12000) or 12000
if HTTP_TIMEOUT_MS < 5000 then HTTP_TIMEOUT_MS = 5000 end
if HTTP_TIMEOUT_MS > 30000 then HTTP_TIMEOUT_MS = 30000 end
local HTTP_MAX_BODY_BYTES = tonumber(data.get("coingecko_ticker.max_body_bytes") or 8192) or 8192
if HTTP_MAX_BODY_BYTES < 2048 then HTTP_MAX_BODY_BYTES = 2048 end
if HTTP_MAX_BODY_BYTES > 32768 then HTTP_MAX_BODY_BYTES = 32768 end
local CACHE_KEY = "coingecko_ticker.cache.v1"

local state = {
  idx = 1,
  last_rotate_ms = 0,
  rotate_interval_ms = ROTATE_INTERVAL_MS,
  req_id = nil,
  req_host_idx = 1,
  last_err = nil,
  last_ok_ms = 0,
  prices = {},
  price_text = {},
  change_pct = {},
  last_req_ms = 0,
  backoff_until_ms = 0,
  backoff_ms = 0,
  cache = {},
}

local function now_ms()
  return sys.now_ms()
end

local function current_symbol()
  return symbols[state.idx]
end

local function symbol_initial(sym)
  if not sym or sym == "" then return "?" end
  return string.upper(string.sub(sym, 1, 1))
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

local function symbol_exists(sym)
  for i = 1, #symbols do
    if symbols[i] == sym then return true end
  end
  return false
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
    updated_at_s = tonumber(entry.updated_at_s) or 0,
  }
  if (out.price == nil) and (out.price_text == "--") and (out.change_pct == nil) then
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
    sys.log("coingecko_ticker cache save failed")
  end
end

local function hydrate_from_cache()
  local now = now_ms()
  local newest_age_ms = nil
  for sym, entry in pairs(state.cache) do
    if entry.price ~= nil then state.prices[sym] = entry.price end
    if entry.price_text and entry.price_text ~= "" then state.price_text[sym] = entry.price_text end
    if entry.change_pct ~= nil then state.change_pct[sym] = entry.change_pct end
    local age_ms = cache_age_ms(entry.updated_at_s)
    if newest_age_ms == nil or age_ms < newest_age_ms then newest_age_ms = age_ms end
  end
  if newest_age_ms ~= nil then
    state.last_req_ms = now - newest_age_ms
  end
end

local function update_cache_entry(sym)
  state.cache[sym] = {
    price = state.prices[sym],
    price_text = state.price_text[sym],
    change_pct = state.change_pct[sym],
    updated_at_s = unix_time_s(),
  }
end

local function build_ids_csv()
  local out = ""
  for i = 1, #symbols do
    local sym = symbols[i]
    local id = cg_id_by_symbol[sym]
    if id and id ~= "" then
      if out == "" then out = id else out = out .. "," .. id end
    end
  end
  return out
end

local function markets_url(host_idx)
  local ids = build_ids_csv()
  if gateway_base ~= "" then
    local base = gateway_base
    if string.sub(base, -1) == "/" then
      base = string.sub(base, 1, #base - 1)
    end
    return string.format(
      "%s/coingecko/simple_price?vs_currencies=usd&ids=%s&include_24hr_change=true&precision=full",
      base,
      ids
    )
  end

  local host = base_hosts[host_idx] or base_hosts[1]
  return string.format(
    "%s/api/v3/simple/price?vs_currencies=usd&ids=%s&include_24hr_change=true&precision=full",
    host,
    ids
  )
end

local function update_from_body(body)
  local obj, err = json.decode(body)
  if not obj then
    state.last_err = err or "JSON ERR"
    return false
  end

  local updated = 0
  for id, row in pairs(obj) do
    local sym = symbol_by_cg_id[tostring(id or "")]
    if sym and type(row) == "table" then
      local p = tonumber(row.usd)
      local pct = tonumber(row.usd_24h_change)
      local p_str = tostring(row.usd or "")
      if p and p_str ~= "" then
        state.prices[sym] = p
        state.price_text[sym] = p_str
        state.change_pct[sym] = pct
        update_cache_entry(sym)
        updated = updated + 1
      end
    end
  end

  if updated == 0 then
    state.last_err = "EMPTY DATA"
    return false
  end

  state.last_ok_ms = now_ms()
  state.last_err = nil
  state.last_req_ms = now_ms()
  save_cache()
  return true
end

local function apply_rate_limit_backoff(reason)
  local now = now_ms()
  if not state.backoff_ms or state.backoff_ms <= 0 then
    state.backoff_ms = 30 * 1000
  else
    state.backoff_ms = state.backoff_ms * 2
  end
  if state.backoff_ms > 5 * 60 * 1000 then
    state.backoff_ms = 5 * 60 * 1000
  end
  state.backoff_until_ms = now + state.backoff_ms
  state.last_err = reason or "HTTP 429"
end

local function is_rate_limited_error(err)
  local s = string.lower(tostring(err or ""))
  return string.find(s, "429", 1, true) ~= nil or
         string.find(s, "too many requests", 1, true) ~= nil
end

local function start_fetch()
  local host_idx = state.req_host_idx or 1
  local url = markets_url(host_idx)

  local ttl_ms = 30 * 1000
  local id, body, age_ms, err = net.cached_get(url, ttl_ms, HTTP_TIMEOUT_MS, HTTP_MAX_BODY_BYTES)
  if err then
    if is_rate_limited_error(err) then
      apply_rate_limit_backoff("HTTP 429")
    else
      state.last_err = err
    end
    return
  end

  if body then
    update_from_body(body)
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

local function maybe_fetch()
  if state.req_id then return end

  local now = now_ms()
  if state.backoff_until_ms and now < state.backoff_until_ms then return end

  local last = state.last_req_ms or 0
  local refresh_ms = MIN_REFRESH_MS
  if now - last < refresh_ms then return end

  start_fetch()
end

local function poll_fetch()
  if not state.req_id then return end

  local done, status, body = net.cached_poll(state.req_id)
  if not done then return end

  local prev_host = state.req_host_idx or 1
  state.req_id = nil

  if status == 200 and body then
    if update_from_body(body) then
      state.req_host_idx = 1
      state.backoff_until_ms = 0
      state.backoff_ms = 0
      return
    end
  end

  if prev_host < #base_hosts then
    state.req_host_idx = prev_host + 1
    start_fetch()
    return
  end

  state.req_host_idx = 1
  if status == 429 then
    apply_rate_limit_backoff("HTTP 429")
  elseif status ~= 200 then
    state.last_err = "HTTP " .. tostring(status)
  elseif not body then
    state.last_err = "EMPTY"
  end
end

function app.init(config)
  sys.log("coingecko_ticker init")
  sys.log("coingecko_ticker title_font=builtin:pressstart2p_regular_8")
  if gateway_base ~= "" then
    sys.log("coingecko_ticker using gateway=" .. gateway_base)
  end
  state.idx = 1
  state.last_rotate_ms = now_ms()
  state.req_id = nil
  state.req_host_idx = 1
  state.last_err = nil
  state.last_ok_ms = 0
  state.prices = {}
  state.price_text = {}
  state.change_pct = {}
  state.last_req_ms = 0
  state.backoff_until_ms = 0
  state.backoff_ms = 0
  state.cache = {}

  local cache, changed = load_cache()
  state.cache = cache
  hydrate_from_cache()
  if changed then save_cache() end

  maybe_fetch()
end

function app.tick(dt_ms)
  local now = now_ms()

  if now - state.last_rotate_ms >= state.rotate_interval_ms then
    state.idx = (state.idx % #symbols) + 1
    state.last_rotate_ms = now
  end

  poll_fetch()
  maybe_fetch()
end

local function pct_color(pct)
  if not pct then return 0x9CF3 end
  if pct > 0 then return 0x07E0 end
  if pct < 0 then return 0xF800 end
  return 0x9CF3
end

local function format_pct(pct)
  if pct == nil then return "--" end
  if pct > 0 then
    return string.format("+%.2f%%", pct)
  end
  return string.format("%.2f%%", pct)
end

local function draw_fallback_badge(fb, sym)
  -- 24x24 icon area: x=[0..23], y=[4..27]
  local cx = 12
  local cy = 16
  local r = 10
  local rr = r * r
  local inner = (r - 1) * (r - 1)
  local C_FILL = 0x31A6
  local C_RING = 0xFFFF
  local C_TEXT = 0xFFFF
  local font = "builtin:silkscreen_regular_8"

  for y = 4, 27 do
    for x = 0, 23 do
      local dx = x - cx
      local dy = y - cy
      local d2 = dx * dx + dy * dy
      if d2 <= rr then
        if d2 >= inner then
          fb:set_px(x, y, C_RING)
        else
          fb:set_px(x, y, C_FILL)
        end
      end
    end
  end

  fb:text_box(0, 12, 24, 8, symbol_initial(sym), C_TEXT, font, 8, "center", true)
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

function app.render_fb(fb)
  local sym = current_symbol()
  local price_txt = state.price_text[sym]
  local pct = state.change_pct[sym]
  local icon = icon_map[sym]

  local C_BG = 0x0000
  local C_TEXT = 0xFFFF
  local C_MUTED = 0x9CF3
  local C_PRICE = 0xFFFF
  local C_WARN = 0xF800

  local font = "builtin:silkscreen_regular_8"
  local font_title = "builtin:pressstart2p_regular_8"

  fb:fill(C_BG)

  draw_fallback_badge(fb, sym)
  if icon then
    fb:image(0, 4, 24, 24, icon)
  end

  local x = 24
  local w = 40

  fb:text_box(x, 2, w, 10, sym, C_TEXT, font_title, 8, "right", true)

  if price_txt then
    fb:text_box(x, 10, w, 8, fit_price_text(price_txt), C_PRICE, font, 8, "right", false)
  else
    fb:text_box(x, 10, w, 8, "LOADING", C_MUTED, font, 8, "right", true)
  end

  if state.last_err then
    fb:text_box(x, 18, w, 8, "NET ERR", C_WARN, font, 8, "right", true)
  else
    fb:text_box(x, 18, w, 8, format_pct(pct), pct_color(pct), font, 8, "right", true)
  end
end


-- __GLOBAL_BOOT_SPLASH_WRAPPER_V1__
local __boot_now_ms = now_ms or (sys and sys.now_ms) or function() return 0 end
local __boot_started_ms = 0
local __boot_ms = tonumber(data.get("coingecko_ticker.boot_splash_ms") or data.get("app.boot_splash_ms") or 1200) or 1200
if __boot_ms < 0 then __boot_ms = 0 end
local __boot_name = tostring(data.get("coingecko_ticker.app_name") or "CoinGecko Ticker")

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
