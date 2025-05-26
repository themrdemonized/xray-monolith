local unlocalize = require("macro/wua/unlocalize").unlocalize
local compile = require("macro/wua/compile").compile

local function expand(src, namespace_name)
   print("* wua: expanding", namespace_name)
   return compile(
      unlocalize(src, namespace_name),
      namespace_name
   )
end

require("scam/compiler").register_extension("script", expand)

return {
   expand = expand
}
