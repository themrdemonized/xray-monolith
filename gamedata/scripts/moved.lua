-- Old path -> new path map
--
-- Used to make sure old .script files are still able to see moved scripts.
--
-- Preferable to only populate scripts with global-scope members,
-- or with local-scope members that may be subject to unlocalization.
--
-- For instance: If a script only contains monkey-patches to existing functions,
-- it doesn't need to be remapped.

local remap = {
   callbacks_gameobject = "patches/boot/_g/callbacks_gameobject",
   axr_beh_patches = "patches/axr_beh/ghentuong",
   class_registrator_modded_exes = "amx/registrator",
   dxml_core = "dxml",
   fakelens = "2d_scopes/fakelens",
   modxml_inject_keybinds = "dxml/inject_keybinds",
   modxml_test = "dxml/test",
}

return {
   remap = remap
}
