require("amx/sandbox")

local compiler = require("scam/compiler")

require("amx/unlocalize")
require("amx/wua")
compiler.set_default_macro("amx/wua.expand")

require("_G")

require("amx/classes")
require("amx/scripts")
