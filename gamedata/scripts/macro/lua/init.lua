local function expand(src, namespace_name)
   print("* lua: expanding", namespace_name)
   print(src)

   local res, mod = pcall(loadstring, src, namespace_name)
   if not res then
      local msg = "! lua: error loading " .. namespace_name .. ":\n\n"
               .. mod .. "\n"
      print(msg)
      error(msg)
   end

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
      local evaluated, out = pcall(mac)
      if not evaluated then
         out = "! lua: error evaluating " .. namespace_name .. ":\n\n"
            .. out .. "\n"
         print(out)
         error(out)
      end

      return out
   end
end

require("scam/compiler").register_extension("lua", expand)

return {
   expand = expand
}
