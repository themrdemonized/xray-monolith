local function expand(src, namespace_name)
   print("* lua: expanding", namespace_name)
   local macro = require("macro")
   return setfenv(
      macro.load_src(src, namespace_name),
      macro.extend_env {
         _PACKAGE = namespace_name
      }
   )
end

require("scam/compiler").register_extension("lua", expand)

package.loaded[_PACKAGE] = {
   expand = expand
}
