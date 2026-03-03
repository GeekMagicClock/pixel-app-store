local app = {}

local last_toggle_ms = 0
local idx = 1

local items = {
  {symbol = "AAPL", name = "APPLE", icon = "S:/littlefs/icon/apple-24.png"},
  {symbol = "TSLA", name = "TESLA", icon = "S:/littlefs/icon/tesla-24.png"},
  {symbol = "META", name = "META", icon = "S:/littlefs/icon/facebook-24.png"},
}

local function fmt_pct(pct)
  local sign = ""
  if pct > 0 then sign = "+" end
  return string.format("%s%.1f%%", sign, pct)
end

local function fmt_price(v)
  return string.format("$%.0f", v)
end

function app.init(config)
  sys.log("fb_stocks init")
  last_toggle_ms = sys.now_ms()
  idx = 1
end

function app.tick(dt_ms)
  local now = sys.now_ms()
  if now - last_toggle_ms >= 5000 then
    idx = (idx % #items) + 1
    last_toggle_ms = now
  end
end

-- Pure framebuffer mode (preferred): draw into fb directly.
-- fb:fill(color565) / fb:rect(x,y,w,h,color565) / fb:set_px(x,y,color565)
function app.render_fb(fb)
  local W = fb:width()
  local H = fb:height()
  fb:fill(0x0000)

  local it = items[idx]
  local price = tonumber(data.get("stocks.price") or 0) or 0
  local pct = tonumber(data.get("stocks.change_pct") or 0) or 0

  local chg_color = 0xFFFF
  if pct > 0.0001 then chg_color = 0x07E0 end
  if pct < -0.0001 then chg_color = 0xF800 end

  -- Left icon area (32x32): border + icon.
  fb:rect(0, 0, 32, 32, 0x0000)
  fb:rect(1, 1, 30, 30, 0x0000)

  fb:image(4, 4, 24, 24, it.icon)

  -- Right text area x=[32..63]
  -- Use SilkScreen font from LittleFS via fb:text.
  local font = "builtin:silkscreen_regular_8"
  fb:text_box(32, 0, 32, 8, it.name, 0xFFFF, font, 8, "left", true)
  fb:text_box(32, 12, 32, 8, fmt_pct(pct), chg_color, font, 8, "left", true)
  fb:text_box(32, 24, 32, 8, fmt_price(price), 0xFFFF, font, 8, "left", true)
end

return app
