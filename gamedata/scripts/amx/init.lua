-- Anomaly Modded eXes Entrypoint
-- Extends base X-Ray script functionality

--- Setup unlocalizer data model
require(_PACKAGE .. ".unlocalize")

--- Patch xr/lua compiler with modded exes extensions
require(_PACKAGE .. ".lua")

--- Export registrator from module to allow foo.bar.baz syntax in script.ltx
return {
   registrator = require(_PACKAGE .. ".registrator")
}
