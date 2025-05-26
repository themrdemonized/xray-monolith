print("Instigating S.C.A.M.")

require("scam/sandbox")

local compiler = require("scam/compiler")
require("macro")
compiler.set_default_macro(require("macro/wua").expand)

require("_G")

require("scam/classes")
require("scam/scripts")

return {}
