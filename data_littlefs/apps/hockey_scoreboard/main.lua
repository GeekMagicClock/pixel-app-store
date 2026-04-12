local app = {}

local FONT = "builtin:silkscreen_regular_8"
local FONT_TITLE = "builtin:pressstart2p_regular_8"

-- 64x32 pixel grid layout (4 rows x 8px):
-- row0: y=0   title + state tag
-- row1: y=8   score line
-- row2: y=16  game status
-- row3: y=24  rotating detail / fallback
local Y_ERR_TITLE = 0
local Y_ERR_BODY = 8
local Y_ERR_FOOTER = 24
local Y_HEADER = -2
local Y_ROW_1 = 8
local Y_ROW_2 = 16
local Y_ROW_3 = 24
local X_TEXT = 0
local W_TEXT = 64
local X_TAG_LIVE = 42
local W_TAG_LIVE = 22
local X_TAG_STALE = 38
local W_TAG_STALE = 26

local C_BG = 0x0000
local C_TEXT = 0xFFFF
local C_MUTED = 0x9CF3
local C_ACCENT = 0x07FF
local C_LIVE = 0xF800
local C_WARN = 0xFD20
local C_GOOD = 0x07E0
local C_BAD = 0xF800
local BOOT_SPLASH_MS = 1200

local function num_setting(key, fallback)
  local v = tonumber(data.get(key))
  if v == nil then return fallback end
  return v
end

local SPORT = "hockey"
local LEAGUE = tostring(data.get("hockey_scoreboard.league") or "nhl")
local TEAM = string.upper(tostring(data.get("hockey_scoreboard.team") or data.get("game_summary.team") or data.get("sports.default_team") or ""))
local TEAM_ID = tostring(data.get("hockey_scoreboard.team_id") or data.get("game_summary.team_id") or data.get("sports.default_team_id") or "")
local EVENT_ID = tostring(data.get("hockey_scoreboard.event_id") or data.get("game_summary.event_id") or "")
local SCOREBOARD_ONLY = tostring(data.get("hockey_scoreboard.scoreboard_only") or data.get("game_summary.scoreboard_only") or ((SPORT == "soccer" and EVENT_ID == "") and "1" or "0")) == "1"
local USE_DATE_FILTER = tostring(data.get("hockey_scoreboard.use_date_filter") or data.get("game_summary.use_date_filter") or "1") ~= "0"
local AUTO_PAGE = tostring(data.get("hockey_scoreboard.auto_page") or data.get("game_summary.auto_page") or "1") ~= "0"
local ROTATE_SEC = num_setting("hockey_scoreboard.rotate_sec", num_setting("game_summary.rotate_sec", nil))
local REFRESH_SEC = num_setting("hockey_scoreboard.refresh_sec", num_setting("game_summary.refresh_sec", nil))
local ROTATE_MS = math.floor((ROTATE_SEC and ROTATE_SEC > 0 and ROTATE_SEC or (num_setting("hockey_scoreboard.rotate_ms", num_setting("game_summary.rotate_ms", 3500)) / 1000)) * 1000)
local TTL_MS = math.floor((REFRESH_SEC and REFRESH_SEC > 0 and REFRESH_SEC or (num_setting("hockey_scoreboard.ttl_ms", num_setting("game_summary.ttl_ms", 60000)) / 1000)) * 1000)
local MAX_BODY = tonumber(data.get("hockey_scoreboard.max_body") or data.get("game_summary.max_body") or 512000) or 512000
local SCOREBOARD_MAX_BODY = tonumber(data.get("hockey_scoreboard.scoreboard_max_body") or data.get("game_summary.scoreboard_max_body") or 458752) or 458752
local SCOREBOARD_MAX_BODY_HARD = 524288
local TIMEOUT_MS = tonumber(data.get("hockey_scoreboard.timeout_ms") or data.get("game_summary.timeout_ms") or 8000) or 8000
local APP_NAME = tostring(data.get("hockey_scoreboard.app_name") or "Hockey Scoreboard")
if ROTATE_MS < 1000 then ROTATE_MS = 1000 end
if TTL_MS < 10000 then TTL_MS = 10000 end
if MAX_BODY < 16384 then MAX_BODY = 16384 end
if MAX_BODY > 512000 then MAX_BODY = 512000 end
if SCOREBOARD_MAX_BODY < 65536 then SCOREBOARD_MAX_BODY = 65536 end
if SCOREBOARD_MAX_BODY > SCOREBOARD_MAX_BODY_HARD then SCOREBOARD_MAX_BODY = SCOREBOARD_MAX_BODY_HARD end

local state = {
  req_id = nil,
  req_kind = nil,
  last_req_ms = 0,
  last_ok_ms = 0,
  err = nil,
  anim_ms = 0,
  resolved_event_id = EVENT_ID,
  payload = nil,
  tried_open_window = false,
  backoff_until_ms = 0,
  boot_started_ms = 0,
  scoreboard_cap = SCOREBOARD_MAX_BODY,
  board_pages = nil,
}

local function board_rotation_enabled()
  -- Auto page rotation is allowed regardless of Team Focus.
  -- Event lock (event_id) disables rotation intentionally.
  return AUTO_PAGE and EVENT_ID == ""
end

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

local function compact_text(s, limit)
  s = tostring(s or "")
  s = string.gsub(s, "%s+", " ")
  s = string.gsub(s, "^%s+", "")
  s = string.gsub(s, "%s+$", "")
  local n = tonumber(limit) or 16
  if #s > n then return string.sub(s, 1, n - 1) .. "…" end
  return s
end

local function scoreboard_url(no_date_filter)
  local base = string.format("https://site.api.espn.com/apis/site/v2/sports/%s/%s/scoreboard", SPORT, LEAGUE)
  if no_date_filter or not USE_DATE_FILTER then return base end
  local t = sys and sys.local_time and sys.local_time() or {}
  local y = tonumber(t.year) or 0
  local m = tonumber(t.month) or 0
  local d = tonumber(t.day) or 0
  if y <= 0 or m <= 0 or d <= 0 then return base end
  return string.format("%s?dates=%04d%02d%02d", base, y, m, d)
end

local function summary_url(event_id)
  return string.format("https://site.api.espn.com/apis/site/v2/sports/%s/%s/summary?event=%s", SPORT, LEAGUE, tostring(event_id))
end

local function team_match(comp)
  if type(comp) ~= "table" then return false end
  local team = comp.team or {}
  if TEAM_ID ~= "" and tostring(team.id or "") == TEAM_ID then return true end
  if TEAM == "" then return false end
  local opts = {
    team.abbreviation,
    team.shortDisplayName,
    team.displayName,
    team.name,
  }
  for i = 1, #opts do
    if string.upper(tostring(opts[i] or "")) == TEAM then return true end
  end
  return false
end

local function find_home_away(competition)
  local comps = competition and competition.competitors or nil
  if type(comps) ~= "table" then return nil, nil end
  local home, away = nil, nil
  for i = 1, #comps do
    local c = comps[i]
    if tostring(c.homeAway or "") == "home" then home = c end
    if tostring(c.homeAway or "") == "away" then away = c end
  end
  if not home then home = comps[1] end
  if not away then away = comps[2] or comps[1] end
  return home, away
end

local function abbr(team_or_comp)
  local team = team_or_comp and (team_or_comp.team or team_or_comp) or {}
  local s = string.upper(tostring(team.abbreviation or team.shortDisplayName or team.name or "---"))
  if #s > 3 then s = string.sub(s, 1, 3) end
  return s
end

local function score_text(comp)
  local s = tostring(comp and comp.score or "-")
  if #s > 3 then s = string.sub(s, #s - 2) end
  return s
end

local function score_num(comp)
  local n = tonumber(comp and comp.score or "")
  if n == nil then return nil end
  return n
end

local function score_header(away, home)
  return string.format("%s %s-%s %s", abbr(away), score_text(away), score_text(home), abbr(home))
end

local function hm_from_iso(iso)
  return string.match(tostring(iso or ""), "T(%d%d:%d%d)") or ""
end

local function is_leap_year(y)
  y = tonumber(y) or 0
  if y % 400 == 0 then return true end
  if y % 100 == 0 then return false end
  return (y % 4) == 0
end

local function days_in_month(y, m)
  if m == 4 or m == 6 or m == 9 or m == 11 then return 30 end
  if m == 2 then return is_leap_year(y) and 29 or 28 end
  return 31
end

local function epoch_from_local_parts(y, m, d, hh, mm, ss)
  y = tonumber(y) or 1970
  m = tonumber(m) or 1
  d = tonumber(d) or 1
  hh = tonumber(hh) or 0
  mm = tonumber(mm) or 0
  ss = tonumber(ss) or 0
  if y < 1970 then return nil end
  local days = 0
  for yy = 1970, y - 1 do
    days = days + (is_leap_year(yy) and 366 or 365)
  end
  for mo = 1, m - 1 do
    days = days + days_in_month(y, mo)
  end
  days = days + math.max(0, d - 1)
  return days * 86400 + hh * 3600 + mm * 60 + ss
end

local function parse_iso_utc_epoch(iso)
  local s = tostring(iso or "")
  local y, m, d, hh, mm, ss = string.match(s, "^(%d%d%d%d)%-(%d%d)%-(%d%d)T(%d%d):(%d%d):(%d%d)Z$")
  if not y then
    y, m, d, hh, mm = string.match(s, "^(%d%d%d%d)%-(%d%d)%-(%d%d)T(%d%d):(%d%d)Z$")
    ss = "00"
  end
  if not y then return nil end
  return epoch_from_local_parts(y, m, d, hh, mm, ss)
end

local function local_utc_offset_sec()
  local lt = sys and sys.local_time and sys.local_time() or nil
  local ut = sys and sys.unix_time and sys.unix_time() or nil
  if type(lt) ~= "table" then return nil end
  ut = tonumber(ut)
  if not ut then return nil end
  local local_epoch = epoch_from_local_parts(lt.year, lt.month, lt.day, lt.hour, lt.min, lt.sec)
  if not local_epoch then return nil end
  return local_epoch - math.floor(ut)
end

local function hm_local_from_iso(iso)
  local utc_epoch = parse_iso_utc_epoch(iso)
  if not utc_epoch then return "" end
  local offset = local_utc_offset_sec()
  if not offset then return "" end
  local local_epoch = utc_epoch + offset
  local minute_of_day = math.floor(local_epoch / 60) % 1440
  if minute_of_day < 0 then minute_of_day = minute_of_day + 1440 end
  local h = math.floor(minute_of_day / 60)
  local m = minute_of_day % 60
  return string.format("%02d:%02d", h, m)
end

local function hm_from_status(detail)
  if type(detail) ~= "string" then return "" end
  local hm = string.match(detail, "(%d%d:%d%d)")
  if hm then return hm end
  local h, m = string.match(detail, "(%d?%d):(%d%d)")
  if not h or not m then return "" end
  h = tonumber(h)
  if not h then return "" end
  return string.format("%02d:%s", h, m)
end

local function kickoff_label(event, status_text)
  local hm = hm_local_from_iso(event and event.date)
  if hm == "" then
    hm = hm_from_status(tostring(status_text or ""))
  end
  if hm == "" then
    hm = hm_from_iso(event and event.date)
  end
  if hm == "" then return "" end
  return compact_text("START " .. hm, 15)
end

local function choose_event(obj)
  local events = obj and obj.events or {}
  local best = nil
  local best_score = -1
  for i = 1, #events do
    local e = events[i]
    local comp = e and e.competitions and e.competitions[1] or nil
    local home, away = find_home_away(comp)
    local st = tostring(e and e.status and e.status.type and e.status.type.state or "")
    local score = 0
    if team_match(home) or team_match(away) then score = score + 100 end
    if st == "in" then score = score + 50 elseif st == "pre" then score = score + 25 else score = score + 10 end
    if score > best_score then
      best = e
      best_score = score
    end
  end
  return best
end

local function parse_event_fallback(event)
  if not event then return nil end
  local comp = event.competitions and event.competitions[1] or nil
  local home, away = find_home_away(comp)
  local status_text = event.status and event.status.type and (event.status.type.shortDetail or event.status.type.detail) or ""
  local state_name = tostring(event.status and event.status.type and event.status.type.state or "")
  return {
    header = compact_text(score_header(away, home), 14),
    away_abbr = abbr(away),
    away_score = score_text(away),
    away_score_num = score_num(away),
    home_abbr = abbr(home),
    home_score = score_text(home),
    home_score_num = score_num(home),
    status = compact_text(status_text, 16),
    detail1 = string.upper(LEAGUE),
    detail2 = TEAM ~= "" and TEAM or string.upper(SPORT),
    start_label = state_name == "pre" and kickoff_label(event, status_text) or "",
    state = state_name,
    accent = state_name == "in" and C_LIVE or C_ACCENT,
  }
end

local function parse_scoreboard_fallback(obj)
  return parse_event_fallback(choose_event(obj))
end

local function event_sort_score(event)
  local st = tostring(event and event.status and event.status.type and event.status.type.state or "")
  if st == "in" then return 300 end
  if st == "pre" then return 200 end
  if st == "post" then return 100 end
  return 0
end

local function build_scoreboard_pages(obj)
  local events = obj and obj.events or {}
  if type(events) ~= "table" or #events == 0 then return {} end
  local pages = {}
  for i = 1, #events do
    local e = events[i]
    local m = parse_event_fallback(e)
    if m then
      pages[#pages + 1] = { event = e, model = m }
    end
  end
  if #pages <= 1 then return pages end
  table.sort(pages, function(a, b)
    local sa = event_sort_score(a.event)
    local sb = event_sort_score(b.event)
    if sa ~= sb then return sa > sb end
    local da = tostring(a.event and a.event.date or "")
    local db = tostring(b.event and b.event.date or "")
    return da < db
  end)
  if #pages > 6 then
    local out = {}
    for i = 1, 6 do out[#out + 1] = pages[i] end
    return out
  end
  return pages
end

local function parse_summary(obj)
  local header = obj and obj.header or {}
  local competition = header and header.competitions and header.competitions[1] or nil
  local home, away = find_home_away(competition)

  if not home or not away then
    local boxteams = obj and obj.boxscore and obj.boxscore.teams or nil
    if type(boxteams) == "table" and #boxteams >= 2 then
      away = { team = boxteams[1].team or boxteams[1], score = boxteams[1].score }
      home = { team = boxteams[2].team or boxteams[2], score = boxteams[2].score }
    end
  end

  if not home or not away then return nil end

  local status = competition and competition.status and competition.status.type and (competition.status.type.shortDetail or competition.status.type.detail)
  local detail1 = ""
  local detail2 = ""

  local function stat_line(team_box)
    if type(team_box) ~= "table" or type(team_box.statistics) ~= "table" then return "" end
    local preferred = {}
    if SPORT == "soccer" then
      preferred = {"shotsOnTarget", "yellowCards", "redCards", "foulsCommitted"}
    elseif SPORT == "football" then
      preferred = {"firstDowns", "passingYards", "rushingYards", "totalYards"}
    elseif SPORT == "basketball" then
      preferred = {"rebounds", "assists", "fieldGoalPct", "threePointFieldGoalPct"}
    else
      preferred = {"shotsOnGoal", "hits", "penaltyMinutes"}
    end
    for i = 1, #preferred do
      local want = preferred[i]
      for j = 1, #team_box.statistics do
        local s = team_box.statistics[j]
        if tostring(s.name or "") == want then
          local label = tostring(s.label or want)
          local value = tostring(s.displayValue or s.value or "")
          if value ~= "" then
            return compact_text(label .. " " .. value, 16)
          end
        end
      end
    end
    return ""
  end

  local situation = obj and obj.situation or nil
  if type(situation) == "table" then
    detail1 = compact_text(situation.downDistanceText or situation.lastPlayText or situation.shortDownDistanceText or "", 16)
    detail2 = compact_text(situation.possessionText or situation.yardLine or "", 16)
  end

  if detail1 == "" and type(obj.boxscore) == "table" and type(obj.boxscore.teams) == "table" then
    detail1 = stat_line(obj.boxscore.teams[1])
    detail2 = stat_line(obj.boxscore.teams[2])
  end

  if detail1 == "" and type(obj.pickcenter) == "table" and type(obj.pickcenter[1]) == "table" then
    detail1 = compact_text(obj.pickcenter[1].details or obj.pickcenter[1].text or "", 16)
  end
  if detail1 == "" and type(obj.news) == "table" and type(obj.news[1]) == "table" then
    detail1 = compact_text(obj.news[1].headline or "", 16)
  end

  if detail1 == "" then detail1 = string.upper(LEAGUE) end
  if detail2 == "" then detail2 = TEAM ~= "" and TEAM or string.upper(SPORT) end

  local state_name = tostring(competition and competition.status and competition.status.type and competition.status.type.state or "")
  return {
    header = compact_text(score_header(away, home), 14),
    away_abbr = abbr(away),
    away_score = score_text(away),
    away_score_num = score_num(away),
    home_abbr = abbr(home),
    home_score = score_text(home),
    home_score_num = score_num(home),
    status = compact_text(status or "", 16),
    detail1 = detail1,
    detail2 = detail2,
    state = state_name,
    accent = state_name == "in" and C_LIVE or C_ACCENT,
  }
end

local function stale_tag()
  if not state.last_ok_ms or state.last_ok_ms <= 0 then return "" end
  if now_ms() - state.last_ok_ms < (TTL_MS * 2) then return "" end
  return "OLD"
end

local function build_mock_payload()
  if not (mock and mock.enabled and mock.enabled()) then return nil end
  local payload = mock.data and mock.data() or nil
  if type(payload) ~= "table" then return nil end
  if type(payload.model) == "table" then return payload.model end
  if payload.header or payload.status or payload.detail1 or payload.detail2 then return payload end
  if payload.header and payload.competitions then return parse_summary(payload) end
  if payload.events then
    if SCOREBOARD_ONLY then return parse_scoreboard_fallback(payload) end
    local event = choose_event(payload)
    if event and event.id then state.resolved_event_id = tostring(event.id) end
    return parse_scoreboard_fallback(payload)
  end
  return nil
end

local function handle_scoreboard_response(status, body)
  if status ~= 200 then
    state.err = "HTTP " .. tostring(status)
    state.backoff_until_ms = now_ms() + 30000
    return
  end
  local obj, jerr = json.decode(body)
  if not obj then
    state.err = jerr or "JSON ERR"
    state.backoff_until_ms = now_ms() + 30000
    return
  end
  local event = choose_event(obj)
  local parsed = parse_scoreboard_fallback(obj)
  local global_rotate = board_rotation_enabled()
  if global_rotate then
    state.board_pages = build_scoreboard_pages(obj)
  else
    state.board_pages = nil
  end
  if not parsed then
    if state.payload then
      -- Keep last good board on temporary empty scoreboard windows.
      state.err = nil
      return
    end
    if USE_DATE_FILTER and not state.tried_open_window then
      state.tried_open_window = true
      state.last_req_ms = 0
      return
    end
    state.payload = {
      header = "NO GAME",
      away_abbr = "---",
      away_score = "-",
      away_score_num = nil,
      home_abbr = "---",
      home_score = "-",
      home_score_num = nil,
      status = compact_text(string.upper(LEAGUE), 15),
      detail1 = "CHECK LATER",
      detail2 = TEAM ~= "" and TEAM or string.upper(SPORT),
      state = "post",
      accent = C_WARN,
    }
    state.err = nil
    state.last_ok_ms = now_ms()
    state.backoff_until_ms = 0
    return
  end
  if SCOREBOARD_ONLY then
    if global_rotate and type(state.board_pages) == "table" and #state.board_pages > 0 then
      state.payload = state.board_pages[1].model
    else
      state.payload = parsed
    end
    state.err = nil
    state.last_ok_ms = now_ms()
    state.tried_open_window = false
    state.backoff_until_ms = 0
    if (not global_rotate) and event and event.id then state.resolved_event_id = tostring(event.id) end
    return
  end
  if not event or not event.id then
    state.payload = parsed
    state.err = nil
    state.last_ok_ms = now_ms()
    state.tried_open_window = false
    state.backoff_until_ms = 0
    return
  end
  -- Scoreboard is always the primary render model, even when summary is available.
  if global_rotate and type(state.board_pages) == "table" and #state.board_pages > 0 then
    state.payload = state.board_pages[1].model
    state.resolved_event_id = ""
  else
    state.payload = parsed
    state.resolved_event_id = tostring(event.id)
  end
  state.err = nil
  state.last_ok_ms = now_ms()
  state.tried_open_window = false
  state.backoff_until_ms = 0
end

local function handle_summary_response(status, body)
  if status ~= 200 then
    state.err = "HTTP " .. tostring(status)
    state.backoff_until_ms = now_ms() + 30000
    return
  end
  local obj, jerr = json.decode(body)
  if not obj then
    state.err = jerr or "JSON ERR"
    state.backoff_until_ms = now_ms() + 30000
    return
  end
  state.payload = parse_summary(obj)
  state.board_pages = nil
  if not state.payload then
    state.err = "BAD DATA"
    state.backoff_until_ms = now_ms() + 30000
    return
  end
  state.err = nil
  state.last_ok_ms = now_ms()
  state.backoff_until_ms = 0
end

local function handle_request_error(kind, err)
  local err_text = string.upper(tostring(err or ""))
  if kind == "scoreboard" and string.find(err_text, "GZIP DECODE FAILED", 1, true) and state.scoreboard_cap < SCOREBOARD_MAX_BODY_HARD then
    state.scoreboard_cap = math.min(SCOREBOARD_MAX_BODY_HARD, state.scoreboard_cap + 65536)
    state.err = "RETRY SCOREBOARD"
    state.backoff_until_ms = now_ms() + 1000
    return
  end
  state.err = tostring(err or "REQ FAIL")
  state.backoff_until_ms = now_ms() + 30000
end

local function start_request(kind)
  if state.req_id then return end
  if state.backoff_until_ms > 0 and now_ms() < state.backoff_until_ms then return end
  local url = kind == "scoreboard" and scoreboard_url(state.tried_open_window) or summary_url(state.resolved_event_id)
  local request_max_body = kind == "scoreboard" and state.scoreboard_cap or MAX_BODY
  local id, body, age_ms, err = net.cached_get(url, TTL_MS, TIMEOUT_MS, request_max_body)
  if err then
    handle_request_error(kind, err)
    return
  end
  if body then
    if kind == "scoreboard" then
      handle_scoreboard_response(200, body)
    else
      handle_summary_response(200, body)
    end
    state.last_req_ms = now_ms()
    return
  end
  if id then
    state.req_id = id
    state.req_kind = kind
    state.last_req_ms = now_ms()
    return
  end
  state.err = "REQ FAIL"
  state.backoff_until_ms = now_ms() + 30000
end

local function ensure_request()
  if SCOREBOARD_ONLY or state.resolved_event_id == "" or board_rotation_enabled() then
    start_request("scoreboard")
  else
    start_request("summary")
  end
end

function app.init()
  state.req_id = nil
  state.req_kind = nil
  state.last_req_ms = 0
  state.last_ok_ms = 0
  state.err = nil
  state.anim_ms = 0
  state.resolved_event_id = EVENT_ID
  state.tried_open_window = false
  state.backoff_until_ms = 0
  state.scoreboard_cap = SCOREBOARD_MAX_BODY
  state.board_pages = nil
  state.payload = build_mock_payload()
  state.boot_started_ms = now_ms()
  if state.payload then
    state.last_ok_ms = now_ms()
    return
  end
  ensure_request()
end

function app.tick(dt_ms)
  state.anim_ms = (state.anim_ms + (dt_ms or 0)) % (ROTATE_MS * 8)
  local mock_payload = build_mock_payload()
  if mock_payload then
    state.req_id = nil
    state.req_kind = nil
    state.err = nil
    state.payload = mock_payload
    state.last_ok_ms = now_ms()
    return
  end
  if state.req_id then
    local done, status, body = net.cached_poll(state.req_id)
    if done then
      local kind = state.req_kind
      state.req_id = nil
      state.req_kind = nil
      if status == 0 then
        handle_request_error(kind, body or "HTTP ERR")
      elseif kind == "scoreboard" then
        handle_scoreboard_response(status, body or "")
      else
        handle_summary_response(status, body or "")
      end
    end
    return
  end
  local interval = state.err and 30000 or TTL_MS
  if now_ms() - state.last_req_ms >= interval then
    ensure_request()
  end
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

  if state.err and not state.payload then
    fb:text_box(0, Y_ERR_TITLE, 64, 8, "SCOREBOARD", C_TEXT, FONT, 8, "center", false)
    fb:text_box(0, Y_ERR_BODY, 64, 8, compact_text(state.err, 16), C_WARN, FONT, 8, "center", false)
    fb:text_box(0, Y_ERR_FOOTER, 64, 8, TEAM ~= "" and TEAM or string.upper(LEAGUE), C_MUTED, FONT, 8, "center", false)
    return
  end

  local model = state.payload or {
    header = "LOADING",
    away_abbr = "---",
    away_score = "-",
    away_score_num = nil,
    home_abbr = "---",
    home_score = "-",
    home_score_num = nil,
    status = "",
    detail1 = string.upper(LEAGUE),
    detail2 = TEAM ~= "" and TEAM or string.upper(SPORT),
    state = "",
    accent = C_ACCENT,
  }
  if board_rotation_enabled() and type(state.board_pages) == "table" and #state.board_pages > 1 then
    local live_idx = nil
    for i = 1, #state.board_pages do
      local m = state.board_pages[i].model
      if m and tostring(m.state or "") == "in" then
        live_idx = i
        break
      end
    end
    if live_idx then
      -- If any game is live, pin screen to live score (no rotation).
      model = state.board_pages[live_idx].model or model
    else
      local pidx = math.floor(state.anim_ms / ROTATE_MS) % #state.board_pages + 1
      model = state.board_pages[pidx].model or model
    end
  end

  local function line_color(self_score, other_score)
    if self_score == nil or other_score == nil then return C_TEXT end
    if self_score > other_score then return C_GOOD end
    if self_score < other_score then return C_BAD end
    return C_TEXT
  end

  local stale = stale_tag()
  local status_line = compact_text(tostring(model.status or ""), 15)
  local function is_generic_detail(v)
    local s = string.upper(tostring(v or ""))
    s = string.gsub(s, "^%s+", "")
    s = string.gsub(s, "%s+$", "")
    if s == "" then return true end
    if s == string.upper(LEAGUE) then return true end
    if s == string.upper(SPORT) then return true end
    if TEAM ~= "" and s == TEAM then return true end
    return false
  end
  local detail = model.detail1
  if AUTO_PAGE and tostring(model.detail2 or "") ~= "" and tostring(model.detail2 or "") ~= tostring(model.detail1 or "") then
    local rot = math.floor(state.anim_ms / ROTATE_MS) % 2
    detail = rot == 0 and model.detail1 or model.detail2
  elseif tostring(detail or "") == "" then
    detail = tostring(model.detail2 or "")
  end
  if tostring(model.state or "") == "in" and status_line ~= "" then
    detail = status_line
  elseif tostring(model.state or "") == "in" then
    if not is_generic_detail(model.detail1) then
      detail = compact_text(model.detail1, 15)
    elseif not is_generic_detail(model.detail2) then
      detail = compact_text(model.detail2, 15)
    else
      detail = "LIVE NOW"
    end
  elseif tostring(model.state or "") == "post" and status_line ~= "" then
    detail = status_line
  elseif tostring(model.state or "") == "post" then
    detail = "FINAL"
  elseif tostring(model.state or "") == "pre" and tostring(model.start_label or "") ~= "" then
    detail = tostring(model.start_label or "")
  end
  local detail_color = C_MUTED
  if stale ~= "" then
    detail_color = C_WARN
  elseif tostring(model.state or "") == "in" and status_line ~= "" then
    detail_color = C_LIVE
  elseif tostring(model.state or "") == "post" and status_line ~= "" then
    detail_color = C_TEXT
  elseif tostring(model.state or "") == "pre" and tostring(model.start_label or "") ~= "" then
    detail_color = C_ACCENT
  end
  local league_tag = compact_text(string.upper(LEAGUE), 8)
  local state_tag = ""
  if model.state == "in" then
    state_tag = "LIVE"
  elseif model.state == "post" then
    state_tag = "FINAL"
  elseif model.state == "pre" then
    state_tag = "UP NEXT"
  elseif stale ~= "" then
    state_tag = stale
  else
    state_tag = compact_text(model.status, 12)
  end
  fb:text_box(0, Y_HEADER, 24, 8, league_tag, C_ACCENT, FONT, 8, "left", false)
  fb:text_box(24, Y_HEADER, 40, 8, state_tag, model.state == "in" and C_LIVE or C_MUTED, FONT, 8, "right", false)

  local away_abbr = tostring(model.away_abbr or "---")
  local away_score = tostring(model.away_score or "-")
  local home_abbr = tostring(model.home_abbr or "---")
  local home_score = tostring(model.home_score or "-")
  fb:text_box(X_TEXT, Y_ROW_1, W_TEXT, 8, compact_text(away_abbr .. " " .. away_score, 12), line_color(model.away_score_num, model.home_score_num), FONT_TITLE, 8, "left", false)
  fb:text_box(X_TEXT, Y_ROW_2, W_TEXT, 8, compact_text(home_abbr .. " " .. home_score, 12), line_color(model.home_score_num, model.away_score_num), FONT_TITLE, 8, "left", false)
  fb:text_box(
    X_TEXT,
    Y_ROW_3,
    W_TEXT,
    8,
    compact_text(detail ~= "" and detail or model.detail2, 15),
    detail_color,
    FONT,
    8,
    "left",
    false
  )
end

return app
