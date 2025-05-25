local function load_src(src, script_name)
   return assert(loadstring(src, script_name))
end

local function extend_env(dest)
   return setmetatable(
      dest,
      {
         __index = _G,
         __newindex = _G,
      }
   )
end

package.loaded["macro"] = {
    load_src = load_src,
    extend_env = extend_env,
}

require("macro/lua")
require("macro/lisp")
require("macro/wua")
