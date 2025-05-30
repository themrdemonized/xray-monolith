--- Lua Entrypoint
--- Called by CScriptEngine at the end of Lua initialization

_PACKAGE = "init"

--- Configure the initial import path
local fs = getFS()
local base = fs:update_path("$game_scripts$", "")
package.path = base .. "?.lua;" .. base .. "?/init.lua"

--- Clear the C import path
package.cpath = ""

--- Initialize environment via the boot module
require("boot")

-- Pass control to modded exes entrypoint
require("amx")
