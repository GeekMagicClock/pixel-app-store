local app = {}

local font = "builtin:silkscreen_regular_8"
local C_BG = 0x0000
local C_TEXT = 0xFFFF
local C_MUTED = 0x9CF3

local NEW_MOON_EPOCH = 947182440
local SYNODIC_SEC = 29.530588853 * 86400.0
local FRAMES = 32
local BASE = "S:/littlefs/apps/moon_phase_png/assets/moon_phase_"

local state = {
  ok_time = false,
  phase = 0.0,
  illum = 0.0,
  idx = 0,
}

local function phase_name(p)
  if p < 0.03 or p >= 0.97 then return "NEW MOON" end
  if p < 0.22 then return "WAXING CRESCENT" end
  if p < 0.28 then return "FIRST QUARTER" end
  if p < 0.47 then return "WAXING GIBBOUS" end
  if p < 0.53 then return "FULL" end
  if p < 0.72 then return "WANING GIBBOUS" end
  if p < 0.78 then return "LAST QUARTER" end
  return "WANING CRESCENT"
end

local function frame_path(i)
  return string.format("%s%d.png", BASE, i)
end

local function phase_lines(name)
  local sp = string.find(name, " ")
  if not sp then
    return name, nil
  end
  local a = string.sub(name, 1, sp - 1)
  local b = string.sub(name, sp + 1)
  return a, b
end

local GLYPH_W = {
  [" "] = 1, ["%"] = 4,
  ["0"] = 4, ["1"] = 3, ["2"] = 4, ["3"] = 4, ["4"] = 4,
  ["5"] = 4, ["6"] = 4, ["7"] = 4, ["8"] = 4, ["9"] = 4,
  ["A"] = 4, ["B"] = 4, ["C"] = 4, ["D"] = 4, ["E"] = 3,
  ["F"] = 3, ["G"] = 4, ["H"] = 4, ["I"] = 1, ["J"] = 4,
  ["K"] = 4, ["L"] = 3, ["M"] = 5, ["N"] = 5, ["O"] = 4,
  ["P"] = 4, ["Q"] = 4, ["R"] = 4, ["S"] = 4, ["T"] = 3,
  ["U"] = 4, ["V"] = 5, ["W"] = 5, ["X"] = 5, ["Y"] = 5, ["Z"] = 3,
}

local function glyph_w(ch)
  return GLYPH_W[ch] or 4
end

local function tight_text_width(s, gap)
  if not s or s == "" then return 0 end
  local n = #s
  local w = 0
  for i = 1, n do
    w = w + glyph_w(string.sub(s, i, i))
    if i < n then w = w + gap end
  end
  return w
end

local function draw_tight_center(fb, x, y, w, text, color)
  if not text or text == "" then return end
  local gap = 1
  local tw = tight_text_width(text, gap)
  local sx = x + math.floor((w - tw) / 2)
  local cx = sx
  for i = 1, #text do
    local ch = string.sub(text, i, i)
    fb:text(cx, y, ch, color, font, 8)
    if i < #text then
      cx = cx + glyph_w(ch) + gap
    end
  end
end

local function update_phase()
  local t = 0
  if sys and sys.unix_time then t = tonumber(sys.unix_time()) or 0 end
  state.ok_time = (t >= 1600000000)
  if not state.ok_time then
    state.phase = 0
    state.illum = 0
    state.idx = 0
    return
  end

  local age_sec = (t - NEW_MOON_EPOCH) % SYNODIC_SEC
  if age_sec < 0 then age_sec = age_sec + SYNODIC_SEC end
  local p = age_sec / SYNODIC_SEC
  state.phase = p
  state.illum = 0.5 * (1.0 - math.cos(2.0 * math.pi * p))

  local idx = math.floor(p * FRAMES + 0.5) % FRAMES
  state.idx = idx
end

function app.init(config)
  sys.log("moon_phase_png init")
  update_phase()
end

function app.tick(dt_ms)
  update_phase()
end

function app.render_fb(fb)
  fb:fill(C_BG)

  local draw_idx = state.idx or 0
  if draw_idx < 0 or draw_idx >= FRAMES then draw_idx = 0 end
  -- Always draw moon frame first; even when time is not ready we keep a visible image.
  fb:image_native(0, 0, frame_path(draw_idx))

  if not state.ok_time then
    fb:text_box(0, 8, 64, 8, "NO TIME", C_TEXT, font, 8, "center", true)
    fb:text_box(0, 18, 64, 8, "WAIT NTP", C_MUTED, font, 8, "center", true)
    return
  end

  local p1, p2 = phase_lines(phase_name(state.phase))
  local lines = { "MOON", p1 }
  if p2 and p2 ~= "" then
    lines[#lines + 1] = p2
  end
  lines[#lines + 1] = string.format("L %d%%", math.floor(state.illum * 100 + 0.5))

  local count = #lines
  local y0 = math.floor((32 - count * 8) / 2)
  for i = 1, count do
    local c = C_TEXT
    if i == 1 then c = C_MUTED end
    draw_tight_center(fb, 31, y0 + (i - 1) * 8, 32, lines[i], c)
  end
end

return app
