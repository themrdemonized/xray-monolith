-- Disable unsafe Lua primitives
require(_PACKAGE .. "/sandbox")

-- Emplace object lookup function
require(_PACKAGE .. "/function_object")

-- Setup S.C.A.M. and emplace Wua as the default language
require("scam").compiler.set_default_macro(
   require(_PACKAGE .. "/wua").expand
)

-- Forcefully load _g.script
package.loaded._G = nil
require("_G")

-- Register classes
require(_PACKAGE .. "/classes")

-- Run common scripts
require(_PACKAGE .. "/scripts")
