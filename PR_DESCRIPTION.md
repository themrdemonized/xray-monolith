# Lua Combat AI Hook System

## Motivation

Several classes of NPC combat behaviour were hardcoded with no Lua entry point, making
rank-based or situation-aware AI tuning impossible from scripts:

- NPCs shot in the open had no suppression path — `brain().affect_cover()` must be `true`
  (already in cover) for any hit-reaction to trigger re-evaluation. Open-field NPCs eating
  bullets had nothing
- Grenade throwing was pure C++ with no gate. Novices threw grenades with perfect accuracy
  and timing regardless of rank
- Enemy acquisition/loss required polling `npc:best_enemy()` every frame in `npc_on_update`,
  iterating `db.storage` 4×/second for every NPC in combat
- All cover selection distances, danger urgency scores, and fire suppression timing were
  hardcoded constants — untunable without a recompile

This PR adds a complete Lua hook system for combat AI, 6 new NPC object exports, and 27
console variables covering all of the above. All changes are backwards-compatible: defaults
match original engine values exactly.

---

## Changes by File

### `src/xrGame/ai/stalker/ai_stalker_fire.cpp`

**Hooks added:**

- `npc_on_critically_wounded(npc, hit, bone_id)` — fires exactly once when an NPC
  transitions into critically wounded state. Placement verified against the full `Hit()`
  pipeline: fires after rank immunity and bone armor are applied, before `BeforeHitCallback`.
  The critical wound determination uses post-bone-armor power — intentional, as
  `BeforeHitCallback` reducing power afterwards cannot retroactively un-wound the NPC

- `npc_on_hit_reaction(npc, hit, bone_id, is_in_cover)` — fires after `inherited::Hit()`
  applies all damage, only on living NPCs. `is_in_cover` reflects cover state at time of
  hit. Primary use: call `npc:best_cover_invalidate()` to force open-field NPCs to seek cover
  after being hit

- `npc_on_weapon_shot_start(npc, weapon)` / `npc_on_weapon_shot_stop(npc)` — pair of hooks
  on the empty virtual stubs `on_weapon_shot_start/stop`. Used together to measure burst
  duration for rank-based burst fire control

- `npc_on_missile_throw_force(npc, base_force, flags)` — return-value hook inside
  `missile_throw_force()`. Scales grenade throw distance by rank. Returns validated: invalid
  or non-positive values are logged and ignored, engine default is preserved

- `npc_on_enemy_wounded_or_killed(npc, victim)` — fires after the kill-cry sound plays on
  the attacker NPC. Null check added on `wounded_or_killed` before `lua_game_object()` call

**Console variables converted** (were `static const`, now globals):

| Variable | Default | Min | Max | What it controls |
|---|---|---|---|---|
| `ai_fire_make_sense_interval` | 10000 ms | 0 | 20000 | How long NPCs fire at last known position after losing sight. 0 = only while visible |
| `ai_fire_range_extension` | 2.5 m | 0 | 10 | Extra range past pick distance where suppression fire makes sense |
| `ai_fire_max_height_diff` | 2.0 m | 0 | 8 | Max vertical gap between NPC and enemy for fire to make sense |
| `ai_fire_min_dist` | 2.5 m | 0 | 20 | Minimum pick distance required for fire to make sense |

---

### `src/xrGame/ai/stalker/ai_stalker_cover.cpp`

**Hooks added:**

- `npc_on_best_cover_changed(npc, cover_pos, is_smart_cover, smart_cover_name)` — fires in
  `on_best_cover_changed()` after internal delegates. `cover_pos` is nil when NPC loses cover
  entirely. `smart_cover_name` is the level object `cName()` of the smart cover position.
  Primary use: squad coordination (detecting when two NPCs select the same position)

- `_g.CAI_Stalker__GetMinCombatDist(npc, base_min)` / `_g.CAI_Stalker__GetMaxCombatDist(npc, base_max)`
  — return-value hooks in `compute_enemy_distances()` after weapon-type defaults are computed.
  Override the min/max distance from enemy that NPCs accept when selecting a cover point.
  Return values validated with `_valid() && >= 0`; invalid returns log with NPC name and fall
  back to engine default. The `goto` originally used for no-weapon fallthrough replaced with
  `if/else` for cleaner control flow

**Console variables converted** (were `static const` / `static const u32`):

| Variable | Default | Min | Max | What it controls |
|---|---|---|---|---|
| `ai_cover_search_near_radius` | 10.0 m | 1 | 30 | First-pass cover search radius |
| `ai_cover_search_far_radius` | 30.0 m | 1 | 80 | Second-pass fallback search radius |
| `ai_cover_pistol_max_dist` | 10.0 m | 3 | 50 | Max cover distance for pistol wielders |
| `ai_cover_shotgun_max_dist` | 5.0 m | 3 | 20 | Max cover distance for shotgun wielders |
| `ai_cover_sniper_min_dist` | 20.0 m | 0 | 80 | Min cover distance for sniper wielders |
| `ai_cover_default_max_dist` | 20.0 m | 3 | 50 | Max cover distance for all other weapons |

**Min reasoning:** `3.0m = MIN_SUITABLE_ENEMY_DISTANCE` — the engine's internal starting
minimum. Below this, `compute_enemy_distances`'s clamp collapses min and max to the same
value, giving the cover evaluator a zero-width range that always fails. `ai_cover_sniper_min_dist`
min is `0.0` since zero means "no extra constraint beyond engine default 3m".

---

### `src/xrGame/ai/stalker/ai_stalker_misc.cpp`

**Hook added:**

- `npc_on_member_death_reaction(npc, member, is_alive)` — fires in `react_on_member_death()`
  after the tolls/wound sound plays. Rate limited in C++ by `TOLLS_INTERVAL = 2000ms` — the
  early-return check runs before the hook, guaranteeing max 1 fire per 2000ms per NPC.
  `is_alive=true` means the member was wounded, `false` means killed. `member` may be nil
  in edge cases — scripts must guard

Added includes: `script_game_object.h`, `ai_space.h` (required for `lua_game_object()` and
`ai().script_engine().functor()`)

---

### `src/xrGame/stalker_combat_planner.cpp`

**Hook added:**

- `npc_on_combat_action_changed(npc, old_op, new_op)` — fires in `update()` when the GOAP
  planner transitions to a new action. `old_op`/`new_op` match `stalker_ids.action_*` values.
  Guarded: `was_initialized` flag prevents firing during first planner initialization (which
  would pass `u32(-1)` as `old_op`)

---

### `src/xrGame/enemy_manager.cpp`

**Hook added:**

- `npc_on_enemy_selected(npc, new_enemy)` — fires in `try_change_enemy()` when `selected()`
  changes value. `new_enemy` is nil on target loss. Replaces per-frame polling of
  `npc:best_enemy()` with an event-driven notification

---

### `src/xrGame/stalker_property_evaluators.cpp`

**Hook added:**

- `npc_on_should_throw(npc, flags)` — gate hook in `CStalkerPropertyEvaluatorThrowGrenade::evaluate()`.
  Hooked in the **evaluator**, not the action executor — blocking here prevents
  `eWorldPropertyShouldThrowGrenade` from being set TRUE, so the GOAP planner never routes
  to ThrowGrenade. No stale world properties result from cancellation. Only fires when
  trajectory is geometrically clear and NPC has a grenade; in-flight throws cannot be cancelled

---

### `src/xrGame/stalker_combat_actions.cpp`

**Console variables converted** (were `const float` / `const u32`):

| Variable | Default | Min | Max | What it controls |
|---|---|---|---|---|
| `ai_close_move_distance` | 1.5 m | 0 | 6 | TakeCover: distance at which NPC switches from running to in-place movement |
| `ai_crouch_look_out_delta` | 5000 ms | 0 | 12000 | LookOut: how often NPC randomises crouch/stand. The "robotic synchronized bob" |
| `ai_wait_in_smart_cover_time` | 30000 ms | 0 | 120000 | How long NPC waits at smart cover for enemy to appear before giving up |
| `ai_cover_detour_radius` | 5.0 m | 0 | 15 | Danger zone radius when NPC abandons cover to flank |
| `ai_cover_detour_time` | 120000 ms | 0 | 300000 | Duration of that danger flag |

---

### `src/xrGame/stalker_danger_unknown_actions.cpp`

**Console variables converted:**

| Variable | Default | Min | Max | What it controls |
|---|---|---|---|---|
| `ai_cover_unknown_radius` | 5.0 m | 0 | 15 | Danger zone radius on unknown threats (sound, grenade) |
| `ai_cover_unknown_time` | 120000 ms | 0 | 300000 | Duration of that danger flag |

---

### `src/xrGame/danger_manager.cpp`

**Console variables converted** (from raw score floats to multipliers):

The original code used hardcoded scores (3000, 2500, 2000, etc.) directly in `do_evaluate()`.
These are meaningless raw numbers to modders. Converted to multipliers applied against the
preserved base scores: `result += base_score * multiplier`.

`1.0` = identical to original engine behaviour. `0.0` = NPC ignores that danger type.
`2.0` = twice as urgent. Base scores remain hardcoded, preserving relative priorities at default.

| Variable | Default | Min | Max | Trigger | Base score |
|---|---|---|---|---|---|
| `ai_danger_ricochet_mult` | 1.0 | 0.0 | 5.0 | Bullet ricochets nearby | 3000 |
| `ai_danger_attack_sound_mult` | 1.0 | 0.0 | 5.0 | Gunfire heard | 2500 |
| `ai_danger_entity_attacked_mult` | 1.0 | 0.0 | 5.0 | Ally hit sound | 2000 |
| `ai_danger_entity_death_mult` | 1.0 | 0.0 | 5.0 | Ally death sound | 3000 |
| `ai_danger_corpse_mult` | 1.0 | 0.0 | 5.0 | Fresh corpse seen | 2250 |
| `ai_danger_attacked_mult` | 1.0 | 0.0 | 5.0 | General attack perceived | 2000 |
| `ai_danger_grenade_mult` | 1.0 | 0.0 | 5.0 | Grenade nearby | 1000 |
| `ai_danger_enemy_sound_mult` | 1.0 | 0.0 | 5.0 | Enemy sound heard | 1000 |

---

### `src/xrGame/script_game_object3.cpp` + `script_game_object.h` + `script_game_object_script2/3.cpp`

**New Lua exports on NPC game objects:**

- `npc:rank_name()` → `string` — returns the stable rank id from `CharacterInfo`
  (`"novice"`, `"experienced"`, `"veteran"`, `"master"`, `"expert"`). Use instead of
  `npc:rank()` which returns a raw integer whose thresholds modpacks change freely

- `npc:best_cover_invalidate()` — forces NPC to re-evaluate its cover point next update.
  Previously used internally by the engine; this export was missing from the Lua API

- `npc:get_current_smart_cover_name()` → `string` — level object `cName()` of the smart
  cover the NPC is currently occupying, `""` if not in a smart cover

- `npc:get_current_loophole_id()` → `string` — loophole id within the current smart cover,
  `""` if not in a smart cover. Pair with existing `npc:set_dest_loophole()` for coordination

All four return `""` safely on non-stalker objects (no crash). Non-stalker calls log a
`[section_name]` error to `xray_*.log`.

---

### `src/xrGame/console_commands.cpp`

**Cover danger zone variables** (from `ai_stalker_fire.cpp`):

| Variable | Default | Min | Max | What it controls |
|---|---|---|---|---|
| `ai_cover_danger_radius` | 3.0 m | 0 | 15 | Danger zone radius when NPC is shot while in cover |
| `ai_cover_danger_time` | 120000 ms | 0 | 300000 | Duration of that danger flag |

All 27 new variables registered here with inline comments explaining each range decision.

---

### `gamedata/scripts/callbacks_gameobject.script`

All `_G.*` engine globals wired to `SendScriptCallback` for fan-out to multiple listeners.
12 new `AddScriptCallback` registrations. All hooks follow existing naming conventions:
- Notification hooks: `_G` uppercase, `npc_on_*` callback names
- Return-value hooks: `_g` lowercase, dispatching via `flags.ret_value` pattern

---

## Error Handling

All new Lua exports and return-value hooks include explicit error logging to `xray_*.log`:

- **Non-stalker object calls** log `"CGameObject: [section] called on non-stalker"` matching
  the `cNameSect_str()` convention used by other exports in `script_game_object3.cpp`
- **Invalid return values** from `GetMinCombatDist`, `GetMaxCombatDist`, and
  `GetMissileThrowForce` are checked with `_valid() && threshold` — invalid values log the
  bad value and the NPC's `cName()`, then fall back to the engine-computed default
- **Null victim** in `OnEnemyWoundedOrKilled` logs the attacker's `cName()` instead of
  crashing on a null `lua_game_object()` call

---

## Save Compatibility

All defaults match the original hardcoded values they replace. `user.ltx` will not contain
any of the new variables on existing installs, so the C++ defaults are used — which are
identical to the original engine behaviour. Zero behaviour change for existing saves.

---

## Confirmed NOT Reachable From Lua

Investigated and confirmed hardcoded with no Lua entry point:
- **Dynamic cover** — cover points are static, pre-baked at level compile time
- **Cover scoring** — `best_cover_value()` and cover evaluators are pure C++
- **GOAP world property flags** — internal C++ state only
- **Script movement during combat** — overwritten every frame by the combat action's own path

---

## Documentation

Full reference documentation including hook signatures, fire conditions, performance
implications, thread safety confirmation, example usage, and a composite rank-based AI
example is in `AI_HOOKS.md`.
