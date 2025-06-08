-- AMX Lua Compiler
-- Patches unlocalization onto the base XR Lua compiler

local xr_lua = require("xr/lua")
local unlocalize = require(_PACKAGE .. "/unlocalize")

local old_compile = xr_lua.compile
function xr_lua.compile(src, namespace_name, script_name)
   if namespace_name then
      print("* " .. _PACKAGE .. ": compiling " .. namespace_name)
   end

   return old_compile(
      unlocalize(src, namespace_name),
      namespace_name,
      script_name
   )
end

require("xr/compiler").register_extension("script", xr_lua.compile)
require("xr/compiler").set_default_macro(xr_lua.compile)
