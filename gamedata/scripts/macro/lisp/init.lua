require(_PACKAGE .. "/macro")

local fennel = require("fennel")

local COMPILER_OPTS = {
   allowedGlobals = false,
   correlate = true,
   useBitLib = true,
   ["error-pinpoint"] = false,
}

local function make_compiler_opts(env)
   local opts = { env = env }
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

local function eval_ast(ast, opts, namespace_name)
   local env = opts.env
   opts.env = nil

   return fennel.loadCode(
      fennel.compile(
         ast,
         opts
      ),
      env,
      namespace_name
   )()
end

local function compile(src, namespace_name)
   print("* lisp: compiling " .. namespace_name)

   local unlocs = require("scam/unlocalize").get(namespace_name)

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
   local lisp_unlocalize = require(_PACKAGE .. "/unlocalize")

   -- If so, inject callback invocations for the given bindings
   if want_unloc then
      ast = lisp_unlocalize.unlocalize(
         form(do_unloc_key),
         list(unlocs),
         ast
      )
   end

   ast = lisp_unlocalize.wrap_do(ast)

   local env = require("macro").extend_env({
      _PACKAGE = namespace_name,
      [do_unloc_key] = do_unloc,
   })

   local compiled, lua = pcall(
      fennel.compile,
      ast,
      make_compiler_opts(env)
   )
   if not compiled then
      lua = "! lisp: error compiling " .. namespace_name .. ":\n\n"
           .. lua .. "\n"
      error(lua)
   end

   local loaded, mod = pcall(fennel.loadCode, lua, env, namespace_name)
   if not loaded then
      mod = "! lisp: error loading " .. namespace_name .. ":\n\n"
           .. mod .. "\n"
      error(mod)
   end

   return function()
      -- Evaluate our modified AST with the unlocalizer callback in scope
      local evaluated, out = pcall(mod)
      if not evaluated then
         out = "! lisp: error evaluating " .. namespace_name .. ":\n\n"
            .. out .. "\n"
         print(out)
         error(out)
      end

      -- Ensure the script's output is unlocalizable
      local ty = type(out)
      if want_unloc and ty ~= "table" then
         local err = "! lisp: cannot unlocalize, script returned non-table: "
            .. tostring(out) .. "(" .. ty .. ")"
         print(err)
         error(err)
      end

      -- Load unlocalized variables into the resulting table
      for k,v in pairs(unlocals) do
         -- Consider already-present keys as more relevant than our unlocal
         if out[k] == nil then
            print("lisp: unlocalized", k, v)
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
