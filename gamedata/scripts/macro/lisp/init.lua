local macro = require("macro")
local fennel = require("fennel")
local scam_unlocalize = require("scam/unlocalize")
local lisp_unlocalize = nil

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

local function eval_ast(ast, opts)
   local env = opts.env
   opts.env = nil

   return fennel.loadCode(
      fennel.compile(
         ast,
         opts
      ),
      env
   )()
end

local function compile(src, namespace_name)
   print("* lisp: compiling " .. namespace_name)

   local unlocs = scam_unlocalize.get(namespace_name)

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

   -- If so, inject callback invocations for the given bindings
   if want_unloc then
      ast = lisp_unlocalize.unlocalize(
         form(do_unloc_key),
         list(unlocs),
         ast
      )
   end

   ast = lisp_unlocalize.wrap_do(ast)

   return function()
      local env = {
         _PACKAGE = namespace_name,
         [do_unloc_key] = do_unloc,
      }

      -- Evaluate our modified AST with the unlocalizer callback in scope
      local out = eval_ast(ast, make_compiler_opts(macro.extend_env(env)))

      -- Ensure the script's output is unlocalizable
      if want_unloc and type(out) ~= "table" then
         assert(
            nil,
            string.format(
               "Cannot unlocalize: Script returned non-table: %s",
               out
            )
         )
      end

      -- Load unlocalized variables into the resulting table
      for k,v in pairs(unlocals) do
         -- Consider already-present keys as more relevant than our unlocal
         if out[k] == nil then
            print("Unlocalized:", k, v)
            out[k] = v
         end
      end

      if namespace_name then
         -- Load the final result as a package
         package.loaded[namespace_name] = out
      end
   end
end

require("scam/compiler").register_extension("fnl", compile)
lisp_unlocalize = require("macro/lisp/unlocalize")

package.loaded["macro/lisp"] = {
   compile = compile
}
