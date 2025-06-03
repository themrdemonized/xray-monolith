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
   print("script:", script)
   local mod = require(script)
   if type(mod) ~= "table" then
      print("Error: " .. script .. " module is not a table")
      return
   end

   local init = mod[script .. "_initialize"]
   if not init then
      goto next_script
   end

   init()

   ::next_script::
end
