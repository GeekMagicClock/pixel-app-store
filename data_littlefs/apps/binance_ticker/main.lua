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

local icon_map = {
  BTCUSDT = "S:/littlefs/apps/binance_ticker/icons/btc-24.png",
  ETHUSDT = "S:/littlefs/apps/binance_ticker/icons/eth-24.png",
  BNBUSDT = "S:/littlefs/apps/binance_ticker/icons/bnb-24.png",
  SOLUSDT = "S:/littlefs/apps/binance_ticker/icons/sol-24.png",
  XRPUSDT = "S:/littlefs/apps/binance_ticker/icons/xrp-24.png",
  ADAUSDT = "S:/littlefs/apps/binance_ticker/icons/ada-24.png",
  DOGEUSDT = "S:/littlefs/apps/binance_ticker/icons/doge-24.png",
  TRXUSDT = "S:/littlefs/apps/binance_ticker/icons/trx-24.png",
  AVAXUSDT = "S:/littlefs/apps/binance_ticker/icons/avax-24.png",
  DOTUSDT = "S:/littlefs/apps/binance_ticker/icons/dot-24.png",
}

local base_hosts = {
  "https://data-api.binance.vision",
}

local state = {
  idx = 1,
  last_rotate_ms = 0,
  rotate_interval_ms = 4000,
  req_id = nil,
  req_symbol = nil,
  req_host_idx = 1,
  last_err = nil,
  last_ok_ms = 0,
  prices = {},
  price_text = {},
  change_pct = {},
  last_req_ms = {},
}

local function now_ms()
  return sys.now_ms()
end

local function current_symbol()
  return symbols[state.idx]
end

local function compact_symbol(sym)
  if string.sub(sym, -4) == "USDT" then
    return string.sub(sym, 1, #sym - 4)
  end
  return sym
end

local function symbol_initial(sym)
  local base = compact_symbol(sym)
  if not base or base == "" then return "?" end
  return string.upper(string.sub(base, 1, 1))
end

local function ticker_url(sym, host_idx)
  local host = base_hosts[host_idx] or base_hosts[1]
  return string.format("%s/api/v3/ticker/24hr?symbol=%s", host, sym)
end

local function update_from_body(sym, body)
  local obj, err = json.decode(body)
  if not obj then
    state.last_err = err or "JSON ERR"
    return false
  end

  local raw_symbol = obj.symbol
  local s = tostring(raw_symbol or sym or "")
  local raw_price = obj.lastPrice
  if raw_price == nil or raw_price == "" then raw_price = obj.price end
  local p_str = tostring(raw_price or "")
  local p = tonumber(raw_price)
  local pct = tonumber(obj.priceChangePercent)
  if not p or p_str == "" then
    sys.log(string.format(
      "binance_ticker bad data expect=%s resp_sym=%s raw_price=%s pct=%s body=%s",
      tostring(sym), tostring(s), tostring(raw_price), tostring(obj.priceChangePercent),
      string.sub(tostring(body or ""), 1, 160)
    ))
    state.last_err = "BAD DATA"
    return false
  end
  if raw_symbol ~= nil and s ~= sym then
    sys.log(string.format("binance_ticker symbol mismatch expect=%s got=%s", tostring(sym), tostring(s)))
  end

  state.prices[sym] = p
  state.price_text[sym] = p_str
  state.change_pct[sym] = pct
  state.last_ok_ms = now_ms()
  state.last_err = nil
  return true
end

local function start_fetch(sym)
  local host_idx = state.req_host_idx or 1
  local url = ticker_url(sym, host_idx)

  local ttl_ms = 30 * 1000
  local id, body, age_ms, err = net.cached_get(url, ttl_ms, 8000, 4096)
  if err then
    state.last_err = err
    return
  end

  if body then
    update_from_body(sym, body)
    state.last_req_ms[sym] = now_ms()
    return
  end

  if id then
    state.req_id = id
    state.req_symbol = sym
    state.last_req_ms[sym] = now_ms()
    return
  end

  state.last_err = "REQ FAIL"
end

local function maybe_fetch(sym)
  if state.req_id then return end

  local now = now_ms()
  local last = state.last_req_ms[sym] or 0
  local refresh_ms = 15 * 1000
  if state.last_err then refresh_ms = 5000 end
  if now - last < refresh_ms then return end

  start_fetch(sym)
end

local function poll_fetch()
  if not state.req_id then return end

  local done, status, body = net.cached_poll(state.req_id)
  if not done then return end

  local sym = state.req_symbol
  local prev_host = state.req_host_idx or 1

  state.req_id = nil
  state.req_symbol = nil

  if status == 200 and body then
    if update_from_body(sym, body) then
      state.req_host_idx = 1
      return
    end
  end

  if prev_host < #base_hosts then
    state.req_host_idx = prev_host + 1
    start_fetch(sym)
    return
  end

  state.req_host_idx = 1
  if status ~= 200 then
    state.last_err = "HTTP " .. tostring(status)
  elseif not body then
    state.last_err = "EMPTY"
  end
end

function app.init(config)
  sys.log("binance_ticker init")
  state.idx = 1
  state.last_rotate_ms = now_ms()
  state.req_id = nil
  state.req_symbol = nil
  state.req_host_idx = 1
  state.last_err = nil
  state.last_ok_ms = 0
  state.prices = {}
  state.price_text = {}
  state.change_pct = {}
  state.last_req_ms = {}

  maybe_fetch(current_symbol())
end

function app.tick(dt_ms)
  local now = now_ms()

  if now - state.last_rotate_ms >= state.rotate_interval_ms then
    state.idx = (state.idx % #symbols) + 1
    state.last_rotate_ms = now
  end

  poll_fetch()
  maybe_fetch(current_symbol())
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
    -- Integer only: hard cap to avoid overflow.
    return string.sub(s, 1, 7)
  end

  local intp = string.sub(s, 1, dot - 1)
  local frac = string.sub(s, dot + 1)
  if frac == "" then return intp .. ".0" end

  -- Drop meaningless trailing zeros first.
  while #frac > 0 and string.sub(frac, -1) == "0" do
    frac = string.sub(frac, 1, #frac - 1)
  end
  if frac == "" then frac = "0" end

  -- Keep at most 7 chars total: "xxxxx.y" / "xxxx.yy" / "0.xxxxx"
  local max_chars = 7
  local room = max_chars - #intp - 1  -- minus dot
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

  -- Left icon block (24x24): draw fallback badge first, then overlay icon if available.
  draw_fallback_badge(fb, sym)
  if icon then
    fb:image(0, 4, 24, 24, icon)
  end

  -- Right text block
  local x = 24
  local w = 40

  -- Line 1: Symbol
  fb:text_box(x, 2, w, 10, compact_symbol(sym), C_TEXT, font_title, 8, "right", true)

  -- Line 2: Price
  if price_txt then
    fb:text_box(x, 10, w, 8, fit_price_text(price_txt), C_PRICE, font, 8, "right", false)
  else
    fb:text_box(x, 10, w, 8, "LOADING", C_MUTED, font, 8, "right", true)
  end

  -- Line 3: 24h change %
  if state.last_err then
    fb:text_box(x, 18, w, 8, "NET ERR", C_WARN, font, 8, "right", true)
  else
    fb:text_box(x, 18, w, 8, format_pct(pct), pct_color(pct), font, 8, "right", true)
  end
end

return app
