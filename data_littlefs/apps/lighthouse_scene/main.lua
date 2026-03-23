local app = {}

local GIF_SRC = "S:/littlefs/apps/lighthouse_scene/assets/lighthouse.gif"

local state = {
  next_due_ms = 0,
  has_frame = false,
  should_advance = true,
}

function app.init()
  state.next_due_ms = 0
  state.has_frame = false
  state.should_advance = true
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
    local d = tonumber(delay_ms) or 83
    if d < 50 then d = 50 end
    local now = sys.now_ms and sys.now_ms() or 0
    state.next_due_ms = now + d
  end
end

return app
