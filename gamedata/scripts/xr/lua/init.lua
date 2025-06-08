-- XR Lua Compiler
-- The original X-Ray script environment, reimplemented as a loadstring wrapper

local compiler = require("xr/compiler")

-- _G wrapper with redirection to package.loaded via `require`
local G = setmetatable(
   {},
   {
      __index = function(self, key)
         -- Check _G for our key and return the result if valid
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

-- Error pretty-printer
local function format_error(msg, err, stack_level)
   stack_level = stack_level or 2

   err = "! " .. _PACKAGE .. ": "
      .. msg .. ":\n\n"
      .. err
      .. "\n"

   return debug.traceback(err, stack_level)
end

-- Error handler constructor
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

-- `loadstring` replacement specialized to X-Ray scripts
local function loadstring(src, namespace_name, script_name)
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

-- Prepare module value
local mod = {
   loadstring = loadstring
}

-- Register this as the compiler for .script files
compiler.register_extension("script", mod)

-- Register this as the default compiler
compiler.set_default_module(mod)

-- Return module value
return mod
