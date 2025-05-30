_PACKAGE = "boot/paths"

local first_run = true

local function init_lua_paths()
   local package_lib = getFS():update_path("$game_scripts$", "packages/lib")
   package.path = package_lib .. [[/?.lua]]
      .. ";" .. package_lib .. [[/?/init.lua]]

   local package_bin = getFS():update_path("$game_scripts$", "packages/bin")
   package.cpath = package_bin .. [[/?.dll]]
      .. ";" .. package_bin .. [[/?/init.dll]]
      .. ";" .. package_bin .. [[/?.so]]
      .. ";" .. package_bin .. [[/?/init.so]]
end

-- Define load path registrator
function _REGISTER_PATHS(...)
   -- If this is the first registered path, clear existing paths
   if first_run then
      first_run = false
      init_lua_paths()
   end

   local ps = {...}
   for i=#ps,1,-1 do
      local p = getFS():update_path("$game_scripts$", ps[i])
      package.path = p .. ";" .. package.path
   end
end
