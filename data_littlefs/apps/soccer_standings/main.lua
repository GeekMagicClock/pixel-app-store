local app = {}

local FONT = "builtin:silkscreen_regular_8"

-- Layout tuning knobs for 64x32 panel.
-- Adjust these constants first for manual standings UI tweaking.
local Y_SHIFT = -2
local Y_BODY_SHIFT = -2
local Y_LINE_TOP = 8 + Y_SHIFT
local Y_ERR_TITLE = 0 + Y_SHIFT
local Y_ERR_BODY = 12 + Y_SHIFT
local Y_ERR_FOOTER = 22 + Y_SHIFT
local Y_HEADER = 0 + Y_SHIFT
local X_HEADER = 1
local W_HEADER = 62
local X_TAG_STALE = 40
local W_TAG_STALE = 23
local Y_ROWS_START = 9 + Y_SHIFT + Y_BODY_SHIFT
local Y_ROW_STEP = 6
local X_COL_TEAM = 1
local W_COL_TEAM = 20   -- 4 chars max
local GAP_TEAM_SECOND = 2
local X_COL_SECONDARY = X_COL_TEAM + W_COL_TEAM + GAP_TEAM_SECOND
local W_COL_SECONDARY = 15 -- 3 chars max
local X_COL_PRIMARY = X_COL_SECONDARY + W_COL_SECONDARY
local W_COL_PRIMARY = 64 - X_COL_PRIMARY
local Y_EMPTY = 14 + Y_SHIFT

local C_BG = 0x0000
local C_TEXT = 0xFFFF
local C_MUTED = 0x9CF3
local C_LINE = 0x18C3
local C_ACCENT = 0x07FF
local C_HI = 0xFFE0
local C_WARN = 0xFD20
local C_ROW_1 = 0xFFFF
local C_ROW_2 = 0x87F0
local C_ROW_3 = 0x07FF
local C_ROW_4 = 0xFD20
local BOOT_SPLASH_MS = 1200

local SPORT = "soccer"
local LEAGUE = tostring(data.get("soccer_standings.league") or "eng.1")
local TEAM = string.upper(tostring(data.get("soccer_standings.team") or data.get("sports.default_team") or ""))
local MODE = tostring(data.get("soccer_standings.mode") or (TEAM ~= "" and "around_team" or "top"))
local AUTO_PAGE = tostring(data.get("soccer_standings.auto_page") or "1") ~= "0"
local ROTATE_MS = tonumber(data.get("soccer_standings.rotate_ms") or 3500) or 3500
local ROWS_PER_PAGE = math.max(1, math.min(4, tonumber(data.get("soccer_standings.rows_per_page") or 4) or 4))
local TTL_MS = tonumber(data.get("soccer_standings.ttl_ms") or 10 * 60 * 1000) or (10 * 60 * 1000)
local MAX_BODY = tonumber(data.get("soccer_standings.max_body") or 393216) or 393216
local TIMEOUT_MS = tonumber(data.get("soccer_standings.timeout_ms") or 8000) or 8000
local APP_NAME = tostring(data.get("soccer_standings.app_name") or "Soccer Standings")

local state = {
  req_id = nil,
  last_req_ms = 0,
  last_ok_ms = 0,
  err = nil,
  anim_ms = 0,
  payload = nil,
  boot_started_ms = 0,
}

local function now_ms()
  return sys.now_ms()
end

local function split_title_lines(name)
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

local function standings_url()
  if SPORT == "soccer" then
    return string.format("https://site.api.espn.com/apis/v2/sports/%s/%s/standings?groups=1&limit=20", SPORT, LEAGUE)
  end
  return string.format("https://site.api.espn.com/apis/v2/sports/%s/%s/standings", SPORT, LEAGUE)
end

local function compact_text(s, limit)
  s = tostring(s or "")
  s = string.gsub(s, "%s+", " ")
  s = string.gsub(s, "^%s+", "")
  s = string.gsub(s, "%s+$", "")
  local n = tonumber(limit) or 10
  if #s > n then return string.sub(s, 1, n - 1) .. "…" end
  return s
end

local function stat_value(entry, names)
  local stats = entry and entry.stats or nil
  if type(stats) ~= "table" then return nil, nil end
  local want = {}
  for j = 1, #names do
    want[string.lower(tostring(names[j]))] = true
  end
  for i = 1, #stats do
    local s = stats[i]
    local n1 = string.lower(tostring(s.name or ""))
    local n2 = string.lower(tostring(s.abbreviation or ""))
    if want[n1] or want[n2] then
      return s.displayValue or s.summary or s.value, tonumber(s.value)
    end
  end
  return nil, nil
end

local function normalize_row(entry, fallback_rank)
  local team = entry and entry.team or {}
  local abbr = string.upper(tostring(team.abbreviation or team.shortDisplayName or team.name or "---"))
  if #abbr > 4 then abbr = string.sub(abbr, 1, 4) end
  local rank = tonumber(entry and entry.position or 0) or 0
  if rank <= 0 then
    local seed_disp = stat_value(entry, {"playoffSeed", "seed", "rank"})
    local seed_num = tonumber(seed_disp)
    if seed_num and seed_num > 0 then
      rank = math.floor(seed_num + 0.5)
    elseif tonumber(fallback_rank or 0) and tonumber(fallback_rank or 0) > 0 then
      rank = tonumber(fallback_rank or 0)
    end
  end

  local points_disp = stat_value(entry, {"points"})
  local record_disp = stat_value(entry, {"overall", "gamesPlayed"})
  local pct_disp = stat_value(entry, {"winPercent", "winpct"})
  local gb_disp = stat_value(entry, {"gamesBehind", "gb"})
  local gd_disp = stat_value(entry, {"pointDifferential", "goalDifference"})
  local wins_disp = stat_value(entry, {"wins", "win", "w"})
  local draws_disp = stat_value(entry, {"ties", "draws"})
  local losses_disp = stat_value(entry, {"losses", "loss", "l"})
  local wins_num = tonumber(wins_disp)
  local losses_num = tonumber(losses_disp)

  local primary = "--"
  local secondary = ""
  if SPORT == "soccer" then
    primary = tostring(points_disp or "--")
    secondary = gd_disp and tostring(gd_disp) or ""
  elseif SPORT == "football" then
    primary = tostring(record_disp or "--")
    secondary = pct_disp and tostring(pct_disp) or ""
  elseif SPORT == "basketball" or SPORT == "baseball" then
    if wins_num and losses_num then
      primary = string.format("%d-%d", wins_num, losses_num)
    else
      local rec = tostring(record_disp or "--")
      if rec == "--" and wins_num then
        rec = tostring(wins_num) .. "-"
      end
      primary = rec
    end
    local gb = gb_disp and tostring(gb_disp) or ""
    if gb == "" or gb == "--" then
      if rank == 1 then
        gb = "0"
      else
        gb = "-"
      end
    end
    secondary = "G" .. gb
  elseif SPORT == "hockey" then
    primary = tostring(points_disp or record_disp or "--")
    secondary = record_disp and tostring(record_disp) or ""
  else
    primary = tostring(record_disp or points_disp or "--")
    secondary = gb_disp and tostring(gb_disp) or ""
  end

  if primary == "--" and wins_disp then
    primary = tostring(wins_disp)
    if draws_disp or losses_disp then
      secondary = tostring(draws_disp or 0) .. "-" .. tostring(losses_disp or 0)
    end
  end

  return {
    rank = rank,
    team = abbr,
    primary = compact_text(primary, 8),
    secondary = compact_text(secondary, 6),
    highlight = TEAM ~= "" and abbr == TEAM,
  }
end

local function collect_entries(obj)
  local rows = {}
  if type(obj) ~= "table" then return rows end
  if obj.standings and type(obj.standings.entries) == "table" then
    for i = 1, #obj.standings.entries do
      rows[#rows + 1] = normalize_row(obj.standings.entries[i], i)
    end
    return rows
  end
  if type(obj.children) == "table" then
    local seq = 0
    for i = 1, #obj.children do
      local child = obj.children[i]
      if child.standings and type(child.standings.entries) == "table" then
        for j = 1, #child.standings.entries do
          seq = seq + 1
          rows[#rows + 1] = normalize_row(child.standings.entries[j], seq)
        end
      end
    end
  end
  return rows
end

local function slice_rows(rows)
  if #rows <= ROWS_PER_PAGE then return rows end
  if MODE ~= "around_team" or TEAM == "" then
    local out = {}
    for i = 1, math.min(ROWS_PER_PAGE, #rows) do out[#out + 1] = rows[i] end
    return out
  end

  local idx = nil
  for i = 1, #rows do
    if rows[i].highlight then idx = i break end
  end
  if not idx then
    local out = {}
    for i = 1, math.min(ROWS_PER_PAGE, #rows) do out[#out + 1] = rows[i] end
    return out
  end

  local start_i = idx - 1
  if start_i < 1 then start_i = 1 end
  if start_i + (ROWS_PER_PAGE - 1) > #rows then start_i = math.max(1, #rows - (ROWS_PER_PAGE - 1)) end
  local out = {}
  for i = start_i, math.min(#rows, start_i + (ROWS_PER_PAGE - 1)) do
    out[#out + 1] = rows[i]
  end
  return out
end

local function build_model(obj)
  local all_rows = collect_entries(obj)
  local rows = slice_rows(all_rows)
  local pages = { rows }
  local can_paginate = (MODE ~= "around_team") or TEAM == ""
  if can_paginate and #all_rows > ROWS_PER_PAGE then
    pages = {}
    local i = 1
    while i <= #all_rows do
      local page = {}
      for j = i, math.min(i + (ROWS_PER_PAGE - 1), #all_rows) do
        page[#page + 1] = all_rows[j]
      end
      pages[#pages + 1] = page
      i = i + ROWS_PER_PAGE
    end
  end
  return {
    title = string.upper(compact_text(tostring(obj.name or LEAGUE), 12)),
    rows = rows,
    pages = pages,
  }
end

local function header_text(model_title)
  if SPORT == "soccer" then
    return "SOCCER STAND"
  end
  local t = string.upper(tostring(model_title or LEAGUE or ""))
  t = string.gsub(t, " STANDINGS", "")
  t = string.gsub(t, " STANDING", "")
  t = string.gsub(t, " TABLE", "")
  t = string.gsub(t, "^%s+", "")
  t = string.gsub(t, "%s+$", "")
  if t == "" then t = string.upper(tostring(LEAGUE)) end
  return compact_text(t, 10)
end

local function build_mock_payload()
  if not (mock and mock.enabled and mock.enabled()) then return nil end
  local payload = mock.data and mock.data() or nil
  if type(payload) ~= "table" then return nil end
  if type(payload.model) == "table" then return payload.model end
  if payload.title or payload.rows or payload.pages then return payload end
  if payload.standings or payload.children then return build_model(payload) end
  return nil
end

local function stale_tag()
  if not state.last_ok_ms or state.last_ok_ms <= 0 then return "" end
  if now_ms() - state.last_ok_ms < (TTL_MS * 2) then return "" end
  return "OLD"
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
  state.payload = build_model(obj)
  state.err = nil
  state.last_ok_ms = now_ms()
end

local function start_request()
  if state.req_id then return end
  local id, body, age_ms, err = net.cached_get(standings_url(), TTL_MS, TIMEOUT_MS, MAX_BODY)
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
  state.err = "REQ FAIL"
end

function app.init()
  state.req_id = nil
  state.last_req_ms = 0
  state.last_ok_ms = 0
  state.err = nil
  state.anim_ms = 0
  state.boot_started_ms = now_ms()
  state.payload = build_mock_payload()
  if state.payload then
    state.last_ok_ms = now_ms()
    return
  end
  start_request()
end

function app.tick(dt_ms)
  local dt = dt_ms or 0
  state.anim_ms = state.anim_ms + dt
  if state.anim_ms > 2147480000 then
    state.anim_ms = state.anim_ms % 2147480000
  end
  local mock_payload = build_mock_payload()
  if mock_payload then
    state.req_id = nil
    state.err = nil
    state.payload = mock_payload
    state.last_ok_ms = now_ms()
    return
  end
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
  local interval = state.err and 60000 or TTL_MS
  if now_ms() - state.last_req_ms >= interval then start_request() end
end

function app.render_fb(fb)
  fb:fill(C_BG)
  if state.boot_started_ms > 0 and (now_ms() - state.boot_started_ms) < BOOT_SPLASH_MS then
    local t1, t2 = split_title_lines(APP_NAME)
    if t2 ~= "" then
      fb:text_box(0, 8, 64, 8, compact_text(t1, 14), C_ACCENT, FONT, 8, "center", false)
      fb:text_box(0, 16, 64, 8, compact_text(t2, 14), C_ACCENT, FONT, 8, "center", false)
    else
      fb:text_box(0, 12, 64, 8, compact_text(t1, 14), C_ACCENT, FONT, 8, "center", false)
    end
    return
  end
  fb:rect(0, Y_LINE_TOP, 64, 1, C_LINE)

  if state.err and not state.payload then
    fb:text_box(0, Y_ERR_TITLE, 64, 8, "STANDINGS", C_TEXT, FONT, 8, "center", false)
    fb:text_box(0, Y_ERR_BODY, 64, 8, compact_text(state.err, 16), C_WARN, FONT, 8, "center", false)
    fb:text_box(0, Y_ERR_FOOTER, 64, 8, string.upper(LEAGUE), C_MUTED, FONT, 8, "center", false)
    return
  end

  local model = state.payload or { title = string.upper(LEAGUE), rows = {}, pages = {} }
  local stale = stale_tag()
  local rows = model.rows
  if AUTO_PAGE and type(model.pages) == "table" and #model.pages > 1 then
    local idx = math.floor(state.anim_ms / ROTATE_MS) % #model.pages + 1
    rows = model.pages[idx]
  end
  fb:text_box(X_HEADER, Y_HEADER, 62, 8, header_text(model.title), C_ACCENT, FONT, 8, "left", false)
  if stale ~= "" then
    fb:text_box(X_TAG_STALE, Y_HEADER, W_TAG_STALE, 8, stale, C_WARN, FONT, 8, "right", false)
  end
  for i = 1, #rows do
    local row = rows[i]
    local y = Y_ROWS_START + (i - 1) * Y_ROW_STEP
    local row_colors = { C_ROW_1, C_ROW_2, C_ROW_3, C_ROW_4 }
    local color = row_colors[((i - 1) % 4) + 1]
    if row.highlight then color = C_HI end
    local team_txt = compact_text(tostring(row.team or "---"), 4)
    local g_txt = compact_text(tostring(row.secondary or ""), 3)
    local primary_txt = compact_text(tostring(row.primary or "--"), 8)
    fb:text_box(X_COL_TEAM, y, W_COL_TEAM, 8, team_txt, color, FONT, 8, "left", false)
    fb:text_box(X_COL_SECONDARY, y, W_COL_SECONDARY, 8, g_txt, color, FONT, 8, "left", false)
    fb:text_box(X_COL_PRIMARY, y, W_COL_PRIMARY, 8, primary_txt, color, FONT, 8, "right", false)
  end
  if #rows == 0 then
    fb:text_box(0, Y_EMPTY, 64, 8, "NO TABLE", C_MUTED, FONT, 8, "center", false)
  end
end


-- __GLOBAL_BOOT_SPLASH_WRAPPER_V1__
local __boot_now_ms = now_ms or (sys and sys.now_ms) or function() return 0 end
local __boot_started_ms = 0
local __boot_ms = tonumber(data.get("soccer_standings.boot_splash_ms") or data.get("app.boot_splash_ms") or 1200) or 1200
if __boot_ms < 0 then __boot_ms = 0 end
local __boot_name = tostring(data.get("soccer_standings.app_name") or "Soccer Standings")

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
