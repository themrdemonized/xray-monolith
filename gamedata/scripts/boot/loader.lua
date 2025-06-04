_PACKAGE = "boot/loader"

local state = {
   callbacks = {}
}

-- Define xray FS loader
function _LOADERS.fs(name)
   local fs = getFS()
   local errs = ""
   local base = fs:update_path("$game_scripts$", "")
   for seg in package.path:gmatch("[^;]+") do
      if seg:sub(1, #base) == base then
         local fname = seg:sub(#base + 1):gsub("?", name):gsub("/", "\\")
         local path = fs:update_path("$game_scripts$", fname)
         if path and fs:exist(path) then
            local src = _LOAD_FILE(path)
            return _COMPILER(src, name, path)
         end

         if #errs > 0 then
            errs = errs .. "\n\t"
         end
         errs = errs .. "No db entry: " .. path
      end
   end

   return errs
end

local loaders = { _LOADERS.fs }

-- Lift into a memoized higher-order loader
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
package.loaders = { _LOADERS.pre, io_loaders }

local function register_on_load_callback(f)
   table.insert(state.callbacks, f)
end

return {
   register_on_load_callback = register_on_load_callback
}
