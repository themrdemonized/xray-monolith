local unlocalizers = {}
local updated = false

function update()
   local fs = getFS()
   local list = fs:file_list_open(
      "$game_config$",
      "unlocalizers\\",
      bit_or(
         FS.FS_ListFiles,
         FS.FS_RootOnly
      )
   )

   if not list then
      return
   end

   local count = list:Size() or 0
   if count == 0 then
      return
   end

   for i=1,count do
      local id = list:GetAt(i - 1)

      if #id < 4 then
         goto next_filename
      end

      if string.sub(id, #id - 3, #id) ~= ".ltx" then
         goto next_filename
      end

      print("opening file:", id)

      local config = ini_file("unlocalizers\\" .. id)
      config:section_for_each(function(section)
         local name = string.lower(section)
         local count = config:line_count(name)
         for j=0,count-1 do
            local res, sec = config:r_line(name, j)
            if not res then
               goto next_line
            end
            unlocalizers[name] = unlocalizers[name] or {}
            table.insert(unlocalizers[name], sec)

            ::next_line::
         end
      end)

      ::next_filename::
   end
end

function get(k)
   if not updated then
      updated = true
      update()
   end
   return unlocalizers[k]
end


package.loaded["scam/unlocalize"] = {
   update = update,
   get = get
}
