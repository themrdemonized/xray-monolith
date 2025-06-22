--- Lua Entrypoint
--- Called by CScriptEngine at the end of Lua initialization

_PACKAGE = "init"

--- Store default Lua loaders for later
_LOADERS = {
   pre = package.loaders[1],
   lib = package.loaders[2],
   bin = package.loaders[3],
   aio = package.loaders[4],
}

-- Emplace working print function
function print(...)
   local str = ""
   for _,v in ipairs({...}) do
      if #str > 0 then
         str = str .. " "
      end

      local s = nil
      if (type(v) == 'userdata') then
         s = 'userdata'
      else
         s = tostring(v)
      end

      str = str .. s
   end

   if (log) then
      log(str)
   else
      get_console():execute("load ~#debug msg:" .. str)
   end
end

--- Customizable environment for .lua files
--- Needed so .script files can propagate their extended _G when requiring .lua
_LUA_G = _G

--- Emplace an error-checked lua compiler with customizable environment
_LOADSTRING = loadstring
function loadstring(src, namespace_name, script_name)
   print("* [lua] loading " .. namespace_name)

   local f, err = _LOADSTRING(src, namespace_name)
   if not f then
      err = "init: error loading " .. namespace_name .. ":\n\n"
               .. err .. "\n"
      err = debug.traceback(err, 2)
      print(err)
      return nil, err
   end

   local mac = setfenv(
      f,
      setmetatable(
         {
            _PACKAGE = namespace_name,
            _FILE = script_name,
         },
         {
            __index = _LUA_G,
            __newindex = _LUA_G,
         }
      )
   )

   return function(...)
      local args = {...}

      local _, out = xpcall(
         function()
            return mac(unpack(args))
         end,
         function(err)
            err = debug.traceback(err, 2)
            print(err)
            error(err)
         end
      )

      return out
   end
end

-- Redirect load through loadstring
load = function(f, name)
   local src = ""

   while true do
      local part = f()
      if part == nil then
         break
      elseif type(part == "string") then
         if #part == 0 then
            break
         end

         src = src .. part
      end
   end

   return loadstring(src, name)
end

-- Redirect loadfile through loadstring
loadfile = function(path)
   local file = io.input(path)
   local src = file:read("*a")
   file:close()
   return loadstring(src)
end


--- Emplace minimal X-Ray FS loader
function _LOADERS.init(name)
   local fs = getFS()
   local sname = name:gsub("%.", "\\")
   local path = fs:update_path("$game_scripts$", sname)
   if not path then
      return "\n\tInvalid path " .. path
   end

   if fs:exist(path .. ".lua") then
      path = path .. ".lua"
   elseif
      fs:exist(path .. "\\init.lua") then
      path = path .. "\\init.lua"
   else
      return "\n\tNo such package: " .. name
   end

   local res, err = loadstring(_LOAD_FILE(path), name, path)
   if res then
      return res
   end

   return err
end

package.loaders = { _LOADERS.init }

--- Hand control to the boot module
require("boot")
