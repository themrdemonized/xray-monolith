_PACKAGE = "boot/paths"

-- Cache default path for later
_DEFAULT_PATH = package.path

-- Define load path registrator
function _REGISTER_PATHS(...)
   local ps = {...}
   for i=#ps,1,-1 do
      local p = getFS():update_path("$game_scripts$", ps[i])
      package.path = p .. ";" .. package.path
   end
end

local fs = getFS()

local base = fs:update_path("$game_scripts$", "")

package.path = base .. [[?.lua]]
     .. ";" .. base .. [[?/init.lua]]
     .. ";" .. base .. [[packages/lib/?.lua]]
     .. ";" .. base .. [[packages/lib/?/init.lua]]

package.cpath = base .. [[packages/bin/?.dll]]
      .. ";" .. base .. [[packages/bin/?/init.dll]]
      .. ";" .. base .. [[packages/bin/?.so]]
      .. ";" .. base .. [[packages/bin/?/init.so]]
