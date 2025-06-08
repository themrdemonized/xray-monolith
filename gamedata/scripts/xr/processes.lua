-- Engine interface to domain-scoped coroutines
-- Formerly part of CScriptManager

local ScriptProcess = require("xr/process")

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

-- Prepare module output
local processes = ScriptProcesses.new()

-- Game process
local ini_script = ini_file("configs\\script.ltx")

local game_scripts = ""
if ini_script:section_exist("single")
   and ini_script:line_exist("single", "script")
then
   game_scripts = ini_script:r_string("single", "script");
end

processes:add("game", game_scripts)

-- Level process
if level.present() then
   local ini_level = ini_file(
      string.format("levels\\%s\\level.ltx", level.name())
   )

   local level_scripts = ""
   if ini_level:section_exist("level_scripts")
      and ini_level:line_exist("level_scripts", "script")
   then
      level_scripts = ini_level:r_string("level_scripts", "script");
   end

   processes:add("level", level_scripts)
end

-- Return module output
return processes
