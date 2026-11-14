-- Opt-in example for addon authors; not installed or auto-loaded by this fork.
-- Wrap/adapt this into your addon's script and call start() from its UI button.
-- This transient example does not save annotations or implement undo/eraser/UI.
local owner = "R1 PDA Map Example"
local token, drawing, last_x, last_y

local function ensure_layer()
    if not (pda_map and pda_map.version() == 1 and pda_map.ready()) then return false end
    if token and pda_map.valid_layer(token) then return true end
    token = pda_map.register_layer(owner)
    return token ~= 0
end

function start()
    return ensure_layer() and pda_map.acquire_input(token)
end

function stop()
    drawing, last_x, last_y = false, nil, nil
    if token and pda_map then pda_map.release_input(token) end
end

local function on_visibility(shown)
    if not shown then stop() end
end

local function on_input(event)
    if event.layer ~= token then return end
    if event.shift then stop(); return end -- retain native Shift+drag panning
    if event.action == "left_down" or event.action == "left_double_click" then
        drawing = true
        last_x, last_y = event.global_x, event.global_y
        event.consume = true
    elseif event.action == "left_up" then
        drawing, last_x, last_y = false, nil, nil
        event.consume = true
    elseif event.action == "mouse_move" and drawing then
        if not event.left_held then drawing, last_x, last_y = false, nil, nil; return end
        if last_x and event.global_x then
            local id = pda_map.add_line(token, "global_map", last_x, last_y,
                event.global_x, event.global_y, 2, 0xffe5b449)
            if id == 0 then stop(); return end -- surface this failure in your own UI
            last_x, last_y = event.global_x, event.global_y
        end
        event.consume = true
    end
end

function on_game_start()
    if not (pda_map and pda_map.version() == 1) then return end
    RegisterScriptCallback("on_pda_map_input", on_input)
    RegisterScriptCallback("on_pda_map_visibility", on_visibility)
    RegisterScriptCallback("actor_on_net_destroy", stop)
    RegisterScriptCallback("actor_on_before_death", stop)
    RegisterScriptCallback("on_before_level_changing", stop)
end
