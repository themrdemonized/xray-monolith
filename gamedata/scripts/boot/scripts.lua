-- Boot Scripts
-- Loads scripts specified in script.ltx

local DISABLE_SCRIPTS = false

if DISABLE_SCRIPTS then
   return
end

local ini = ini_file("script.ltx")
if not ini then
   return
end

if not ini:section_exist("common") then
   error("Missing common section")
end

if not ini:line_exist("common", "script") then
   error("Missing script line")
end

local scripts = ini:r_string("common", "script", "")

for script in scripts:gmatch("[^,]+") do
   print("requiring " .. script)
   local mod = require(script)
   if type(mod) == "table" then
      local init = mod[script .. "_initialize"]
      if init then
         init()
      end
   end
end
