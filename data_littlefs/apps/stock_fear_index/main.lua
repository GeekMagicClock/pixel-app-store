local app = {}

local FONT_UI = "builtin:silkscreen_regular_8"
local proxy_base = tostring(data.get("proxy.market_data_base") or data.get("proxy.stock_base") or "")
local URL = "https://production.dataviz.cnn.io/index/fearandgreed/graphdata"
if proxy_base ~= "" then
  if string.sub(proxy_base, -1) == "/" then
    proxy_base = string.sub(proxy_base, 1, #proxy_base - 1)
  end
  URL = proxy_base .. "/proxy/cnn/index/fearandgreed/graphdata"
end
local TTL_MS = tonumber(data.get("stock_fear_index.ttl_ms") or (15 * 60 * 1000)) or (15 * 60 * 1000)
if TTL_MS < 60 * 1000 then TTL_MS = 60 * 1000 end
if TTL_MS > 6 * 60 * 60 * 1000 then TTL_MS = 6 * 60 * 60 * 1000 end
local FETCH_INTERVAL_MS = tonumber(data.get("stock_fear_index.refresh_interval_ms") or (60 * 1000)) or (60 * 1000)
if FETCH_INTERVAL_MS < 10 * 1000 then FETCH_INTERVAL_MS = 10 * 1000 end
if FETCH_INTERVAL_MS > 60 * 60 * 1000 then FETCH_INTERVAL_MS = 60 * 60 * 1000 end
local RETRY_INTERVAL_MS = tonumber(data.get("stock_fear_index.retry_interval_ms") or (5 * 1000)) or (5 * 1000)
if RETRY_INTERVAL_MS < 2 * 1000 then RETRY_INTERVAL_MS = 2 * 1000 end
if RETRY_INTERVAL_MS > 60 * 1000 then RETRY_INTERVAL_MS = 60 * 1000 end

local C_BG = 0x0000
local C_LINE = 0x18C3
local C_TEXT = 0xFFDF
local C_TEXT_DIM = 0x9CD3
local C_OK = 0x87F0
local C_WARN = 0xFD20
local C_ERR = 0xF800

local DIGITS = {
  ["0"] = {"111","101","101","101","111"},
  ["1"] = {"010","110","010","010","111"},
  ["2"] = {"111","001","111","100","111"},
  ["3"] = {"111","001","111","001","111"},
  ["4"] = {"101","101","111","001","001"},
  ["5"] = {"111","100","111","001","111"},
  ["6"] = {"111","100","111","101","111"},
  ["7"] = {"111","001","010","010","010"},
  ["8"] = {"111","101","111","101","111"},
  ["9"] = {"111","101","111","001","111"},
}

local state = {
  req_id = nil,
  score = nil,
  label = nil,
  change = nil,
  prev_close = nil,
  history = {},
  updated = nil,
  err = nil,
  last_fetch_ms = 0,
  next_fetch_due_ms = 0,
  block_backoff_ms = 0,
  cache_body = nil,
  cache_at_ms = 0,
}

local function rand_range(min_v, max_v)
  if max_v <= min_v then return min_v end
  return min_v + math.random(0, max_v - min_v)
end

local function schedule_next(success)
  local now = sys.now_ms()
  if success then
    local base = FETCH_INTERVAL_MS
    local jitter = math.max(1000, math.floor(base * 0.35))
    state.next_fetch_due_ms = now + rand_range(base - jitter, base + jitter)
    state.block_backoff_ms = 0
    return
  end
  local base = RETRY_INTERVAL_MS
  local jitter = math.max(500, math.floor(base * 0.30))
  state.next_fetch_due_ms = now + rand_range(base - jitter, base + jitter)
end

local function rect_safe(fb, x, y, w, h, c)
  if w <= 0 or h <= 0 then return end
  fb:rect(x, y, w, h, c)
end

local function draw_digit(fb, digit, x, y, scale, color)
  local pat = DIGITS[digit]
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

local function draw_number(fb, value, x, y, scale, color)
  local text = tostring(value or "--")
  local cur = x
  for i = 1, #text do
    draw_digit(fb, string.sub(text, i, i), cur, y, scale, color)
    cur = cur + 3 * scale + scale
  end
end

local function text_shadow(fb, x, y, w, text, color, align)
  fb:text_box(x + 1, y + 1, w, 8, text, C_BG, FONT_UI, 8, align or "left", false)
  fb:text_box(x, y, w, 8, text, color, FONT_UI, 8, align or "left", false)
end

local function band_color(score)
  local s = tonumber(score or 0) or 0
  if s < 25 then return C_ERR end
  if s < 45 then return C_WARN end
  if s < 56 then return 0xFFE0 end
  if s < 75 then return C_OK end
  return 0x07FF
end

local function split_label(label)
  local s = string.upper(tostring(label or ""))
  if s == "EXTREME FEAR" then return "EXTREME", "FEAR" end
  if s == "EXTREME GREED" then return "EXTREME", "GREED" end
  return s, nil
end

local function draw_label(fb, x, y, w, label, color)
  local line1, line2 = split_label(label)
  if line2 then
    text_shadow(fb, x, y, w, line1, color, "left")
    text_shadow(fb, x, y + 6, w, line2, color, "left")
    return
  end
  text_shadow(fb, x, y + 3, w, line1, color, "left")
end

local function parse_history(obj)
  local out = {}
  if type(obj.history) ~= "table" then return out end
  for i = 1, #obj.history do
    local row = obj.history[i]
    local v = tonumber(row.score)
    if v then out[#out + 1] = v end
    if #out >= 7 then break end
  end
  return out
end

local function title_case_words(s)
  local text = string.lower(tostring(s or ""))
  local out = string.gsub(text, "(%a)([%w_]*)", function(a, b)
    return string.upper(a) .. string.lower(string.gsub(b, "_", " "))
  end)
  out = string.gsub(out, "_", " ")
  return out
end

local function parse_cnn_history(obj)
  local out = {}
  local hist = obj and obj.fear_and_greed_historical
  local rows = hist and hist.data
  if type(rows) ~= "table" then return out end
  for i = #rows, 1, -1 do
    local row = rows[i]
    local v = tonumber(row and row.y)
    if v then out[#out + 1] = v end
    if #out >= 7 then break end
  end
  return out
end

local function parse_body(body)
  local obj, err = json.decode(body)
  if not obj then return nil, err or "JSON ERR" end

  local score = nil
  local label = ""
  local updated = ""
  local hist = {}
  local prev_close = nil

  if type(obj.fear_and_greed) == "table" then
    local fg = obj.fear_and_greed
    score = tonumber(fg.score)
    label = title_case_words(fg.rating or "")
    updated = tostring(fg.timestamp or "")
    prev_close = tonumber(fg.previous_close)
    hist = parse_cnn_history(obj)
  else
    score = tonumber(obj.score)
    label = tostring(obj.label or "")
    updated = tostring(obj.timestamp or "")
    hist = parse_history(obj)
  end

  if not score then return nil, "NO SCORE" end
  local prev = prev_close or hist[2]
  state.score = math.floor(score + 0.5)
  state.label = label
  state.change = prev and (score - prev) or nil
  state.prev_close = prev_close
  state.history = hist
  state.updated = updated
  state.err = nil
  return true
end

local function start_fetch()
  local now = sys.now_ms()
  if state.cache_body and (now - (state.cache_at_ms or 0) <= TTL_MS) then
    local ok, perr = parse_body(state.cache_body)
    if not ok then
      state.err = perr
      schedule_next(false)
    else
      schedule_next(true)
    end
    return
  end
  local headers = {
    ["User-Agent"] = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36",
    ["Accept"] = "application/json,text/plain,*/*",
    ["Accept-Language"] = "en-US,en;q=0.9",
    ["Referer"] = "https://www.cnn.com/markets/fear-and-greed",
    ["Origin"] = "https://www.cnn.com",
    ["Cache-Control"] = "no-cache",
    ["Pragma"] = "no-cache",
  }
  local id, err = net.http_get(URL, 12000, 262144, headers)
  if err then
    state.err = err
    schedule_next(false)
    return
  end
  state.req_id = id
end

local function poll_fetch()
  if not state.req_id then return end
  local done, status, body = net.http_poll(state.req_id)
  if not done then return end
  state.req_id = nil
  if status ~= 200 then
    state.err = "HTTP " .. tostring(status)
    if status == 418 or status == 403 then
      if state.block_backoff_ms <= 0 then
        state.block_backoff_ms = 5 * 60 * 1000
      else
        state.block_backoff_ms = state.block_backoff_ms * 2
      end
      if state.block_backoff_ms > 30 * 60 * 1000 then
        state.block_backoff_ms = 30 * 60 * 1000
      end
      local jitter = rand_range(0, 60 * 1000)
      state.next_fetch_due_ms = sys.now_ms() + state.block_backoff_ms + jitter
    else
      schedule_next(false)
    end
    return
  end
  local ok, perr = parse_body(body)
  if not ok then
    state.err = perr
    schedule_next(false)
    return
  end
  state.cache_body = body
  state.cache_at_ms = sys.now_ms()
  schedule_next(true)
end

local function maybe_fetch()
  if state.req_id then return end
  local now = sys.now_ms()
  if now < (state.next_fetch_due_ms or 0) then return end
  state.last_fetch_ms = now
  start_fetch()
end

local function draw_meter(fb, score)
  local y = 28
  for i = 0, 19 do
    local band_score = (i / 19) * 100
    rect_safe(fb, 2 + i * 3, y, 2, 2, band_color(band_score))
  end
  local px = 2 + math.floor(((tonumber(score or 0) or 0) / 100) * 57)
  rect_safe(fb, px, y - 1, 3, 4, C_TEXT)
end

local function draw_history(fb, hist)
  if #hist == 0 then return end
  local base_x = 61
  for i = 1, math.min(#hist, 7) do
    local v = tonumber(hist[i] or 0) or 0
    local h = 1 + math.floor(v / 17)
    local c = band_color(v)
    rect_safe(fb, base_x - (i - 1) * 3, 7 - h, 2, h, c)
  end
end

function app.init(config)
  state.req_id = nil
  state.score = nil
  state.label = nil
  state.change = nil
  state.prev_close = nil
  state.history = {}
  state.updated = nil
  state.err = nil
  state.last_fetch_ms = 0
  state.next_fetch_due_ms = 0
  state.block_backoff_ms = 0
  state.cache_body = nil
  state.cache_at_ms = 0
  math.randomseed(sys.now_ms())
  maybe_fetch()
end

function app.tick(dt_ms)
  poll_fetch()
  maybe_fetch()
end

function app.render_fb(fb)
  fb:fill(C_BG)
  rect_safe(fb, 0, 0, 64, 1, C_LINE)
  rect_safe(fb, 0, 31, 64, 1, C_LINE)

  text_shadow(fb, 2, -2, 28, "STOCK", C_TEXT, "left")
  draw_history(fb, state.history)

  if not state.score then
    text_shadow(fb, 0, 11, 64, state.err and "DATA ERR" or "LOADING", state.err and C_WARN or C_OK, "center")
    return
  end

  local accent = band_color(state.score)
  draw_number(fb, string.format("%02d", state.score), 2, 8, 3, accent)
  draw_label(fb, 29, 6, 33, state.label, accent)

  local delta_txt = "--"
  local delta_color = C_TEXT_DIM
  if state.change then
    delta_txt = string.format("%+.1f 1D", state.change)
    delta_color = state.change >= 0 and C_OK or C_ERR
  end
  text_shadow(fb, 29, 19, 33, delta_txt, delta_color, "left")
  draw_meter(fb, state.score)
end


-- __GLOBAL_BOOT_SPLASH_WRAPPER_V1__
local __boot_now_ms = now_ms or (sys and sys.now_ms) or function() return 0 end
local __boot_started_ms = 0
local __boot_ms = tonumber(data.get("stock_fear_index.boot_splash_ms") or data.get("app.boot_splash_ms") or 5000) or 5000
if __boot_ms < 0 then __boot_ms = 0 end
local __boot_name = tostring(data.get("stock_fear_index.app_name") or "Stock Fear")

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
