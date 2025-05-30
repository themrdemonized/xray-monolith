_PACKAGE = "boot/paths"

local first_run = true

-- Define load path registrator
function _REGISTER_PATHS(...)
   -- If this is the first registered path, clear existing paths
   if first_run then
      first_run = false
      package.path = ""
   end

   local ps = {...}
   for i=#ps,1,-1 do
      p = getFS():update_path("$game_scripts$", ps[i])
      package.path = p .. ";" .. package.path
   end

   print("package.path: " .. package.path)
end
