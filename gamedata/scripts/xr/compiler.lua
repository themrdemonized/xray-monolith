-- XR Compiler Dispatch
-- Extends loadstring with extension-aware dispatch

local PATTERN_FILE_PATH = "^(.-)([^\\/]-)%.([^\\/%.]-)%.?$"
local PATTERN_MACRO_TAG = "[^ ]+ +=%*= +lang: +([^ ]+) +=%*=[^\n]*(\n.*)"

-- Store the original loadstring implementation into a compiler module
local default = { loadstring = loadstring }

-- Map from file extension to compiler module
local extensions = {}

-- Associate the default loadstring implementation with the lua file extension
extensions.lua = default

-- Override loadstring with extensible dispatch
function loadstring(src, namespace_name, script_name)
   local mod = nil

   if script_name then
      local _,_,ext = script_name:match(PATTERN_FILE_PATH)
      if extensions[ext] then
         mod = extensions[ext]
      end
   end

   local tag,rest = src:match(PATTERN_MACRO_TAG)
   if tag ~= nil then
      src = rest
      mod = require(tag)
   end

   if mod == nil then
      mod = default
   end

   return mod.loadstring(src, namespace_name, script_name)
end

-- Associate a file extension with a compiler module
local function register_extension(k, mod)
   print(_PACKAGE .. ": registering extension: " .. k)
   _REGISTER_PATHS(
      "?." .. k,
      "?/init." .. k
   )
   extensions[k] = mod
end

-- Return a list of registered extensions
local function get_extensions()
   local out = {}
   for k in pairs(extensions) do
      table.insert(out, k)
   end
   return out
end

-- Set the default compiler module
local function set_default_module(mod)
   print(_PACKAGE .. ": setting default module...")
   default = mod
end

-- Return module result
return {
   PATTERN_FILE_PATH = PATTERN_FILE_PATH,
   PATTERN_MACRO_TAG = PATTERN_MACRO_TAG,
   register_extension = register_extension,
   get_extensions = get_extensions,
   set_default_module = set_default_module
}
