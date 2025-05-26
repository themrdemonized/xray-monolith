local function load_src(src, script_name)
   return assert(loadstring(src, script_name))
end

local function extend_env(dest)
   return setmetatable(
      dest,
      {
         __index = _G,
         __newindex = _G,
      }
   )
end

package.loaded[_PACKAGE] = {
   load_src = load_src,
   extend_env = extend_env,
}

package.loaded[_PACKAGE].lua = require(_PACKAGE .. "/lua")
package.loaded[_PACKAGE].lisp = require(_PACKAGE .. "/lisp")
package.loaded[_PACKAGE].wua = require(_PACKAGE .. "/wua")
