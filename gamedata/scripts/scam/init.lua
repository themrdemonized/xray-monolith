print("Instigating S.C.A.M.")

local compiler = require("scam/compiler")

require("macro/wua")

compiler.set_default_macro("macro/wua.expand")

require("_G")

require("scam/classes")
require("scam/scripts")
