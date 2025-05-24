local function load_src(src, script_name)
   return assert(loadstring(src, script_name))
end

local function extend_env(dest)
   for k,v in pairs(getfenv(0)) do
       dest[k] = v
   end
   return dest
end

package.loaded["macro"] = {
    load_src = load_src,
    extend_env = extend_env,
}

require("macro/lua")
require("macro/lisp")
require("macro/wua")
