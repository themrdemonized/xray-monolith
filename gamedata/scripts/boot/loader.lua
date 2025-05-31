_PACKAGE = "boot/loader"

local state = {
   callbacks = {}
}

-- Emplace boot-time passthrough compiler
function _COMPILER(src, namespace_name)
   print("* " .. _PACKAGE .. ": loading", namespace_name)

   local out, err = loadstring(src, namespace_name)

   if not out then
      error(
         "! init: error loading " .. namespace_name .. ":\n"
         .. err
      )
   end

   return setfenv(
      out,
      setmetatable(
         { _PACKAGE = namespace_name },
         {
            __index = _G,
            __newindex = _G,
         }
      )
   )
end

-- Define xray script reader
local function read_fs(name)
   local fs = getFS()

   local errs = ""
   local base = fs:update_path("$game_scripts$", "")
   for seg in package.path:gmatch("[^;]+") do
      if seg:sub(1, #base) == base then
         local fname = seg:sub(#base + 1):gsub("?", name):gsub("/", "\\")
         local path = fs:update_path("$game_scripts$", fname)
         if path and fs:exist(path) then
            local file = fs:r_open(path)
            if file then
               local size = file:r_elapsed()
               local src = file:r_stringZ():sub(1, size)
               return src, path
            end
         end

         if #errs > 0 then
            errs = errs .. "\n\t"
         end
         errs = errs .. "No db entry: " .. path
      end
   end

   return nil, nil, errs
end

-- Define reader -> loader transformer
local function loader(with)
   return function(name)
      local src, path, err = with(name)
      if not src then
         return err
      end

      local res, out = pcall(_COMPILER, src, name, path)
      if not res then
         print(out)
         error(out)
      end

      return out
   end
end

-- Define the set of readers to register as loaders
local readers = {
   read_fs
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
local function io_loaders(name)
   local io_miss = _SCRIPT_STORAGE:get("io_loader", name)
   if io_miss then
      return io_miss
   end

   local err = ""
   for i=1,#loaders do
      local out = loaders[i](name)

      local ty = type(out)
      if ty == "function" then
         local already_loaded = package.loaded[name]
         local res = out()
         package.loaded[name] = res
         if already_loaded == nil then
            for _,f in ipairs(state.callbacks) do
               f(name)
            end
         end
         return function()
            package.loaded[name] = res
            return res
         end
      else
         package.loaded[name] = nil
         if #err > 0 then
            err = err .. "\n"
         end
         if ty == "string" then
            err = err .. out
         elseif ty == "nil" then
            error("No such module: " .. name)
         else
            error("Loader returned invalid value: " .. tostring(out))
         end
      end
   end

   _SCRIPT_STORAGE:set("io_loader", name, err)
   return err
end

-- Replace the loader list with the preloader plus our memoized IO loader
package.loaders = { preload_loader, io_loaders }

local function register_on_load_callback(f)
   table.insert(state.callbacks, f)
end

return {
   register_on_load_callback = register_on_load_callback
}
