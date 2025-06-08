_PACKAGE = "xr"
_FILE = "xr/init.lua"

-- Initialize S.C.A.M. environment
local scam = require("scam")

-- Load xr/unlocalize before xr/lua to avoid circular referencing
require(_PACKAGE .. "/unlocalize")

-- Setup xr/lua as the default language
scam.compiler.set_default_macro(
   require(_PACKAGE .. "/lua").expand
)

-- Forcefully load _g.script
package.loaded._G = nil
require("_G")

-- Register classes
require(_PACKAGE .. "/classes")

-- Setup script processes
local processes = require(_PACKAGE .. "/processes")

-- Game process
local ini_script = ini_file("configs\\script.ltx")
print("ini_script:", ini_script)

local game_scripts = ""
if ini_script:section_exist("single")
   and ini_script:line_exist("single", "script")
then
   print("reading from ini")
   game_scripts = ini_script:r_string("single", "script");
end

print("game_scripts:", game_scripts)
processes:add("game", game_scripts)

-- Level process
if level.present() then
   local ini_level = ini_file(
      string.format("levels\\%s\\level.ltx", level.name())
   )
   print("ini_level:", ini_level)

   local level_scripts = ""
   if ini_level:section_exist("level_scripts")
      and ini_level:line_exist("level_scripts", "script")
   then
      level_scripts = ini_level:r_string("level_scripts", "script");
   end

   processes:add("level", level_scripts)
end

-- Run common scripts
require(_PACKAGE .. "/scripts")

