local unlocalize = require("amx/wua/unlocalize").unlocalize
local compile = require("amx/wua/compile").compile

local function expand(src, namespace_name, script_name)
   print("* " .. _PACKAGE .. ": expanding", namespace_name)
   return compile(
      unlocalize(src, namespace_name),
      namespace_name,
      script_name
   )
end

require("scam/compiler").register_extension("script", expand)

return {
   expand = expand
}
