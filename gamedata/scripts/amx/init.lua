_PACKAGE = "amx"
_FILE = "amx/init.lua"

-- Setup S.C.A.M. environment
local scam = require("scam")

-- Setup wua as the default language
scam.compiler.set_default_macro(
   import("wua").expand
)

-- Forcefully load _g.script
package.loaded._G = nil
import("/_G")

-- Register classes
import("classes")

-- Run common scripts
import("scripts")
