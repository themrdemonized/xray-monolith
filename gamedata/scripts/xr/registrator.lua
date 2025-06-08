-- Add custom classes to register here
local function cs_register(factory,client_object_class,server_object_class,clsid,script_clsid)
	factory:register(client_object_class,server_object_class,clsid,script_clsid)
end

local function c_register(factory,client_object_class,clsid,script_clsid)
	if (editor() == false) then
		factory:register(client_object_class,clsid,script_clsid)
	end
end

local function s_register(factory,server_object_class,clsid,script_clsid)
	factory:register(server_object_class,clsid,script_clsid)
end

local function register(object_factory)
	cs_register(object_factory, "CWeaponSSRS", "se_item.se_weapon_magazined", "_WP_SSRS", "wpn_ssrs_s")
end

return {
   cs_register = cs_register,
   c_register = c_register,
   s_register = s_register,
   register = register,
}
