TAG_MACRO = "#macro "

local state = {
   default_macro = nil
}

function set_default_macro(mac)
   state.default_macro = mac
end

local old_compiler = _COMPILER
function _COMPILER(src, script_name, namespace_name)
   if string.sub(src, 1, #TAG_MACRO) == TAG_MACRO then
      src = string.sub(src, #TAG_MACRO + 1)
      local tag, rest = string.match(src, "([^%s]+)(%s+.*)")
      local path = "macro/" .. tag
      return function_object(path)(rest, namespace_name)
   elseif state.default_macro then
      return function_object(state.default_macro)(src, namespace_name)
   end

   return old_compiler(src, script_name, namespace_name)
end

package.loaded["scam/compiler"] = {
   compile = _COMPILER,
   set_default_macro = set_default_macro
}
