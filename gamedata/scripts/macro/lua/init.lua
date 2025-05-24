local function expand(src, namespace_name)
   print("* lua: expanding " .. namespace_name)
   local macro = require("macro")
   return setfenv(
      macro.load_src(src),
      macro.extend_env {
         script_name = function()
            return namespace_name
         end
      }
   )
end

require("scam/compiler").register_extension("lua", expand)

package.loaded["macro/lua"] = {
   expand = expand
}
