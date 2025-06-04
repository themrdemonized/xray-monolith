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

-- Set to true if we're unwinding the stack following an error
_UNWIND = false

--- Emplace minimal compiler
function _COMPILER(src, namespace_name)
   print("* init: loading " .. namespace_name)
   return setfenv(
      function(...)
         local f, err = loadstring(src, namespace_name)
         if not f then
            local err = "init: error loading " .. namespace_name .. ":\n\n"
                     .. err .. "\n"
            if not _UNWIND then
               print(debug.traceback(err, 2))
               _UNWIND = true
            end
            error(err)
         end

         local res, out = pcall(f, ...)
         if not res then
            if not _UNWIND then
               print(debug.traceback(out, 2))
               _UNWIND = true
            end
            error(out)
         end

         return out
      end,
      setmetatable(
         { _PACKAGE = namespace_name },
         {
            __index = _G,
            __newindex = _G,
         }
      )
   )
end

--- Emplace minimal X-Ray FS loader
function _LOADERS.init(name)
   local fs = getFS()
   local path = fs:update_path("$game_scripts$", name:gsub("/", "\\"))
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

   local res, out = pcall(_COMPILER, _LOAD_FILE(path), name, path)
   if not res then
      print(out)
      error(out)
   end
   return out
end

package.loaders = { _LOADERS.init }

--- Initialize environment via the boot module
require("boot")

-- Pass control to modded exes entrypoint
require("amx")
