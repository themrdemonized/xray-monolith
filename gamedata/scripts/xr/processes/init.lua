-- Engine interface to domain-scoped coroutines
-- Formerly part of CScriptManager

local ScriptProcess = require("xr/processes/process")

-- Script processes class
local ScriptProcesses = {}

-- Constructor
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

-- Add a script process by name, with a set of scripts to add by default
function ScriptProcesses:add(name, scripts)
   self.processes[name] = ScriptProcess.new(name, scripts)
end

-- Remove a script process by name
function ScriptProcesses:remove(name)
   self.processes[name] = nil
end

-- Test the existence of a script process by name
function ScriptProcesses:has(name)
   return self.processes[name] ~= nil
end

-- Get a script process by name
function ScriptProcesses:get(name)
   return self.processes[name]
end

-- Prepare package output
local processes = ScriptProcesses.new()

-- Setup game process
do
   local ini_script = ini_file("configs\\script.ltx")

   local game_scripts = ""
   if ini_script:section_exist("single")
      and ini_script:line_exist("single", "script")
   then
      game_scripts = ini_script:r_string("single", "script");
   end

   processes:add("game", game_scripts)
end

-- If a level exists...
if level.present() then
   -- Setup level process
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

-- Return package output
return processes
