local fennel = require("fennel")

local COMPILER_OPTS = {
   correlate = true,
   env = "_COMPILER",
   useBitLib = true,
   ["error-pinpoint"] = false,
}

local function handle_error(msg)
   return function(err)
      err = "! lisp_macro: "
         .. msg .. ":\n\n"
         .. fennel.traceback(err .. "\n", 3)
         .. "\n"
      print(err)
      error(err)
   end
end

local function compile(src, namespace_name)
   print("* lisp_macro: compiling " .. namespace_name)

   return function()
      local _, out = xpcall(
         function()
            return fennel.eval(
               src,
               COMPILER_OPTS
            )
         end,
         handle_error("error compiling" .. namespace_name)
      )

      if namespace_name then
         fennel["macro-loaded"][namespace_name] = out
      end

      return out
   end
end

return {
   compile = compile
}
