function expand(src, namespace_name)
   print("lua: expanding " .. namespace_name)
   return setfenv(
      macro.load_src(src),
      macro.extend_env {
         script_name = function()
            return namespace_name
         end
      }
   )
end
