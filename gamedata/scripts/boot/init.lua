_PACKAGE = "boot"

-- Make print work
require("boot/print")

-- Setup loading machinery
require("boot/loader")

-- Pass control to modded exes entrypoint
require("amx")
