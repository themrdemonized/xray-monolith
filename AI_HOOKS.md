# xray-monolith AI Hook System - Context Document
## Branch: `personal/ai-hooks` (fork: TheLostInPlace/xray-monolith)

This document describes all AI-related Lua hooks, new Lua exports, and console variables
added to the `personal/ai-hooks` branch of xray-monolith (STALKER Anomaly modded exes fork).
This branch is NOT upstreamed - it lives on the personal fork only.

The working PR (#560, branch `all-in-one-vs2022-wpo`) only contains `g_launcher_dynamic_range_zoom`.
Everything in this document is exclusive to `personal/ai-hooks`.

---

## Architecture Overview

All Lua↔engine hooks follow one of two patterns established in the codebase:

**Pattern A - Return-value hook** (`_g` lowercase)
The engine calls a global Lua function and uses its return value to replace or modify a
computed result. If the global does not exist, the engine's default is used unchanged.
```lua
-- Example: override weapon accuracy per NPC
_g.CAI_Stalker__GetWeaponAccuracy = function(npc, weapon, base, body_state, move_type)
    return base * 0.5 -- tighter aim
end
```

**Pattern B - Notification/gate hook** (`_G` uppercase + `SendScriptCallback`)
The engine calls a global, which fans out to any registered Lua listeners via
`SendScriptCallback`. Gate hooks use a `flags` table with `ret_value` to allow
cancellation. Scripts subscribe with `RegisterScriptCallback`.
```lua
RegisterScriptCallback("npc_on_hit_reaction", function(npc, hit, bone_id, is_in_cover)
    if not is_in_cover then npc:best_cover_invalidate() end
end)
```

**Key rule on globals vs per-NPC behavior:**
Console variables are GLOBAL - they affect all NPCs simultaneously. For rank-based or
per-NPC behavior, handle the logic entirely inside the Lua callback using `npc:rank_name()`
and `npc:best_cover_invalidate()`. Never change a global console var per-NPC.

---

## New Lua Exports on NPC Game Objects

These are new methods callable on any `CScriptGameObject` that is a stalker NPC.

### `npc:rank_name()` → `string`
Returns the stable rank id string directly from `CharacterInfo`.
- **Returns:** `"novice"`, `"experienced"`, `"veteran"`, `"master"`, `"expert"`
- **Use instead of:** `npc:rank()` which returns a raw integer whose thresholds mods change
- **Stable across modpacks:** rank names come from character description XMLs, not numeric thresholds
- **Non-stalker objects:** Returns `""` (empty string) and logs an engine error. Always guard:
  `if npc:rank_name() == "" then return end` or check `IsStalker(npc)` before calling
- **Source:** `script_game_object3.cpp` → `GetRankName()`

### `npc:get_current_smart_cover_name()` → `string`
Returns the level object name (`cName()`) of the smart cover the NPC is currently occupying.
- **Returns:** level object name string, e.g. `"smart_cover_window_01"`, or `""` if not in a smart cover
- **Returns `""`** on non-stalker objects (no error logged)
- **Use for:** squad coordination - detect when two NPCs have selected the same smart cover
- **Pair with:** `npc:get_current_loophole_id()` to identify the exact position within the cover
- **Source:** `script_game_object3.cpp` → `GetCurrentSmartCoverName()`

### `npc:get_current_loophole_id()` → `string`
Returns the loophole id string of the specific position within the current smart cover.
- **Returns:** loophole id string, e.g. `"default"`, `"left"`, `"right"`, or `""` if not in a smart cover
- **Returns `""`** on non-stalker objects (no error logged)
- **Use for:** identifying which firing position within a smart cover the NPC is using
- **Pair with:** `npc:set_dest_loophole("id")` (already exported pre-branch) to redirect NPCs
- **Source:** `script_game_object3.cpp` → `GetCurrentLoopholeId()`

### `npc:best_cover_invalidate()` → `void`
Forces the NPC to re-evaluate its cover point on the next update cycle.
- **Effect:** Sets `m_best_cover_actual = false`, triggering `update_best_cover_actuality()`
- **Primary use:** Call inside hook callbacks to make NPCs seek new cover in response to
  being shot, suppressed, or having their cover threatened
- **Source:** `script_game_object3.cpp` → `best_cover_invalidate()`

---

## Lua Hooks

### `npc_on_enemy_selected`
**Signature:** `(npc, new_enemy)`
**Pattern:** B (notification)
**Fires:** `CEnemyManager::try_change_enemy()` when `selected()` changes
**Notes:**
- `new_enemy` is `nil` when the NPC loses its target (target dropped, killed, etc.)
- Only fires on actual transitions - not every frame
- Replaces per-frame polling of `npc:best_enemy()` in `npc_on_update`
- Guarded: never fires during first planner initialization

```lua
RegisterScriptCallback("npc_on_enemy_selected", function(npc, new_enemy)
    if not new_enemy then
        -- NPC lost its target - could retreat, regroup, etc.
    end
end)
```

---

### `npc_on_combat_action_changed`
**Signature:** `(npc, old_op, new_op)`
**Pattern:** B (notification)
**Fires:** `CStalkerCombatPlanner::update()` when GOAP transitions to a new action
**Notes:**
- `old_op` and `new_op` are integers matching `stalker_ids.action_*` values
- Does NOT fire on first planner initialization (both values are always real action IDs)
- Key action IDs: `stalker_ids.action_take_cover=21`, `action_look_out=22`,
  `action_hold_position=23`, `action_detour_enemy=25`, `action_search_enemy=26`

```lua
RegisterScriptCallback("npc_on_combat_action_changed", function(npc, old_op, new_op)
    if new_op == stalker_ids.action_take_cover then
        -- NPC just started moving to cover
    end
end)
```

---

### `npc_on_hit_reaction`
**Signature:** `(npc, hit, bone_id, is_in_cover)`
**Pattern:** B (notification)
**Fires:** `CAI_Stalker::Hit()` after `inherited::Hit()` applies damage, only on living NPCs
**Pipeline position:** After outfit armor is applied and damage is finalized
**Notes:**
- `hit` is a `CScriptHit` object (same type as `BeforeHitCallback`)
- `is_in_cover` reflects `brain().affect_cover()` at time of hit
- Does NOT fire on killing blows to dead NPCs (`g_Alive()` guard)
- Call `npc:best_cover_invalidate()` here to force repositioning after being shot

```lua
RegisterScriptCallback("npc_on_hit_reaction", function(npc, hit, bone_id, is_in_cover)
    if not is_in_cover then
        npc:best_cover_invalidate() -- NPC was in the open, force cover-seek
    end
end)
```

---

### `npc_on_critically_wounded`
**Signature:** `(npc, hit, bone_id)`
**Pattern:** B (notification)
**Fires:** `CAI_Stalker::Hit()` exactly once when NPC first transitions into critically wounded state
**Verified pipeline position (confirmed against source):**
```
1. HDS.power * m_fRankImmunity              (rank immunity applied)
2. Bone armor reduces hit_power
3. HDS.power = hit_power written back
4. update_critical_wounded() called          <- critical wound determined here
5. OnCriticallyWounded fires HERE            <- our hook
6. BeforeHitCallback fires
7. ApplyScriptHit / inherited::Hit()
```
**Notes:**
- Fires BEFORE `BeforeHitCallback` and BEFORE `inherited::Hit()`
- Since this hook fires before `BeforeHitCallback`, a `BeforeHitCallback` mod that reduces
  `hit.power` below the critical threshold cannot prevent this hook from firing. The critical
  wound determination is based on post-bone-armor, pre-`BeforeHitCallback` power. This is intentional
- Never fires again for the same NPC once they are critically wounded
- `critically_wounded()` returns true when this fires

```lua
RegisterScriptCallback("npc_on_critically_wounded", function(npc, hit, bone_id)
    -- NPC just went critical - play audio, trigger retreat logic, etc.
end)
```

---

### `npc_on_best_cover_changed`
**Signature:** `(npc, cover_pos, is_smart_cover, smart_cover_name)`
**Pattern:** B (notification)
**Fires:** `CAI_Stalker::on_best_cover_changed()` after internal cover delegates run
**Notes:**
- `cover_pos` is a `vector` of the new cover point position, or `nil` when NPC loses its cover entirely
- `is_smart_cover` - `true` if the new cover point is a smart cover position
- `smart_cover_name` - level object `cName()` of the smart cover (`""` if not a smart cover)
- Fires on EVERY cover change, including during normal patrol - not combat-exclusive
- Use `npc:get_current_smart_cover_name()` and `npc:get_current_loophole_id()` inside the
  callback to get detailed position info for squad coordination

```lua
-- Squad coordination: track which smart covers are occupied
local occupied_smart_covers = {}  -- smart_cover_name -> npc_id

RegisterScriptCallback("npc_on_best_cover_changed", function(npc, cover_pos, is_smart_cover, smart_cover_name)
    local id = npc:id()
    -- release any previously held smart cover
    for name, holder_id in pairs(occupied_smart_covers) do
        if holder_id == id then occupied_smart_covers[name] = nil end
    end
    -- claim new smart cover
    if is_smart_cover and smart_cover_name ~= "" then
        occupied_smart_covers[smart_cover_name] = id
    end
end)

RegisterScriptCallback("game_object_on_net_destroy", function(obj)
    local id = obj:id()
    for name, holder_id in pairs(occupied_smart_covers) do
        if holder_id == id then occupied_smart_covers[name] = nil end
    end
end)
```

---

### `npc_on_get_min_combat_dist`
**Signature:** `(npc, base_min, flags)` → `flags.ret_value` (float)
**Pattern:** A via B (return-value via flags table)
**Engine global:** `_g.CAI_Stalker__GetMinCombatDist`
**Fires:** `CAI_Stalker::compute_enemy_distances()` after weapon-type defaults are computed
**Notes:**
- `base_min` is the engine-computed minimum distance (post weapon-type switch, pre-clamp)
- Set `flags.ret_value` to override. This is the MINIMUM distance from enemy a cover point must be
- Sniper rifles default to `ai_cover_sniper_min_dist` (20m) - overriding allows rank-based scaling
- Clamped internally: min is always ≤ max after both hooks run
- **Do NOT assign `_g.CAI_Stalker__GetMinCombatDist` directly** - use `RegisterScriptCallback`

### `npc_on_get_max_combat_dist`
**Signature:** `(npc, base_max, flags)` → `flags.ret_value` (float)
**Pattern:** A via B (return-value via flags table)
**Engine global:** `_g.CAI_Stalker__GetMaxCombatDist`
**Fires:** `CAI_Stalker::compute_enemy_distances()` after weapon-type defaults are computed
**Notes:**
- `base_max` is the engine-computed maximum distance (post weapon-type switch, pre-clamp)
- Set `flags.ret_value` to override. This is the MAXIMUM distance from enemy a cover point must be
- Shotguns default to `ai_cover_shotgun_max_dist` (5m); pistols to `ai_cover_pistol_max_dist` (10m)
- **Do NOT assign `_g.CAI_Stalker__GetMaxCombatDist` directly** - use `RegisterScriptCallback`

```lua
-- Rank-based weapon switching via cover distance: force snipers to use distant cover,
-- novices with rifles to use close cover regardless of weapon type
RegisterScriptCallback("npc_on_get_min_combat_dist", function(npc, base_min, flags)
    local rank = npc:rank_name()
    -- low-rank NPCs with any weapon: pick cover closer than the weapon type normally allows
    if rank == "novice" then flags.ret_value = 3.0 end
end)

RegisterScriptCallback("npc_on_get_max_combat_dist", function(npc, base_max, flags)
    local rank = npc:rank_name()
    -- veterans and above seek cover further away (better tactical positioning)
    if rank == "veteran" or rank == "master" or rank == "expert" then
        flags.ret_value = math.max(base_max, 15.0)
    end
end)
```

---

### `npc_on_take_cover_destination`
**Signature:** `(npc, cover_pos, enemy)`
**Pattern:** B (notification)
**Fires:** `CStalkerActionTakeCover::execute()` after a real cover point is selected and `setup_cover()` called
**Notes:**
- Does NOT fire when falling back to teammate stacking or nearest-accessible-position
- `cover_pos` is a `vector` (Fvector) of the selected cover point position
- `enemy` may be `nil` if no enemy is currently selected
- **This is an observe-only hook.** Cover-point replacement (passing a different vertex id
  back) is not yet supported - it would require exposing `setup_cover()` or a write-back
  channel. Primary uses: debug logging, playing audio when NPC starts moving to cover,
  analytics

```lua
RegisterScriptCallback("npc_on_take_cover_destination", function(npc, cover_pos, enemy)
    -- NPC is heading to this cover point
end)
```

---

### `npc_on_danger_location_add`
**Signature:** `(npc, pos, radius, in_open, cover_threatened)`
**Pattern:** B (notification)
**Fires:** `CAI_Stalker::on_danger_location_add()` for every danger type
**Thread safety:** Dispatched synchronously on the main game thread via
`CAgentLocationManager::add()`. Safe for all standard Lua operations including in the MT build.

**Danger types that trigger this:**
- Bullet ricochet nearby (`ai_danger_ricochet_score`)
- Gunfire heard (`ai_danger_attack_sound_score`)
- Ally hit sound (`ai_danger_entity_attacked_score`)
- Ally death sound (`ai_danger_entity_death_score`)
- Fresh corpse seen (`ai_danger_corpse_score`)
- General attack perceived (`ai_danger_attacked_score`)
- Grenade nearby (`ai_danger_grenade_score`)
- Enemy sound (`ai_danger_enemy_sound_score`)

**Parameters:**
- `pos` - `vector` position of the danger
- `radius` - float radius of the danger zone
- `in_open` - `true` when NPC has no cover point selected (standing in the open)
- `cover_threatened` - `true` when the danger overlaps the NPC's current cover position
- Both `false` simultaneously means: NPC has cover, danger is nearby but not overlapping
  their cover point - the most common and usually ignorable case. The early-return
  `if not (in_open or cover_threatened) then return end` in the example handles this

**PERFORMANCE WARNING - READ BEFORE USING:**
This hook fires for every danger type for every NPC in range. In a firefight with 10+ NPCs
this is 50–100+ callbacks per second. Callbacks registered here MUST use per-NPC timestamp
throttling. `string` operations, `pairs()` table iteration, and `db.storage` lookups are
not safe here without throttling.

```lua
-- REQUIRED: per-NPC timestamp throttle
local npc_danger_next = {}

RegisterScriptCallback("npc_on_danger_location_add", function(npc, pos, radius, in_open, cover_threatened)
    if not (in_open or cover_threatened) then return end

    local tg = time_global()
    local id = npc:id()
    if (npc_danger_next[id] or 0) > tg then return end  -- max 1 reaction per second per NPC
    npc_danger_next[id] = tg + 1000

    local rank = npc:rank_name()
    local threshold = ({ novice=1, experienced=2, veteran=3, master=4, expert=5 })[rank] or 2
    -- Simple example: always invalidate once throttle passes. For hit-count accumulation
    -- see the composite example at the end of this document.
    npc:best_cover_invalidate()
end)

-- Clean up when NPC goes offline (not necessarily dead - alife cycle).
-- Counter resets on offline transition; counts do NOT accumulate across online/offline cycles.
RegisterScriptCallback("game_object_on_net_destroy", function(obj)
    npc_danger_next[obj:id()] = nil
end)
```

---

### `npc_on_weapon_shot_start`
**Signature:** `(npc, weapon)`
**Pattern:** B (notification)
**Fires:** `CAI_Stalker::on_weapon_shot_start()` when NPC begins firing a burst
**Notes:**
- `weapon` is the `CScriptGameObject` of the weapon being fired (may be nil - guard for this)
- Fires at the START of each burst, not each individual shot
- Pair with `npc_on_weapon_shot_stop` to measure burst length
- Use for rank-based burst fire: force cover after N milliseconds of continuous fire

---

### `npc_on_weapon_shot_stop`
**Signature:** `(npc)`
**Pattern:** B (notification)
**Fires:** `CAI_Stalker::on_weapon_shot_stop()` when NPC ceases firing
**Notes:**
- Pair with `npc_on_weapon_shot_start` to measure how long the NPC fired continuously

```lua
local shot_start_time = {}

RegisterScriptCallback("npc_on_weapon_shot_start", function(npc, weapon)
    shot_start_time[npc:id()] = time_global()
end)

RegisterScriptCallback("npc_on_weapon_shot_stop", function(npc)
    local id = npc:id()
    local duration = time_global() - (shot_start_time[id] or 0)
    local rank = npc:rank_name()
    -- Novices fire too long and expose themselves; force cover after 1.5s
    if rank == "novice" and duration > 1500 then
        npc:best_cover_invalidate()
    end
    shot_start_time[id] = nil
end)
```

---

### `npc_on_should_throw`
**Signature:** `(npc, flags)` → `flags.ret_value` (bool)
**Pattern:** B (gate hook)
**Fires:** `CStalkerPropertyEvaluatorThrowGrenade::evaluate()` - in the EVALUATOR, not the
action executor. This means blocking here prevents `eWorldPropertyShouldThrowGrenade` from
ever being set TRUE, so the GOAP planner never routes to ThrowGrenade. No stale world
properties result from cancellation.
**Notes:**
- Only fires when trajectory is geometrically clear AND NPC has a grenade AND is in cover/hold
- Set `flags.ret_value = false` to cancel the throw entirely
- Does NOT fire when trajectory is obstructed (engine already blocks that)
- If a grenade throw is already in progress (grenade slot is active item), the evaluator
  returns `true` immediately without calling this hook - an in-flight throw cannot be cancelled

```lua
RegisterScriptCallback("npc_on_should_throw", function(npc, flags)
    local rank = npc:rank_name()
    if rank == "novice" then
        flags.ret_value = false -- novices don't throw grenades
    elseif rank == "experienced" then
        flags.ret_value = math.random() > 0.5 -- 50% chance
    end
    -- veteran/master/expert: always throw (default true)
end)
```

---

### `npc_on_missile_throw_force`
**Signature:** `(npc, base_force, flags)` → `flags.ret_value` (float)
**Pattern:** A-style via B (return-value via flags table)
**Engine global:** `_g.CAI_Stalker__GetMissileThrowForce`
**Fires:** `CAI_Stalker::missile_throw_force()` after throw velocity is computed
**How to use:** Call `RegisterScriptCallback("npc_on_missile_throw_force", ...)` - do NOT
assign `_g.CAI_Stalker__GetMissileThrowForce` directly. The `_g` global is already wired
in `callbacks_gameobject.script` to dispatch to the callback. Overwriting the `_g` global
directly would bypass all other registered listeners.
**Notes:**
- `base_force` is the engine-computed throw force magnitude
- Set `flags.ret_value` to override. Lower = shorter throw range
- Analogous to `_g.CAI_Stalker__GetWeaponAccuracy` for grenades
- Affects where the grenade lands, not whether it is thrown (`npc_on_should_throw` handles that)

```lua
RegisterScriptCallback("npc_on_missile_throw_force", function(npc, base_force, flags)
    local rank = npc:rank_name()
    local multipliers = { novice=0.6, experienced=0.8, veteran=0.9, master=1.0, expert=1.1 }
    flags.ret_value = base_force * (multipliers[rank] or 1.0)
end)
```

---

### `npc_on_enemy_wounded_or_killed`
**Signature:** `(npc, victim)`
**Pattern:** B (notification)
**Fires:** `CAI_Stalker::on_enemy_wounded_or_killed()` after the kill-cry sound plays
**Notes:**
- `npc` is the ATTACKER who confirmed the kill or wound
- `victim` is the target that was wounded or killed
- Only fires on NPCs that are alive and can play the kill-cry sound
- Use for tactical AI: veterans press the advantage by repositioning, novices retreat

```lua
RegisterScriptCallback("npc_on_enemy_wounded_or_killed", function(npc, victim)
    local rank = npc:rank_name()
    if rank == "master" or rank == "expert" then
        npc:best_cover_invalidate() -- veterans reposition to press the advantage
    elseif rank == "novice" then
        npc:best_cover_invalidate() -- novices panic and scramble to new cover
    end
    -- (both call best_cover_invalidate; GOAP decides direction based on enemy state)
end)
```

---

### `npc_on_member_death_reaction`
**Signature:** `(npc, member, is_alive)`
**Pattern:** B (notification)
**Fires:** `CAI_Stalker::react_on_member_death()` after the tolls/wound sound plays
**Rate limit:** Enforced in C++ via `TOLLS_INTERVAL = 2000ms`. The early-return check
`if (Device.dwTimeGlobal < reaction.m_time + TOLLS_INTERVAL) return;` runs before the
hook fires. Guaranteed maximum 1 fire per 2000ms per NPC regardless of incoming hits.
**Notes:**
- `member` is the squad member who went down (may be nil in edge cases - always guard)
- `is_alive = true` means the member was wounded (still alive), `false` means killed
- Use for rank-based morale: novices panic and seek cover, veterans hold position

```lua
RegisterScriptCallback("npc_on_member_death_reaction", function(npc, member, is_alive)
    if is_alive then return end  -- wounded, not dead - ignore for simple morale
    local rank = npc:rank_name()
    if rank == "novice" or rank == "experienced" then
        npc:best_cover_invalidate() -- low-rank NPCs panic when squadmates are killed
    end
end)
```

---

## Console Variables

All variables take effect immediately (read at execution time, not cached).
All variables are saved to `user.ltx` on game exit and default to original hardcoded values -
existing saves see zero behavior change.
Changing them affects ALL NPCs globally - use Lua callbacks for per-NPC scaling.

### Danger Perception Multipliers
Multipliers on the base danger scores inside `CDangerManager::do_evaluate()`.
Formula: `(base_score * multiplier) * 10 + time_since_perceived`.
`1.0` = original engine behaviour. `0.0` = NPC ignores that danger type entirely. `2.0` = twice as urgent.
The base scores (3000 / 2500 / 2000 / 3000 / 2250 / 2000 / 1000 / 1000) remain hardcoded - only the
multiplier is tunable, preserving the relative priorities between types at default.

| Variable | Default | Min | Max | Trigger |
|---|---|---|---|---|
| `ai_danger_ricochet_mult` | 1.0 | 0.0 | 5.0 | Bullet ricochets nearby |
| `ai_danger_attack_sound_mult` | 1.0 | 0.0 | 5.0 | Gunfire is heard |
| `ai_danger_entity_attacked_mult` | 1.0 | 0.0 | 5.0 | Ally is heard being hit |
| `ai_danger_entity_death_mult` | 1.0 | 0.0 | 5.0 | Ally death sound heard |
| `ai_danger_corpse_mult` | 1.0 | 0.0 | 5.0 | Fresh corpse is seen |
| `ai_danger_attacked_mult` | 1.0 | 0.0 | 5.0 | General attack perceived |
| `ai_danger_grenade_mult` | 1.0 | 0.0 | 5.0 | Grenade is nearby |
| `ai_danger_enemy_sound_mult` | 1.0 | 0.0 | 5.0 | Enemy sound heard |

---

### Fire Decision (`fire_make_sense()`)
Controls when the engine considers it worthwhile to fire at a last-known position.
All read inside `fire_make_sense()` at call-time - changes via `get_console():execute()` are immediate.

| Variable | Default | Min | Max | Effect |
|---|---|---|---|---|
| `ai_fire_make_sense_interval` | 10000 ms | 0 | 20000 | How long NPCs fire at last known position after losing sight of enemy. 0 = only fire while currently visible |
| `ai_fire_range_extension` | 2.5 m | 0 | 10 | Extra metres past pick distance where suppression fire still makes sense |
| `ai_fire_max_height_diff` | 2.0 m | 0 | 8 | Max vertical gap between NPC and enemy for firing to make sense (~2 floors) |
| `ai_fire_min_dist` | 2.5 m | 0 | 20 | Minimum pick distance required for firing to make sense |

---

### Cover Danger Zones
Controls how wide and how long a cover point is flagged as dangerous after various events.

| Variable | Default | Min | Max | Event that triggers it |
|---|---|---|---|---|
| `ai_cover_danger_radius` | 3.0 m | 0 | 15 | NPC is shot while in cover |
| `ai_cover_danger_time` | 120000 ms | 0 | 300000 | NPC is shot while in cover |
| `ai_cover_detour_radius` | 5.0 m | 0 | 15 | NPC abandons cover to flank |
| `ai_cover_detour_time` | 120000 ms | 0 | 300000 | NPC abandons cover to flank |
| `ai_cover_unknown_radius` | 5.0 m | 0 | 15 | Unknown threat (grenade, sound) |
| `ai_cover_unknown_time` | 120000 ms | 0 | 300000 | Unknown threat (grenade, sound) |

---

### Cover Distance Constraints (`compute_enemy_distances()`)
Controls the min/max distance from the enemy that NPCs accept when selecting a cover point.
Set by weapon type by default. Override per-NPC with `npc_on_get_min_combat_dist` / `npc_on_get_max_combat_dist`.

**Engine note:** Min of `_max_dist` vars is `3.0m = MIN_SUITABLE_ENEMY_DISTANCE`. Below this the cover
evaluator receives a degenerate zero-width range and the cover search will always fail.
`ai_cover_sniper_min_dist` min is `0.0` - zero means "use engine default 3m minimum" with no extra constraint.

| Variable | Default | Min | Max | Effect |
|---|---|---|---|---|
| `ai_cover_pistol_max_dist` | 10.0 m | 3.0 | 50 | Max cover distance for pistol wielders (ef_weapon_type=5) |
| `ai_cover_shotgun_max_dist` | 5.0 m | 3.0 | 20 | Max cover distance for shotgun wielders (ef_weapon_type=9) |
| `ai_cover_sniper_min_dist` | 20.0 m | 0.0 | 80 | Min cover distance for sniper rifle wielders (ef_weapon_type=11,12) |
| `ai_cover_default_max_dist` | 20.0 m | 3.0 | 50 | Max cover distance for all other weapons (rifles, etc.) |

### Cover Search Radii (`find_best_cover()`)

**Engine note:** Min of `1.0m` prevents a zero-radius search that always fails silently and falls
through to the teammate-stacking fallback.

| Variable | Default | Min | Max | Effect |
|---|---|---|---|---|
| `ai_cover_search_near_radius` | 10.0 m | 1 | 30 | First-pass cover search radius around the NPC |
| `ai_cover_search_far_radius` | 30.0 m | 1 | 80 | Second-pass (fallback) cover search radius if no near cover found |

### Combat Behavior Timings

| Variable | Default | Min | Max | Effect |
|---|---|---|---|---|
| `ai_close_move_distance` | 1.5 m | 0 | 6 | Distance at which TakeCover switches from running to in-place movement. 0 = always in-place |
| `ai_crouch_look_out_delta` | 5000 ms | 0 | 12000 | How often NPCs toggle crouch↔stand while peeking during LookOut. Randomize per-NPC in Lua to break the synchronized "robotic bob" |
| `ai_wait_in_smart_cover_time` | 30000 ms | 0 | 120000 | How long NPC waits at a smart cover position for the enemy to appear before giving up |

---

## Composing Hooks Together - Rank-Based Reactive AI

This is the concrete use case that motivated the entire branch. All of this was previously
impossible from Lua. The composite example uses five hooks together.

```lua
-- ============================================================================
-- Rank-based reactive combat AI
-- ============================================================================

local last_invalidate = {}   -- per-NPC throttle for best_cover_invalidate
local shot_start      = {}   -- per-NPC burst start timestamp

local function try_invalidate(npc)
    local tg = time_global()
    local id = npc:id()
    if (last_invalidate[id] or 0) > tg then return end
    last_invalidate[id] = tg + 1000  -- max 1 invalidate per second per NPC
    npc:best_cover_invalidate()
end

-- 1. When shot in the open: novices and experienced panic, veterans hold
RegisterScriptCallback("npc_on_hit_reaction", function(npc, hit, bone_id, is_in_cover)
    if is_in_cover then return end
    local react = { novice=true, experienced=true, veteran=false, master=false, expert=false }
    if react[npc:rank_name()] then try_invalidate(npc) end
end)

-- 2. When a squadmate is killed: morale break for lower ranks
RegisterScriptCallback("npc_on_member_death_reaction", function(npc, member, is_alive)
    if is_alive then return end
    local react = { novice=true, experienced=true, veteran=false, master=false, expert=false }
    if react[npc:rank_name()] then try_invalidate(npc) end
end)

-- 3. Novices fire too long and expose themselves: duck back after 1.5s
RegisterScriptCallback("npc_on_weapon_shot_start", function(npc, weapon)
    shot_start[npc:id()] = time_global()
end)
RegisterScriptCallback("npc_on_weapon_shot_stop", function(npc)
    local id = npc:id()
    local duration = time_global() - (shot_start[id] or 0)
    shot_start[id] = nil
    if npc:rank_name() == "novice" and duration > 1500 then
        try_invalidate(npc)
    end
end)

-- 4. Grenade control: novices never throw, experienced 50%, veterans+ always
RegisterScriptCallback("npc_on_should_throw", function(npc, flags)
    local rank = npc:rank_name()
    if rank == "novice" then
        flags.ret_value = false
    elseif rank == "experienced" then
        flags.ret_value = math.random() > 0.5
    end
end)

-- 5. Grenade throw distance scaled by rank
RegisterScriptCallback("npc_on_missile_throw_force", function(npc, base_force, flags)
    local mult = ({ novice=0.6, experienced=0.8, veteran=0.9, master=1.0, expert=1.1 })
    flags.ret_value = base_force * (mult[npc:rank_name()] or 1.0)
end)

-- Cleanup
RegisterScriptCallback("game_object_on_net_destroy", function(obj)
    local id = obj:id()
    last_invalidate[id] = nil
    shot_start[id]       = nil
end)
```

---

## Already-Existing Hooks (Available Before This Branch)

These were already in the engine and are documented here for completeness.

| Hook | Pattern | Signature | Notes |
|---|---|---|---|
| `_g.CAI_Stalker__GetWeaponAccuracy` | A (return float) | `(npc, weapon, base, body_state, move_type)` | Override dispersion in radians. Lower = tighter. Full rank/stance/distance logic goes here |
| `_G.CAI_Stalker__BeforeHitCallback` | A (return bool) | `(npc, hit, bone_id)` | Return false to cancel hit. `hit.power` is writable. Fires AFTER `npc_on_critically_wounded` |
| `_G.CAI_Stalker__CombatSetBodyState` | A+B (return body_state) | `(npc, world_operator, body_state)` | Override crouch/stand per combat operator. Write `csbs_flags.body_state` |
| `_g.update_best_weapon` | A (return object) | `(npc, current_weapon)` | Override weapon selection |

**Multi-mod warning for Pattern A hooks:** `_g.CAI_Stalker__GetWeaponAccuracy` and
`_g.update_best_weapon` are direct global assignments (Pattern A). If two mods both assign
the same `_g` global, the last one loaded wins and silently stomps the other. If multiple
mods need to modify the same value, coordinate via a shared chaining pattern - each mod
reads the current `_g` global, wraps it, and re-assigns - or follow the `npc_on_missile_throw_force`
model (Pattern A-via-B) where all listeners cooperate through `flags.ret_value` via
`SendScriptCallback`.

---

## Confirmed NOT Reachable From Lua

These were investigated against the C++ source and confirmed hardcoded with no Lua entry point:

- **Dynamic cover** - Cover points in X-Ray are static, pre-baked into the nav mesh at level compile time. NPCs cannot dynamically use cars, barrels, or world objects as cover unless cover points were authored around them at level design time. This is a fundamental engine limitation, not addressable from Lua or C++ hooks
- **Cover scoring** - `best_cover_value()` and the cover evaluators are pure C++. `npc:best_cover()` queries the result but cannot change scoring. The distance constraints (now exposed via `npc_on_get_min/max_combat_dist`) influence which cover points are considered, but not how they are ranked within the valid set
- **GOAP world property flags** - `eWorldPropertyInCover` etc. are internal C++ state with no read/write API
- **Flanking target selection** - `CStalkerActionDetourEnemy` uses hardcoded angle constants
- **Script movement during combat** - Any `set_dest_level_vertex_id()` from `npc_on_update` is overwritten every frame by the combat action's own path. These calls are no-ops during active combat
- **FIRE_MAKE_SENSE_INTERVAL per-NPC** - Now a global console var (`ai_fire_make_sense_interval`), but still global; cannot be set per-NPC from Lua

---

## Key Source File Locations

```
src/xrGame/ai/stalker/ai_stalker_fire.cpp       Shot hooks, throw hooks, morale hooks, fire_make_sense vars
src/xrGame/ai/stalker/ai_stalker_misc.cpp        react_on_member_death hook
src/xrGame/ai/stalker/ai_stalker_cover.cpp       on_danger_location_add hook, best_cover_invalidate
src/xrGame/stalker_combat_planner.cpp            npc_on_combat_action_changed hook
src/xrGame/stalker_combat_actions.cpp            cover vars, close_move_distance, crouch_look_out_delta
src/xrGame/stalker_property_evaluators.cpp       npc_on_should_throw hook (evaluator, not action)
src/xrGame/enemy_manager.cpp                     npc_on_enemy_selected hook
src/xrGame/danger_manager.cpp                    danger score vars
src/xrGame/script_game_object3.cpp               rank_name(), best_cover_invalidate() exports
src/xrGame/console_commands.cpp                  all console var registrations
gamedata/scripts/callbacks_gameobject.script     all _G.* hook wiring and AddScriptCallback
```

---

## Thread Safety and Save Compatibility

**Thread safety:**
- All hooks fire on the main game thread
- `npc_on_danger_location_add` is dispatched via `CAgentLocationManager::add()` which is
  synchronous on the main thread - safe for all standard Lua operations including in the MT build
- No console var is read or written from a secondary thread

**Save compatibility:**
- All console vars default to the exact original hardcoded values they replace - existing
  saves see zero behavior change
- Console vars are written to `user.ltx` on game exit. A fresh install without these vars
  in `user.ltx` uses the C++ defaults, which are identical to the original engine behavior
- `npc:rank_name()` reads from `CharacterInfo` which is stable. Never use `npc:rank()`
  (raw integer whose thresholds modpacks change freely)
