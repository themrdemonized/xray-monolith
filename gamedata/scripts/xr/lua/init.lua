--- XR Lua Compiler
--- The original X-Ray script environment, reimplemented as a loadstring wrapper

local compiler = require("xr.compiler")

local XR_LOADERS = {
   _LOADERS.pre,
   _LOADERS.lib,
   _LOADERS.bin,
   _LOADERS.aio,
}

-- `package` module override for X-Ray Lua scripts
local XR_PACKAGE = setmetatable(
   {
      -- Use unconfigured Lua loaders
      loaders = XR_LOADERS,
   },
   {
      __index = package,
   }
)

-- Global scope wrapper for X-Ray Lua scripts
-- Indirects through `_G`, and `package.loaded` via `require`
local XR_G = setmetatable(
   {
      -- Indirect package to our wrapper
      package = XR_PACKAGE,
   },
   {
      -- Override key reads
      __index = function(self, key)
         -- If the index is _G, indirect back to this object
         if key == "_G" then
            return self
         end

         -- Check the real _G for our key and return the result if valid
         local gv = _G[key]
         if gv ~= nil then
            return gv
         end

         -- Otherwise, try to auto-load via the global package.path
         local res, out = pcall(require, key)
         if res then
            return out
         end

         -- Otherwise, try to auto-load via xr/lua's local package.path
         local package_path_old = package.path
         local package_cpath_old = package.cpath
         package.path = _DEFAULT_PATH
         package.cpath = _DEFAULT_CPATH
         res, out = pcall(require, key)
         package.path = package_path_old
         package.cpath = package_cpath_old
         if res then
            return out
         end
      end,
      -- Send key writes straight to `_G`
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
local function compile(src, namespace_name, script_name)
   -- Construct our script's environment table
   local env = {
      _PACKAGE = namespace_name,
      _FILE = script_name,
   }

   -- Create a wrapper around lua's base loadstring that runs in our environment
   local function base_loadstring(s, name)
      return setfenv(
         _LOADSTRING(s, name),
         env
      )
   end

   -- Construct the metatable for our environment
   local mt = {
      __index = function(self, key)
         -- Dynamically indirect to our loadstring wrapper
         if key == "loadstring" then
            return base_loadstring
         end

         -- Otherwise, indirect to XR_G
         return XR_G[key]
      end
   }

   -- If we're loading _g.script, forward environment writes to _G
   if namespace_name == "_G" then
      mt.__newindex = XR_G
   end

   -- Associate our environment with its metatable
   setmetatable(env, mt)

   -- If we have a namespace name, inject locals that derive from it
   if namespace_name then
      src = "local this = _G[script_name()] " .. src
      src = "local script_name = function() return \"" .. namespace_name .. "\" end " .. src
   end

   -- Load the resulting script, using the filename to ensure `debug` compat
   local mod, err = _LOADSTRING(src, script_name and ("@" .. script_name))

   -- On load failure, annotate the error and early-out
   if not mod then
      err = format_error("error loading " .. (namespace_name or "script"), err)
      return nil, err
   end

   -- Apply our environment to the resulting function
   local mac = setfenv(mod, env)

   -- Return a package constructor for the loaded module
   return function()
      -- Prepopulate `package.loaded` in case of reentrancy
      if namespace_name then
         package.loaded[namespace_name] = env
      end

      local lua_g_old = _LUA_G
      _LUA_G = env

      -- Call our script function with an appropriate error handler
      local _, out = xpcall(
         mac,
         handle_error(
            "error evaluating " .. (namespace_name or "script"),
            namespace_name
         )
      )

      _LUA_G = lua_g_old

      -- If we have a namespace name...
      if namespace_name then
         -- Return the prepopulated package
         return package.loaded[namespace_name]
      else
         -- Otherwise, return the module output directly
         return out
      end
   end
end

local function loadstring(src, namespace_name, script_name)
   if namespace_name then
      print("* [" .. _PACKAGE .. "] compiling " .. namespace_name)
   end

   return compile(src, namespace_name, script_name)
end

-- Prepare module value
local mod = {
   compile = compile,
   loadstring = loadstring
}

-- Register this as the compiler for .script files
compiler.register_extension("script", mod)

-- Register this as the default compiler
compiler.set_default_module(mod)

-- Return module value
return mod
