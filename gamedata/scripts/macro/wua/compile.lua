local MOVED = {
   _g_patches = "patches/_g",
}

local G = setmetatable(
   {},
   {
      __index = function(_, key)
         local redir = MOVED[key]
         if redir ~= nil then
            key = redir
         end

         local gv = _G[key]
         if gv ~= nil then
            return gv
         end

         local res, out = pcall(require, key)
         if res then
            return out
         end
      end,
      __newindex = _G
   }
)

local function compile(src, namespace_name)
   local is_g = namespace_name == "_G"

   local mt = {
      __index = G
   }

   if is_g then
      mt.__newindex = G
   end

   local env = setmetatable({ _G = G }, mt)

   if not is_g then
      env._M = env
      if namespace_name then
         env._PACKAGE = namespace_name
         env._COMPILER = _COMPILER
         env.loadstring = _COMPILER
         env[namespace_name] = env
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

   local mod = setfenv(
      require("macro").load_src(src, namespace_name),
      env
   )

   return function()
      mod()
      return env
   end
end

return {
   compile = compile
}
