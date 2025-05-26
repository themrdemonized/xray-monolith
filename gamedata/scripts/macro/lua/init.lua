local function handle_error(msg)
   return function(err)
      err = "! lua: "
         .. msg .. ":\n\n"
         .. debug.traceback(err .. "\n", 2)
         .. "\n"
      print(err)
      error(err)
   end
end

local function expand(src, namespace_name)
   print("* lua: expanding", namespace_name)

   local _, mod = xpcall(
      function()
         return loadstring(src, namespace_name)
      end,
      handle_error("error loading " .. namespace_name)
   )

   local mac = setfenv(
      mod,
      setmetatable(
         { _PACKAGE = namespace_name },
         {
            __index = _G,
            __newindex = _G,
         }
      )
   )

   return function()
      local _, out = xpcall(
         mac,
         handle_error("error evaluating " .. namespace_name)
      )

      return out
   end
end

require("scam/compiler").register_extension("lua", expand)

return {
   expand = expand
}
