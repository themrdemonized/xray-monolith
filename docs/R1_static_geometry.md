# Runtime static geometry API

An engine extension for addons that enable or disable authored parts of the loaded
level. No building, recipe, economy, workbench UI, map name or addon assets are
included in the executable. Original level geometry remains the default.

## Lua interface (API version 1)

| Function | Result |
| --- | --- |
| `level.static_geometry_version()` | `1` |
| `level.static_geometry_file_signature(name)` | IEEE CRC32 and byte length as `xxxxxxxx:length`; nil if absent or unsupported |
| `level.static_visual_count()` | Number of loaded static render visuals; zero without a level |
| `level.static_collision_count()` | Number of static CFORM triangles; zero without a level |
| `level.static_visual_enabled(id)` | Current visibility; false for an invalid ID |
| `level.static_collision_enabled(id)` | Current collision state; false for an invalid ID |
| `level.set_static_visuals_enabled(ids, enabled)` | Validate the entire ID table, then change visibility; boolean success |
| `level.set_static_collision_enabled(ids, enabled)` | Validate the entire ID table, then publish the collision mask; boolean success |
| `level.set_static_geometry_enabled(visual_ids, triangle_ids, enabled)` | Validate both lists before changing either; boolean success |
| `level.reset_static_geometry()` | Enable all visuals and triangles in this loaded level; boolean success |

IDs are **zero-based** indices in this exact level's `level`/`level.cform`, not
ALife IDs. An authoring/export tool must produce a manifest for the level files it
actually ships. Never reuse IDs from another map revision. The table contains
integer IDs as values. Invalid types, fractions, negative values and out-of-range
IDs reject the entire call before any state changes. Empty tables are valid.

For compatibility guards, obtain the winning loaded map's resources through the
engine VFS using `static_geometry_file_signature`. Accepted names are `level`,
`level.geom`, `level.geomx`, `level.cform` and `level.hom`. File names/paths outside
this list are refused. Compare the signatures and counts to your shipped manifest
before changing any ID; if a map replacement differs or a required resource or
manifest is missing, log a skip and leave the level untouched. Compute offline
signatures with the standard IEEE CRC32 (for example Python `zlib.crc32`) plus the
exact byte length. It detects accidental map mismatches, not maliciously forged
content. Call this once during level initialization, not in a per-frame loop.

```lua
-- Example manifest belongs to the addon, not the engine.
local manifest = addon_geometry_manifest.for_current_level()
assert(level.set_static_geometry_enabled(manifest.debris_visuals, manifest.debris_triangles, false))
assert(level.set_static_geometry_enabled(manifest.repaired_visuals, manifest.repaired_triangles, true))
```

The example requires an addon-defined `addon_geometry_manifest`; it is not a new
engine API. Scripts must apply their saved state after each level load. The engine
stores only runtime masks and does not change the save format or level files.
Do not call the global reset when another addon owns its own disabled geometry;
restore only your manifest's IDs. This API is a low-level authoring interface.

## Implementation constraints

Visibility is checked at static scene traversal, including hierarchy and LOD
parents. Collision masks are checked before accepting triangle hits in ray, box
and frustum queries, preserving ONLYFIRST/ONLYNEAREST behaviour. Queries retain an
immutable mask snapshot. Static ray and ODE triangle caches are invalidated by a
generation change, including when geometry is re-enabled. Changes queue a bounds
notification for the physics-step owner to wake nearby sleeping bodies and reset
stale character support state. Frozen bodies and static shells remain unchanged.

This interface does not add triangles or render resources. Ship damaged and
repaired geometry as pre-authored alternatives in the level, or use other native
object/attachment APIs for the replacement. The engine does not infer which
collision triangles correspond to a visual.

HOM occluders, sectors/portals, AI navigation and baked lighting are not rebuilt.
Author affected areas without stale occluders and provide appropriate navigation
and lighting. When enabling solid geometry, place characters outside its volume
beforehand; this low-level API is not a placement/interpenetration solver. Changes are
runtime state, not a general-purpose destructive level editor.

## Validation

Build the ordinary DX11 x64 solution, then run the real CDB tests documented in
`R1_Tests/R1_Static_Geometry/R1_Readme_EN.md` (RU instructions are alongside).
They cover mask updates, concurrent immutable snapshots and query semantics.
The console host does not initialize Lua, rendering or ODE gameplay. An external
stock Army Warehouses tank scenario has separate October 12 in-game evidence;
that evidence does not certify later revisions, other renderers or modpacks.

A missing optional addon manifest is safely refused; a missing mandatory level
resource may fail in the engine loader before addon Lua can run. This API guards
addon operations, not broken map installations.
