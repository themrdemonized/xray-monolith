# R1 PDA Map API v1

This fork adds the Lua module `pda_map` to Modded Exes. It supplies generic map drawing and input capabilities for independent addons. Addons choose their tools, buttons, colors, erasing, undo and save format. R1 Map Pencil 0.4.0 is a read-only reference; its DLL, UI, icon atlas and binary save format are not incorporated.

Source baseline: ordinary Modded Exes `2026.10.11`, commit `c57fe5b9fab4fe0506659db03137c52d2500eb2f`; MT baseline `871e2491228a64f0d5e257c7a7e0255feb841ac8`. The API uses engine source integration and Lua exports, with no EXE offsets, signatures, FFI or external hook DLL. An older executable does not gain this API by installing scripts. Addons must check `pda_map and pda_map.version() == 1`.

## Coordinates and rendering

- `map_name = "global_map"`: global canvas coordinates, X right and Y down, with the native UI aspect adjustment removed. These are coordinates on the map artwork, not world meters.
- A level name from `level_maps_single`: world X/Z coordinates in meters; the API's second coordinate is Z. This keeps annotations anchored when the level's global rectangle moves.
- `screen` coordinates are the engine's virtual UI coordinates, normally 1024 x 768, also used by the PDA cursor. They are not desktop pixels.
- Projection follows the current map rectangle, including smooth zoom and pan. Both conversions account for UI aspect ratio. Rotated individual map windows are rejected; the standard PDA maps do not use that mode.
- Drawing runs in the native UI pass, after the map/spot children and before surrounding frames, navigation controls and context menus. Geometry is clipped to the map viewport, including the lit/3D PDA vertex path. Lines share batched triangle submissions; shaders/textures are cached per layer. Runtime appearance and performance require a game test.

## Lua functions

`token` is a nonzero layer handle. `id` is a nonzero command handle. Failure returns `0` for these handles, or `false` for boolean operations.

| Function | Result / meaning |
|---|---|
| `pda_map.version()` | API version, currently `1` |
| `pda_map.ready()` | A fully initialized PDA map exists |
| `pda_map.viewport()` | Table: `ready`; when ready also `shown`, `x`, `y`, `width`, `height`, `zoom` |
| `pda_map.register_layer(owner)` | New token; `owner` is a unique nonempty addon/layer name (up to 128 bytes). A duplicate owner is rejected |
| `pda_map.valid_layer(token)` | Whether the token belongs to the current map UI |
| `pda_map.clear_layer(token)` | Remove that layer's commands and cached textures; keep its token |
| `pda_map.release_layer(token)` | Delete the layer and release its input ownership |
| `pda_map.set_visible(token, bool)` | Show/hide that layer; hiding releases its input ownership |
| `pda_map.count(token)` | Number of commands; `0` for an invalid token |
| `pda_map.remove(token, id)` | Remove one command only from that layer |
| `pda_map.add_line(token, map_name, x1, y1, x2, y2, width, color)` | New line command id; a coincident pair draws a square dot |
| `pda_map.add_polyline(token, map_name, points, width, color, closed)` | New command id; `points` is a contiguous array `{{x,y}, {x,y}, ...}` |
| `pda_map.add_marker(token, map_name, texture, x, y, width, height, color, scale_with_map)` | New command id; centered textured rectangle |
| `pda_map.to_screen(map_name, x, y)` | `ok, screen_x, screen_y` |
| `pda_map.from_screen(map_name, screen_x, screen_y)` | `ok, map_x, map_y` |
| `pda_map.acquire_input(token)` | Acquire exclusive map-tool input ownership; fails if another layer owns it or the map is hidden |
| `pda_map.release_input(token)` | Release input only if this token owns it |

Line width is in virtual UI units and stays constant during map zoom. Line segments have flat joins/caps. Marker sizes are virtual UI units when `scale_with_map=false`; when true they are map-coordinate units transformed by map zoom and UI aspect. Markers accept registered UI texture IDs (including atlas rectangles) or existing raw DDS texture paths without `.dds`. Their resources remain the addon's responsibility. Color is unsigned ARGB (`0xffffffff` = opaque white).

Limits: 32 layers per map UI, 4096 commands per layer, 65536 points across all layers, 4096 points per polyline, 128 distinct cached textures per layer (including the white rectangle for lines). Line width must be >0 and <=128; marker sizes >0 and <=2048. Coordinates must be finite and within +/-10000000. Open polylines require at least two points, closed polylines at least three. Unknown maps, unavailable textures, excess resources and invalid handles are rejected. Submission does not partially append a failed command.

## Callbacks and input

Install the fork's updated `callbacks_gameobject.script` with its engine build. It registers:

```lua
RegisterScriptCallback("on_pda_map_visibility", function(shown) ... end)
RegisterScriptCallback("on_pda_map_input", function(event) ... end)
```

Visibility fires after the map's show/hide operation. Input fires only for the current input owner, when the cursor is in the usable map viewport; navigation controls, a locked map and an open context menu keep their normal behavior. Acquisition selects the map tool; it does not capture the operating system mouse or guarantee events outside the viewport.

The event contains `owner`, `layer`, `action`, `dik` (`-1` for mouse), `screen_x`, `screen_y`, `shift`, `ctrl`, `alt`, `left_held`, `consume=false`; valid projection also supplies `global_x`, `global_y`. Over a level rectangle it adds `level_name`, `world_x`, `world_z`. Level detection uses the current rectangles rather than the previous frame's hover pointer. It does not raycast for world Y or calculate graph vertices.

Actions: `left_down`, `left_up`, `left_double_click`, `right_down`, `right_up`, `middle_down`, `middle_up`, `mouse_move`, `wheel_up`, `wheel_down`, `key_down`, `key_up`. Set `event.consume=true` only for input handled by your tool. Otherwise the original map input/spot actions continue. Filter on your own `event.layer`; never overwrite another addon's consumption decision. Callback handlers can add/remove/release commands because no layer mutex is held while Lua runs.

## Lifecycle and persistence

Closing the map releases input, but retains drawings until the map UI resets or is destroyed. Reacquire input explicitly when reopening your tool. Map UI reset and destruction delete all layers/resources. Tokens are never reused during the process lifetime: keep `valid_layer()` checks and recreate layers after loading a save or UI recreation. Command ids are temporary too.

The engine does not serialize annotations, change game save layouts, migrate the old DLL blobs, or save undo history. An addon must keep serializable map coordinates/styles in its own versioned save state and rebuild its layer after load; never save native handles or UI coordinates as permanent map positions. Disable/release the input tool on close, actor teardown, death and level transition as appropriate. Keep sampling inside the viewport and use `left_held` to recover if release occurred outside it.

See [R1_Pda_Map_Example.lua](../examples/R1_Pda_Map_Example.lua) for a small opt-in tool. It is outside `gamedata/scripts`, so this fork does not activate a pencil by itself. Existing DLL-based addons require an explicit adaptation to the API; their binaries do not automatically start using it.

## Build and validation

Use the upstream VS2022 `src/engine-vs2022.sln` build procedure and preserve the original license/credits. The new `.cpp` is listed in `xrGame.vcxproj`; the module is registered from `ui_export_script.cpp`. [RU documentation](R1_PDA_Map_API_RU.md).

Verification evidence is documented in [R1_PDA_Map_Validation.md](R1_PDA_Map_Validation.md). Full ordinary and MT DX11 builds were linked; ordinary native 2D drawing, independent layers, input ownership and Lua save/cold-load restoration passed. Physical freehand input, 3D PDA, other renderers/AVX, modpack compatibility and performance remain unverified. MT native acceptance is incomplete. This source contribution does not publish a binary release.

Game acceptance scenarios: two independent addon layers; drawing/panning/zooming over map spots; viewport-edge clipping in 2D and 3D PDA; wheel/nav/context-menu behavior while a tool owns input; hide/reopen/death/load/transition cleanup; failed duplicate input acquisition; point/texture limits and malformed data. Check ordinary and MT builds and each required renderer/configuration separately.
