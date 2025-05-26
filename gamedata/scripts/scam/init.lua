print("Instigating S.C.A.M.")

require("scam/sandbox")

local compiler = require("scam/compiler")
require("macro")

local wua = require("macro/wua").expand
print("setting default macro to wua:", wua)
compiler.set_default_macro(wua)

require("_G")

require("scam/classes")
require("scam/scripts")

package.loaded["scam"] = {}
