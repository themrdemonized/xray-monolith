_PACKAGE = "amx"
_FILE = "amx/init.lua"

-- Initialize S.C.A.M. environment
local scam = require("scam")

-- Load unlocalize before wua to avoid circular referencing
require("amx/unlocalize")

-- Setup wua as the default language
scam.compiler.set_default_macro(
   require("amx/wua").expand
)

-- Forcefully load _g.script
package.loaded._G = nil
require("_G")

-- Register classes
require("amx/classes")

-- Run common scripts
require("amx/scripts")
