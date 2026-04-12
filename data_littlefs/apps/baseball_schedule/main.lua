local app = {}

local FONT = "builtin:silkscreen_regular_8"
local FONT_TITLE = "builtin:pressstart2p_regular_8"

-- Layout tuning knobs for 64x32 panel.
-- Adjust these constants to shift schedule UI without touching render logic.
local Y_LINE_TOP = 5
local Y_LINE_BOTTOM = 24
local Y_ERR_TITLE = 0
local Y_ERR_BODY = 12
local Y_ERR_FOOTER = 22
local Y_HEADER = -3
local Y_ROW_1 = 10
local Y_ROW_2 = 17
local Y_ROW_3 = 25
local Y_NEXT_ROW_2 = Y_ROW_1 + 12 -- 8px text height + 4px visual gap
local X_TEXT = 1
local W_TEXT = 62
local X_TAG_LIVE = 42
local W_TAG_LIVE = 21
local X_TAG_STALE = 40
local W_TAG_STALE = 23

local C_BG = 0x0000
local C_TEXT = 0xFFFF
local C_MUTED = 0x9CF3
local C_LINE = 0x18C3
local C_ACCENT = 0x07FF
local C_WARN = 0xFD20
local C_WIN = 0x87F0
local C_LOSS = 0xF800
local BOOT_SPLASH_MS = 1200

local SPORT = "baseball"
local LEAGUE = tostring(data.get("baseball_schedule.league") or "mlb")
local TEAM = string.upper(tostring(data.get("baseball_schedule.team") or data.get("sports.default_team") or ""))
local TEAM_ID = tostring(data.get("baseball_schedule.team_id") or data.get("sports.default_team_id") or "")
local COUNT = tonumber(data.get("baseball_schedule.count") or 3) or 3
local SCHEDULE_LIMIT = math.max(1, tonumber(data.get("baseball_schedule.schedule_limit") or 8) or 8)
local SCORE_DAYS = math.max(1, tonumber(data.get("baseball_schedule.score_days") or 7) or 7)
local INCLUDE_LAST_RESULT = tostring(data.get("baseball_schedule.include_last_result") or "1") ~= "0"
local ROTATE_MS = tonumber(data.get("baseball_schedule.rotate_ms") or 4000) or 4000
local TTL_MS = tonumber(data.get("baseball_schedule.ttl_ms") or 10 * 60 * 1000) or (10 * 60 * 1000)
local TEAMS_MAX_BODY = tonumber(data.get("baseball_schedule.teams_max_body") or 65536) or 65536
local SCOREBOARD_MAX_BODY = tonumber(data.get("baseball_schedule.scoreboard_max_body") or 262144) or 262144
local TEAM_SCHEDULE_MAX_BODY = tonumber(data.get("baseball_schedule.team_schedule_max_body") or 196608) or 196608
local TIMEOUT_MS = tonumber(data.get("baseball_schedule.timeout_ms") or 30000) or 30000
local ERR_RETRY_MS = tonumber(data.get("baseball_schedule.err_retry_ms") or 15000) or 15000
local APP_NAME = tostring(data.get("baseball_schedule.app_name") or "Baseball Schedule")
-- ESPN team schedule endpoint can return very large season payloads on multiple sports.
-- Keep it disabled to avoid repeated >500KB gzip decode failures on device.
local USE_TEAM_SCHEDULE_ENDPOINT = false

local state = {
  req_id = nil,
  req_kind = nil,
  last_req_ms = 0,
  last_ok_ms = 0,
  err = nil,
  anim_ms = 0,
  resolved_team_id = TEAM_ID,
  payload = nil,
  boot_started_ms = 0,
  disable_team_schedule = false,
  empty_label = "NO EVENTS",
}

local MONTHS = {
  "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
  "JUL", "AUG", "SEP", "OCT", "NOV", "DEC",
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

local function compact_text(s, limit)
  s = tostring(s or "")
  s = string.gsub(s, "%s+", " ")
  s = string.gsub(s, "^%s+", "")
  s = string.gsub(s, "%s+$", "")
  local n = tonumber(limit) or 12
  if #s > n then return string.sub(s, 1, n - 1) .. "…" end
  return s
end

local function iso_date(iso)
  local y, m, d = string.match(tostring(iso or ""), "^(%d%d%d%d)%-(%d%d)%-(%d%d)")
  if not y then return nil, nil, nil end
  return y, m, d
end

local function date_label(iso)
  local y, m, d = iso_date(iso)
  if not y then return "" end
  m = tonumber(m) or 1
  d = tonumber(d) or 1
  return (MONTHS[m] or "DAY") .. " " .. tostring(d)
end

local function hm_label(iso)
  return string.match(tostring(iso or ""), "T(%d%d:%d%d)") or "--:--"
end

local function days_in_month(y, m)
  if m == 4 or m == 6 or m == 9 or m == 11 then return 30 end
  if m == 2 then
    local leap = ((y % 4 == 0 and y % 100 ~= 0) or (y % 400 == 0))
    return leap and 29 or 28
  end
  return 31
end

local function epoch_from_parts(y, m, d, hh, mm, ss)
  y = tonumber(y) or 1970
  m = tonumber(m) or 1
  d = tonumber(d) or 1
  hh = tonumber(hh) or 0
  mm = tonumber(mm) or 0
  ss = tonumber(ss) or 0
  if y < 1970 then return nil end
  local days = 0
  for yy = 1970, y - 1 do
    local leap = ((yy % 4 == 0 and yy % 100 ~= 0) or (yy % 400 == 0))
    days = days + (leap and 366 or 365)
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
  return epoch_from_parts(y, m, d, hh, mm, ss)
end

local function local_utc_offset_sec()
  local lt = sys and sys.local_time and sys.local_time() or nil
  local ut = sys and sys.unix_time and sys.unix_time() or nil
  if type(lt) ~= "table" then return nil end
  ut = tonumber(ut)
  if not ut then return nil end
  local local_epoch = epoch_from_parts(lt.year, lt.month, lt.day, lt.hour, lt.min, lt.sec)
  if not local_epoch then return nil end
  return local_epoch - math.floor(ut)
end

local function kickoff_label_local(iso)
  local utc_epoch = parse_iso_utc_epoch(iso)
  local offset = local_utc_offset_sec()
  if not utc_epoch or not offset then
    return compact_text(date_label(iso) .. " " .. hm_label(iso), 16)
  end
  local local_epoch = utc_epoch + offset
  local minute_of_day = math.floor(local_epoch / 60) % 1440
  if minute_of_day < 0 then minute_of_day = minute_of_day + 1440 end
  local h = math.floor(minute_of_day / 60)
  local m = minute_of_day % 60
  local days_since_epoch = math.floor(local_epoch / 86400)
  local y = 1970
  local dleft = days_since_epoch
  while true do
    local leap = ((y % 4 == 0 and y % 100 ~= 0) or (y % 400 == 0))
    local yd = leap and 366 or 365
    if dleft >= yd then
      dleft = dleft - yd
      y = y + 1
    else
      break
    end
  end
  local mo = 1
  while true do
    local md = days_in_month(y, mo)
    if dleft >= md then
      dleft = dleft - md
      mo = mo + 1
    else
      break
    end
  end
  local day = dleft + 1
  return compact_text(string.format("%s %d %02d:%02d", MONTHS[mo] or "DAY", day, h, m), 16)
end

local function extract_hm(text)
  local s = tostring(text or "")
  local hh, mm = string.match(s, "(%d?%d):(%d%d)")
  if not hh or not mm then return "" end
  hh = tonumber(hh)
  if hh == nil then return "" end
  return string.format("%02d:%s", hh, mm)
end

local function kickoff_line(item)
  local y, mo, d = iso_date(item and item.date_raw or "")
  local date_part = ""
  if mo and d then
    date_part = tostring(tonumber(mo) or mo) .. "/" .. tostring(tonumber(d) or d)
  else
    date_part = date_label(item and item.date_raw or "")
  end
  local local_line = kickoff_label_local(item and item.date_raw or "")
  if string.find(local_line, "%d%d:%d%d") then
    local hm = extract_hm(local_line)
    if hm ~= "" and date_part ~= "" then return compact_text(date_part .. " " .. hm, 16) end
    return compact_text(local_line, 16)
  end
  local hm = extract_hm((item and item.time_label) or "")
  if hm == "" then hm = extract_hm((item and item.detail) or "") end
  if hm ~= "" then
    if date_part ~= "" then return compact_text(date_part .. " " .. hm, 16) end
    return compact_text(hm, 16)
  end
  return compact_text(local_line ~= "" and local_line or date_part, 16)
end

local function now_unix_sec()
  local u = sys and sys.unix_time and sys.unix_time() or nil
  u = tonumber(u)
  if u then return math.floor(u) end
  local lt = sys and sys.local_time and sys.local_time() or nil
  if type(lt) ~= "table" then return nil end
  local approx = epoch_from_parts(lt.year, lt.month, lt.day, lt.hour, lt.min, lt.sec)
  if not approx then return nil end
  return math.floor(approx)
end

local function calendar_phase_label(obj)
  local leagues = obj and obj.leagues or nil
  if type(leagues) ~= "table" or type(leagues[1]) ~= "table" then return nil end
  local cal = leagues[1].calendar
  if type(cal) ~= "table" then return nil end
  local now_sec = now_unix_sec()
  if not now_sec then return nil end

  local function in_range(start_iso, end_iso)
    local s = parse_iso_utc_epoch(start_iso)
    local e = parse_iso_utc_epoch(end_iso)
    if not s or not e then return false end
    return now_sec >= s and now_sec <= e
  end

  for i = 1, #cal do
    local item = cal[i]
    if type(item) == "table" then
      -- Prefer top-level phase label (e.g. "Off Season") over nested week labels.
      if in_range(item.startDate, item.endDate) then
        return tostring(item.label or "")
      end
      local entries = item.entries
      if type(entries) == "table" then
        for j = 1, #entries do
          local e = entries[j]
          if type(e) == "table" and in_range(e.startDate, e.endDate) then
            return tostring(e.label or item.label or "")
          end
        end
      end
    end
  end
  return nil
end

local function teams_url()
  return string.format("https://site.api.espn.com/apis/site/v2/sports/%s/%s/teams", SPORT, LEAGUE)
end

local function ymd_add_days(y, m, d, delta)
  local yy, mm, dd = tonumber(y) or 0, tonumber(m) or 0, tonumber(d) or 0
  local n = tonumber(delta) or 0
  if yy <= 0 or mm <= 0 or dd <= 0 then return 0, 0, 0 end
  while n > 0 do
    dd = dd + 1
    local dim = days_in_month(yy, mm)
    if dd > dim then
      dd = 1
      mm = mm + 1
      if mm > 12 then mm = 1; yy = yy + 1 end
    end
    n = n - 1
  end
  return yy, mm, dd
end

local function scoreboard_url()
  local base = string.format("https://site.api.espn.com/apis/site/v2/sports/%s/%s/scoreboard", SPORT, LEAGUE)
  local t = sys and sys.local_time and sys.local_time() or {}
  local y = tonumber(t.year) or 0
  local m = tonumber(t.month) or 0
  local d = tonumber(t.day) or 0
  if y <= 0 or m <= 0 or d <= 0 then return base end
  -- Keep fallback scoreboard request narrow to avoid huge league payloads (e.g. MLB).
  return string.format("%s?dates=%04d%02d%02d", base, y, m, d)
end

local function team_schedule_url(team_id)
  local base = string.format("https://site.api.espn.com/apis/site/v2/sports/%s/%s/teams/%s/schedule", SPORT, LEAGUE, tostring(team_id or ""))
  local t = sys and sys.local_time and sys.local_time() or {}
  local y = tonumber(t.year) or 0
  local m = tonumber(t.month) or 0
  local d = tonumber(t.day) or 0
  if y <= 0 or m <= 0 or d <= 0 then return base end
  local y2, m2, d2 = ymd_add_days(y, m, d, SCORE_DAYS)
  return string.format("%s?dates=%04d%02d%02d-%04d%02d%02d", base, y, m, d, y2, m2, d2)
end

local function abbr_from_team(team)
  local s = string.upper(tostring(team and (team.abbreviation or team.shortDisplayName or team.name) or "---"))
  if #s > 4 then s = string.sub(s, 1, 4) end
  return s
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

local function parse_short_matchup(short_name)
  local s = tostring(short_name or "")
  if s == "" then return nil, nil, nil end
  local a, b = string.match(s, "^%s*(.-)%s+@%s+(.-)%s*$")
  if not a or not b then
    a, b = string.match(s, "^%s*(.-)%s+[aA][tT]%s+(.-)%s*$")
  end
  if not a or not b then return nil, nil, nil end
  return compact_text(a, 16), compact_text(b, 16), true -- away @ home
end

local function team_match_key(team)
  if TEAM == "" or type(team) ~= "table" then return false end
  local options = {
    team.abbreviation,
    team.shortDisplayName,
    team.displayName,
    team.name,
  }
  for i = 1, #options do
    if string.upper(tostring(options[i] or "")) == TEAM then return true end
  end
  return false
end

local function team_matches_runtime(team)
  if type(team) ~= "table" then return false end
  if state.resolved_team_id ~= "" and tostring(team.id or "") == tostring(state.resolved_team_id) then return true end
  if TEAM_ID ~= "" and tostring(team.id or "") == TEAM_ID then return true end
  return team_match_key(team)
end

local function collect_team_entries(node, out)
  if type(node) ~= "table" then return end
  if type(node.team) == "table" and node.team.id ~= nil then
    out[#out + 1] = {
      team = node.team,
      nextEvent = node.nextEvent or node.team.nextEvent,
      events = node.events or node.team.events,
    }
  end
  for _, v in pairs(node) do
    if type(v) == "table" then collect_team_entries(v, out) end
  end
end

local function normalize_schedule_event(event)
  local competition = event and ((event.competitions and event.competitions[1]) or event.competition or event) or nil
  local home, away = find_home_away(competition)
  if not home or not away then
    local away_name, home_name = parse_short_matchup((event and event.shortName) or (competition and competition.shortName))
    if away_name and home_name then
      local mine_label = TEAM ~= "" and TEAM or (state.payload and state.payload.title) or ""
      mine_label = string.upper(tostring(mine_label))
      local away_u = string.upper(away_name)
      local home_u = string.upper(home_name)
      local is_home = false
      if mine_label ~= "" then
        if away_u == mine_label then is_home = false
        elseif home_u == mine_label then is_home = true
        else
          local mine_short = string.sub(mine_label, 1, 3)
          is_home = string.find(home_u, mine_short, 1, true) ~= nil
        end
      end
      local opp = is_home and away_name or home_name
      local st_obj = (competition and competition.status and competition.status.type) or {}
      local st = tostring(st_obj.state or "")
      local detail = tostring(st_obj.shortDetail or st_obj.detail or "")
      return {
        id = tostring((event and event.id) or ""),
        date_raw = tostring((event and event.date) or (competition and competition.date) or ""),
        state = st,
        opp = compact_text(opp, 4),
        mine = TEAM ~= "" and TEAM or "TEAM",
        home = is_home,
        date_label = date_label((event and event.date) or (competition and competition.date)),
        time_label = compact_text(detail ~= "" and detail or hm_label((event and event.date) or (competition and competition.date)), 12),
        detail = compact_text(detail, 12),
        outcome = "",
        score_label = "--",
      }
    end
    return nil
  end

  local home_is_mine = team_matches_runtime(home.team or {})
  local away_is_mine = team_matches_runtime(away.team or {})
  if not home_is_mine and not away_is_mine then
    if TEAM == "" and TEAM_ID == "" and tostring(state.resolved_team_id or "") == "" then
      home_is_mine = true
    else
      return nil
    end
  end
  local is_home = home_is_mine
  local mine = is_home and home or away
  local opp = is_home and away or home
  local function score_num(v)
    if type(v) == "table" then
      if tonumber(v.value) ~= nil then return tonumber(v.value) end
      if tonumber(v.displayValue) ~= nil then return tonumber(v.displayValue) end
      return nil
    end
    return tonumber(v)
  end
  local function score_text(v)
    if type(v) == "table" then
      if tostring(v.displayValue or "") ~= "" then return tostring(v.displayValue) end
      if tonumber(v.value) ~= nil then return tostring(math.floor(tonumber(v.value) + 0.5)) end
      return "-"
    end
    if tonumber(v) ~= nil then return tostring(math.floor(tonumber(v) + 0.5)) end
    return "-"
  end
  local st_obj = (competition and competition.status and competition.status.type) or (event and event.status and event.status.type) or {}
  local my_score = score_num(mine and mine.score)
  local opp_score = score_num(opp and opp.score)
  local st = tostring(st_obj.state or "")
  local detail = tostring(st_obj.shortDetail or st_obj.detail or "")

  local outcome = ""
  if my_score and opp_score then
    if my_score > opp_score then outcome = "W"
    elseif my_score < opp_score then outcome = "L"
    else outcome = "D"
    end
  end

  return {
    id = tostring(event.id or ""),
    date_raw = tostring((event and event.date) or (competition and competition.date) or ""),
    state = st,
    opp = abbr_from_team(opp and opp.team),
    mine = abbr_from_team(mine and mine.team),
    home = is_home,
    date_label = date_label((event and event.date) or (competition and competition.date)),
    time_label = compact_text(detail ~= "" and detail or hm_label((event and event.date) or (competition and competition.date)), 12),
    detail = compact_text(detail, 12),
    outcome = outcome,
    score_label = (my_score ~= nil and opp_score ~= nil) and (score_text(mine and mine.score) .. "-" .. score_text(opp and opp.score)) or "--",
  }
end

local function build_schedule_model(obj)
  local events = obj and obj.events or {}
  local future = {}
  local past = {}
  local live = nil

  for i = 1, #events do
    local item = normalize_schedule_event(events[i])
    if item then
      if item.state == "in" then
        live = item
      elseif item.state == "post" then
        past[#past + 1] = item
      else
        future[#future + 1] = item
      end
    end
  end

  table.sort(future, function(a, b)
    return tostring(a.date_raw or "") < tostring(b.date_raw or "")
  end)
  table.sort(past, function(a, b)
    return tostring(a.date_raw or "") > tostring(b.date_raw or "")
  end)

  local next_games = {}
  for i = 1, math.min(COUNT, #future) do
    next_games[#next_games + 1] = future[i]
  end

  local title = TEAM
  if title == "" and type(obj.team) == "table" then
    title = abbr_from_team(obj.team)
  end
  if title == "" and live and live.mine then
    title = tostring(live.mine)
  end
  if title == "" and next_games[1] and next_games[1].mine then
    title = tostring(next_games[1].mine)
  end
  if title == "" then title = "TEAM" end

  return {
    title = title,
    live = live,
    next_games = next_games,
    last_result = INCLUDE_LAST_RESULT and past[1] or nil,
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
  if payload.title or payload.live or payload.last_result or payload.next_games then return payload end
  if payload.events then return build_schedule_model(payload) end
  return nil
end

local function handle_teams_response(status, body)
  if status ~= 200 then
    state.err = "HTTP " .. tostring(status)
    return
  end
  local obj, jerr = json.decode(body)
  if not obj then
    state.err = jerr or "JSON ERR"
    return
  end
  local entries = {}
  collect_team_entries(obj, entries)
  local fallback_entry = nil
  if TEAM == "" and TEAM_ID == "" and tostring(state.resolved_team_id or "") == "" and #entries > 0 then
    fallback_entry = entries[1]
  end

  local function apply_entry(entry)
    local t = entry and entry.team or {}
    state.resolved_team_id = tostring(t.id or "")
    local merged = { team = t, events = {} }
    local add = function(list)
      if type(list) ~= "table" then return end
      for j = 1, #list do merged.events[#merged.events + 1] = list[j] end
    end
    add(entry.nextEvent)
    add(entry.events)
    add(t.nextEvent)
    add(t.events)
    state.payload = build_schedule_model(merged)
    local p = state.payload
    local has_frames = (type(p) == "table") and (p.live or (type(p.next_games) == "table" and #p.next_games > 0) or p.last_result)
    if has_frames then
      state.err = nil
      state.last_ok_ms = now_ms()
    else
      state.err = "NO EVENTS"
    end
  end

  for i = 1, #entries do
    local entry = entries[i]
    local t = entry.team or {}
    if team_matches_runtime(t) then
      apply_entry(entry)
      return
    end
  end
  if fallback_entry then
    apply_entry(fallback_entry)
    return
  end
  state.err = "TEAM ?"
end

local function handle_scoreboard_response(status, body)
  if status ~= 200 then
    state.err = "HTTP " .. tostring(status)
    return
  end
  local obj, jerr = json.decode(body)
  if not obj then
    state.err = jerr or "JSON ERR"
    return
  end
  local phase = string.upper(tostring(calendar_phase_label(obj) or ""))
  if string.find(phase, "OFF", 1, true) and string.find(phase, "SEASON", 1, true) then
    state.empty_label = "OFF SEASON"
  else
    state.empty_label = "NO EVENTS"
  end
  state.payload = build_schedule_model({ team = { abbreviation = TEAM }, events = obj.events or {} })
  state.err = nil
  state.last_ok_ms = now_ms()
end

local function handle_team_schedule_response(status, body)
  if status ~= 200 then
    state.err = "HTTP " .. tostring(status)
    return
  end
  local obj, jerr = json.decode(body)
  if not obj then
    state.err = jerr or "JSON ERR"
    return
  end
  state.payload = build_schedule_model(obj)
  local p = state.payload
  local has_frames = (type(p) == "table") and (p.live or (type(p.next_games) == "table" and #p.next_games > 0) or p.last_result)
  if not has_frames then
    state.err = "NO EVENTS"
    return
  end
  state.err = nil
  state.last_ok_ms = now_ms()
end

local function payload_has_frames(payload)
  if type(payload) ~= "table" then return false end
  if payload.live then return true end
  if type(payload.next_games) == "table" and #payload.next_games > 0 then return true end
  if payload.last_result then return true end
  return false
end

local function start_request(kind)
  if state.req_id then return end
  local url = teams_url()
  local max_body = TEAMS_MAX_BODY
  if kind == "scoreboard" then
    url = scoreboard_url()
    max_body = SCOREBOARD_MAX_BODY
  elseif kind == "team_schedule" then
    if state.disable_team_schedule then
      start_request("scoreboard")
      return
    end
    if state.resolved_team_id == "" then
      state.err = "TEAM ?"
      return
    end
    url = team_schedule_url(state.resolved_team_id)
    max_body = TEAM_SCHEDULE_MAX_BODY
  end
  local id, body, age_ms, err = net.cached_get(url, TTL_MS, TIMEOUT_MS, max_body)
  if err then
    state.err = tostring(err)
    return
  end
  if body then
    if kind == "scoreboard" then
      handle_scoreboard_response(200, body)
    elseif kind == "team_schedule" then
      handle_team_schedule_response(200, body)
    else
      handle_teams_response(200, body)
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
end

local function ensure_request()
  start_request("teams")
end

function app.init()
  state.req_id = nil
  state.req_kind = nil
  state.last_req_ms = 0
  state.last_ok_ms = 0
  state.err = nil
  state.anim_ms = 0
  state.boot_started_ms = now_ms()
  state.payload = build_mock_payload()
  state.resolved_team_id = TEAM_ID
  state.disable_team_schedule = false
  state.empty_label = "NO EVENTS"
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
        local err_text = string.upper(tostring(body or "HTTP ERR"))
        if kind == "team_schedule" and (string.find(err_text, "GZIP DECODE FAILED", 1, true) or string.find(err_text, "BODY TOO LARGE", 1, true)) then
          state.disable_team_schedule = true
          state.err = "FALLBACK SCOREBOARD"
          start_request("scoreboard")
        else
          state.err = body or "HTTP ERR"
        end
      else
        if kind == "scoreboard" then
          handle_scoreboard_response(status, body or "")
        elseif kind == "team_schedule" then
          handle_team_schedule_response(status, body or "")
        else
          handle_teams_response(status, body or "")
          if not payload_has_frames(state.payload) then
            if USE_TEAM_SCHEDULE_ENDPOINT and (not state.disable_team_schedule) and state.resolved_team_id ~= "" then
              start_request("team_schedule")
            else
              start_request("scoreboard")
            end
          end
        end
      end
    end
    return
  end
  local interval = state.err and ERR_RETRY_MS or TTL_MS
  if now_ms() - state.last_req_ms >= interval then ensure_request() end
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
    fb:text_box(0, Y_ERR_TITLE, 64, 8, "SCHEDULE", C_TEXT, FONT, 8, "center", false)
    fb:text_box(0, Y_ERR_BODY, 64, 8, compact_text(state.err, 16), C_WARN, FONT, 8, "center", false)
    fb:text_box(0, Y_ERR_FOOTER, 64, 8, TEAM ~= "" and TEAM or string.upper(LEAGUE), C_MUTED, FONT, 8, "center", false)
    return
  end

  local payload = state.payload or { title = TEAM ~= "" and TEAM or "TEAM", next_games = {}, last_result = nil, live = nil }
  local frames = {}
  if payload.live then frames[#frames + 1] = { kind = "live", item = payload.live } end
  for i = 1, #payload.next_games do
    frames[#frames + 1] = { kind = "next", item = payload.next_games[i] }
  end
  if payload.last_result then frames[#frames + 1] = { kind = "last", item = payload.last_result } end
  if #frames == 0 then
    fb:text_box(0, Y_HEADER, 64, 8, "SCHEDULE", C_ACCENT, FONT, 8, "center", false)
    fb:text_box(0, Y_ERR_BODY, 64, 8, state.empty_label or "NO EVENTS", C_MUTED, FONT, 8, "center", false)
    return
  end

  local idx = math.floor(state.anim_ms / ROTATE_MS) % #frames + 1
  local frame = frames[idx]
  local item = frame.item
  local title = payload.title
  local stale = stale_tag()
  local function detail_compact(txt)
    txt = compact_text(txt or "", 16)
    if txt == "" then return "" end
    if frame.kind == "last" and string.upper(txt) == "FINAL" then return "" end
    return txt
  end

  if frame.kind == "live" then
    fb:text_box(X_TEXT, Y_HEADER, W_TEXT, 8, compact_text(title, 10), C_ACCENT, FONT, 8, "left", false)
    fb:text_box(X_TAG_LIVE, Y_HEADER, W_TAG_LIVE, 8, "LIVE", C_LOSS, FONT, 8, "right", false)
    fb:text_box(X_TEXT, Y_ROW_1, W_TEXT, 8, compact_text((item.home and "VS " or "@ ") .. item.opp, 16), C_TEXT, FONT_TITLE, 8, "left", false)
    fb:text_box(X_TEXT, Y_ROW_2, W_TEXT, 8, compact_text("SCORE " .. tostring(item.score_label or "--"), 16), C_TEXT, FONT, 8, "left", false)
    fb:text_box(X_TEXT, Y_ROW_3, W_TEXT, 8, detail_compact(item.detail), C_MUTED, FONT, 8, "left", false)
    return
  end

  if frame.kind == "last" then
    local accent = item.outcome == "W" and C_WIN or (item.outcome == "L" and C_LOSS or C_WARN)
    fb:text_box(X_TEXT, Y_HEADER, W_TEXT, 8, compact_text(title, 10), C_ACCENT, FONT, 8, "left", false)
    fb:text_box(X_TAG_LIVE, Y_HEADER, W_TAG_LIVE, 8, "FINAL", accent, FONT, 8, "right", false)
    if stale ~= "" then
      fb:text_box(X_TAG_STALE, Y_HEADER, W_TAG_STALE, 8, stale, C_WARN, FONT, 8, "right", false)
    end
    fb:text_box(X_TEXT, Y_ROW_1, W_TEXT, 8, compact_text((item.home and "VS " or "@ ") .. item.opp, 16), C_TEXT, FONT_TITLE, 8, "left", false)
    fb:text_box(X_TEXT, Y_ROW_2, W_TEXT, 8, compact_text("SCORE " .. tostring(item.score_label or "--"), 16), C_TEXT, FONT, 8, "left", false)
    local last_note = (item.outcome ~= "" and (item.outcome .. " ") or "") .. detail_compact(item.detail)
    fb:text_box(X_TEXT, Y_ROW_3, W_TEXT, 8, compact_text(last_note, 16), accent, FONT, 8, "left", false)
    return
  end

  fb:text_box(X_TEXT, Y_HEADER, W_TEXT, 8, compact_text(title, 10), C_ACCENT, FONT, 8, "left", false)
  fb:text_box(X_TAG_LIVE, Y_HEADER, W_TAG_LIVE, 8, "NEXT", C_ACCENT, FONT, 8, "right", false)
  if stale ~= "" then
    fb:text_box(X_TAG_STALE, Y_HEADER, W_TAG_STALE, 8, stale, C_WARN, FONT, 8, "right", false)
  end
  fb:text_box(X_TEXT, Y_ROW_1, W_TEXT, 8, compact_text((item.home and "VS " or "@ ") .. item.opp, 16), C_TEXT, FONT_TITLE, 8, "left", false)
  fb:text_box(X_TEXT, Y_NEXT_ROW_2, W_TEXT, 8, kickoff_line(item), C_TEXT, FONT, 8, "left", false)
  -- NEXT view keeps only opponent + kickoff line to avoid redundant or overlapping text.
end

return app
