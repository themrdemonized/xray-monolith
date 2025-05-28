--- Lua Entrypoint
--- Configures the import path and hands control to the boot module

local fs = getFS()
local base = fs:update_path("$game_scripts$", "")
package.path = base .. "?.lua;" .. base .. "?/init.lua"

require("boot")

