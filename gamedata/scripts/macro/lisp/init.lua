local scam_unlocalize = require("scam/unlocalize")
local lisp_unlocalize = require("macro/lisp/unlocalize")

COMPILER_OPTS = {
   allowedGlobals = false,
   correlate = true,
   useBitLib = true,
   ["error-pinpoint"] = false,
}

function make_compiler_opts(env)
   local opts = { env = env }
   for k,v in pairs(COMPILER_OPTS) do
      opts[k] = v
   end
   return opts
end

function fennel_form(src)
   local _, form = assert(
      fennel.parser(src)()
   )
   return form
end

function fennel_forms(src)
   local forms = {}
   for ok, form in fennel.parser(src) do
      assert(ok, "Invalid form")
      table.insert(forms, form)
   end
   return forms
end

function fennel_list(lst)
   return fennel_form("[" .. table.concat(lst, " ") .. "]")
end

function fennel_eval_ast(ast, opts)
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

function compile(src, namespace_name)
   print("lisp: compiling " .. namespace_name)

   local unlocs = scam_unlocalize.get(namespace_name)

   return function()
      local ast = fennel_forms(src)

      if #unlocs then
         ast = lisp_unlocalize.unlocalize(
            fennel_list(unlocs),
            unpack(ast)
         )
      end

      package.loaded[namespace_name] = fennel_eval_ast(
         ast,
         make_compiler_opts(
            macro.extend_env {
               script_name = function()
                  return namespace_name
               end
            }
         )
      )
   end
end
