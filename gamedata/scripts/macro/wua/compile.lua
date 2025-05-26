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

local function handle_error(msg)
   return function(err)
      err = "! wua: "
         .. msg .. ":\n\n"
         .. debug.traceback(err .. "\n", 2)
         .. "\n"
      print(err)
      error(err)
   end
end

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
      src = "local script_name = function() return _PACKAGE end " .. src
   end

   src = "local this = _M " .. src

   local res, mod = pcall(loadstring, src, namespace_name)
   if not res then
      handle_error("error loading " .. namespace_name)(mod)
   end

   local mac = setfenv(mod, env)

   return function()
      xpcall(mac, handle_error("error evaluating " .. namespace_name))
      return env
   end
end

return {
   compile = compile
}
