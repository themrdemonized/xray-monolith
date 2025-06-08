-- Boot Sandbox
-- Disables dangerous Lua primitives

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

for k,v in pairs(disabled) do
   for i=1,#v do
      _G[k][v[i]] = nil
   end
end
