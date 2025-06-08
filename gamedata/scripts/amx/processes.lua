-- Engine interface to domain-scoped coroutines
-- Formerly part of CScriptManager

local ScriptProcess = require("amx/process")

local ScriptProcesses = {}

function ScriptProcesses.new()
   return setmetatable(
      {
         processes = {}
      },
      {
         __index = ScriptProcesses,
         __tostring = function(self)
            return "ScriptProcesses {"
               .. "\n   processes: " .. tostring(self.processes)
               .. "\n}"
         end
      }
   )
end

function ScriptProcesses:add(name, scripts)
   self.processes[name] = ScriptProcess.new(name, scripts)
end

function ScriptProcesses:remove(name)
   self.processes[name] = nil
end

function ScriptProcesses:has(name)
   return self.processes[name] ~= nil
end

function ScriptProcesses:get(name)
   return self.processes[name]
end

return ScriptProcesses.new()
