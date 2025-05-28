local unlocalize = require(_PACKAGE .. "/unlocalize").unlocalize
local compile = require(_PACKAGE .. "/compile").compile

local function expand(src, namespace_name)
   print("* " .. _PACKAGE .. ": expanding", namespace_name)
   return compile(
      unlocalize(src, namespace_name),
      namespace_name
   )
end

require("scam/compiler").register_extension("script", expand)

return {
   expand = expand
}
