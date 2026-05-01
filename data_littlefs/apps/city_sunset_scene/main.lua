local app = {}

local state = { anim_ms = 0 }

local C_SKY_TOP_DAY = 0xFB80
local C_SKY_BOT_DAY = 0xF2A0
local C_SKY_TOP_DUSK = 0x824F
local C_SKY_BOT_DUSK = 0xD20B
local C_SKY_TOP_NIGHT = 0x086F
local C_SKY_BOT_NIGHT = 0x0005
local C_HAZE_DAY = 0xFBC9
local C_HAZE_NIGHT = 0x30A8
local C_GLOW_DAY = 0xFD60
local C_GLOW_NIGHT = 0xA1A8
local C_CLOUD_DAY = 0xF4C0
local C_CLOUD_DUSK = 0xB22A
local C_CLOUD_NIGHT = 0x18A6
local C_SUN = 0xFFE0
local C_SUN_CORE = 0xFFF7
local C_SUN_SHADE = 0xFC80
local C_FAR_CITY_DAY = 0x5A8E
local C_FAR_CITY_NIGHT = 0x0865
local C_NEAR_CITY_DAY = 0x39A8
local C_NEAR_CITY_NIGHT = 0x0001
local C_STREET = 0x2127
local C_STREET_NIGHT = 0x086B
local C_WIN_WARM = 0xFE68
local C_WIN_COOL = 0xC73F
local C_NEON_A = 0xF81F
local C_NEON_B = 0x07FF
local C_NEON_C = 0xFD20
local C_STAR = 0xFFFF

local STAR_POINTS = {
  { 4, 3, 0.08, 3600 },
  { 11, 6, 0.16, 4100 },
  { 18, 2, 0.22, 3300 },
  { 26, 5, 0.14, 4700 },
  { 40, 3, 0.10, 3900 },
  { 47, 7, 0.18, 5200 },
  { 55, 4, 0.12, 4300 },
  { 60, 8, 0.26, 3500 },
  { 36, 9, 0.24, 4800 },
  { 8, 10, 0.32, 5400 },
  { 20, 8, 0.34, 4900 },
  { 51, 10, 0.30, 4600 },
}

local CLOUD_STRIPS = {
  { x = 6,  y = 6,  w = 16, h = 3 },
  { x = 39, y = 8,  w = 18, h = 4 },
  { x = 18, y = 12, w = 20, h = 3 },
}

local FAR_BUILDINGS = {
  { x = 0,  y = 18, w = 8,  h = 6 },
  { x = 7,  y = 16, w = 7,  h = 8 },
  { x = 15, y = 13, w = 6,  h = 11 },
  { x = 22, y = 17, w = 5,  h = 7 },
  { x = 28, y = 15, w = 7,  h = 9 },
  { x = 36, y = 12, w = 5,  h = 12 },
  { x = 42, y = 17, w = 6,  h = 7 },
  { x = 49, y = 14, w = 6,  h = 10 },
  { x = 56, y = 16, w = 8,  h = 8 },
}

local NEAR_BUILDINGS = {
  { x = 0,  y = 21, w = 9,  h = 11, spire = 0 },
  { x = 10, y = 18, w = 10, h = 14, spire = 2 },
  { x = 22, y = 22, w = 7,  h = 10, spire = 0 },
  { x = 30, y = 15, w = 11, h = 17, spire = 3 },
  { x = 43, y = 19, w = 9,  h = 13, spire = 0 },
  { x = 53, y = 13, w = 11, h = 19, spire = 4 },
}

local function set_px_safe(fb, x, y, c)
  if x < 0 or x >= 64 or y < 0 or y >= 32 then return end
  fb:set_px(x, y, c)
end

local function rect_safe(fb, x, y, w, h, c)
  if w <= 0 or h <= 0 then return end
  fb:rect(x, y, w, h, c)
end

local function line_safe(fb, x0, y0, x1, y1, c)
  local dx = math.abs(x1 - x0)
  local sx = x0 < x1 and 1 or -1
  local dy = -math.abs(y1 - y0)
  local sy = y0 < y1 and 1 or -1
  local err = dx + dy
  while true do
    set_px_safe(fb, x0, y0, c)
    if x0 == x1 and y0 == y1 then break end
    local e2 = err * 2
    if e2 >= dy then
      err = err + dy
      x0 = x0 + sx
    end
    if e2 <= dx then
      err = err + dx
      y0 = y0 + sy
    end
  end
end

local function fill_circle(fb, cx, cy, r, c)
  for dy = -r, r do
    for dx = -r, r do
      if dx * dx + dy * dy <= r * r then
        set_px_safe(fb, cx + dx, cy + dy, c)
      end
    end
  end
end

local function clamp(v, lo, hi)
  if v < lo then return lo end
  if v > hi then return hi end
  return v
end

local function smoothstep(t)
  local v = clamp(t, 0, 1)
  return v * v * (3 - 2 * v)
end

local function triangle_wave(period_ms, phase_ms)
  local period = period_ms or 2000
  local t = (state.anim_ms + (phase_ms or 0)) % period
  local half = period / 2
  if t < half then
    return t / half
  end
  return 1 - ((t - half) / half)
end

local function rgb565_to_rgb(c)
  return (c >> 11) & 0x1F, (c >> 5) & 0x3F, c & 0x1F
end

local function rgb_to_565(r, g, b)
  local rr = clamp(math.floor(r + 0.5), 0, 31)
  local gg = clamp(math.floor(g + 0.5), 0, 63)
  local bb = clamp(math.floor(b + 0.5), 0, 31)
  return (rr << 11) | (gg << 5) | bb
end

local function blend565(a, b, t)
  local v = clamp(t, 0, 1)
  if v <= 0 then return a end
  if v >= 1 then return b end
  local ar, ag, ab = rgb565_to_rgb(a)
  local br, bg, bb = rgb565_to_rgb(b)
  return rgb_to_565(
    ar + (br - ar) * v,
    ag + (bg - ag) * v,
    ab + (bb - ab) * v
  )
end

local function hash01(n)
  local v = math.sin(n * 12.9898) * 43758.5453
  return v - math.floor(v)
end

local function sky_row_color(y, dusk_p, night_p)
  local top = blend565(C_SKY_TOP_DAY, C_SKY_TOP_DUSK, dusk_p)
  top = blend565(top, C_SKY_TOP_NIGHT, night_p)
  local bot = blend565(C_SKY_BOT_DAY, C_SKY_BOT_DUSK, dusk_p)
  bot = blend565(bot, C_SKY_BOT_NIGHT, night_p)
  local row_t = smoothstep(y / 31)
  local c = blend565(top, bot, row_t)
  local haze_band = clamp(1 - math.abs(y - 19) / 4.5, 0, 1)
  if haze_band > 0 then
    local haze = blend565(C_HAZE_DAY, C_HAZE_NIGHT, night_p)
    c = blend565(c, haze, haze_band * (0.65 - 0.4 * night_p))
  end
  return c
end

local function draw_sun(fb, sky_rows, dusk_p, night_p)
  local sun_drop = smoothstep(clamp(state.anim_ms / 22000, 0, 1))
  local cx = 31
  local cy = 7 + math.floor(15 * sun_drop + 0.5)
  local sun_alpha = clamp(1 - smoothstep((state.anim_ms - 18000) / 8000), 0, 1)
  if sun_alpha <= 0 then return end

  for dy = -8, 8 do
    local y = cy + dy
    if y < 0 or y >= 32 then
      goto continue_halo
    end
    for dx = -11, 11 do
      local dist = dx * dx + dy * dy
      if dist <= 92 then
        local glow = clamp((92 - dist) / 92, 0, 1) * 0.75 * sun_alpha
        if glow > 0 then
          local base = sky_rows[y]
          local tint = blend565(C_GLOW_DAY, C_GLOW_NIGHT, dusk_p * 0.8)
          set_px_safe(fb, cx + dx, y, blend565(base, tint, glow))
        end
      end
    end
    ::continue_halo::
  end

  fill_circle(fb, cx, cy, 6, blend565(C_SUN, C_GLOW_NIGHT, night_p * 0.35))
  fill_circle(fb, cx, cy, 4, blend565(C_SUN_CORE, C_SUN, dusk_p * 0.35))
  rect_safe(fb, cx - 5, cy - 1, 11, 1, C_SUN_SHADE)
  rect_safe(fb, cx - 4, cy + 2, 9, 1, blend565(C_SUN_SHADE, C_HAZE_NIGHT, night_p * 0.35))
  rect_safe(fb, cx - 3, cy - 4, 7, 1, blend565(C_SUN_CORE, C_SUN, 0.4))
end

local function draw_stars(fb, star_p)
  if star_p <= 0.02 then return end
  for i = 1, #STAR_POINTS do
    local s = STAR_POINTS[i]
    local tw = triangle_wave(s[4], i * 370)
    local vis = clamp((star_p - s[3]) / 0.24, 0, 1) * (0.45 + tw * 0.55)
    if vis > 0.18 then
      local c = blend565(0x8C71, C_STAR, vis)
      set_px_safe(fb, s[1], s[2], c)
      if vis > 0.46 then
        set_px_safe(fb, s[1] - 1, s[2], blend565(c, 0x0000, 0.45))
        set_px_safe(fb, s[1] + 1, s[2], blend565(c, 0x0000, 0.45))
        set_px_safe(fb, s[1], s[2] - 1, blend565(c, 0x0000, 0.52))
        set_px_safe(fb, s[1], s[2] + 1, blend565(c, 0x0000, 0.52))
      end
    end
  end
end

local function draw_far_city(fb, dusk_p, night_p)
  local c = blend565(C_FAR_CITY_DAY, C_FAR_CITY_NIGHT, night_p)
  local rim = blend565(0x9CD3, blend565(c, 0x0000, 0.08), night_p)
  local warm = blend565(C_HAZE_DAY, C_GLOW_DAY, 0.35 + dusk_p * 0.25)
  for i = 1, #FAR_BUILDINGS do
    local b = FAR_BUILDINGS[i]
    rect_safe(fb, b.x, b.y, b.w, b.h, c)
    rect_safe(fb, b.x, b.y, b.w, 1, rim)
    if b.h >= 8 then
      rect_safe(fb, b.x + 1, b.y + 1, b.w - 2, 1, blend565(c, warm, 0.18 * (1 - night_p)))
    end
    if i % 2 == 0 then
      rect_safe(fb, b.x + b.w - 1, b.y + 1, 1, b.h - 1, blend565(c, 0x18C3, 0.18 * (1 - night_p) + night_p * 0.04))
    end
    if i % 3 == 1 then
      rect_safe(fb, b.x, b.y + 1, 1, b.h - 1, blend565(c, warm, 0.12 * (1 - night_p)))
    end
    if b.w >= 6 then
      rect_safe(fb, b.x + 1, b.y + b.h - 1, b.w - 2, 1, blend565(c, 0x0000, 0.14 + night_p * 0.08))
    end
    if i == 3 or i == 6 or i == 8 then
      rect_safe(fb, b.x + (b.w // 2), b.y - 2, 1, 2, blend565(c, 0x6B5D, night_p * 0.7))
      if night_p > 0.42 then
        set_px_safe(fb, b.x + (b.w // 2), b.y - 2, blend565(C_NEON_B, C_STAR, triangle_wave(2400, i * 160) * night_p * 0.42))
      end
    end
  end
  rect_safe(fb, 0, 23, 64, 1, blend565(C_HAZE_DAY, C_HAZE_NIGHT, night_p))
end

local function draw_near_city(fb, dusk_p, night_p)
  local base = blend565(C_NEAR_CITY_DAY, C_NEAR_CITY_NIGHT, night_p)
  local edge = blend565(0x632C, blend565(base, 0x0000, 0.16), night_p)
  local rim = blend565(0xA4F6, blend565(base, 0x0000, 0.06), night_p)
  local sun_edge = blend565(C_GLOW_DAY, C_HAZE_DAY, 0.45 + dusk_p * 0.15)

  for i = 1, #NEAR_BUILDINGS do
    local b = NEAR_BUILDINGS[i]
    rect_safe(fb, b.x, b.y, b.w, b.h, base)
    rect_safe(fb, b.x, b.y, b.w, 1, rim)
    rect_safe(fb, b.x + b.w - 1, b.y + 1, 1, b.h - 1, edge)
    rect_safe(fb, b.x, b.y, 1, b.h, blend565(base, rim, 0.22 * (1 - night_p)))
    rect_safe(fb, b.x + 1, b.y + 1, b.w - 2, 1, blend565(base, sun_edge, 0.12 * (1 - night_p)))
    if night_p > 0.35 then
      rect_safe(fb, b.x, b.y + 1, 1, b.h - 1, blend565(0x0000, 0x2149, 0.55))
      if b.h > 12 then
        rect_safe(fb, b.x + 2, b.y + 2, 1, b.h - 4, blend565(0x0000, 0x18C3, 0.2))
      end
    else
      rect_safe(fb, b.x, b.y + 1, 1, b.h - 1, blend565(base, sun_edge, 0.14))
    end
    if b.w >= 8 then
      rect_safe(fb, b.x + 1, b.y + b.h - 1, b.w - 2, 1, blend565(base, 0x0000, 0.18 + night_p * 0.08))
    end
    if b.spire and b.spire > 0 then
      rect_safe(fb, b.x + (b.w // 2), b.y - b.spire, 1, b.spire, edge)
      if night_p > 0.45 then
        set_px_safe(fb, b.x + (b.w // 2), b.y - b.spire, blend565(C_NEON_A, C_STAR, triangle_wave(1800, i * 120) * 0.5))
      end
    end
  end

  rect_safe(fb, 0, 27, 64, 5, blend565(C_STREET, C_STREET_NIGHT, night_p))
end

local function draw_windows(fb, dusk_p, night_p, late_p)
  local idx = 0
  local day_dim_base = 0.08 * (1 - night_p)
  local evening_clock = clamp(state.anim_ms - 10000, 0, 14000)
  local late_clock = clamp(state.anim_ms - 29000, 0, 8000)
  for i = 1, #NEAR_BUILDINGS do
    local b = NEAR_BUILDINGS[i]
    local top = b.y + 2
    for wy = top, b.y + b.h - 4, 4 do
      for wx = b.x + 2, b.x + b.w - 3, 3 do
        idx = idx + 1
        local day_dim = ((idx + i) % 5 == 0) and day_dim_base or 0
        local on_at = hash01(idx * 11 + i * 5) * 12000
        local on_fade = smoothstep((evening_clock - on_at) / 2200)
        local off_at = hash01(idx * 17 + i * 9) * 7000
        local late_off = 1 - smoothstep((late_clock - off_at) / 1800)
        if idx % 9 == 0 then
          late_off = math.max(late_off, 0.24)
        end
        local fade = math.max(day_dim, on_fade * late_off)
        if fade > 0 then
          local cool = (idx % 4 == 0) and 1 or 0
          local lit = cool == 1 and C_WIN_COOL or C_WIN_WARM
          local tint = blend565(lit, C_WIN_COOL, night_p * 0.3 + late_p * 0.18)
          local c = blend565(0x0000, tint, fade)
          rect_safe(fb, wx, wy, 1, 2, c)
        end
      end
    end
  end
end

local function draw_neon(fb, night_p)
  if night_p <= 0.12 then return end
  local neon_p = smoothstep(clamp((state.anim_ms - 17000) / 8000, 0, 1))
  local glow_a = 0.45 + triangle_wave(2100, 400) * 0.55
  local glow_b = 0.42 + triangle_wave(2600, 900) * 0.58
  local glow_c = 0.38 + triangle_wave(3100, 1400) * 0.62

  local neon_a = blend565(C_NEON_A, C_STAR, neon_p * glow_a * 0.32)
  local neon_b = blend565(C_NEON_B, C_STAR, neon_p * glow_b * 0.28)
  local neon_c = blend565(C_NEON_C, C_STAR, neon_p * glow_c * 0.24)

  rect_safe(fb, 4, 22, 8, 1, neon_a)
  rect_safe(fb, 5, 23, 6, 1, blend565(0x0000, neon_a, 0.72))

  rect_safe(fb, 33, 17, 6, 1, neon_b)
  rect_safe(fb, 35, 18, 2, 4, blend565(0x0000, neon_b, 0.56))

  rect_safe(fb, 55, 15, 5, 1, neon_c)
  rect_safe(fb, 56, 16, 3, 1, blend565(0x0000, neon_c, 0.68))
  rect_safe(fb, 57, 12, 1, 1, neon_b)

  rect_safe(fb, 5, 28, 6, 1, blend565(0x0000, neon_a, 0.58))
  rect_safe(fb, 34, 28, 5, 1, blend565(0x0000, neon_b, 0.54))
  rect_safe(fb, 55, 29, 4, 1, blend565(0x0000, neon_c, 0.52))
  set_px_safe(fb, 57, 11, blend565(C_NEON_A, C_STAR, glow_a * neon_p * 0.7))
end

function app.init()
  state.anim_ms = 0
end

function app.tick(dt_ms)
  state.anim_ms = (state.anim_ms + (dt_ms or 0)) % 42000
end

function app.render_fb(fb)
  local dusk_p = smoothstep(clamp((state.anim_ms - 5000) / 13000, 0, 1))
  local night_p = smoothstep(clamp((state.anim_ms - 15000) / 10000, 0, 1))
  local late_p = smoothstep(clamp((state.anim_ms - 29000) / 8000, 0, 1))
  local star_p = smoothstep(clamp((state.anim_ms - 26000) / 9000, 0, 1))
  local sky_rows = {}

  for y = 0, 31 do
    local c = sky_row_color(y, dusk_p, night_p)
    sky_rows[y] = c
    rect_safe(fb, 0, y, 64, 1, c)
  end

  draw_sun(fb, sky_rows, dusk_p, night_p)
  draw_stars(fb, star_p)
  draw_far_city(fb, dusk_p, night_p)
  draw_near_city(fb, dusk_p, night_p)
  draw_windows(fb, dusk_p, night_p, late_p)
  draw_neon(fb, night_p)

  line_safe(fb, 0, 26, 63, 26, blend565(C_HAZE_DAY, 0x1082, night_p))
  line_safe(fb, 0, 31, 63, 31, 0x0000)
end


-- __GLOBAL_BOOT_SPLASH_WRAPPER_V1__
local __boot_now_ms = now_ms or (sys and sys.now_ms) or function() return 0 end
local __boot_started_ms = 0
local __boot_ms = tonumber(data.get("city_sunset_scene.boot_splash_ms") or data.get("app.boot_splash_ms") or 5000) or 5000
if __boot_ms < 0 then __boot_ms = 0 end
local __boot_name = tostring(data.get("city_sunset_scene.app_name") or "City Sunset Scene")

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
