-- XR Class Registration

-- Open script.ltx
local ini = ini_file("script.ltx")
if not ini then
    return
end

-- Check for the `common` section
if not ini:section_exist("common") then
   error("Missing common section")
end

-- Check for class registrators
if not ini:line_exist("common", "class_registrators") then
   error("Missing class_registrators line")
end

-- Read class registrators
local regs = ini:r_string("common", "class_registrators", "")

-- Iterate, load from environment, and invoke each with the object factory
for reg_path in regs:gmatch("[^,]+") do
   local reg = function_object(reg_path)
   reg(_OBJECT_FACTORY)
end

-- Invoke object factory registration method
_OBJECT_FACTORY:register_script()

