print("Instigating S.C.A.M.")

local compiler = require(_PACKAGE .. "/compiler")

return {
   compiler = compiler,
   lua = require(_PACKAGE .. "/lua"),
   lisp = require(_PACKAGE .. "/fennel"),
}
