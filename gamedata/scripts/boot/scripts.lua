-- Boot Scripts
-- Loads scripts specified in script.ltx

local DISABLE_SCRIPTS = false

if DISABLE_SCRIPTS then
   return
end

-- Open script.ltx
local ini = ini_file("script.ltx")
if not ini then
   return
end

-- Check for the common section
if not ini:section_exist("common") then
   error("Missing common section")
end

-- Check for the script list
if not ini:line_exist("common", "script") then
   error("Missing script line")
end

-- Read the script list
local scripts = ini:r_string("common", "script", "")

-- Iterate scripts, requiring each one and optionally calling an init function
for script in scripts:gmatch("[^,]+") do
   local mod = require(script)
   if type(mod) == "table" then
      local init = mod[script .. "_initialize"]
      if init then
         init()
      end
   end
end
