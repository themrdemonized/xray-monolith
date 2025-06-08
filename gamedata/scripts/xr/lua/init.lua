--local remap = import("/moved").remap

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

local function format_error(msg, err, stack_level)
   stack_level = stack_level or 2

   err = "! " .. _PACKAGE .. ": "
      .. msg .. ":\n\n"
      .. err
      .. "\n"

   return debug.traceback(err, stack_level)
end

local function handle_error(msg, namespace_name)
   return function(err)
      if namespace_name then
         package.loaded[namespace_name] = nil
      end

      err = format_error(msg, err, 3)

      print(err)
      error(err)
   end
end

local compile
compile = function(src, namespace_name, script_name)
   local is_g = namespace_name == "_G"

   local mt = {
      __index = G
   }

   if is_g then
      mt.__newindex = G
   end

   local env = setmetatable({ _G = G }, mt)

   if not is_g then
      -- If this is a named module, emplace relevant globals
      if namespace_name then
         env._M = env
         env._PACKAGE = namespace_name
         env._FILE = script_name
         env[namespace_name] = env
      end

      -- Selectively patch the package module
      -- to restore unconfigured Lua environment
      local pkg = {}
      for k,v in pairs(package) do
         pkg[k] = v
      end
      pkg.path = _DEFAULT_PATH
      pkg.loaders = {
         _LOADERS.pre,
         _LOADERS.lib,
         _LOADERS.bin,
         _LOADERS.aio,
      }
      env.package = pkg
   end

   if namespace_name then
      src = "local script_name = function() return _PACKAGE end " .. src
      src = "local this = _M " .. src
   end

   local mod, err = _LOADSTRING(src, namespace_name)
   if not mod then
      err = format_error("error loading " .. (namespace_name or "script"), err)
      return nil, err
   end

   local mac = setfenv(mod, env)

   return function()
      if namespace_name then
         package.loaded[namespace_name] = env
      end

      local _, out = xpcall(
         mac,
         handle_error(
            "error evaluating " .. (namespace_name or "script"),
            namespace_name
         )
      )

      if namespace_name then
         return package.loaded[namespace_name]
      else
         return out
      end
   end
end

local compiler = require("xr/compiler")
compiler.register_extension("script", compile)
compiler.set_default_macro(compile)

return {
   compile = compile
}
