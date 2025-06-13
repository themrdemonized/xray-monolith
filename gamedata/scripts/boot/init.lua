--- Boot Kernel
--- Establishes a functioning X-Ray Lua environment

_PACKAGE = "boot"

-- Disable unsafe Lua primitives
require("boot.sandbox")

-- Setup package.path machinery
require("boot.paths")

-- Setup package.loaders machinery
require("boot.loader")

-- Setup engine interface
require("boot.function_object")

-- Ensure _G loads on first require
package.loaded._G = nil

-- Run startup modules defined in script.ltx
require("boot.scripts")
