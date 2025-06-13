-- AMX Lua Compiler
-- Patches unlocalization onto the base XR Lua compiler

local xr_lua = require("xr.lua")
local unlocalize = require(_PACKAGE .. ".unlocalize").unlocalize

local old_loadstring = xr_lua.loadstring
function xr_lua.loadstring(src, namespace_name, script_name)
   if namespace_name then
      print("* " .. _PACKAGE .. ": compiling " .. namespace_name)
   end

   return old_loadstring(
      unlocalize(src, namespace_name),
      namespace_name,
      script_name
   )
end

-- Run unit tests
require("amx.lua.tests")
