-- A collection of domain-scoped coroutines,
-- with time-sliced update logic to process one per frame.
--
-- Formerly CScriptProcess and CScriptThread

local DEBUG = false
local DISABLE_SCRIPTS = false

-- Script process class
ScriptProcess = {}

-- Constructor
function ScriptProcess.new(name, scripts)
   if DEBUG then
      print("* Initializing " .. name .. " script process")
   end

   local out = setmetatable(
      {
         name = name,
         coroutines = {},
         iterator = 0,
      },
      {
         __index = ScriptProcess,
         __tostring = function(self)
            return string.format(
               "ScriptProcess {"
               .. "\n   name = " .. self.name
               .. "\n   coroutines = " .. tostring(self.coroutines)
               .. "\n   iterator = " .. tostring(self.iterator)
               .. "\n}"
            )
         end
      }
   )

   for script in scripts:gmatch("[^;]+") do
      out:add_script(script)
   end

   return out
end

-- Update entrypoint, called by the engine
function ScriptProcess:update()
   if DISABLE_SCRIPTS then
      while #self.coroutines > 0 do
         table.remove(self.coroutines)
      end
   end

   if #self.coroutines == 0 then
      self.iterator = 0
      return
   end

   local co = self.coroutines[self.iterator + 1]

   if not co then
      return
   end

   local res, err = coroutine.resume(co)

   if res then
      self.iterator = (self.iterator + 1) % #self.coroutines
   else
      if err ~= "cannot resume dead coroutine" then
         print("! ScriptProcess: " .. err)
      end
      table.remove(self.coroutines, self.iterator + 1)
   end
end

-- Add a raw coroutine to the process
function ScriptProcess:add_coroutine(co)
   if DEBUG then
      print(
         "* Adding coroutine " .. tostring(co)
         .. " to " .. self.name
         .. " script process"
      )
   end

   table.insert(self.coroutines, co)
end

-- Add a function to the process as a coroutine
function ScriptProcess:add_function(f)
   if DEBUG then
      print(
         "* Adding function " .. tostring(f)
         .. " to " .. self.name
         .. " script process"
      )
   end

   self:add_coroutine(coroutine.create(f))
end

-- Add a string of source code to the script process
function ScriptProcess:add_string(src)
   if DEBUG then
      print(
         "* Adding string ".. src .. " to " .. self.name .. " script process"
      )
   end

   self:add_function(loadstring(src, nil, "console command"))
end

-- Add a package's main function to the process by name
-- Optionally force-reloading it
function ScriptProcess:add_script(script_name, reload)
   if DEBUG then
      print("* Adding script ".. script_name .. " to " .. self.name .. " script process")
   end

   self:add_function(
      function()
         if reload then
            package.loaded[script_name] = nil
         end

         local res = require(script_name)

         if type(res) ~= "table" then
            return
         end

         local main = res.main
         if type(main) ~= "function" then
            return
         end

         main()
      end
   )
end

-- Return class as package value
return ScriptProcess
