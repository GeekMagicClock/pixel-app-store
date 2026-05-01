local app = {}

local state = { anim_ms = 0 }

local C_SKY_TOP = 0x0004
local C_SKY_MID = 0x0848
local C_SKY_LOW = 0x10AB
local C_STAR = 0xFFFF
local C_AUR_GLOW = 0x02A0
local C_AUR_GREEN = 0x07E0
local C_AUR_CYAN = 0x07FF
local C_AUR_VIOLET = 0xA81F
local C_MTN_LEFT = 0x10A2
local C_MTN_LEFT_SHADE = 0x0821
local C_MTN_LEFT_RIDGE = 0x7D34
local C_MTN_RIGHT = 0x0000
local C_MTN_RIGHT_RIDGE = 0x39E7
local C_SNOW = 0xC6DF
local C_ICE = 0x7DFF

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

local function aurora_top(x, phase, base_y)
  local a = math.sin(x * 0.17 + phase) * 2.8
  local b = math.sin(x * 0.07 - phase * 0.65) * 1.9
  local c = math.sin(x * 0.29 + phase * 0.35) * 0.8
  return math.floor(base_y + a + b + c)
end

local function aurora_len(x, phase, base_len)
  local s = math.sin(x * 0.11 + phase * 1.1) * 1.5
  local t = math.sin(x * 0.21 - phase * 0.8) * 0.9
  return math.max(6, math.floor(base_len + s + t))
end

local function draw_sky_layer(fb)
  for y = 0, 31 do
    local c = C_SKY_TOP
    if y >= 7 and y < 14 then
      c = C_SKY_MID
    elseif y >= 14 then
      c = C_SKY_LOW
    end
    rect_safe(fb, 0, y, 64, 1, c)
  end

  for i = 0, 8 do
    rect_safe(fb, (i * 7 + 5) % 62, (i * 5 + 2) % 9, 1, 1, C_STAR)
  end
  rect_safe(fb, 58, 2, 1, 1, C_STAR)
  rect_safe(fb, 45, 4, 1, 1, C_STAR)
end

local function draw_aurora_layer(fb)
  local phase = state.anim_ms * 0.0022

  for x = 0, 63 do
    local top_a = aurora_top(x, phase, 5)
    local len_a = aurora_len(x, phase, 11)
    local top_b = aurora_top(x, phase + 1.2, 8)
    local len_b = aurora_len(x, phase + 0.6, 9)

    for i = 0, len_b do
      local y = top_b + i
      local c = C_AUR_GLOW
      if i <= 1 then
        c = C_AUR_CYAN
      elseif i <= 3 then
        c = C_AUR_GREEN
      end
      set_px_safe(fb, x, y, c)
    end

    for i = 0, len_a do
      local y = top_a + i
      local c = C_AUR_GLOW
      if i == 0 then
        c = C_AUR_VIOLET
      elseif i <= 2 then
        c = C_AUR_CYAN
      elseif i <= 5 then
        c = C_AUR_GREEN
      end
      set_px_safe(fb, x, y, c)
      if i > 3 and i < len_a - 1 and x % 3 == 0 then
        set_px_safe(fb, x, y + 1, C_AUR_GLOW)
      end
    end
  end
end

local function draw_left_mountain(fb)
  for x = 0, 31 do
    local top_y
    if x <= 17 then
      top_y = math.floor(26 - (x * 13 / 17))
    else
      top_y = math.floor(13 + ((x - 17) * 13 / 14))
    end
    for y = top_y, 31 do
      set_px_safe(fb, x, y, C_MTN_LEFT)
    end
  end

  line_safe(fb, 0, 26, 17, 13, C_SNOW)
  line_safe(fb, 17, 13, 31, 26, C_MTN_LEFT)
  line_safe(fb, 6, 24, 17, 13, C_MTN_LEFT_RIDGE)
  line_safe(fb, 10, 23, 19, 15, C_ICE)
  line_safe(fb, 4, 29, 13, 20, C_MTN_LEFT_SHADE)
  line_safe(fb, 12, 27, 17, 19, C_MTN_LEFT_SHADE)
  line_safe(fb, 19, 16, 27, 24, C_MTN_LEFT_SHADE)
  rect_safe(fb, 8, 24, 4, 1, C_MTN_LEFT_RIDGE)
  rect_safe(fb, 11, 22, 3, 1, C_ICE)
  rect_safe(fb, 14, 15, 6, 1, C_SNOW)
  rect_safe(fb, 15, 16, 4, 1, C_SNOW)
  rect_safe(fb, 16, 17, 2, 1, C_ICE)
  rect_safe(fb, 12, 19, 3, 1, C_ICE)
  rect_safe(fb, 20, 18, 2, 1, C_MTN_LEFT_RIDGE)
end

local function draw_right_mountain(fb)
  for x = 18, 63 do
    local top_y
    if x <= 44 then
      top_y = math.floor(27 - ((x - 18) * 18 / 26))
    else
      top_y = math.floor(9 + ((x - 44) * 18 / 19))
    end
    for y = top_y, 31 do
      set_px_safe(fb, x, y, C_MTN_RIGHT)
    end
  end

  line_safe(fb, 24, 23, 44, 9, C_SNOW)
  line_safe(fb, 29, 22, 44, 9, C_MTN_RIGHT_RIDGE)
  line_safe(fb, 34, 19, 45, 12, C_ICE)
  line_safe(fb, 39, 15, 48, 25, C_MTN_RIGHT_RIDGE)
  line_safe(fb, 48, 12, 58, 24, C_MTN_RIGHT_RIDGE)
  line_safe(fb, 52, 11, 61, 22, C_MTN_RIGHT_RIDGE)
  rect_safe(fb, 39, 11, 8, 1, C_SNOW)
  rect_safe(fb, 41, 12, 5, 1, C_ICE)
  rect_safe(fb, 42, 13, 3, 1, C_ICE)
  rect_safe(fb, 36, 17, 4, 1, C_MTN_RIGHT_RIDGE)
  rect_safe(fb, 46, 16, 3, 1, C_ICE)
  rect_safe(fb, 53, 16, 2, 1, C_MTN_RIGHT_RIDGE)
end

local function draw_foreground_layer(fb)
  draw_left_mountain(fb)
  draw_right_mountain(fb)
  rect_safe(fb, 0, 30, 64, 1, C_ICE)
  rect_safe(fb, 0, 31, 64, 1, C_SNOW)
end

function app.init()
  state.anim_ms = 0
end

function app.tick(dt_ms)
  state.anim_ms = (state.anim_ms + (dt_ms or 0)) % 60000
end

function app.render_fb(fb)
  draw_sky_layer(fb)
  draw_aurora_layer(fb)
  draw_foreground_layer(fb)
end


-- __GLOBAL_BOOT_SPLASH_WRAPPER_V1__
local __boot_now_ms = now_ms or (sys and sys.now_ms) or function() return 0 end
local __boot_started_ms = 0
local __boot_ms = tonumber(data.get("aurora_scene.boot_splash_ms") or data.get("app.boot_splash_ms") or 5000) or 5000
if __boot_ms < 0 then __boot_ms = 0 end
local __boot_name = tostring(data.get("aurora_scene.app_name") or "Aurora Scene")

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
