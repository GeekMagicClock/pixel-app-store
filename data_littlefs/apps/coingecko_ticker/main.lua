local app = {}

local symbols = {
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

-- Optional LAN gateway. Empty by default so the app uses the public HTTPS API.
local gateway_base = data.get("proxy.coingecko_base") or ""

local base_hosts = {
  "https://api.coingecko.com",
}

local state = {
  idx = 1,
  last_rotate_ms = 0,
  rotate_interval_ms = 4000,
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
    return string.format("%s/coingecko/simple_price?ids=%s", base, ids)
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
  return true
end

local function start_fetch()
  local host_idx = state.req_host_idx or 1
  local url = markets_url(host_idx)

  local ttl_ms = 30 * 1000
  local id, body, age_ms, err = net.cached_get(url, ttl_ms, 7000, 4096)
  if err then
    state.last_err = err
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
  local refresh_ms = 30 * 1000 -- hard cap <= 30s
  if state.last_err then refresh_ms = 5000 end
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
    state.last_err = "HTTP 429"
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

  start_fetch()
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

return app
