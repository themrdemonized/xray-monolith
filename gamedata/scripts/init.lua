package.loaded._G = nil

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

-- Setup script load paths
local scripts_path = getFS():update_path("$game_scripts$", ""):gsub("\\", "/")
local paths = {
   "?.script",
   "?/init.script",
   "?.lua",
   "?/init.lua",
   "?.fnl",
   "?/init.fnl",
}

for i=#paths,1,-1 do
   local path = paths[i]
   package.path = scripts_path .. path .. ";" .. package.path
end

-- Define *.db reader
function read_db(name)
   local fs = getFS()
   local fname = name:gsub("/", "\\") .. ".script"
   local path = fs:update_path("$game_scripts$", fname)
   local file = fs:r_open(path)

   if not file then
      return nil, nil, "No db entry: gamedata/scripts/" .. fname
   end

   local src = ""
   while not file:r_eof() do
      src = src .. string.char(file:r_u8())
   end

   return src, path
end

-- Define IO reader
function read_io(name)
   local errs = ""
   for seg in package.path:gmatch("[^;]+") do
      local path = seg:gsub("?", name)

      local file, err = io.open(path)
      if file == nil then
         if #errs > 0 then
            errs = errs .. "\n"
         end
         errs = errs .. err
         goto next_seg
      end

      local src = file:read("*a")
      file:close()

      if src then
         return src, path
      end

      ::next_seg::
   end

   return nil, nil, errs
end

function _COMPILER(src, script_name, namespace_name)
   print("* init: loading " .. namespace_name)
   return loadstring(src, namespace_name)
end

-- Define reader -> loader transformer
function loader(with)
   return function(name)
      local src, path, err = with(name)
      if not src then
         return "\n\t" .. err
      end

      local mod = _COMPILER(src, path, name)

      if mod then
         return mod
      end

      return "\n\tFailed to compile " .. name
   end
end

-- Define the set of readers to register as loaders
local readers = {
   read_io,
   read_db
}

-- Pop the preloader off the loader list
local preload_loader = table.remove(package.loaders, 1)

-- Pop the default textual script loader
table.remove(package.loaders, 1)

-- Emplace new loaders for filesystem and db
for i=#readers,1,-1 do
   table.insert(package.loaders, 1, loader(readers[i]))
end


-- Lift into a memoized higher-order loader
local loaders = package.loaders
local io_miss = {}
local function io_loader(name)
   local io_miss = _SCRIPT_STORAGE:get("io_loader", name)
   if io_miss then
      return io_miss
   end

   local err = ""
   for i=1,#loaders do
      local res = loaders[i](name)

      local ty = type(res)
      if ty == "function" then
         return res
      else
         if #err > 0 then
            err = err .. "\n"
         end
         if ty == "string" then
            err = err .. res
         else
            err = err .. "Loader returned invalid type: " .. ty
         end
      end
   end

   _SCRIPT_STORAGE:set("io_loader", name, err)
   return err
end

-- Replace the loader list with the preloader plus our memoized IO loader
package.loaders = { preload_loader, io_loader }

-- Extend require with path support
function function_object(str)
   local path = {}
   for v in string.gmatch(str, "[^%.]+") do
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

-- Pass control to amx entrypoint
require("amx")
