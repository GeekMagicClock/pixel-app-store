local app = {}

local FONT_UI = "builtin:silkscreen_regular_8"

local C_BG = 0x0000
local C_PANEL = 0x0821
local C_PANEL_2 = 0x1042
local C_TEXT = 0xFFDF
local C_TEXT_DIM = 0x9CD3
local C_SHADOW = 0x0000
local C_ACCENT = 0x07FF
local C_WARN = 0xFD20
local C_ERR = 0xF800
local C_OK = 0x87F0

local THEMES = {
  global = {
    {name = "NPR", url = "https://feeds.npr.org/1001/rss.xml"},
    {name = "SKY", url = "https://feeds.skynews.com/feeds/rss/world.xml"},
    {name = "WSJ", url = "https://feeds.a.dj.com/rss/RSSWorldNews.xml"},
  },
  tech = {
    {name = "ARS", url = "https://feeds.arstechnica.com/arstechnica/index"},
    {name = "ENG", url = "https://www.engadget.com/rss.xml"},
    {name = "HN", url = "https://hnrss.org/frontpage"},
  },
  markets = {
    {name = "CNBC", url = "https://search.cnbc.com/rs/search/combinedcms/view.xml?partnerId=wrss01&id=100003114"},
    {name = "MW", url = "https://feeds.marketwatch.com/marketwatch/topstories/"},
    {name = "WSJ", url = "https://feeds.a.dj.com/rss/RSSMarketsMain.xml"},
  },
}

local state = {
  theme = "global",
  theme_key = "global",
  sources = {},
  feed_idx = 1,
  req_id = nil,
  req_source_idx = 0,
  items = {},
  headline_idx = 1,
  scroll_x = 64,
  last_scroll_ms = 0,
  scroll_step_ms = 40,
  scroll_gap_px = 20,
  scroll_hold_ms = 800,
  hold_until_ms = 0,
  headline_px = 0,
  last_fetch_ms = 0,
  ttl_ms = 10 * 60 * 1000,
  err = nil,
  last_ok_ms = 0,
  source_health = {},
  failed_sources = 0,
}

local function rect_safe(fb, x, y, w, h, c)
  if w <= 0 or h <= 0 then return end
  fb:rect(x, y, w, h, c)
end

local function draw_text_shadow(fb, x, y, w, text, color, align)
  fb:text_box(x + 1, y + 1, w, 8, text, C_SHADOW, FONT_UI, 8, align or "left", false)
  fb:text_box(x, y, w, 8, text, color, FONT_UI, 8, align or "left", false)
end

local function safe_upper(s)
  return string.upper(tostring(s or ""))
end

local function compact_title(s)
  s = tostring(s or "")
  s = string.gsub(s, "&amp;", "&")
  s = string.gsub(s, "&quot;", "\"")
  s = string.gsub(s, "&#39;", "'")
  s = string.gsub(s, "&lt;", "<")
  s = string.gsub(s, "&gt;", ">")
  s = string.gsub(s, "[\r\n\t]", " ")
  s = string.gsub(s, "%s+", " ")
  s = string.gsub(s, "[^%g ]", "")
  s = string.gsub(s, "^%s+", "")
  s = string.gsub(s, "%s+$", "")
  return s
end

local function estimate_text_px(text)
  local s = tostring(text or "")
  local w = 0
  for i = 1, #s do
    local ch = string.sub(s, i, i)
    if ch == " " then
      w = w + 3
    elseif ch == "I" or ch == "1" or ch == "'" or ch == "." or ch == "," or ch == ":" then
      w = w + 2
    else
      w = w + 5
    end
  end
  return w
end

local function user_sources()
  local out = {}
  for i = 1, 3 do
    local name = tostring(data.get("news_ticker.custom_" .. i .. "_name") or ("C" .. tostring(i)))
    local url = tostring(data.get("news_ticker.custom_" .. i .. "_url") or "")
    if url ~= "" and string.match(url, "^https?://") then
      out[#out + 1] = {name = safe_upper(name), url = url}
    end
  end
  return out
end

local function get_theme_sources()
  local theme = tostring(data.get("news_ticker.theme") or "global")
  if THEMES[theme] then
    return theme, THEMES[theme]
  end
  local custom = user_sources()
  if #custom > 0 then
    return "custom", custom
  end
  return "global", THEMES.global
end

local function source_state(url)
  local st = state.source_health[url]
  if not st then
    st = {failures = 0, disabled = false, last_err = nil}
    state.source_health[url] = st
  end
  return st
end

local function recount_failed_sources()
  local failed = 0
  for i = 1, #state.sources do
    if source_state(state.sources[i].url).disabled then
      failed = failed + 1
    end
  end
  state.failed_sources = failed
end

local function prune_items_for_url(url)
  local keep = {}
  for i = 1, #state.items do
    local item = state.items[i]
    if item.source_url ~= url then
      keep[#keep + 1] = item
    end
  end
  state.items = keep
  if state.headline_idx > #state.items then
    state.headline_idx = 1
  end
end

local function mark_source_ok(src)
  if not src then return end
  local st = source_state(src.url)
  st.failures = 0
  st.last_err = nil
end

local function mark_source_fail(src, reason)
  if not src then
    state.err = reason
    return
  end
  local st = source_state(src.url)
  st.failures = st.failures + 1
  st.last_err = reason
  if st.failures >= 2 then
    st.disabled = true
    prune_items_for_url(src.url)
  end
  recount_failed_sources()
  state.err = reason
end

local function sync_sources()
  local theme_key, sources = get_theme_sources()
  local changed = theme_key ~= state.theme_key or #sources ~= #state.sources
  if not changed then
    for i = 1, #sources do
      local a = state.sources[i]
      local b = sources[i]
      if not a or a.url ~= b.url or a.name ~= b.name then
        changed = true
        break
      end
    end
  end
  if not changed then
    recount_failed_sources()
    return
  end

  state.theme_key = theme_key
  state.theme = theme_key
  state.sources = {}
  state.items = {}
  state.headline_idx = 1
  state.scroll_x = 64
  state.hold_until_ms = sys.now_ms() + state.scroll_hold_ms
  state.feed_idx = 1
  state.req_id = nil
  state.req_source_idx = 0
  for i = 1, #sources do
    state.sources[i] = {name = sources[i].name, url = sources[i].url}
    local st = source_state(sources[i].url)
    st.failures = 0
    st.disabled = false
    st.last_err = nil
  end
  recount_failed_sources()
end

local function healthy_source_count()
  local n = 0
  for i = 1, #state.sources do
    if not source_state(state.sources[i].url).disabled then
      n = n + 1
    end
  end
  return n
end

local function headline_key(item)
  return tostring(item.source_url or "") .. "|" .. tostring(item.title or "")
end

local function merge_feed_items(src, parsed)
  if not src or not parsed or type(parsed.items) ~= "table" then return end
  local seen = {}
  for i = 1, #state.items do
    seen[headline_key(state.items[i])] = true
  end
  local added = 0
  for i = 1, #parsed.items do
    local it = parsed.items[i]
    local title = compact_title(it.title)
    if title ~= "" then
      local item = {
        source = safe_upper(src.name),
        source_url = src.url,
        title = title,
        published = tostring(it.published or ""),
      }
      local key = headline_key(item)
      if not seen[key] then
        state.items[#state.items + 1] = item
        seen[key] = true
        added = added + 1
      end
    end
    if #state.items >= 18 then break end
  end
  if added > 0 and #state.items > 18 then
    while #state.items > 18 do
      table.remove(state.items)
    end
  end
end

local function active_item()
  if #state.items == 0 then return nil end
  if state.headline_idx < 1 or state.headline_idx > #state.items then
    state.headline_idx = 1
  end
  return state.items[state.headline_idx]
end

local function active_headline_text()
  local item = active_item()
  if not item then return "" end
  return safe_upper(item.title)
end

local function reset_scroll()
  local now = sys.now_ms()
  local text = active_headline_text()
  state.scroll_x = 64
  state.hold_until_ms = now + state.scroll_hold_ms
  state.last_scroll_ms = now
  state.headline_px = estimate_text_px(text)
end

local function advance_headline()
  if #state.items == 0 then
    state.headline_idx = 1
    return
  end
  state.headline_idx = (state.headline_idx % #state.items) + 1
  reset_scroll()
end

local function start_fetch(source_idx)
  local src = state.sources[source_idx]
  if not src then return end
  if source_state(src.url).disabled then return end
  local id, body, age_ms, err = net.cached_get(src.url, state.ttl_ms, 10000, 24576)
  if err then
    mark_source_fail(src, err)
    return
  end
  if body then
    local parsed, perr = xml.decode_feed(body, 6)
    if parsed then
      merge_feed_items(src, parsed)
      mark_source_ok(src)
      state.last_ok_ms = sys.now_ms()
      state.err = nil
      if #state.items == 1 then
        reset_scroll()
      end
    else
      mark_source_fail(src, perr or "XML ERR")
    end
    return
  end
  if id then
    state.req_id = id
    state.req_source_idx = source_idx
  end
end

local function poll_fetch()
  if not state.req_id then return end
  local done, status, body = net.cached_poll(state.req_id)
  if not done then return end
  local src = state.sources[state.req_source_idx]
  state.req_id = nil
  state.req_source_idx = 0
  if status ~= 200 then
    mark_source_fail(src, "HTTP " .. tostring(status))
    return
  end
  local parsed, err = xml.decode_feed(body, 6)
  if not parsed then
    mark_source_fail(src, err or "XML ERR")
    return
  end
  merge_feed_items(src, parsed)
  mark_source_ok(src)
  state.last_ok_ms = sys.now_ms()
  state.err = nil
  if #state.items == 1 then
    reset_scroll()
  end
end

local function maybe_fetch()
  if state.req_id then return end
  if #state.sources == 0 then return end
  if healthy_source_count() == 0 then return end
  local now = sys.now_ms()
  if now - state.last_fetch_ms < 1200 then return end

  local tries = 0
  while tries < #state.sources do
    local idx = state.feed_idx
    state.feed_idx = (state.feed_idx % #state.sources) + 1
    state.last_fetch_ms = now
    tries = tries + 1
    local src = state.sources[idx]
    if src and not source_state(src.url).disabled then
      start_fetch(idx)
      return
    end
  end
end

function app.init(config)
  sys.log("global_news_ticker init")
  sync_sources()
  state.last_scroll_ms = sys.now_ms()
  state.last_fetch_ms = 0
  state.err = nil
  state.last_ok_ms = 0
  reset_scroll()
  maybe_fetch()
end

function app.tick(dt_ms)
  sync_sources()
  poll_fetch()
  maybe_fetch()

  if #state.items == 0 then return end
  local now = sys.now_ms()
  if now < state.hold_until_ms then return end
  if now - state.last_scroll_ms < state.scroll_step_ms then return end

  state.scroll_x = state.scroll_x - 1
  state.last_scroll_ms = now
  if state.scroll_x + state.headline_px < -state.scroll_gap_px then
    advance_headline()
  end
end

function app.render_fb(fb)
  fb:fill(C_BG)
  rect_safe(fb, 0, 0, 64, 1, C_PANEL_2)
  rect_safe(fb, 0, 24, 64, 1, C_PANEL_2)
  rect_safe(fb, 0, 25, 64, 7, C_PANEL)

  local item = active_item()
  if not item then
    draw_text_shadow(fb, 0, 5, 64, "NEWS TICKER", C_TEXT, "center")
    if healthy_source_count() == 0 then
      draw_text_shadow(fb, 0, 15, 64, "ALL FEEDS OFF", C_WARN, "center")
    elseif state.err then
      draw_text_shadow(fb, 0, 15, 64, "WAIT FEEDS", C_WARN, "center")
    else
      draw_text_shadow(fb, 0, 15, 64, "LOADING", C_OK, "center")
    end
    return
  end

  local headline = active_headline_text()
  fb:text_box(state.scroll_x + 1, 9, state.headline_px + 32, 8, headline, C_SHADOW, FONT_UI, 8, "left", false)
  fb:text_box(state.scroll_x, 8, state.headline_px + 32, 8, headline, C_TEXT, FONT_UI, 8, "left", false)

  draw_text_shadow(fb, 2, 26, 24, safe_upper(item.source or "NEWS"), C_ACCENT, "left")

  local src_txt = string.format("%d/%d", healthy_source_count(), #state.sources)
  draw_text_shadow(fb, 23, 26, 18, src_txt, C_TEXT_DIM, "center")

  if state.failed_sources > 0 then
    draw_text_shadow(fb, 39, 26, 23, "ERR" .. tostring(state.failed_sources), C_WARN, "right")
  else
    draw_text_shadow(fb, 44, 26, 18, "LIVE", C_OK, "right")
  end
end

return app
