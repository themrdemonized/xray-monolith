local run_tests = require("tests.util").run_tests
local unlocalize_with = require("amx.lua.unlocalize").unlocalize_with

local SRC = {
   declare_function = [[
local function x(a, b, c) -- define a function
   return a, b, c
end
   ]],

   declare_var_single = [[
local a -- declare a single var
   ]],

   define_var_single = [[
local a = 1 -- define a single var
   ]],
   declare_var_multi = [[
local a, b, c -- declare multiple vars
   ]],
   define_var_multi = [[
local a, b, c = 1, 2, 3 -- define multiple vars
   ]],
   declare_var_single_sequence = [[
local a; local b; local c; -- declare a sequence of vars
   ]],
   define_var_single_sequence = [[
local a = 1; local b = 2; local c = 3; define a sequence of vars
   ]],
}

local PASSES = {
   declare_function = [[
function x(a, b, c) -- define a function
   return a, b, c
end
   ]],

   declare_var_single = [[
a = nil -- declare a single var
   ]],

   define_var_single = [[
a = 1 -- define a single var
   ]],
   declare_var_multi = [[
a, b, c = nil -- declare multiple vars
   ]],
   define_var_multi = [[
a, b, c = 1, 2, 3 -- define multiple vars
   ]],
}

local FAILS = {
   declare_var_single_sequence = [[
a = nil; b = nil; c = nil; -- declare a sequence of vars
   ]],
   define_var_single_sequence = [[
a = 1; b = 2; c = 3; -- define a sequence of vars
   ]],
}

local unlocalizer = {"x", "a", "b", "c"}

local tests = {}

local ERR_MISMATCHED =
[[Mismatched unlocalizer output
src:
%s
unlocalized:
%s
target:
%s]]

for k,targ in pairs(PASSES) do
   tests[k] = function()
      local src = SRC[k]
      local res = unlocalize_with(src, unlocalizer)

      if res == targ then
         return true
      else
         return false, string.format(ERR_MISMATCHED, v, res, targ)
      end
   end
end

local ERR_UNEXPECTED =
[[Unexpected unlocalizer output
src:
%s
unlocalized:
%s
target:
%s]]

for k,targ in pairs(FAILS) do
   tests[k .. " (unsupported)"] = function()
      local src = SRC[k]
      local res = unlocalize_with(src, unlocalizer)
      if res ~= targ then
         return true
      else
         return false, string.format(ERR_UNEXPECTED, src, res, targ)
      end
   end
end

run_tests("amx/lua", tests)
