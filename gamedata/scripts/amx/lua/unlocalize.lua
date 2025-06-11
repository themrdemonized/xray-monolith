-- Unlocalizer for xr/lua scripts

local DEBUG = false

local function string_trim(s, v)
   if v == nil then
      v = " \t\n\r\f\v"
   end
   local pattern = string.format("^[%s]*([^%s]*)[%s]*$", v, v, v)
   return string.match(s, pattern)
end

local function contains(lst, a)
   for _,b in ipairs(lst) do
      if a == b then
         return true
      end
   end

   return false
end

local function unlocalize_with(src, unlocalizer)
   local unlocal_performed = false

   local temp = src
   local tokens = {}
   for line in string.gmatch(temp, "[^\n\r]+") do
      table.insert(tokens, line)
   end

   for i,s in ipairs(tokens) do
      s = string_trim(s, "\n\r")
      tokens[i] = s

      if s == "" then
         goto next_token
      end

      -- local function x(a,b,c)
      local _, _, c, d, e, f, g = string.match(
         s,
         [[^(local)([\t ]+)(function)([\t ]+)([_a-zA-Z].*)([\t ]*)(%(.*)$]]
      )

      if e then
         if contains(unlocalizer, e)  then
            if DEBUG then
               print("[unlocal_regex] found variable " .. e .. " to unlocal")
            end
            tokens[i] = c .. d .. e .. f .. g
            unlocal_performed = true
         end
         goto next_token
      end

      -- local a = ...
      -- local a
      -- local a,b,c = ... (if one of a,b,c is in unlocalizers list - all of them will be unlocalized)
      -- local x; local y; - unsupported yet
      local c = string.match(s, [[^local%s+(.*)]]) or ""
      if #c > 0 then
         local r = [[(.*)--.*]]
         local nc = string.match(c, r)
         if nc then
            c = nc
         end
      end

      local variables = string.match(c, [[^([^=]+)]])
      local values = string.match(c, [[=([^=]+)$]])
      if variables then
         for v in string.gmatch(variables, "[^, ]+") do
            v = string_trim(v)
            if contains(unlocalizer, v) then
               unlocal_performed = true
               if DEBUG then
                  print("found variable", v, "to unlocal")
               end
               s = c
               if not values then
                  local lhs, rhs = string.match(s, [[([^-]+)(%-%-[^-]*)]])
                  if lhs and rhs then
                     s = lhs .. "= nil " .. rhs
                  else
                     s = s .. "= nil"
                  end
               end
               tokens[i] = s
               break
            end
         end
      end

      ::next_token::
   end

   if unlocal_performed then
      return table.concat(tokens, "\n")
   end

   return src
end

local function unlocalize(src, namespace_name)
   if not namespace_name then
      return src
   end

   local unlocalizer = require("amx/unlocalize").get(namespace_name)
   if not unlocalizer then
      return src
   end

   return unlocalize_with(src, unlocalizer)
end

return {
   unlocalize_with = unlocalize_with,
   unlocalize = unlocalize,
}
