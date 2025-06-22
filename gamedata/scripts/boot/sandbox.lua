-- Boot Sandbox
-- Disables dangerous Lua primitives

-- Map from package name to dangerous primitives
local disabled = {
   os = {
      "execute",
      "rename",
      "remove",
      "exit",
   },
   io = {
      "popen"
   }
}

-- Iterate disabled map and nil corresponding primitives
for k,v in pairs(disabled) do
   for i=1,#v do
      _G[k][v[i]] = nil
   end
end
