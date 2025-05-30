-- Old path -> new path map
--
-- Used to make sure old .script files are still able to see moved scripts.
--
-- Preferable to only populate scripts with global-scope members,
-- or with local-scope members that may be subject to unlocalization.
--
-- For instance: If a script only contains monkey-patches to existing functions,
-- it doesn't need to be remapped.
--
-- Table keys:
--                 `to` - Defines the script's new path

-- `if_not_overwritten` - Does not remap if a copy of the original still exists
--                        used to account for cases like `scopeRadii` where
--                        the script is designed to be overridden by mods

local remap = {
   callbacks_gameobject = {
      to = "patches/boot/_g/callbacks_gameobject",
   },
   axr_beh_patches = {
      to = "patches/axr_beh/ghentuong",
   },
   class_registrator_modded_exes = {
      to = "amx/registrator",
   },
   dxml_core = {
      to = "dxml",
   },
   fakelens = {
      to = "2d_scopes/fakelens",
   },
   modxml_inject_keybinds = {
      to = "dxml/inject_keybinds",
   },
   modxml_test = {
      to = "dxml/test",
   },
   scopeRadii = {
      to = "2d_scopes/scope_radii",
      if_not_overwritten = true
   },
   slaxml = {
      to = "packages/lib/slaxml"
   }
}

return {
   remap = remap
}
