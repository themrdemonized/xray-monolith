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
         .. debug.traceback(err .. "\n", 2)
         .. "\n"
      print(err)
      error(err)
   end
end

local function compile(src, namespace_name)
   print("* lisp_macro: compiling " .. namespace_name)

   return function()
      local res, out = pcall(
         fennel.eval,
         src,
         COMPILER_OPTS
      )

      if not res then
         handle_error("error compiling" .. namespace_name)(out)
      end

      if namespace_name then
         fennel["macro-loaded"][namespace_name] = out
      end

      return out
   end
end

return {
   compile = compile
}
