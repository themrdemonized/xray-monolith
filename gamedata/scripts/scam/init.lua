--- Script Compilers And Macros
--- Racket-inspired language-oriented programming in Lua

-- Setup compiler
local compiler = require("scam/compiler")

-- Load initial languages
require("scam/lua")
require("scam/fennel")

-- Setup import machinery
require("scam/import")

return {
   compiler = compiler
}
