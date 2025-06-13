-- Engine interface
-- Behaves like _G-aware `require` with recursive . indexing

function function_object(str)
   local path = {}
   for v in string.gmatch(str, "[^%.]+") do
      v = v:gsub("/", ".")
      table.insert(path, v)
   end

   local mod_name = table.remove(path, 1)

   local mod = nil
   if mod_name == "_G" then
      mod = _G
   elseif _G[mod_name] then
      mod = _G[mod_name]
   else
      mod = require(mod_name)
   end

   local val = mod
   for _, seg in ipairs(path) do
      val = val[seg]
   end

   return val
end
