# R1 Multi View integration — ordinary DX11

This branch adds an auxiliary-camera API to ordinary Modded Exes, based on
`c57fe5b9fab4fe0506659db03137c52d2500eb2f` (2026.10.11).
It contains engine source and this documentation. Addon Lua, MCM, weapon models,
door textures, anomaly events, EXE/PDB and test saves remain separate.
No new third-party library is required: the renderer already links D3DCompiler;
DirectXMath and WRL are SDK headers.

## Engine responsibilities

- Up to eight independent camera slots, each with a retained output texture.
- A bounded three-resolution scratch pool shared by equal-sized views.
- Relative/absolute cameras, affine camera mapping, oblique clipping, FOV/aspect,
  update interval and three quality levels.
- Texture-only output, a screen rectangle, or a depth-tested world quad/mesh.
- A replacement screen view for stereo, avoiding a third main world render.
- Scoped camera/target/options restoration and existing worker synchronization.
  World passes share one simulation frame; they do not repeat actor/Lua updates.
- Separate per-view light submission; camera-dependent HOM cache bypass;
  once-per-frame grass fade and correct detail-list consumption.
- Culling of invisible world surfaces and explicit reset on level/device unload.

Scope selection, stereo IPD, mirror-plane reflection matrices, door geometry and
animation, gameplay events, UI settings and persistence belong to addons. The
engine contains no door/mirror/weapon definitions or bundled addon assets.

## Lua API v1

`level.r1_view_api_version()`, `r1_view_set(id, parameters)`,
`r1_view_clear(id)`, `r1_view_status(id)` and `r1_view_geometry(id, geometry)`.
Addon owners must coordinate IDs and clear their slots on load/exit/change.
Unrefreshed descriptors expire after 1500 ms. The renderer accepts IDs 0–7,
dimensions 64–4096, interval 1–120 and quality 0–2. It rejects invalid numeric
descriptors, singular affine transforms and degenerate absolute camera bases.

```lua
level.r1_view_set(5, {
    relative = true, width = 640, height = 360, fov = 20,
    hud = false, quality = 1, interval = 1,
    output = 1, rect_x = 0.75, rect_y = 0.02,
    rect_w = 0.24, rect_h = 0.25
})
-- Texture-only consumers can bind "$user$r1mv_5".
-- Refresh while active; clear when no longer needed.
level.r1_view_clear(5)
```

Important parameters:

| Parameter | Contract |
|---|---|
| relative | Current camera, or explicit position/direction/up when false |
| offset | Local right/up/forward displacement in metres |
| camera_transform | Optional nonsingular affine matrix |
| fov / aspect | Horizontal FOV in degrees; aspect is height/width; zero selects defaults |
| projection_shift / far_distance | Horizontal projection offset; optional far-distance cap |
| clip / plane_x/y/z/d | Oblique near plane, n·position+d=0 |
| flip_x | Flip image sampling in compositor; custom raw-texture consumers flip U themselves |
| hud / actor_body | Weapon HUD / full actor body for this world pass |
| output | 0 texture, 1 screen rectangle, 2 world surface |
| primary | Replace main world; requires output=1 and interval=1 |
| quad_position/right/up | World quad centre and half-extent vectors |

`geometry.vertices` contains triangle vertices, at most 256 entries and a count
divisible by three. Each entry is `{position=vector(), u=0, v=0, material=0}`.
Material 0 samples the camera; 1/2 sample `texture1`/`texture2` game texture names.
Material illumination is currently unlit. Ordinary screen PiP preserves main HUD
UI. Replacing stereo output captures UI once and composites it into screen slots
with `hud=true`.

Status exposes valid/ready/error, actual width/height, simulation frame, render
and compositor draw counts, CPU render_ms/total_ms. The CPU timer has integer
millisecond precision; it is not a GPU measurement. Errors include -2 HDR/MSAA,
-3 scratch allocation, -4 unexpected frame advance and -5 compositor setup.

## Validation boundary

Built as x64 DX11 non-AVX using VS2022/MSVC 14.44 and SDK 10.0.26100.0.
Individual SBS, ACOG ADS, original Door opening and Mirror were exercised in
isolated native-game fixtures. The simultaneous fixture covers ACOG plus Door
and Mirror, independent clear/resize, and ordinary screen PiP plus both world
surfaces. Exact binary hashes, logs, screenshots and size comparison accompany
the integration handoff outside this source branch.

HDR/MSAA, MT and other render backends are not accepted. Auxiliary temporal
TAA/SSR history is disabled. R1 surfaces are composited into the main view;
recursive mirrors/portals are not rendered. Combined SBS with these extra views,
physical glasses, full teleport round trips, long gameplay and arbitrary modpack
compatibility are not accepted. This branch is a source integration candidate,
not a claim of an already merged or generally stable release.

Auxiliary grass currently reuses the main-camera visibility/cache. Grass outside
that cache can be absent in remote views; separate remote grass streaming is not
part of this minimal integration.
