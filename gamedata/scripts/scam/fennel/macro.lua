local fennel = require("fennel")

local COMPILER_OPTS = {
   correlate = true,
   env = "_COMPILER",
   useBitLib = true,
   ["error-pinpoint"] = false,
}

local function handle_error(msg, namespace_name)
   return function(err)
      package.loaded[namespace_name] = nil
      err = "! " .. _PACKAGE .. ": "
         .. msg .. ":\n\n"
         .. fennel.traceback(err .. "\n", 3)
         .. "\n"
      print(err)
      error(err)
   end
end

local function compile(src, namespace_name)
   print("* " .. _PACKAGE .. ": compiling " .. namespace_name)

   return function()
      local _, out = xpcall(
         function()
            package.loaded[namespace_name] = {}
            return fennel.eval(
               src,
               COMPILER_OPTS
            )
         end,
         handle_error("error compiling" .. namespace_name, namespace_name)
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
