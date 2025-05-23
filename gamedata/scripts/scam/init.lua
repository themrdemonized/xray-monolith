print("Instigating S.C.A.M.")

require("scam/sandbox")

local compiler = require("scam/compiler")

require("macro")
require("scam/unlocalize")
require("macro/wua")

compiler.set_default_macro("macro/wua.expand")

require("_G")

require("scam/classes")
require("scam/scripts")
