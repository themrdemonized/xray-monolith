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
      local res, out = pcall(
         fennel.eval,
         src,
         COMPILER_OPTS
      )

      if not res then
         local err = "! lisp_macro: error compiling " .. namespace_name .. ":\n"
                     .. out
         print(err)
         error(err)
      end

      if namespace_name then
         fennel["macro-loaded"][namespace_name] = out
         package.loaded[namespace_name] = out
      end

      return out
   end
end

return {
   compile = compile
}
