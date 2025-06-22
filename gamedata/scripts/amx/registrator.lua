-- AMX Class Registrator
-- Add custom classes for registration here

local function cs_register(factory, ...)
   factory:register(...)
end

local function c_register(factory, ...)
   if editor() == false then
      factory:register(...)
   end
end

local function s_register(factory, ...)
   factory:register(...)
end

local function register(object_factory)
   cs_register(
      object_factory,
      "CWeaponSSRS",
      "se_item.se_weapon_magazined",
      "_WP_SSRS",
      "wpn_ssrs_s"
   )
end

return {
   cs_register = cs_register,
   c_register = c_register,
   s_register = s_register,
   register = register,
}
