local extensions = {}

local old_compiler = _COMPILER
function _COMPILER(src, script_name, namespace_name)
   local default = nil

   local _,_,ext = script_name:match("^(.-)([^\\/]-)%.([^\\/%.]-)%.?$")
   local mac = extensions[ext]
   if mac then
      default = mac
   end

   local tag,rest = src:match("[^ ]+ +=%*= +lang: +([^ ]+) +=%*=[^\n]*(\n.*)")
   if tag ~= nil then
      local f = function_object(tag)
      return f(rest, namespace_name, script_name)
   end

   if default then
      return default(src, namespace_name, script_name)
   end

   return old_compiler(src, script_name, namespace_name)
end

local function register_extension(k, v)
   _REGISTER_PATHS(
      "?." .. k,
      "?/init." .. k
   )
   extensions[k] = v
end

package.loaded["scam/compiler"] = {
   compile = _COMPILER,
   register_extension = register_extension
}
