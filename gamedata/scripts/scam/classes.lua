local ini = ini_file("script.ltx")
if not ini then
    return
end

if not ini:section_exist("common") then
   error("Missing common section")
end

if not ini:line_exist("common", "class_registrators") then
   error("Missing class_registrators line")
end


local fac = get_object_factory()

local regs = ini:r_string("common", "class_registrators", "")
for reg_path in regs:gmatch("[^,]+") do
   local reg = function_object(reg_path)
   reg(fac)
end

fac:register_script()
