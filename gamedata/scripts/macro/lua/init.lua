local function handle_error(msg, namespace_name)
   return function(err)
      package.loaded[namespace_name] = nil
      err = "! " .. _PACKAGE .. ": "
         .. msg .. ":\n\n"
         .. debug.traceback(err .. "\n", 2)
         .. "\n"
      print(err)
      error(err)
   end
end

local function expand(src, namespace_name, script_name)
   print("* " .. _PACKAGE .. ": expanding", namespace_name)

   local mod, err = loadstring(src, namespace_name)
   if not mod then
      handle_error(
         "error loading " .. namespace_name,
         namespace_name
   )(err)
   end

   local env = setmetatable(
      {
         _PACKAGE = namespace_name,
         _FILE = script_name,
      },
      {
         __index = _G,
         __newindex = _G,
      }
   )

   local mac = setfenv(mod, env)

   return function()
      package.loaded[namespace_name] = env
      local _, out = xpcall(
         mac,
         handle_error(
            "error evaluating " .. namespace_name,
            namespace_name
         )
      )

      return out
   end
end

require("scam/compiler").register_extension("lua", expand)

return {
   expand = expand
}
