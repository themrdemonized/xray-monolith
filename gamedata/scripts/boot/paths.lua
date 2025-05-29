_PACKAGE = "boot/paths"

-- Define load path registrator
function _REGISTER_PATHS(...)
   local ps = {...}
   for i=#ps,1,-1 do
      p = getFS():update_path("$game_scripts$", ps[i])
      package.path = p .. ";" .. package.path
   end
end
