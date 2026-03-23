local app = {}

local GIF_SRC = "S:/littlefs/apps/great_wave_art/assets/great_wave.gif"

local state = {
  has_frame = false,
  should_advance = true,
  next_due_ms = 0,
}

function app.init()
  state.has_frame = false
  state.should_advance = true
  state.next_due_ms = 0
end

function app.tick(_dt_ms)
  local now = sys.now_ms and sys.now_ms() or 0
  if now >= (state.next_due_ms or 0) then
    state.should_advance = true
  end
end

function app.render_fb(fb)
  if not fb.gif then
    fb:fill(0x0000)
    return
  end

  local advance = state.should_advance or (not state.has_frame)
  local _wrapped, delay_ms = fb:gif(0, 0, 64, 32, GIF_SRC, advance)
  if advance then
    state.should_advance = false
    state.has_frame = true
    local d = tonumber(delay_ms) or 1000
    if d < 1000 then d = 1000 end
    local now = sys.now_ms and sys.now_ms() or 0
    state.next_due_ms = now + d
  end
end

return app
