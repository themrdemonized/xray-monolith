--- Boot Kernel
--- Establishes a basic functioning X-Ray Lua environment

_PACKAGE = "boot"

-- Disable unsafe Lua primitives
require("boot/sandbox")

-- Make print work
require("boot/print")

-- Setup path machinery
require("boot/paths")

-- Setup loading machinery
require("boot/loader")

-- Setup engine interface
require("boot/function_object")
