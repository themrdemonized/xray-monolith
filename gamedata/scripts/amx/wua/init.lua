local unlocalize = require(_PACKAGE .. "/unlocalize").unlocalize
local compile = require(_PACKAGE .. "/compile").compile

local function expand(src, namespace_name, script_name)
   if namespace_name then
      print("* " .. _PACKAGE .. ": expanding " .. namespace_name)
   end

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
