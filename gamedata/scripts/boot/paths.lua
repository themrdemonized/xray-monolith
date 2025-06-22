-- Boot Paths
-- Configures package.path to root at gamedata/scripts,
-- and gamedata/scripts/packages

_PACKAGE = "boot/paths"

-- Cache default paths for later
_DEFAULT_PATH = package.path
_DEFAULT_CPATH = package.cpath

-- Define load path registrator
function _REGISTER_PATHS(...)
   local ps = {...}
   for i=#ps,1,-1 do
      local p = getFS():update_path("$game_scripts$", ps[i])
      package.path = p .. ";" .. package.path
   end
end

-- Fetch filesystem handle
local fs = getFS()

-- Get base scripts folder path
local base = fs:update_path("$game_scripts$", "")

-- Search for lua files in scripts, and scripts/packages/lib
package.path = base .. [[?.lua]]
     .. ";" .. base .. [[?\init.lua]]
     .. ";" .. base .. [[packages\lib\?.lua]]
     .. ";" .. base .. [[packages\lib\?\init.lua]]

-- Search for binary libraries in packages/bin
package.cpath = base .. [[packages\bin\?.dll]]
      .. ";" .. base .. [[packages\bin\?\init.dll]]
      .. ";" .. base .. [[packages\bin\?.so]]
      .. ";" .. base .. [[packages\bin\?\init.so]]
