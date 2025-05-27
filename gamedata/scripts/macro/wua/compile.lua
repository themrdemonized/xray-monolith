local MOVED = {
   _g_patches = "patches/_g",
   options_builder = "options/builder",
   options_modded_exes = "options/modded_exes",
   options_modded_exes_visual = "options/modded_exes/visual",
   options_modded_exes_ui_hud = "options/modded_exes/visual/ui_hud",
   options_modded_exes_crosshair = "options/modded_exes/visual/crosshair",
   options_modded_exes_3d_scopes = "options/modded_exes/visual/3d_scopes",
   options_modded_exes_hdr10 = "options/modded_exes/visual/hdr10",
   options_modded_exes_particles = "options/modded_exes/visual/particles",
   options_modded_exes_wallmarks = "options/modded_exes/visual/wallmarks",
   options_modded_exes_control = "options/modded_exes/control",
   options_modded_exes_keyboard = "options/modded_exes/control/keyboard",
   options_modded_exes_mouse = "options/modded_exes/control/mouse",
   options_modded_exes_camera = "options/modded_exes/control/camera",
   options_modded_exes_pda = "options/modded_exes/control/pda",
   options_modded_exes_sound = "options/modded_exes/sound",
   options_modded_exes_doppler = "options/modded_exes/sound/doppler",
   options_modded_exes_gameplay = "options/modded_exes/gameplay",
   options_modded_exes_3d_ballistics = "options/modded_exes/gameplay/3d_ballistics",
   options_modded_exes_first_person_death = "options/modded_exes/gameplay/first_person_death",
   options_modded_exes_monsters = "options/modded_exes/gameplay/monsters",
   options_modded_exes_aim = "options/modded_exes/gameplay/aim",
   options_modded_exes_saves = "options/modded_exes/saves",
   options_modded_exes_crash_saves = "options/modded_exes/saves/crash_saves",
   options_modded_exes_debug = "options/modded_exes/debug",
   options_modded_exes_logging = "options/modded_exes/debug/logging",
   options_modded_exes_metrics = "options/modded_exes/debug/metrics",
}

local G = setmetatable(
   {},
   {
      __index = function(_, key)
         local redir = MOVED[key]
         if redir ~= nil then
            key = redir
         end

         local gv = _G[key]
         if gv ~= nil then
            return gv
         end

         local res, out = pcall(require, key)
         if res then
            return out
         end
      end,
      __newindex = _G
   }
)

local function handle_error(msg)
   return function(err)
      err = "! wua: "
         .. msg .. ":\n\n"
         .. debug.traceback(err .. "\n", 2)
         .. "\n"
      print(err)
      error(err)
   end
end

local function compile(src, namespace_name)
   local is_g = namespace_name == "_G"

   local mt = {
      __index = G
   }

   if is_g then
      mt.__newindex = G
   end

   local env = setmetatable({ _G = G }, mt)

   if not is_g then
      env._M = env
      if namespace_name then
         env._PACKAGE = namespace_name
         env._COMPILER = _COMPILER
         env.loadstring = _COMPILER
         env[namespace_name] = env
      end
   end

   if namespace_name then
      src = "local script_name = function() return _PACKAGE end " .. src
   end

   src = "local this = _M " .. src

   local mod, err = loadstring(src, namespace_name)
   if not mod then
      handle_error("error loading " .. namespace_name)(err)
   end

   local mac = setfenv(mod, env)

   return function()
      xpcall(mac, handle_error("error evaluating " .. namespace_name))
      return env
   end
end

return {
   compile = compile
}
