require("scam/fennel/macro")

local fennel = require("fennel")

local COMPILER_OPTS = {
   allowedGlobals = false,
   correlate = true,
   useBitLib = true,
   ["error-pinpoint"] = false,
}

local function make_compiler_opts(env, filename)
   local opts = {
      env = env,
      filename = filename,
   }
   for k,v in pairs(COMPILER_OPTS) do
      opts[k] = v
   end
   return opts
end

local function form(src)
   local _, form = assert(
      fennel.parser(src)()
   )
   return form
end

local function forms(src)
   local forms = {}
   for ok, form in fennel.parser(src) do
      assert(ok, "Invalid form")
      table.insert(forms, form)
   end
   return forms
end

local function list(lst)
   return form("[" .. table.concat(lst, " ") .. "]")
end

local function handle_error(msg)
   return function(err)
      err = "! " .. _PACKAGE .. ": "
         .. msg .. ":\n\n"
         .. fennel.traceback(err .. "\n", 2)
         .. "\n"
      print(err)
      error(err)
   end
end

local function compile(src, namespace_name)
   print("* " .. _PACKAGE .. ": compiling " .. namespace_name)

   local unlocs = require("amx/unlocalize").get(namespace_name)

   -- Compile the given source to Fennel AST
   local ast = forms(src)

   -- Define a symbol for our unlocalize callback
   local do_unloc_key = "_UNLOCAL"

   -- Define unlocalizer callback
   local unlocals = {}
   local do_unloc = function(k, f)
      unlocals[k] = f()
   end

   -- Determine if we want to unlocalize
   local want_unloc = unlocs and #unlocs > 0

   -- Late-load the unlocalize module to ensure it can compile
   local lisp_unlocalize = require("scam/fennel/unlocalize")

   -- If so, inject callback invocations for the given bindings
   if want_unloc then
      ast = lisp_unlocalize.unlocalize(
         form(do_unloc_key),
         list(unlocs),
         ast
      )
   end

   ast = lisp_unlocalize.wrap_do(ast)

   local env = setmetatable(
      {
         _PACKAGE = namespace_name,
         [do_unloc_key] = do_unloc,
      },
      {
         __index = _G,
         __newindex = _G,
      }
   )

   local _, lua = xpcall(
      function()
         return fennel.compile(
            ast,
            make_compiler_opts(env, namespace_name)
         )
      end,
      handle_error("error compiling " .. namespace_name)
   )

   local _, mod = xpcall(
      function()
         return fennel.loadCode(lua, env, namespace_name)
      end,
      handle_error("error loading " .. namespace_name)
   )

   return function()
      -- Evaluate our modified AST with the unlocalizer callback in scope
      local _, out = xpcall(
         mod,
         handle_error("error evaluating " .. namespace_name)
      )

      -- Ensure the script's output is unlocalizable
      local ty = type(out)
      if want_unloc and ty ~= "table" then
         handle_error(
            "cannot unlocalize "
            .. namespace_name
            .. ", script returned non-table: "
         )(tostring(out) .. "(" .. ty .. ")")
      end

      -- Load unlocalized variables into the resulting table
      for k,v in pairs(unlocals) do
         -- Consider already-present keys as more relevant than our unlocal
         if out[k] == nil then
            print(_PACKAGE .. ": unlocalized", k, v)
            out[k] = v
         end
      end

      return out
   end
end

require("scam/compiler").register_extension("fnl", compile)

return {
   compile = compile
}
