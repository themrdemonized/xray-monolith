--- Boot Kernel
--- Establishes a basic functioning X-Ray Lua environment

_PACKAGE = "boot"

-- Disable unsafe Lua primitives
require("boot/sandbox")

-- Make print work
require("boot/print")

-- Setup loading machinery
require("boot/loader")

-- Setup path machinery
require("boot/paths")

-- Setup engine interface
require("boot/function_object")
