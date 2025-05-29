local unlocalize = import("unlocalize").unlocalize
local compile = import("compile").compile

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
