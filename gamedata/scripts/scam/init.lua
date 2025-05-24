print("Instigating S.C.A.M.")

require("scam/sandbox")

require("macro")
require("scam/unlocalize")
require("scam/compiler")
require("macro/lua")
require("macro/lisp")
require("macro/wua")

require("_G")

require("scam/classes")
require("scam/scripts")

package.loaded["scam"] = {}
