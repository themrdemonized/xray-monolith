-- Boot Loader
-- Configures package.loaders with support for X-Ray FS

_PACKAGE = "boot/loader"

local state = {
   callbacks = {}
}

-- X-Ray FS Loader
function _LOADERS.fs(name)
   -- Check whether the target is known to not exist on the FS
   local io_miss = _SCRIPT_STORAGE:get("io_loader", name)
   if io_miss then
      -- If so, early out
      return io_miss
   end

   -- Allocate error storage
   local errs = ""

   -- Fetch filesystem handle
   local fs = getFS()

   -- Update the FS' view of our script directory
   local base = fs:update_path("$game_scripts$", "")

   -- Iterate over our search paths
   for seg in package.path:gmatch("[^;]+") do
      -- If the segment begins with our base directory
      if seg:sub(1, #base) == base then
         -- Strip the base path, interpolate package name,
         -- and replace separators to produce a filename
         local sname = name:gsub("%.", "\\")
         local fname = seg:sub(#base + 1)
         fname = fname:gsub("?", sname)

         -- Get an xray path from our filename
         local path = fs:update_path("$game_scripts$", fname)

         -- If the path is valid and exists...
         if path and fs:exist(path) then
            -- Load the corresponding file into a string
            local src = _LOAD_FILE(path)

            -- Compile it into a Lua function
            local mac, err = loadstring(src, name, path)

            -- If we don't have a result...
            if not mac then
               -- Print and throw the corresponding error
               print(err)
               error(err)
            end

            -- Cache any loaded copy of this package
            local already_loaded = package.loaded[name]

            -- Evaluate the compiled Lua function
            local res = mac()

            -- Emplace the result in package.loaded so callbacks can see it
            package.loaded[name] = res

            -- If this package wasn't already loaded...
            if already_loaded == nil then
               -- Fire on-load callbacks
               for _,f in ipairs(state.callbacks) do
                  f(name)
               end
            end

            -- Finally, return a function that populates package.loaded
            -- with the result and returns it,
            -- to ensure `require` returns the correct value
            -- regardless of re-entrant loading that may occur in the interim
            return function()
               package.loaded[name] = res
               return res
            end
         else
            -- Otherwise, add to our error accumulator
            if #errs > 0 then
               errs = errs .. "\n\t"
            end
            errs = errs .. "No db entry: " .. path
         end
      end
   end

   -- Cache the IO miss for later
   _SCRIPT_STORAGE:set("io_loader", name, errs)

   -- Return error accumulator
   return errs
end

-- Replace the loader list with the preloader plus our FS loader
package.loaders = { _LOADERS.pre, _LOADERS.fs }

-- Define callback registrator
local function register_on_load_callback(f)
   table.insert(state.callbacks, f)
end

-- Return final module
return {
   register_on_load_callback = register_on_load_callback
}
