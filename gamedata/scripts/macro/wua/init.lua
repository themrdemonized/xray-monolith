local unlocalizers = require("unlocalizers")
local macro = require("macro")

local function string_trim(s, v)
   if v == nil then
      v = " \t\n\r\f\v"
   end
   local pattern = string.format("^[%s]*([^%s]*)[%s]*$", v, v, v)
   print("pattern:", pattern)
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

local function unlocal_regex(unlocals, s)
   local pattern = [[^(local)([\t ]+)(function)([\t ]+)([_a-zA-Z].*)([\t ]*)(%(.*)$]]
   local _, _, c, d, e, f, g = string.match(s, pattern)

   if e and contains(unlocals, e) then
      print("[unlocal_regex] found variable " .. e .. " to unlocal")
      print("s", s)
      s = c .. d .. e .. f .. g
      print("s'", s)
      return s
   end

   return nil
end

local function unlocalize(src, namespace_name)
   if not namespace_name then
      return src
   end
   
   local unlocalizer = unlocalizers.get(namespace_name)
   if not unlocalizer then
      return src
   end

   local unlocal_performed = false

   local temp = src
   local tokens = {}
   for line in string.gmatch(temp, "[^\n]+") do
      table.insert(tokens, line)
   end

   for i,s in ipairs(tokens) do
      print("s", s)
      s = string_trim(s, "\n\r")
      print("trimmed", s)
      tokens[i] = s

      if s == "" then
         goto next_token
      end

      -- local function x(a,b,c)
      local ur = unlocal_regex(unlocalizer, s)
      if ur then
         tokens[i] = ur
         unlocal_performed = true
         tokens[i] = s
         goto next_token
      end

      -- local a = ...
      -- local a
      -- local a,b,c = ... (if one of a,b,c is in unlocalizers list - all of them will be unlocalized)
      -- local x; local y; - unsupported yet
      local pattern = [[^local%s+(.*)]]
      local c = string.match(s, pattern) or ""
      if #c > 0 then
         local r = [[(.*)--.*]]
         local nc = string.match(c, r)
         if nc then
            c = nc
         end
      end

      local pattern = [[([^=]+)=(.*)]]
      local variables, values = string.match(c, pattern)
      if variables then
         for v in string.gmatch(variables, "[^,]+") do
            v = string_trim(v)
            if contains(unlocalizer, v) then
               unlocal_performed = true
               print("found variable", v, "to unlocal")
               s = c
               if not values then
                  local r = [[(.*)(--.*)]]
                  local lhs, rhs = string.match(s, r)
                  if lhs and rhs then
                     s = lhs .. "= nil " .. rhs
                  else
                     s = s .. " = nil"
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

local function compile(src, namespace_name)
   return function()
      local is_g = namespace_name == "_G"

      local G = setmetatable(
         {},
         {
            __index = function(_, key)
               local gv = _G[key]
               if gv ~= nil then
                  return gv
               end

               local res, out = pcall(require, key)
               if res then
                  return out
               end
            end,
            __newindex = function(_, k, v)
               _G[k] = v
            end
         }
      )

      local mt = {
         __index = G
      }

      if is_g then
         mt.__newindex = function(_, k, v)
            _G[k] = v
         end
      end

      local env = setmetatable({ _G = G }, mt)

      if not is_g then
         env._M = env
         if namespace_name then
            env._PACKAGE = namespace_name
            -- Prepopulate the environment in case of indirection
            package.loaded[namespace_name] = env
         end
      end


      if namespace_name then
         src = [[
local script_name = function()
   return _PACKAGE
end
         ]] .. src
      end

      src = [[
local this = _M
      ]] .. src

      local mod = require("macro").load_src(src, namespace_name)
      setfenv(mod, env)()

      -- Emplace in package.loaded so require returns env
      package.loaded[namespace_name] = env
   end
end

local function expand(src, namespace_name)
   print("wua: expanding " .. namespace_name)
   return compile(
      unlocalize(src, namespace_name),
      namespace_name
   )
end

package.loaded["macro/wua"] = {
   expand = expand
}
