# Spawn overlays

---

## Intro

Spawn overlays are edits the engine applies on top of the loaded `all.spawn`, every time the spawn
registry loads (a new game and every save load). Nothing is written back: `all.spawn` and the level
files stay as shipped.

They come from four places:

| Source | What it adds or changes |
|---|---|
| `configs\spawn_overlays.ltx` | graph links and unlinks, removals, additions, patches, level cuts |
| `spawns\<level>\<anything>.spawn` / `.game` | objects and patrol paths on one level (SDK exports) |
| `spawns\<pack>.spawn` | level packs: whole levels with their objects and paths |
| `configs\patrol_paths.ltx` | patrol paths written as text |

Mods extend `spawn_overlays.ltx` from `configs\mod_spawn_overlays_<mod>.ltx` with `![section]` (DLTX).
A mod needs the `!` on every section `spawn_overlays.ltx` already declares (`[settings]`, `[graph_links]`,
`[graph_unlinks]`, `[spawn_remove]`, `[spawn_replace]`, `[path_remove]`, `[level_cut]`). A plain
`[spawn_remove]` in a mod file stops the load with `[DLTX] Duplicate section 'spawn_remove' wasn't marked as an override`.
Each section is described [below](#spawn_overlaysltx-sections).

---

## What to use for what

| I want to... | Use |
|---|---|
| add whole new levels (`level.ai`, `level.spawn`, ...) | a [level pack](#level-packs): the mod's `all.spawn` renamed to `spawns\<mod>.spawn`, plus a `[levelNN]` in `game.ltx` per level |
| connect or disconnect levels | [`[graph_links]` / `[graph_unlinks]`](#graph_links-and-graph_unlinks) |
| move a level on the global map | the `[levelNN]` offset in `game.ltx` ([`sync_level_offsets`](#settings)) |
| add objects to an existing level | a [level.spawn fragment](#objects-levelspawn-fragments): `spawns\<level>\<anything>.spawn` (SDK export or ACDC `-compile -level`) |
| delete a vanilla object | [`[spawn_remove]`](#spawn_remove-and-spawn_replace) |
| edit a vanilla object (position, logic, story id) | [`[spawn_patch@<name>]`](#spawn_patchname) |
| swap a vanilla object for my pack's or fragment's copy | [`[spawn_replace]`](#spawn_remove-and-spawn_replace) (same name, same place) |
| change where a level changer leads | [`[spawn_patch@<name>]`](#spawn_patchname) `dest_*` keys |
| add a level changer, a transition restrictor or another single object in ltx | [`[spawn_add@<name>]`](#spawn_addname) |
| add patrol paths | a [level.game fragment](#patrol-paths): `spawns\<level>\<anything>.game` (SDK export), or `patrol_paths.ltx` sections with a `level = <name>` key |
| fix or replace a compiled patrol path | a `patrol_paths.ltx` section of the same name, with `level = <name>` |
| delete a patrol path | [`[path_remove]`](#path_remove) |
| take a whole level out of play | [`[level_cut]`](#level_cut) |

---

## Rules of thumb

* **The base `all.spawn` always wins over a pack**, except for what a rebuilding pack owns (see
  [Rebuilt levels](#rebuilt-levels)). A pack record that names an object the base already has on
  the same level within 1 m is ignored, even when the pack's copy differs. Express
  the difference with `[spawn_patch@]` (for the fields it covers), or take the pack's copy with
  `[spawn_replace]`. The same goes for paths: a pack path whose name the base has is ignored;
  override it from `patrol_paths.ltx`.
* **A pack only contributes what the base lacks.** Don't ship edits to vanilla objects or paths
  inside a pack. Ship them in `spawn_overlays.ltx`, so they survive a base `all.spawn` update and
  other mods can patch them.
* **Ids are never keys.** Refer to objects by name (plus position when the name is shared), to
  levels by name, and to graph vertices by `level:x,y,z`. Ids differ between spawn files and change
  whenever the installed content changes. Scripts and configs never write game vertex ids down; see
  [Scripts without game vertex ids](#scripts-without-game-vertex-ids).
* **Objects are new-game content.** An existing save already holds its objects, and removing or
  patching a template doesn't touch them. Saves hold no game graph and no patrol paths, so graph
  edits (links, unlinks, offsets) and every path edit (additions, replacements, `[path_remove]`,
  cuts) apply to existing saves too.
* **A placed object takes the nearest graph vertex and that vertex's AI node.** `[spawn_add@]`, a
  `[spawn_patch@]` `position` and level.spawn fragment records don't look up the node under the
  position, because a new game loads only the current level's `level.ai`. An object that uses AI
  locations, smart terrains and smart covers among them, moves to the node under it and that
  node's graph vertex at the first ALife switch pass after the actor reaches its level, and the
  save keeps them; other space restrictors keep the vertex's node. When the vertex's node lies far
  from the position, the search from it can miss, and the object keeps the vertex's node while it
  is offline and reaches the node under it two passes after it comes online. Smart terrains and
  smart covers can't switch offline, so they come online in that first pass. A smart terrain's
  client object never looks its node up again and keeps the node it came online with, in that
  case the vertex's node; a smart cover's client can end up one cell off. Scripts that need the node
  read the server object. A smart terrain on that level builds its jobs while the level loads,
  before that pass, so until the game next loads, its task and its animpoint and smart cover jobs
  point at the vertex's node. A script that needs the spot itself, such as a squad spawned at its
  smart, reads `level.vertex_id(position)` while that level is loaded.
* **Added objects share 65,535 object ids with everything else in ALife.** A new game creates the
  base templates first, then the added ones (fragments, packs, `[spawn_add@]`). An added template
  makes no object once fewer than 16,384 ids are free, and one `!` line gives how many it skipped.
  The check never skips a base template. Stock content that comes back through a pack that
  rebuilds a level, through `[spawn_replace]`, or through a `[spawn_remove]` with a `[spawn_add@]`
  of the same name counts as added, so the check can skip it. The check runs while templates
  spawn, before the game's own scripts spawn anything, so the ids it keeps free are left for those
  scripts. A script `alife():create(spawn_id)` of an added template passes the same check, returns
  nil when it fails, and logs one `!` line.
* **Changing the level set changes the spawn GUID**, and saves made before it don't load. See
  [Spawn GUID and saves](#spawn-guid-and-saves) for exactly what counts.
* **A compat patch is an ordinary DLTX file.** It can add to any section, override a
  `[spawn_patch@]` key, or override a whole path section. Prefer that over editing another mod's
  files.
* **Read the log.** See [Log](#log).

---

## Addressing

**Graph endpoints** are `level_name:x,y,z`: a level point of the named level. The nearest
game-graph vertex of that level within `link_tolerance` metres is used. Level changers stand on
graph vertices, so their coordinates work.

**Objects** are named by their object name. Some names are shared by several templates
(furniture, boxes, ladders), and those also need the instance's position, matched within 0.5 m.
Object ids match without regard to case, because the ini reader lower-cases section headers, so
`[spawn_patch@ESC_Smart_Terrain_2_12]` reaches the engine as `esc_smart_terrain_2_12`. An id that
matches two templates differing only in case is refused (`!` log), and a `[spawn_add@]` whose id
differs from a template's only in case counts as the same object id.

**Levels** are named by their level name.

**Values** are read whole. A number is any finite number C `strtof` reads (`12.5`, `-3`, `+1e2`,
hex floats such as `0x1p3`); `5abc`, `nan`, `inf`, a second number where one is expected (`1,2`)
or a fourth coordinate (`1,2,3,4`) are refused with a `!` line. Each kind has a bound, and a value
beyond it is refused the same way:

| Kind | Keys | Bound |
|---|---|---|
| position, x, y, z | `position`, `dest_position`, `match_position`, graph endpoint points | each coordinate within ±100000 m (the physics engine counts a point 100 km outside the level as invalid) |
| angles, x, y, z in radians | `direction`, `dest_direction` | each within ±100000, kept as written; the engine has no limit here, so the bound only refuses runaway values |
| box edges, sphere radius | `box`, `sphere` | above 0 and at most 100000 m, a loose bound as for angles |
| distance | `link_tolerance` | 0 to 100000 |
| smart cover numbers | `hold_position_time`, `enter_min_enemy_distance`, `exit_min_enemy_distance` | 0 to 100000, a loose bound as for angles |

The `[settings]` and smart cover keys have their own ranges, listed with each key. A section header
of 256 characters or more is skipped (`!` log) before any of its keys is read; the engine's ini
reader cannot open it.

---

## Scripts without game vertex ids

Place objects as records (a pack or a level.spawn fragment), or from Lua by level name and
position. Ids read from live objects (`se.m_game_vertex_id`, `db.actor:game_vertex_id()`) may be
passed on as they are.

| Instead of | Use |
|---|---|
| `alife():create(sec, pos, lvid, gvid)` | `alife():create_on_level(sec, "<level>", pos [, lvid])`: exact on the current level, elsewhere re-snapped to `pos` when the level loads |
| `alife():teleport_object(id, gvid, lvid, pos)` | `gvid = game_graph():nearest_vertex("<level>", pos)` |
| `ChangeLevel(pos, lvid, gvid, dir, ...)` | `gvid` from `nearest_vertex` as above; `lvid` recorded, or `game_graph():vertex(gvid):level_vertex_id()` |
| level changer `data.dest_game_vertex_id = <number>` | `gvid` from `nearest_vertex(dest level, dest position)`, or ship the changer as a record (its destination is resolved from its position) |
| `data.game_vertex_id = <number>` after a create | nothing: `create_on_level` already set it |
| `se.m_game_vertex_id == <number>` ("is it in place?") | `se.m_level_vertex_id` plus the level name, `alife():level_name(game_graph():vertex(gvid):level_id())` |
| node -> gvid tables, "vertex N is on my level" guards | delete them |

---

## spawn_overlays.ltx sections

### [settings]

```ini
[settings]
link_tolerance = 2.0
sync_level_offsets = true
```

* `link_tolerance`: the largest distance in metres from an endpoint to its graph vertex, 0 to
  100000. Any other value keeps the default 2.0 (`!` log).
* `sync_level_offsets`: `true`, `false`, `yes`, `no`, `on`, `off`, `1` or `0` (any case); any other
  value keeps the default `true` (`!` log). It moves each level of the base spawn to the `offset` of its `[levelNN]`
  section in `game.ltx` when that differs from the one compiled into the graph. The `[levelNN]`
  sections are the ones listed under `[levels]`; vanilla keeps them in `game_levels.ltx`, and mods
  add or override theirs with DLTX. The edges that cross into the moved level are reweighted. A
  level pack that relocates existing levels only has to override that offset. Levels a pack adds
  are always placed at their `[levelNN]` offset, whatever this setting says.

### [graph_links] and [graph_unlinks]

```ini
[graph_links]
level_a:x,y,z = level_b:x,y,z

[graph_unlinks]
level_a = level_b                   ; every edge between the two levels
level_a:x,y,z = level_b:x,y,z       ; one specific edge
```

`[graph_links]` adds a game-graph edge in both directions between two level points. Its weight is
the distance between the vertices' global positions, which is how xrAI encodes cross-level edges.
An edge that already exists is left alone.

`[graph_unlinks]` removes game-graph edges in both directions.

An endpoint of 256 characters or more, or a point that isn't x, y, z within the
[position bound](#addressing), skips its line (`!` log).

Keys are unique within a section. To cut one level from several, put that level on the value side
(`l09_deadcity = l07_military`, `k02_trucks_cemetery = l07_military`), or use a suffixed section
per line. Both sections can be declared with a suffix, such as `[graph_unlinks@<mod>]`. That is a
separate section with its own keys, applied the same way. Use one when a level must be the key of
more than one line, or to keep a mod's lines from colliding with another mod's keys.

### [spawn_remove] and [spawn_replace]

```ini
[spawn_remove]
gar_smart_terrain_5_8_box
level_prefix_inventory_box_0000 = 12.3, 4.5, -6.7
```

`[spawn_remove]` removes spawn templates by object name, one per line. A shared name needs the
instance's position as the value; a value that isn't x, y, z within the
[position bound](#addressing) skips the line (`!` log). It affects new games only: objects already created from the
template live in the save. It applies after fragments and packs, so a removed object stays removed
even when a pack still carries it. The removed template's `story_id` and `spawn_story_id` are freed
for a `[spawn_patch@]` or `[spawn_add@]` to reuse. Removing a template that has a `spawn_story_id`, here or in
`[spawn_replace]`, is logged `!` because scripts that spawn it by that id will fail; it is still removed.
`alife():spawn_id` returns 65535 for a story id no template carries, and `alife():create` returns nil for a
spawn id no template has. Both log a `!` line.
Removes apply before adds, so once every template of a name is removed, a `[spawn_add@]` of that
name, in the same file or another, creates a new object with a new spawn id. Removing a smart
terrain drops the start squads that the population file (`misc\simulation.ltx` in stock) places in
it, with one `sim_board:fill_start_position incorrect smart by name` line for each. A smart added
back under the same name gets them.

`[spawn_replace]` uses the same syntax but applies before fragments and packs. The base template
goes, and a fragment's or pack's record of the same name at the same place is taken instead. Without
it, the base would win over that record.

`spawn_overlays.ltx` declares both sections, so a mod's `mod_spawn_overlays_<mod>.ltx` writes them
as `![spawn_remove]` and `![spawn_replace]`; the plain header stops the load with a DLTX duplicate
section error.

### [spawn_patch@\<name\>]

Edits one spawn template. The section suffix is the object name. All keys are optional:

| Key | Meaning |
|---|---|
| `match_position = x, y, z` | selects the instance when the name is shared (within 0.5 m). A value that isn't x, y, z within the [position bound](#addressing) skips the **whole** section (`!` log). |
| `level = <level name>` | moves the object to another level. Needs `position` too, because coordinates are level-local and don't carry over. A level that isn't installed skips the **whole** section, with no key applied. An empty value, or a `position` that is missing or isn't x, y, z, also skips the whole section (`!` log). |
| `position = x, y, z` | level-local position; the graph vertex and AI node are re-resolved as the [rules of thumb](#rules-of-thumb) say. Without `level`, a bad value is ignored (`!` log) and the rest of the patch applies. Without `position` the template keeps its stored vertex and node, including the graph vertex some smart covers store apart from their node's. |
| `direction = x, y, z` | angles in radians, each within ±100000. A bad value is ignored (`!` log) and the rest of the patch applies. |
| `custom_data = ...` | `\n` is a line break and `\\` a literal backslash (needed before a folder starting with `n`) |
| `custom_data_cfg = scripts\x.ltx` | shorthand for `custom_data = [logic]\ncfg = scripts\x.ltx`, with no escaping, at most 505 characters. Ignored on a smart terrain (`!` log), which reads `[smart_terrain]` instead. A longer value is ignored (`!` log). |
| `story_id = <int>` | a whole integer from `-1` to `4294967295`, `-1` = none. Any other value is ignored (`!` log). An id another template already holds is skipped (`!` log). |
| `spawn_story_id = <int>` | as `story_id` |
| `object_flags = 0x...` | an unsigned integer up to `0xFFFFFFFF`, in hex with `0x`, decimal, or octal with a leading `0`, with an optional `+`. A minus sign or any other value is ignored (`!` log). |
| `dest_level = <level name>` | level changers only: the level it leads to. Needs `dest_position`. A level that isn't installed skips the three `dest_*` keys with one `-` line on `dest_level`. Without `dest_level`, a changer whose own destination isn't installed skips them with the `-` line on `dest_position`. An empty value skips them too, with one `!` line on `dest_level` listing the keys it skipped. |
| `dest_position = x, y, z` | level changers only: arrival point on the destination level; its graph vertex and AI node are re-resolved. A bad value, or one with no graph vertex to land on, is ignored together with `dest_level` and `dest_direction` (`!` log) and the rest of the patch applies. |
| `dest_direction = x, y, z` | level changers only: the actor's facing on arrival, in radians within ±100000. A bad value is ignored (`!` log) and the rest of the patch applies. |
| `box = width, height, depth` | shaped objects only (restrictors, smart terrains, anomalies, level changers, smart covers): replaces the shape with one box centred on the object, values as in [`[spawn_add@]`](#spawn_addname). Given with `sphere`, both are ignored (`!` log). A bad value is ignored (`!` log) and the rest of the patch applies. |
| `sphere = radius` | shaped objects only: replaces the shape with one sphere centred on the object, values as in [`[spawn_add@]`](#spawn_addname). A bad value is ignored (`!` log) and the rest of the patch applies. |
| `restrictor_type = <type>` | restrictors only, smart terrains included: values as in [`[spawn_add@]`](#spawn_addname). A bad value is ignored (`!` log) and the rest of the patch applies. |
| `description`, `loopholes`, `hold_position_time`, ... | smart covers only, see [Smart covers](#smart-covers) |

Like every template edit, a new shape or restrictor type reaches new games only.

```ini
[spawn_patch@gar_physic_object_bandit_base_generator]
position = 63.9, 0.6, -23.1

[spawn_patch@yan_level_changer_to_dead_city_1]
dest_position  = 12.3, 4.5, -6.7
dest_direction = 0, 1.57, 0
```

### [spawn_add@\<name\>]

Adds one object, written out in full. The section suffix is the object name. New games only.

The object is built from its system.ltx section plus the keys below, so it can be anything that
section fully describes:

* space restrictors and level changers (transitions, see the example below);
* anomalies (`zone_*` sections), with a `box` or `sphere`;
* items on the ground: weapons, ammo, consumables, artefacts;
* smart terrains: `section = smart_terrain` with `custom_data = [smart_terrain]\ncfg = scripts\...\<smart>.ltx`
  (a folder starting with `n` needs `\\`). `custom_data_cfg` writes `[logic]`, which doesn't
  configure a smart terrain, so the engine ignores it there (`!` log);
* smart covers and animpoints: `section = smart_cover` with a `sphere`, see [Smart covers](#smart-covers);
* inventory boxes and props whose section has a `visual` line (give a model its own section to
  place it).

Creatures can't be added (scripts spawn them), nor objects inside a box or on a character.

| Key | Meaning |
|---|---|
| `section = <system.ltx section>` | required: what to create |
| `level = <level name>` | required. A level that isn't installed skips the section. |
| `position = x, y, z` | required, level-local. For a restrictor or level changer, the centre of its shape. The graph vertex and AI node follow the [rules of thumb](#rules-of-thumb). |
| `direction = x, y, z` | rotates the object and its shape |
| `box = width, height, depth` | every shaped object (restrictors, smart terrains, anomalies, level changers, smart covers) needs `box` or `sphere`, else the section is skipped (`!` log): a box centred on `position`, width along the object's x, height up, depth along z, each edge above 0 and at most 100000 |
| `sphere = radius` | a sphere centred on `position`, radius above 0 and at most 100000 |
| `restrictor_type = <type>` | restrictors: `default_none` (if omitted), `default_out`, `default_in`, `none`, `in`, `out` |
| `custom_data`, `custom_data_cfg`, `story_id`, `spawn_story_id`, `object_flags` | as in [`[spawn_patch@]`](#spawn_patchname) |
| `dest_level = <level name>`, `dest_position = x, y, z` | level changers, required: where it leads. A destination level that isn't installed, or a `dest_level` of 256 characters or more, skips the section. |
| `dest_direction = x, y, z` | level changers: facing on arrival, in radians within ±100000. A bad value is ignored (`!` log) and the rest of the section applies. |
| `silent_mode = true` | level changers: change level on contact, without the dialog (`false` if omitted). A value other than `true`, `false`, `yes`, `no`, `on`, `off`, `1` or `0` (any case) is ignored (`!` log). |
| `hint = <string id>` | level changers: the dialog text (`level_changer_invitation` if omitted) |
| `description = <id>` and the other smart cover keys | smart covers, `description` required, see [Smart covers](#smart-covers) |

* If an object of that name is already within 1 m on that level, it is left as it is and nothing is
  added. That lets a compat patch restore an object that is missing only in some setups.
* The name of an object elsewhere can't be reused.
* `[spawn_patch@]` and `[level_cut]` act on added objects.
* `[spawn_remove]` runs before the adds: removing an object and adding it under the same name
  redefines it (any object, also a pack's). `[spawn_remove]` can't name an added object; to cancel
  another mod's add, delete its section with DLTX `!![spawn_add@<name>]`.

Copying an object from a spawn: vanilla shapes are boxes centred on the object, so `position`,
`direction` and the box size copy over unchanged.

A transition the way Anomaly builds them: a restrictor opens the travel dialog and drops the actor
into a silent level changer next to it. Besides the two objects, your mod adds the restrictor's
`sr_teleport_sections.ltx` entry and the two patrol paths that entry names:

```ini
; configs\mod_spawn_overlays_<mod>.ltx
[spawn_add@esc_space_restrictor_to_my_level]
section         = space_restrictor
level           = l01_escape
position        = 120.0, 2.5, -340.0
direction       = 0, 1.57, 0
box             = 5.0, 7.0, 5.0
restrictor_type = none
custom_data     = [story_object]\nstory_id = esc_space_restrictor_to_my_level\n[logic]\ncfg = scripts\sr_teleport_logic.ltx

[spawn_add@esc_level_changer_to_my_level]
section         = level_changer
level           = l01_escape
position        = 128.0, 2.5, -340.0
direction       = 0, 1.57, 0
box             = 5.0, 6.0, 12.0
restrictor_type = none
silent_mode     = true
dest_level      = my_level
dest_position   = 10.0, 0.5, 25.0
dest_direction  = 0, 3.14, 0

; configs\mod_sr_teleport_sections_<mod>.ltx
[esc_space_restrictor_to_my_level]
enable      = true
spot        = level_changer_right
text        = st_level_changer_descr
hint        = space_restrictor_to_my_level_desc
path        = esc_space_restrictor_to_my_level_drop
reject_path = esc_to_my_level_if_reject_way
autosave    = true

; configs\mod_patrol_paths_<mod>.ltx
[esc_space_restrictor_to_my_level_drop]
level = l01_escape
points = p0
p0:name = wp00
p0:position = 129.0, 2.5, -340.0

[esc_to_my_level_if_reject_way]
level = l01_escape
points = p0,p1
p0:name = wp00
p0:position = 112.0, 2.5, -335.0
p0:links = p1(1)
p1:name = wp01
p1:position = 110.0, 2.5, -330.0
```

Two level changers on one level that overlap are logged `!` with their sources: both stay, and
whichever the actor touches first wins. Remove one with `[spawn_remove]`, or, if it was added with
`[spawn_add@]`, delete its section with DLTX `!![spawn_add@<name>]`. Overlap here means their
positions are closer than their sizes added up (a sphere's radius, half a box's longest edge).
Changers that lead to the same level aren't reported.

#### Smart covers

`[spawn_add@]` and `[spawn_patch@]` take these keys on a smart cover (`section = smart_cover`). On any
other object they log `unknown key`. A section's smart cover keys apply together after its other
keys, in any order. Like every add and patch, they reach new games only. The defaults below are
for `[spawn_add@]`; `[spawn_patch@]` keeps the template's value for a key it leaves out.

| Key | Meaning |
|---|---|
| `description = <id>` | an id of the Lua table `smart_covers.descriptions`: `animpoint_sit_ass`, `combat_front`, ... Required on an add. An unknown id skips the add section, or every smart cover key of a patch (`!` log). |
| `loopholes = <id>, <id>` | the loopholes to enable, the description's others are disabled. If omitted, an add enables every loophole of the description, and a patch keeps the cover's loopholes, or enables every loophole of a new `description`. An id the description doesn't have is dropped (`!` log). A list with no id of the description skips the add section, or every smart cover key of a patch (`!` log). |
| `hold_position_time = <seconds>` | `0` if omitted. These three take a number from 0 to 100000; any other value is ignored (`!` log). The SDK editor offers 0 to 60 here and 0 to 100 for the distances. |
| `enter_min_enemy_distance = <m>`, `exit_min_enemy_distance = <m>` | the `[smart_cover]` section's values if omitted (15 and 10 in stock; a mod that overrides `[smart_cover]` changes them) |
| `is_combat_cover = true`, `can_fire = true` | `is_combat_cover` is the `[smart_cover]` section's value if omitted, and stock makes every cover a combat cover, so an animpoint sets both to `false`. Without a valid `can_fire`, a section that gives a valid `is_combat_cover` sets it as the engine does: a combat cover can fire and any other takes `can_fire` from the object's system.ltx section (`false` for stock `[smart_cover]`, or when the section has no `can_fire`). A mod that overrides `[smart_cover]` changes these defaults. A patch that gives neither keeps the cover's `can_fire`. A value other than `true`, `false`, `yes`, `no`, `on`, `off`, `1` or `0` (any case) is ignored (`!` log). |

The cover's script state (`se_smart_cover`) holds the loophole choice, so a saved game keeps it.
The save keeps the description id too. Removing the mod whose `smart_covers.descriptions` entry a
saved cover uses breaks that save: `se_smart_cover.script` logs `has wrong description` and stops
on the next line.

A file with `[spawn_add@]` smart covers needs an exe that has these keys. An older exe still adds the
cover, without a description, and `se_smart_cover.script` stops the new game on it.

```ini
; an animpoint for the Marsh base camp
[spawn_add@mar_smart_terrain_base_animpoint_8]
section         = smart_cover
level           = k00_marsh
position        = -146.3, 0.59, -290.5
direction       = 0, -2.817, 0
sphere          = 2
description     = animpoint_sit_ass
is_combat_cover = false
can_fire        = false

; a combat cover with its two standing loopholes off
[spawn_add@mar_combat_cover_1]
section            = smart_cover
level              = k00_marsh
position           = -176.4, 1.83, -280.0
sphere             = 2
description        = combat_front
loopholes          = crouch_front_left, crouch_front, crouch_front_right
hold_position_time = 30
```

### [path_remove]

Removes patrol paths by name, one per line, after pack paths were added. To replace a compiled
path, a `patrol_paths.ltx` section with `level = <name>` and the path's name is enough; no remove is
needed.

### [level_cut]

Takes levels out of play, one level name per line (the value is ignored). The level stays in the
game graph, so no vertex id changes and ids baked into scripts and configs keep pointing where they
did. It is cut off, though. The following are removed, whatever added them (base, fragments, packs,
links, adds, patches):

* every edge between it and another level;
* every template on it;
* every level changer on another level that leads to it;
* every patrol path on it.

A level a pack would rebuild isn't rebuilt (`*` log). A level that only a pack brings isn't added at
all (`*` log), and its level id stays reserved.

The actor template is kept (`!` log). A removed object with a story id is logged `!`; that covers
the engine `story_id` / `spawn_story_id` and a custom-data `[story_object] story_id`. The mod that
cuts the level must fix the scripts and tasks that use it, and the configs naming the level's smart
terrains (simulation, PDA map, start positions).

Anomaly's transitions are `sr_teleport` space restrictors (`sr_teleport_sections.ltx`) standing on
the neighbouring levels. They carry the PDA spot and the travel prompt, and they aren't level
changers, so list them in `[spawn_remove]` (for example `esc_space_restrictor_to_pole_1` when
cutting `y04_pole`).

Cutting a level that is in the graph is save-compatible like `[spawn_remove]`: objects an older save
already holds stay on the level, unreachable. Cutting a level that only a pack brings changes the
spawn GUID (see [Spawn GUID and saves](#spawn-guid-and-saves)).

---

## Objects: level.spawn fragments

Drop SDK-format `level.spawn` files into `spawns\<level_name>\<anything>.spawn`. They come from
LevelEditor "Build -> spawn", or Universal ACDC `-compile <dir> -level`.

* Every record becomes a template of that level. Its graph vertex is the one nearest the record
  position and its AI node is that vertex's node (see [Rules of thumb](#rules-of-thumb)); the graph
  vertex, AI node and spawn ids baked into the file are ignored. A level
  changer's destination is resolved from its destination level name and position.
* A record the level already has (same name within 1 m of the same position) is skipped silently,
  so a whole-level SDK export works. `[spawn_replace]` takes the fragment's record instead.
* Creatures (scripts spawn them), records nested in another object and unknown sections are skipped
  with a log line. `graph_point` records are ignored.
* A record whose `story_id` / `spawn_story_id` another template already holds has that id cleared
  (`!` log) rather than being skipped outright.
* A level changer whose destination level isn't installed is skipped (see
  [Levels that are not installed](#levels-that-are-not-installed)).

Fragments are new-game content.

---

## Patrol paths

### patrol_paths.ltx

`configs\patrol_paths.ltx` (vanilla root; extend it with `mod_patrol_paths_<mod>.ltx`) accepts a
section-level `level = <level name>` key. The points of such a section may omit `game_vertex_id` and
`level_vertex_id`. The engine approximates them from the position at load and snaps them to the AI
map when the level is entered, as xrAI would have compiled them. `p<i>:flags` is optional in any
section, and a point without it has flags 0.

```ini
[my_mod_walk]
level = l01_escape
points = p0,p1
p0:name = wp00
p0:position = 12.3,4.5,-6.7
p0:links = p1(1)
p1:name = wp01
p1:position = 15.3,4.5,-6.7
```

A section with `level = <name>` named like a compiled path replaces it (`*` log); a section without
one can't, and is skipped on the clash. Any section that can't be loaded (missing key, unknown link
target) is skipped and logged `!` instead of failing; a compiled path it would replace is kept.

### level.game fragments

The binary equivalent is an SDK `level.game` (LevelEditor "Build" output, next to `level.spawn`)
dropped into `spawns\<level_name>\<anything>.game`. Its paths are loaded the same way, approximated
and snapped on that level.

A path whose name is already known (compiled, `patrol_paths.ltx`, an earlier fragment) is skipped
silently, so a whole-level export adds only the level's new paths. To replace a compiled path, use a
`patrol_paths.ltx` section. Only the patrol path chunk is read; the multiplayer respawn points in
`level.game` are ignored.

---

## Level packs

A level pack is an `all.spawn` that adds levels, renamed to `spawns\<pack name>.spawn` (any name
but the base spawn's). Every `*.spawn` directly in `spawns\` other than the base spawn is opened as
a pack, so don't leave backup copies there. Several packs load in file-name order.

### What is taken from a pack

Only what the base lacks:

* **levels absent from the base graph**, with their edges, death points and cross tables (see
  [Registering a pack level](#registering-a-pack-level));
* **spawn records the base doesn't have.** The same name on the same level within 1 m is the same
  object; records on a level the pack added are all new. Records are exact on levels the pack added
  or rebuilt, and placed by position on other levels;
* **patrol paths whose names are new;**
* **the objects and patrol paths it owns on a level it rebuilds**, see
  [Rebuilt levels](#rebuilt-levels).

Everything else the pack holds about existing levels, records and paths is ignored. Use the
[`spawn_overlays.ltx` sections](#spawn_overlaysltx-sections) for such edits: for example
`[spawn_remove]` for changers the pack retires and `[graph_unlinks]` for edges it drops.

A record's `story_id` / `spawn_story_id` that another template already holds is cleared, logged
`!`, as with fragments.

A record added on a level the pack didn't add is still added, but logged `!` when it looks like an
existing object (base, fragment or earlier pack) the pack's build changed. That happens when a
template of the same section stands within 1 m of it on the same level ("renamed?"), or a template
of the same name is on another level ("moved?"). Settle a rename with `[spawn_remove]` of the old
name, and a move with `[spawn_replace]`.

### Registering a pack level

Each level a pack adds needs a `[levelNN]` section listed under `game.ltx` `[levels]` (name, id,
offset; see [`sync_level_offsets`](#settings)). Name the section after the level, so two mods can't
declare the same one (a duplicate section stops DLTX):

```ini
![levels]
level_my_level

[level_my_level]
name     = my_level
caption  = "my_level"
offset   = 3000.0, 1000.0, 3000.0
weathers = [default]
id       = auto
```

**Level id.** The level gets that section's id, never the pack's. Keep it in sync with any level
id the mod's scripts hardcode. A level whose id is missing, outside 1..254, or already taken (by a
base level or an earlier pack) is skipped and logged `!`. A numeric id is never renumbered. Levels
the base already has keep their ids.

With `id = auto` instead of a number, the engine picks the id. Use it when nothing shipped with the
level (scripts, configs) hardcodes its number. Explicit ids always win: every base level id and
every numeric id listed under `[levels]` is reserved, installed or not. Then every `id = auto` level
the base lacks, installed or not, cut or not, takes the highest free id from 254 down, in name
order (logged `game.ltx: level 'x' id = auto -> N`). So an auto id depends on the whole `[levels]`
list: a new auto level whose name sorts earlier renumbers the ones after it. Scripts of such a
level find it by name (`alife():level_name(id)`, `game_graph():levels()`), never by number.

**Offset.** It also comes from that section. Without an `offset` key the pack's own is used.

**Level set.** `game.ltx` `[levels]` is the level set. Lua reads it as `game.level_names()`: the
`[levels]` names minus the `[level_cut]` levels, sorted; it works in the main menu too.
A pack level that an install may not have ships its `[levelNN]` in the same mod option as its
`levels\<level>` folder:

| `[levelNN]` | `levels\<level>` | Log |
|---|---|---|
| yes | yes | added |
| no | no | `-` not installed |
| yes | no | `!` |
| no | yes | `!` |

### Rebuilt levels

A pack rebuilds a level of the base all.spawn when you install the pack's
`levels\<level>\level.ai` over the base one: the base build no longer matches the installed
`level.ai`, so the pack whose build matches is used. Only one pack rebuilds a level, the first in
file-name order whose build matches; other packs' builds of it are skipped. A level a pack adds is
never rebuilt.

**The graph** of the level always comes from the pack: graph points, AI nodes, death points, inner
edges, cross table and GUID. The pack's graph points take the level's vertex ids in order, extra
points get new ids, and leftover ids lead to the level's first point, so ids written for the old
build still land on the level. Edges to other levels stay on their vertex ids; fix connection
points the pack moved or removed with `[graph_unlinks]` (`level_a = level_b`) and `[graph_links]`.

**Objects and patrol paths** each have one owner on the level: the rebuilding pack if it has any on
the level (a record counts where it stands, a path on the level of its first point on the AI map),
otherwise the base. Objects and paths are decided separately.

| Owner | The level gets |
|---|---|
| the pack | the pack's set as it is. The base's set and other packs' on the level are dropped; for objects, so are the level changers on other levels that lead into it. |
| the base | the base's set, moved to the new AI map by position: objects, path points, and the arrival point of level changers into the level. Other packs add theirs as on any base level. |

Either way, the overlay edits apply to what the owner brought: `[spawn_remove]`, `[spawn_patch@]`,
[`[spawn_add@]`](#spawn_addname), fragments, and `patrol_paths.ltx` sections (one with `level =`
replaces a path of the same name).

What follows from it:

* **One record or path makes the pack the owner.** A pack that adds a level and puts a single
  level changer on the rebuilt one owns its objects, and all of its vanilla objects are dropped. To
  add a few objects or paths to a rebuilt level without that, use fragments (`spawns\<level>\`),
  `[spawn_add@]` or `patrol_paths.ltx` instead of the pack.
* **What the owner lacks is gone.** When the pack owns the objects, a vanilla level changer or story
  object it has no copy of is removed (`!` log). Restore what you need with `[spawn_add@]`.
* **Objects find their paths by name** (smart terrain jobs, `path_walk` and similar logic keys).
  When objects and paths have different owners, every path the objects name must be in the paths'
  owner or in `patrol_paths.ltx`.
* **Other packs' level changers** on, or leading into, one of their own added or rebuilt levels
  are kept even when the pack owns the objects, so their levels stay connected. Two changers are
  the same only within 1 m; when they overlap (logged `!`), remove one with `[spawn_remove]`
  `<name> = x, y, z`.
* **Base content on a new AI map** can end up off it. Check the log after the first load: move
  objects and arrival points with `[spawn_patch@]` `position` / `dest_position`, and fix paths in
  `patrol_paths.ltx`.

**Shipping a rebuild.** A mod that recompiles a vanilla level ships that `level.ai` plus its
`all.spawn` renamed, and owns the level's objects and paths. A mod that only fixes the AI map ships
the same with the spawn chunk (1) and patrol path chunk (3) removed, so the base keeps owning them
and other mods' edits still apply. Both change the spawn GUID (see
[Spawn GUID and saves](#spawn-guid-and-saves)).

Either way the level must carry its own GUID, which a fresh xrAI compile writes. A build that
reuses the merged build's GUID is not picked (`! ... has the same GUID, so it was not picked`).

### Partial packs

A level that can't be added or rebuilt (not installed, wrong build, no `[levelNN]` section or id)
only holds back that level. The pack's records and paths on levels that did work are still added.
The ones on the missing level are skipped with a `-` log line.

### Connecting packs

Connections between two independently installed packs are ordinary `[graph_links]` lines plus level
changer fragments. Either mod can ship them: while the other pack's level isn't installed, they
are skipped (see [Levels that are not installed](#levels-that-are-not-installed)).

### Spawn GUID and saves

A save stores the spawn GUID. Loading it under a different GUID aborts the game with "Saved game
doesn't correspond to the spawn : DELETE SAVED GAME!", unless the game runs with
`-ignore_save_incompatibility`.

The spawn GUID is the base spawn's GUID combined with every pack that adds or rebuilds a level.
For each such pack it takes in:

* the pack's file name, so renaming the pack changes it;
* the pack's own all.spawn GUID, so a rebuilt pack (a mod update) changes it;
* the id and name of every level whose id differs from its id in the pack, so a renumbered level
  changes it (for example an `id = auto` level shifted by a new auto level);
* when some of the pack's levels are missing (not installed, cut, another build), the GUIDs of the
  levels it did add, so installing or removing part of a pack changes it.

A pack that only adds records or paths is save-compatible and doesn't affect the GUID. It is logged
with a `(save-compatible, does not change the spawn GUID)` marker.

A save made with a pack's own all.spawn as the base spawn (the mod installed the classic way) still
loads: its vertex and spawn ids are translated to the assembled spawn, as long as all the pack's
levels are in the graph and the pack has its spawn records.

---

## Levels that are not installed

A line that names a level that isn't installed is skipped with a `-` log line, such as
`- [spawn_overlays] ... level '<name>' is not installed, skipped`. That is not an error, so a mod
can ship lines for optional levels. Whether a level is installed is decided once per load, when
the graph and spawn are assembled; nothing waits for a level. This holds for links, unlinks,
patches, adds, fragments (`.spawn` and `.game`), packs, `patrol_paths.ltx` and `[level_cut]`.

For a pack's own levels, installed means `levels\<level>\level.ai` exists. For everything else it
means the level is in the assembled game graph: in the base spawn, or added by a pack.

The exception is the `level` key of `[spawn_patch@]` and `[spawn_add@]` sections, and the `dest_level`
key of `[spawn_add@]`: they gate the whole section, not just that one line.

---

## Application order

One load runs these steps in this order.

**all.spawn chunk 4 (game graph)**

1. `[settings] sync_level_offsets`: base levels moved to their `game.ltx` offset, their edges
   reweighted.
2. Packs: levels the base lacks are appended, with the pack's edges and weights. A base level whose
   `level.ai` the pack matches and the base doesn't is rebuilt from the pack.
3. `[graph_unlinks]`, `[graph_unlinks@*]`.
4. `[graph_links]`, `[graph_links@*]`.
5. `[level_cut]`: every edge to or from the level removed.

**all.spawn chunk 1 (spawn templates)**

1. `[spawn_replace]`: base templates removed so a copy below is taken.
2. Rebuilt levels: base templates on the level, and base level changers into it, removed when the
   pack owns the objects; otherwise moved to the new AI map.
3. `spawns\<level>\*.spawn` fragments: records the base lacks are added.
4. `spawns\<pack>.spawn` packs: records the base lacks are added (all packs, sorted by file name).
5. `[spawn_remove]`: applies to base, fragment and pack templates.
6. `[spawn_add@*]`: objects defined in ltx are added.
7. `[spawn_patch@*]`: applies to base and added templates alike.
8. `[level_cut]`: templates on the level, and level changers into it, removed.
9. Level changers on one level that overlap are logged `!`.

**all.spawn chunk 3 (patrol paths)**

1. `patrol_paths.ltx` sections, with or without a `level = <name>` key. A section with one replaces
   a compiled path of the same name; one without is skipped on a clash.
2. `spawns\<level>\*.game` fragments: paths whose names are new.
3. Rebuilt levels: compiled paths on the level removed when the pack owns the paths. The path
   points left on the level are moved to the new AI map.
4. Packs: paths whose names are new.
5. `[path_remove]`.
6. `[level_cut]`: paths on the level removed.

**Spawn GUID** = the base GUID combined with every pack that added or rebuilt a level (see
[Spawn GUID and saves](#spawn-guid-and-saves)).

---

## Log

Every line starts with `[spawn_overlays]`, after a prefix:

| Prefix | Meaning |
|---|---|
| `*` | applied, or information (an edge already linked, a pack with nothing new) |
| `!` | a problem: the line or record was skipped, or applied with a warning (story id cleared, actor kept, renamed? / moved?), or a reported clash (level changers that overlap) |
| `-` | skipped because a level is not installed; not an error |

The `spawn_overlays_dump <file name>` console command writes the assembled spawn to
`$app_data_root$`.
