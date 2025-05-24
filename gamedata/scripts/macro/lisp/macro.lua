local fennel = require("fennel")

local COMPILER_OPTS = {
   correlate = true,
   env = "_COMPILER",
   useBitLib = true,
   ["error-pinpoint"] = false,
}

local function compile(src, namespace_name)
   print("* lisp_macro: compiling " .. namespace_name)

   return function()
      local macros = fennel.eval(
         src,
         COMPILER_OPTS
      )

      if namespace_name then
         fennel["macro-loaded"][namespace_name] = macros
         package.loaded[namespace_name] = macros
      end
   end
end

package.loaded["macro/lisp/macro"] = {
   compile = compile
}
