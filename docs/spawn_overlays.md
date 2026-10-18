# Spawn overlays

---

## Intro

Spawn overlays are edits the engine applies on top of the loaded `all.spawn`, every time the spawn
registry loads (a new game and every save load). Nothing is written back: `all.spawn` and the level
files stay as shipped.

They come from four places:

| Source | What it adds or changes |
|---|---|
| `configs\spawn_overlays.ltx` | graph links and unlinks, removals, patches, level cuts |
| `spawns\<level>\<anything>.spawn` / `.game` | objects and patrol paths on one level (SDK exports) |
| `spawns\<pack>.spawn` | level packs: whole levels with their objects and paths |
| `configs\patrol_paths.ltx` | patrol paths written as text |

Mods extend `spawn_overlays.ltx` from `configs\mod_spawn_overlays_<mod>.ltx` with `![section]` (DLTX).
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
| add patrol paths | a [level.game fragment](#patrol-paths): `spawns\<level>\<anything>.game` (SDK export), or `patrol_paths.ltx` sections with a `level = <name>` key |
| fix or replace a compiled patrol path | a `patrol_paths.ltx` section of the same name, with `level = <name>` |
| delete a patrol path | [`[path_remove]`](#path_remove) |
| take a whole level out of play | [`[level_cut]`](#level_cut) |

---

## Rules of thumb

* **The base `all.spawn` always wins over a pack.** A pack record that names an object the base
  already has on the same level within 1 m is ignored, even when the pack's copy differs. Express
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

**Levels** are named by their level name.

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

* `link_tolerance`: the largest distance in metres from an endpoint to its graph vertex.
* `sync_level_offsets`: moves each level of the base spawn to the `offset` of its `[levelNN]`
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
instance's position as the value. It affects new games only: objects already created from the
template live in the save. It applies after fragments and packs, so a removed object stays removed
even when a pack still carries it. The removed template's `story_id` and `spawn_story_id` are freed
for a `[spawn_patch@]` to reuse. Removing a template that has a `spawn_story_id`, here or in
`[spawn_replace]`, is logged `!` because scripts that spawn it by that id will fail; it is still removed.

`[spawn_replace]` uses the same syntax but applies before fragments and packs. The base template
goes, and a fragment's or pack's record of the same name at the same place is taken instead. Without
it, the base would win over that record.

### [spawn_patch@\<name\>]

Edits one spawn template. The section suffix is the object name. All keys are optional:

| Key | Meaning |
|---|---|
| `match_position = x, y, z` | selects the instance when the name is shared (within 0.5 m) |
| `level = <level name>` | moves the object to another level. Needs `position` too, because coordinates are level-local and don't carry over. A level that isn't installed skips the **whole** section, with no key applied. An empty value or a missing `position` also skips the whole section (`!` log). |
| `position = x, y, z` | level-local position; the graph vertex and AI node are re-resolved |
| `direction = x, y, z` | |
| `custom_data = ...` | `\n` is a line break and `\\` a literal backslash (needed before a folder starting with `n`) |
| `custom_data_cfg = scripts\x.ltx` | shorthand for `custom_data = [logic]\ncfg = scripts\x.ltx`, with no escaping |
| `story_id = <int>` | `-1` = none. An id another template already holds is skipped (`!` log). |
| `spawn_story_id = <int>` | `-1` = none. An id another template already holds is skipped (`!` log). |
| `object_flags = 0x...` | |
| `dest_level = <level name>` | level changers only: the level it leads to. Needs `dest_position`. A level that isn't installed skips the three `dest_*` keys. |
| `dest_position = x, y, z` | level changers only: arrival point on the destination level; its graph vertex and AI node are re-resolved |
| `dest_direction = x, y, z` | level changers only: the actor's facing on arrival |

```ini
[spawn_patch@gar_physic_object_bandit_base_generator]
position = 63.9, 0.6, -23.1

[spawn_patch@yan_level_changer_to_dead_city_1]
dest_position  = 12.3, 4.5, -6.7
dest_direction = 0, 1.57, 0
```

### [path_remove]

Removes patrol paths by name, one per line, after pack paths were added. To replace a compiled
path, a `patrol_paths.ltx` section with `level = <name>` and the path's name is enough; no remove is
needed.

### [level_cut]

Takes levels out of play, one level name per line (the value is ignored). The level stays in the
game graph, so no vertex id changes and ids baked into scripts and configs keep pointing where they
did. It is cut off, though. The following are removed, whatever added them (base, fragments, packs,
links, patches):

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

* Every record becomes a template of that level. Its graph vertex and AI node come from the record
  position; the graph vertex, AI node and spawn ids baked into the file are ignored. A level
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
map when the level is entered, as xrAI would have compiled them.

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
* **everything on a level it rebuilds**, see [Rebuilt levels](#rebuilt-levels).

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
`levels\<level>\level.ai` over the base one. The installed `level.ai` decides which build is used:

* the base build no longer matches it, so the pack whose build matches takes the level over: its
  graph points, AI nodes, death points, inner edges, cross table and GUID come from that pack;
* only one pack rebuilds a level. Other packs' builds of it are skipped, and so is their content on
  it (see below).

A level a pack adds is never rebuilt: it only joins when its build matches the installed
`level.ai`.

The pack's graph points take the level's vertex ids in order: its first point gets the level's
first id, and so on. Points past the level's count are appended, with ids that depend on the
installed packs. When the pack has fewer points, the leftover ids lead to the level's first point,
so ids written for the old build still land on the level.

The level's objects and patrol paths come from the pack, and so do the level changers on other
levels that lead into it: the base all.spawn's ones are dropped. A vanilla level changer or story
object the pack has no copy of is removed (`!` log). `[spawn_remove]`, `[spawn_patch@]` and
`patrol_paths.ltx` sections apply to the pack's copies. Other packs' objects and paths on the
level, and their level changers into it, are skipped. A pack's level changer that stands on or leads
into one of its own added or rebuilt levels is kept; when two packs carry it, the first in file
order wins. Copies are the same changer only within 1 m of each other; remove a farther duplicate
with `[spawn_remove]` `<name> = x, y, z`.

Edges to other levels stay on their vertex ids. A pack that moves or removes a level's connection
points fixes the connections with `[graph_unlinks]` (`level_a = level_b`) and `[graph_links]`.

So a mod that recompiles a vanilla level ships that `level.ai` plus its `all.spawn` renamed. The
level must carry its own GUID, which a fresh xrAI compile writes. A build that reuses the merged
build's GUID is not picked (`! ... has the same GUID, so it was not picked`).

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
levels are in the graph.

---

## Levels that are not installed

A line that names a level that isn't installed is skipped with a `-` log line, such as
`- [spawn_overlays] ... level '<name>' is not installed, skipped`. That is not an error, so a mod
can ship lines for optional levels. Whether a level is installed is decided once per load, when
the graph and spawn are assembled; nothing waits for a level. This holds for links, unlinks,
patches, fragments (`.spawn` and `.game`), packs, `patrol_paths.ltx` and `[level_cut]`.

For a pack's own levels, installed means `levels\<level>\level.ai` exists. For everything else it
means the level is in the assembled game graph: in the base spawn, or added by a pack.

The exception is a `[spawn_patch@]` section's `level` key: it gates the whole section, not just that
one line.

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
2. Rebuilt levels: base templates on the level, and base level changers into it, removed.
3. `spawns\<level>\*.spawn` fragments: records the base lacks are added.
4. `spawns\<pack>.spawn` packs: records the base lacks are added (all packs, sorted by file name).
5. `[spawn_remove]`: applies to base and added templates alike.
6. `[spawn_patch@*]`: applies to base and added templates alike.
7. `[level_cut]`: templates on the level, and level changers into it, removed.

**all.spawn chunk 3 (patrol paths)**

1. `patrol_paths.ltx` sections, with or without a `level = <name>` key. A section with one replaces
   a compiled path of the same name; one without is skipped on a clash.
2. `spawns\<level>\*.game` fragments: paths whose names are new.
3. Rebuilt levels: compiled paths on the level removed.
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
| `!` | a problem: the line or record was skipped, or applied with a warning (story id cleared, actor kept, renamed? / moved?) |
| `-` | skipped because a level is not installed; not an error |

The `spawn_overlays_dump <file name>` console command writes the assembled spawn to
`$app_data_root$`.
