require("amx/sandbox")

local compiler = require("scam/compiler")
require("amx/unlocalize")
compiler.set_default_macro(require("amx/wua").expand)

package.loaded._G = nil
require("_G")

require("amx/classes")
require("amx/scripts")
