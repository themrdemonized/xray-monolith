local PATTERN_FILE_PATH = "^(.-)([^\\/]-)%.([^\\/%.]-)%.?$"
local PATTERN_MACRO_TAG = "[^ ]+ +=%*= +lang: +([^ ]+) +=%*=[^\n]*(\n.*)"

local extensions = {}
local state = {
   default = _COMPILER
}

function _COMPILER(src, namespace_name, script_name)
   local mac = nil

   if script_name then
      local _,_,ext = script_name:match(PATTERN_FILE_PATH)
      if extensions[ext] then
         mac = extensions[ext]
      end
   end

   local tag,rest = src:match(PATTERN_MACRO_TAG)
   if tag ~= nil then
      src = rest
      mac = function_object(tag)
   end

   if mac == nil then
      mac = state.default
   end

   local res, out = pcall(mac, src, namespace_name, script_name)
   if not res then
      print(out)
      error(out)
      if namespace_name then
         package.loaded[namespace_name] = nil
      end
   end

   return out
end

local function register_extension(k, v)
   print("compiler: registering script extension: " .. k)
   _REGISTER_PATHS(
      "?." .. k,
      "?/init." .. k
   )
   extensions[k] = v
end

local function get_extensions()
   local out = {}
   for k in pairs(extensions) do
      table.insert(out, k)
   end
   return out
end

local function set_default_macro(mac)
   state.default = mac
end

package.loaded[_PACKAGE] = {
   PATTERN_FILE_PATH = PATTERN_FILE_PATH,
   PATTERN_MACRO_TAG = PATTERN_MACRO_TAG,
   compile = _COMPILER,
   register_extension = register_extension,
   get_extensions = get_extensions,
   set_default_macro = set_default_macro
}
