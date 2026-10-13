# R1 Menu Presentation API (experimental)

Base: Modded Exes ordinary 2026.10.11. Four functions in `level`:

```lua
token = level.menu_scene_begin(update_level) -- false: frozen pause; true: live level
ok = level.menu_scene_set_camera(token, position, direction, up, fov_degrees)
active = level.menu_scene_active(token)
ok = level.menu_scene_end(token)
```

Call from the main-menu UI `Update`, after activation and level precaching. No loaded, ready single-player level means `begin` returns 0. It does not load a scene or open the menu. A second owner is rejected. End and camera calls require the returned nonzero token. Directions are vectors, not HPB angles; zero/parallel/nonfinite vectors and FOV outside (1,179) are rejected.

Calls are accepted on the menu's thread. A camera request is applied after UI `Update` in the menu's frame callback; camera getters read inside `Update` can still contain the actor camera calculated earlier that frame. Verify the rendered pose in the resulting frame. The example expects a transparent main-menu UI supplied by the integrating addon.

World rendering continues beneath the normal menu. Frozen mode uses the existing timer pause and suspended level callback; UI and camera poses use the existing `device():time_continual()`. Live mode resumes the timer and executes the removed level callback once per menu frame. The normal menu sound pause is retained. Live presentation saves are rejected before sending a save packet.

Ownership ends on menu close, UI reload, level stop or menu destruction. Camera, projection, pause and second viewport are restored. This transient state is not serialized. The ordinary menu is unchanged unless the API is explicitly acquired.

Keep location loading, NPC choice, animation, orbit paths, collision checks, input, HUD/body visibility, UI and random selection in Lua. This API contains no R1 addon scene data, binary addresses, DLL hooks or alternate clock. Lua code that independently mutates the world can still change it during pause; visual/audio effects need scenario-specific acceptance. MT and other renderers need separate adaptation/testing.

`examples/R1_Menu_Presentation_Demo/gamedata/scripts/R1_menu_presentation_demo.script` shows a Lua-owned pause orbit; it requires a loaded level and this custom EXE. It is not the full R1 3D Menu addon or a migration of its DLL backend.

The tested two-file fixture is in `examples/R1_Menu_API_Test`; see its RU/EN readmes. Its NPC creation and acceptance runner are external to the installable addon.
