--local remap = require("moved").remap

local G = setmetatable(
   {},
   {
      __index = function(self, key)
         --[[
         -- Fetch the remap for this key
         local redir = remap[key]

         -- If we have a redirection...
         if redir ~= nil then
            -- Check the no-overwrite flag;
            -- If set and the key exists in _G, return its value
            if redir.if_not_overwritten then
               local gv = _G[key]
               if gv then
                  return gv
               end

               local res, out = pcall(require, key)
               if res then
                  return out
               end
            end

            -- Otherwise, fetch the redirected key
            if type(redir.to) ~= "string" then
               error(
                  "Redirection from " .. key
                  .. " has invalid 'to' field: " .. redir.to
               )
            end

            -- And recurse with it
            local rv = self[redir.to]

            -- If if exists, return it
            if rv ~= nil then
               return rv
            end
         end
         --]]

         -- Otherwise, check the global key's value and return if valid
         local gv = _G[key]
         if gv ~= nil then
            return gv
         end

         -- Otherwise, try to auto-load the key as a script
         local res, out = pcall(require, key)
         if res then
            return out
         end
      end,
      __newindex = _G
   }
)

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

local function compile(src, namespace_name, script_name)
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
         env._FILE = script_name
         env._COMPILER = _COMPILER
         env.loadstring = _COMPILER
         env[namespace_name] = env
      end
   end

   if namespace_name then
      src = "local script_name = function() return _PACKAGE end " .. src
   end

   src = "local this = _M " .. src

   local mod, err = loadstring(src, namespace_name)
   if not mod then
      handle_error("error loading " .. namespace_name, namespace_name)(err)
   end

   local mac = setfenv(mod, env)

   return function()
      package.loaded[namespace_name] = env
      xpcall(
         mac,
         handle_error(
            "error evaluating " .. namespace_name,
            namespace_name
         )
      )
      return package.loaded[namespace_name]
   end
end

return {
   compile = compile
}
