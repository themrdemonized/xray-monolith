[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/themrdemonized/xray-monolith)

# STALKER-Anomaly-modded-exes

Here is list of exe files for Anomaly 1.5.3 that contains all engine patches by community required for some advanced mods to work.

# Versions
The Modded Exes come with standard and MT versions (currently in test phase designated as MT-TEST).

MT is a version with numerous performance improvements to the engine adapted from [IX-Ray](https://github.com/ixray-team/ixray-1.6-stcop) and [OpenXRay](https://github.com/OpenXRay/xray-16).

On average expect 30%-50% performance increase, will be higher if you were CPU bound before.

MT version is packed into separate `STALKER-Anomaly-modded-exes-MT-TEST` archive and are designated as `MT-TEST` in main menu and log.

MT version includes all features of standard Modded Exes described below, plus:
  * Reworked render graph, sector and portal traversals
  * Optional support for wallmarks on stalkers, mutants and other dynamic objects
  * Particle interpolation between frames for smoother appearance
  * Multithreaded: 
    * Loading resources (textures, models, CFORM (collisions))
    * HOM (Visibility tests)
    * Grass rendering
    * Rain
    * Particles
    * Bones calculations for models
    * Engine scheduler, split between real-time updated objects on main thread and others on separate thread with configurable batch amount to do per frame
    * Feel and Vision for AI
    * Task Manager
    * Parallel execution of `CreateTimeEvent` and `AddUniqueCall` commands (disabled by default)
    * Logger
  * Toggleable options available in Modded Exes options
  * Idle Time Parallel Lua GC
    * When frame is prepared in Renderer, repeatedly call Lua GC with small step to keep it busy and reduce cleanup work later, reduces stutters
    * After frame is rendered, check if Lua memory usage is good, perform big GC with usual GC step if its not
  * Updated Luabind to latest version from (https://github.com/ForserX/luabind-latest)
  * Functor cache for Lua calls, disabled by default, didn't show any performance difference
  * Enhanced `smart_cast` with specializations
  * Simplified `shared_str` container with string pooling in blocks, Robin Hood hashing, increased lookup buffer
  * Significantly reduced compilation time and PDB size
  * A lot of small fixes and improvements

Future MT versions will include LuaJIT 2.1 64 bit version, it will be incompatible with existing savefiles so for now its on a hold.

Known issues with MT version
  * Due to aggressive culling some spots on the map might bug out and don't render properly. For example a place behind basement entrance in Rookie Village
  * Increased possibility to have a crash on loading the whole game or a savefile
  * Longer pause on escaping to main menu or saving the game
  * Trees might have minor flickering, especially with mods that alter weather parameters via scripts
  * Occassional visual bugs like seldom flickering lights, model animations
  * Inconsistencies with some Lua mods like Interaction Dot Marks that might result in buggy behaviour
  * DX8, 9 and 10 versions are largely untested, they do load and render correctly on the first glance
  * Some modpacks might crash on load, tested with vanilla and GAMMA only and they do work

# Read the instructions PLEASE!!!
![изображение](https://github.com/user-attachments/assets/1b792ffc-127f-400f-8a2d-1f701803837d)

* **Windows 10 1903 Update at least is required!!!** 
* Install the latest Visual C++ Redistributables: https://www.techpowerup.com/download/visual-c-redistributable-runtime-package-all-in-one/
* Download STALKER-Anomaly-modded-exes_`<version>`.zip archive.
* Unpack all directories directly into your Anomaly game folder, overwrite files if requested.
* Delete shader cache in launcher before first launch of the game with new exes. You only have to do it once.

# TROUBLESHOOTING
* **If you are updating from version older than 2025.04.21 and use Reshade, you need to update Reshade as well**

* Q: How to reinstall modded exes from scratch?
* A: 
  * Delete everything in `db/mods` folders
  * Delete everything in `gamedata/configs` folder except `localization.ltx`, `axr_options.ltx` and `cache_dbg.ltx"` files
  * Delete these folders if you have them:
    * `gamedata/materials`
    * `gamedata/scripts`
    * `gamedata/shaders`
  * Install modded exes following the instructions above

* Q: The game crashes on DX11 Fullscreen on Linux
* A: You need to add `--dxgi-old` parameter into `commandline.txt` file or via making a shortcut to exe and adding argument there

* Q: I have low FPS near campfires or when shooting
* A: Disable "Volumetric lighting", known issue for now: https://github.com/themrdemonized/xray-monolith/issues/94 https://github.com/themrdemonized/xray-monolith/issues/123

* Q: I have conflicts, crashes and bugs with shaders when i use Beef NVG, SSS, or Enhanced Shaders
* A: If you are using those mods, install this package via MO2, and put it higher priority than those mods: https://github.com/deggua/xray-hdr10-shaders/releases/latest

## X-Ray Monolith Edition for S.T.A.L.K.E.R. Anomaly
----
* based on Open X-Ray Call of Chernobyl Edition
* based on X-Ray 1.6 Engine

This repository contains XRAY Engine sources based on version 1.6.02 and the Call of Chernobyl 1.4.22 modifications.
The original engine is used in S.T.A.L.K.E.R. Call of Pripyat game released by GSC Game World and any changes to this engine are allowed for ***non-commercial*** use only.

# List of patches

* Moved solution to Visual Studio 2022, In case you have problems, make sure you installed the latest Visual C++ Redistributables. You can find them here: https://www.techpowerup.com/download/visual-c-redistributable-runtime-package-all-in-one/

* DLTX by MerelyMezz with edits and bugfixes by demonized, differences compare to original:
  * Attempting to override sections no longer crash the game, but prints the message into the log. All sections that triggers that error will be printed
  * Duplicate section errors now prints the root file where the error happened for easier checking mod_... ltxes
  * DLTX received possibility to create section if it doesn't exists and override section if it does with the same symbol `@`.
  Below is the example for `newsection` that wasn't defined. Firstly its created with one param `override = false`, then its overridden with `override = true`

  ```
  @[newsection]
  override = false

  @[newsection]
  override = true
  
  ```

  * DLTX received possibility to add items to parameter's list if the parameter has structure like 
  
  ```name = item1, item2, item3```
  
    * `>name = item4, item5` will add item4 and item5 to list, the result would be `name = item1, item2, item3, item4, item5`
    * `<name = item3` will remove item3 from the list, the result would be `name = item1, item2`
    * example for mod_system_...ltx: 
    
    ```
      ![info_portions]
      >files                                    = ah_info, info_hidden_threat

      ![dialogs]
      >files                                    = AH_dialogs, dialogs_hidden_threat
      
      ![profiles]
      >files                                    = npc_profile_ah, npc_profile_hidden_threat
      >specific_characters_files                = character_desc_ah, character_desc_hidden_threat
    ```

  * Here you can get the LTXDiff tool with set of scripts for converting ordinary mods to DLTX format (https://www.moddb.com/mods/stalker-anomaly/addons/dltxify-by-right-click-for-modders-tool).

* DXML by demonized
  * Allows to modify contents of loaded xml files before processing by engine by utilizing Lua scripts
  * For more information see DXML.md guide.

* Possibility to unlocalize Lua variables in scripts before loading, making them global to the script namespace
  * For unlocalizing a variable in the script, please refer to documentation in test file in `gamedata/configs/unlocalizers` folder

* Doppler effect of sounds based on code by Cribbledirge and edited by demonized.
* True First Person Death Camera, that will stay with player when he dies and will react accordingly to player's head position and rotation, with possibility to adjust its settings.
  * Known bugs:
    * If the player falls with face straight into the ground, the camera will clip underground due to model being clipped as well with

* Optional gameplay enhancements

* Additional functions and console commands described in `lua_help_ex.script`

* Additional callbacks described in `callbacks_gameobject.script`

* Additional edits and bugfixes by demonized
  
* Added printing of engine stack trace in the log via StackWalker library https://github.com/JochenKalmbach/StackWalker
 * To make it work you need to download `pdb` file for your DX/AVX version and put it into same place as `exe` file. PDB files are here: https://github.com/themrdemonized/xray-monolith/releases/latest

* Fixes and features by Lucy
  * Reshade shaders won't affect UI, full addon support version of Reshade is required (see TROUBLESHOOTING for details)
  * fix for hands exported from blender (you no longer have to reassign the motion references)
  * fix for silent error / script freeze when getting player accuracy in scripts
  * animation fixes (shotgun shell didn't sync with add cartridge and close anims)
  * game no longer crashes on missing item section when loading a save (should be possible to uninstall most mods mid-game now)
  * fix for mutants stuck running in place (many thanks to Arszi for finding it)
  * fix for two handed detector/device animations (swaying is now applied to both arms instead of only the left one)
  * it's now possible to play script particle effects in hud_mode with :play(true) / :play_at_pos(pos, true)
  * the game will now display a crash message when crashing due to empty translation string
  * Scripted Debug Render functions
  * Debug renderer works on DX10/11
    * Many thanks to OpenXRay and OGSR authors:
    * https://github.com/OpenXRay/xray-16/commit/752cfddc09989b1f6545f420a5c76a3baf3004d7
    * https://github.com/OpenXRay/xray-16/commit/a1e581285c21f2d5fd59ffe8afb089fb7b2da154
    * https://github.com/OpenXRay/xray-16/commit/c17a38abf6318f1a8b8c09e9e68c188fe7b821c1
    * https://github.com/OGSR/OGSR-Engine/commit/d359bde2f1e5548a053faf0c5361520a55b0552c
  * Exported a few more vector and matrix functions to lua
  * Optional third argument for world2ui to return position even if it's off screen instead of -9999
  * Unified bone lua functions and made them safer
  * It's now possible to get player first person hands bone data in lua

* LTX based patrol paths definitions by NLTP_ASHES: https://github.com/themrdemonized/xray-monolith/pull/61

* Redotix: 3D Shader scopes (3DSS) support: https://github.com/themrdemonized/xray-monolith/pull/62

* deggua: HDR10 output support to the DX11 renderer: https://github.com/themrdemonized/xray-monolith/pull/63
  * Included shaders works with vanilla Anomaly. For compatibility with SSS and GAMMA, download GAMMA shaders from here https://github.com/deggua/xray-hdr10-shaders/releases/latest

* Fixes and features by DPurple
  * Fix of using `%c[color]` tag with multibyte font causing unexpected line ending by DPurple
  * Ability to autosave the game before crash occurs, can be disabled with console command `crash_save 0` and enabled with `crash_save 1`. Maximum amount of saves can be specified with command `crash_save_count <number>`, where number is between 0 to 20 (default is 10)

* Smooth Particles with configurable update rate by vegeta1k95
  * Possibility to set particle update delta in milliseconds in .pe files for fine tuning with `update_step` field

* Shader Scopes by CrookR and enhanced by Edzan, integrated into Modded Exes

* OpenAL 1.23.1 with support for EFX, working sound environments from IX-Ray Engine

* Commits from IX-Ray Engine: https://github.com/ixray-team/ixray-1.6-stcop

<!----><a name="script_debugger_instructions"></a>
* Debug scripts with VSCode and LuaPanda, support by IX-Ray Platform. How to use it: https://anomaly-modding-book.netlify.app/docs/tutorials/addons/lua-debugger

* All settings can be edited from the game options in "Modded Exes" tab
![image](http://puu.sh/JC40Y/9315119150.jpg)

## Below are the edits that are supplemental to the mods, the mods themselves **are not included**, download the mods by the links. If mods in the links provide their own exes, you can ignore them, all necessary edits are already included on this page. 

* BaS engine edits by Mortan (https://www.moddb.com/mods/stalker-anomaly/addons/boomsticks-and-sharpsticks)

* Screen Space Shaders by Ascii1457 (https://www.moddb.com/mods/stalker-anomaly/addons/screen-space-shaders)

* Heatvision by vegeta1k95 (https://www.moddb.com/mods/stalker-anomaly/addons/heatvision-v02-extension-for-beefs-nvg-dx11engine-mod/)

## How to make my own modded exe?

How to compile exes:
1. Fork this xray-monolith repo, the main branch is `all-in-one-vs2022-wpo`
2. Download the fork onto your pc
3. Open Git Bash or terminal in the downloaded folder and run `git submodule update --init --recursive` to pull git submodules
4. Compile the engine-vs2022.sln solution with VS2022
5. For batch builds of all configurations use `batch_build.bat`
6. For successful compilation, **the latest build tools with MFC and ATL libraries is required**

### Working with Hot Reload feature using VS2022 for quick feedback flow (by lulnope):
1. Follow the "How to compile exes" guide above
2. Start VS2022 solution as admin
3. Select VerifiedDX11 Solution Configuration **note that this configuration compiles .exes in an extremely unoptimized state. To launch the game you might need as much as 20gb of free RAM and the game will run at about 50% of the performance of .exes built using release configurations. The upsides are a working Hot Reload feature and much easier debugging as no symbols are optimized-away and no methods are inlined so the code executes the exact way it reads**
4. VS2022 -> top menu bar -> Build -> Rebuild Solution
5. Upon successful build you will see a log output with a path like so: `\xray-monolith\_build\_game\bin_dbg`
6. Copy the newly built .exe file AND the .pdb file into your Anomaly/bin folder
7. Start the game using the newly built .exe file, it does not matter if it's vanilla Anomaly, GAMMA or a different modpack as long as you use the newly built .exes
8. Once the game started ATL+TAB to VS2022 and navigate to Debug -> Attack to process... (CTRL+ALT+P)
9. Attach to AnomalyDX11.exe **this operation requires VS2022 to be launched using admin permissions, you will be prompted to relaunch the VS2022 as admin if you haven't**
10. Introduce some minor change to the code, save the edited file and execute Hot Reload (VS2022 -> top menu bar -> Debug -> Apply Code Changes (ALT+F10)) **the option to Hot Reload the code will become available only once you've successfully attached a debugger  to a running process and VS2022 switched to debug view**
11. If everything worked correctly you should see a loading bar indicating recompilation & reload process, once finished you should immediately be able to observe difference in the execution of the code
12. Happy modding! Makes sure to read Hot Reload documentation: https://learn.microsoft.com/en-us/visualstudio/debugger/supported-code-changes-cpp?view=vs-2022
13. A short video demonstration of the entire process: https://youtu.be/MmZwyM2QO38

## Changelog
**2026.04.06**

* MT:
  * Wallmarks
    * Restore `! Failed to render dynamic wallmark` try-catch block, fixes crashes with certain script mods
    * Disable `g_wallmark_range_static` and `g_wallmark_range_skeleton` commands, they are unused
  * Alternative solution to fix of complete lockup of engine due to calculating bones in separate thread, fixes `HudItem.cpp (551): CHudItem::UpdateCL` crash
  * `CVisualMemoryManager::visible_object` nullptr check in `m_objects`

**2026.04.05 (Prerelease)**

* Main and MT:
  * Legs: Fix rendering attachment shadows with multiple light sources
  * BusyHandsDebug: Do not engage if `db.actor` is nil, doesn't matter at this point
  * DXML: Safer Lua callback, fixes possible crashes such as when throwing grenades with right mouse button
  * `duplicate_story_id_crash` console command to disable crash on `"Specified story object is already in the Story registry!"` error
  * GhenTuong: CWeaponStatMgun: Introduce field "on_range_fov" to change gunner visibility range. Export lua game object functions (https://github.com/themrdemonized/xray-monolith/pull/498)

* MT:
  * Revert "Wallmarks: Increase MAX_TRIS from 16384 to 32768"
  * Revert changes to visual memory manager and remove unnecessary critical section guards
  * Split `CObjectList::destroy_queue` processing:
    * When `mt_scheduler 1`, delegate will be pushed into `seqParallelBeforRender` to the next frame
    * `net_RelCase` for bullet manager
    * `empty()` checks for restrictions
    * Possibly fixes random crashes when an object is destroyed while mt scheduler is processing objects
  * Wallmarks refactoring
    * Static wallmarks are grouped by sectors. Only visible sectors will render wallmarks
    * Update wallmarks lifetime before rendering, simplify rendering loop
    * Removal of skeleton wallmarks on object's `net_destroy`, fixes floating wallmarks in the air or stretched wallmarks artifacts
    * Remove distance check when adding wallmarks to render queue, fixes absent wallmarks on objects that were killed more than 50 meters away
    * `r_wallmarks_ssa_k` console command to limit rendering distance of wallmarks, default 40. More value means LESS rendering distance
  * Fixed excessive smearing when using SSS with motion vectors
  * Update global Feel::Vision data when an object changes it visuals, possibly fixes crashes related to `get_new_local_point_on_mesh`
  * Possible fix of complete lockup of engine due to calculating bones in separate thread

**2026.03.29**

* Main and MT:
  * Optimization of `CEnemyManager::useful`:
    * Add short live caching of Lua call results, prevents expensive lua calls each frame esp. in GAMMA
    * Jitter cache time based on `entity_alive->ID` so that the updates will be spread out between frames
    * Big performance gain in firefights vs NPCs, up to 100% in GAMMA
    * `g_enemy_manager_useful_cache_time` to control the cache expiration time. Default is 250ms. -1 will disable caching
    
    ![image](http://puu.sh/KKJxE/a4770b7660.jpg)

  * Legs: 
    * Fixed rendering attached items shadows such as headlight
    * `g_legs_render_attachments_shadow` to toggle rendering attached items shadows, default enabled 
  * Fixed Out Of Memory error due to abnormal size of underbarrel ammo in net packet
  * `alife():object_count()` function to return current alife count
  * Lua changes:
    * `_g_patches`:
      * Patch `pairs` and `ipairs` to use methods from metatables if they are defined (Lua 5.2 functionality)
      * Simpler `empty_table` and `iempty_table`
      * `get_object_by_id` uses `gameobjects_registry`, more reliable than `db.storage`
      * `alife_object` uses `server_objects_registry`, less calls to engine unless necessary
      * `fis_zero` and `fsimilar` functions
      * `MinHeap` return self reference whenever possible
      * simple `OrderedTable` class
      * `_G` metatable newindex change to prevent shadowing const table
    * `callbacks_gameobject`:
      * `server_objects_registry` table contains all alife server objects for fast lookup without engine call
    * `item_weapon`:
      * A little more optimized ammo aggregation algorithm
  * erepb: expose CALifeSimulator::update_scheduled to lua as force_update (https://github.com/themrdemonized/xray-monolith/pull/493)
  * Verdatim25: Add a new method to CWeapon to allow for force changing zoom type (https://github.com/themrdemonized/xray-monolith/pull/495)

MT:
  * Fixed crash when using `log_timestamps`, fixes https://github.com/themrdemonized/xray-monolith/issues/485
  * Fixed "Out of memory" error in `feel_vision.h` in `feel_vision_get` method
  * Disabled multithreaded HOM, fixes artifacts near screen borders
  * Fixed potential nullptr crash in `CMemoryManager::make_object_visible_somewhen`
  * Fixed potential game lockup due to invalid coordinates for IK calculation
  * Wallmarks: Increase MAX_TRIS from 16384 to 32768

**2026.03.22**

Main and MT:
  * `play_cycle CA->PlayCycle` return value check on nullptr
  * Legs improvements:
    * In shadowmap phase render full body model without hiding bones instead of moving the player's, fixes some bugs like reappearing level transition dialog
    * Adjust player's torch and bolt offsets so they won't float in the space
    * Model is attached to the camera, fixing bugs with body displacement when colliding with objects or stalkers
    * `g_legs_in_low_crounch` command to disable legs rendering when low crouching, default enabled
  * `lua_busy_hands_debug` command to debug common "Busy Hands" errors, default enabled:
    * Currently, it debugs two groups of possible errors, the most common ones:
      * Calling methods on already destroyed `CScriptGameObject` objects
      * Mismatched parameters when calling methods
    * When critical error occurs that will lead to "Busy Hands":
      * Lua callback will make a temporary save and popup "Lua Critical Error" window
      * In the window you can choose to immediately return to Main Menu, reload the temporary save, or ignore and continue
      ![image](http://puu.sh/KKpnk/2353c0f344.jpg)
  * Replacing time event system with Indexed Min-Heap based time events
    * Peek only closest event
    * Fast insert and removal
    * Safety check when creating time event but function is nil
    * Postpone events when `sleep_active` flag is active so they won't be fired all at once when the flag is lifted
    * My `optimized_time_events` script is blacklisted from loading so that new system will work all the time
  * Minor optimizations in `_g.script`
    * Simpler `is_empty`
    * Simpler `shuffle_table`
    * Simpler `size_table`
    * Reservoir sampling based `random_key_table`
    * `random_choice` without allocating tables
  * Revised the fix for `DynamicNewsManager` to be more robust
  * Fixed script_fixes_mp:727 `attempt to index local tm (nil value)`
  * Fixed `state_mgr_animation.delayed_attach` to not spam time events
  * Fixed `ui_debug_main.delayed_attach` to not spam time events
  * Fixed `ui_enemy_health.cs_remove` spamming in time events because the wrapper function for time event doesn't return true
  * Faster algorithm for `spairs` if order function is not provided  
  * leer-h: Update poltergeist.cpp (https://github.com/themrdemonized/xray-monolith/pull/475)
  * erepb:
    * fix moving target pathing (https://github.com/themrdemonized/xray-monolith/pull/474)
    * use m_location_level to sort map spots (https://github.com/themrdemonized/xray-monolith/pull/476)
    * fix infinite loop when no sound devices in system (https://github.com/themrdemonized/xray-monolith/pull/479)

MT:
  * .peak lights has slightly higher intensity to be more visually noticeable compared to SSS

**2026.03.16**

Main and MT:
  * LuaJIT increase memory allocation to 512MB
  * Fixed potential crash in `spairs` if item was deleted from a table while iterating
  * Stricter check for nil in `spairs` when ordering
  * Replace only `__index` in `_g` metatable for always returning a copy of `VEC_ZERO`, `VEC_X`, `VEC_Y`, `VEC_Z`

MT:
  * Revert "Merge pull request #462 from knallpsi/detail-cache-optimization", needs further testing
  * Revert `Alife registry uses sparse_map data structure for faster insertion, removal and iteration`, can cause random bugs

**2026.03.15h1**

Main and MT:
  * Fixed Interaction Dot Marks issue when legs are enabled
  * Fix spazzing shadow of legs in shadow phase, fix wrong placement of active item

**2026.03.15**

Main:
  * Backport from MT: SSS phase_ssfx_sss_ext add more safety

Main and MT:
  * Legs rendering improvements
    * Code cleanup
    * `bip01_spine` is attached to pelvis with optional y offset `g_legs_spine_offset_y`, default 0.1
    * Hiding neck instead of head
    * Correct player shadow placement when legs are enabled
  * Min-Heap based `spairs` iterator
    * Supports early break without sorting whole table
    * Faster retrieval of the first item
  * Disable `alife():object(id)` invalid id spam on `pda.calculate_rankings`
  * When `on_loading_screen_key_prompt` happens, perform Lua GC and call `jit.flush`
  * leer-h: Command to disable actor body/legs model rotation delay. Functionality to load new animations for Actor\NPC without editing the existing stalker_animation.omf or including new omf's in the model's motion refs (https://github.com/themrdemonized/xray-monolith/pull/457)
  * knallpsi: UI optimization, crc32 replacement (https://github.com/themrdemonized/xray-monolith/pull/463)
  * GhenTuong:
    * CWeaponStatMgun: Add camera effect when shooting and hand model/animation (https://github.com/themrdemonized/xray-monolith/pull/464)
    * ltx_help_ex.script edit for WeaponStatMgun and Projector (https://github.com/themrdemonized/xray-monolith/pull/471)

MT:
  * nullptr check in `CAgentManagerPropertyEvaluatorEnemy::_value_type CAgentManagerPropertyEvaluatorEnemy::evaluate()`
  * Alife registry uses `sparse_map` data structure for faster insertion, removal and iteration
  * knallpsi: Details cache update optimization (https://github.com/themrdemonized/xray-monolith/pull/462)

**2026.03.10**

MT:
  * Additional safety checks in visual memory manager
  * `CPHSimpleCharacter::UpdateDynamicDamage`, nullptr check should reduce crashes when interacting with dead bodies
  * `cNameVisual_set`, updating visuals will wait until the object is finished being processed in feel vision routine
  * `CHudItem::renderable_Render`, nullptr check for owner

**2026.03.07**

Main and MT:
  * Spawn Antifreeze: If the object has server counterpart, check if server object is still in alife after prefetching
  * Fixed issue with Interaction Dot Marks mod due to `game_tutorials` cache
  * Fixed potential crash in `CMovementManager::process_game_path` if dest_level_vertex_id is invalid
  * Always return a copy of `VEC_ZERO`, `VEC_X`, `VEC_Y`, `VEC_Z` in scripts to prevent unwanted changes to original objects, potentially fixes a plethora of vanilla bugs related to these variables. Thanks to PrivatePirate97 for addressing the issue
  * Possible fix to `aaaa_script_fixes_mp.script:652: 'for' initial value must be a number`
  * `server_object_on_(un)register` callbacks for all server objects
  * Removal of stale data in `bind_item` and `item_parts` when brand new server object is created, fixes bugs with having weapon or outfit parts on unrelated objects or having wrong item uses and condition on freshly crafted items
  * Moved callback based script fixes from `callbacks_gameobject` to `aaaa_script_fixes_mp`
  * Kutez: Spatial Audio Rework - The ability to overwrite EFX's reverb, allowing for controllable reverb (https://github.com/themrdemonized/xray-monolith/pull/451)
  * leer-h: Added "show_actor_body" (https://github.com/themrdemonized/xray-monolith/pull/454)
  * tabudz: Potential Vulnerability in Cloned Code (https://github.com/themrdemonized/xray-monolith/pull/453)
  * knallpsi: Engine-side First Person Body implementation (https://github.com/themrdemonized/xray-monolith/pull/437)
    * Type `g_legs 1` in console to enable legs
    * `g_legs_fwd_offset` to adjust position for legs if it isn't specified in the config, see below
    * `g_legs_in_demo_record` to enable rendering legs while `demo_record` is active
    * By default, the existing player model based on current outfit will be used with hiding head and arm bones
    * Possibility to specify custom model for legs. In an outfit section where `actor_visual` is defined, or `[actor]` section for the default legs, either use DLTX or adjust LTX directly:
      1. Add `legs_visual` field with the path to the model to use (`legs_visual = sm\actor_legs\jacket_loner.ogf`)
      2. Optionally add `legs_fwd_offset` field to adjust position of the legs (`legs_fwd_offset = -0.55`)
      
MT:
  * Fixed possible crash when using `GT - Emplacement` mod
  * `g_sv_Spawn` safety features
  * `CUIWindow` postponed deletion of `AutoDelete` items
  * `CWeapon::GetFireDispersion` added `pOwner` check for safety
  * `CObject::net_Destroy()` will wait until the object is finished being processed in feel vision routine

**2026.03.01**

Main and MT:
  * DLTX: hide Malformed Line and Invalid Section Parent warnings behind `print_dltx_warnings`
  * Fixed stuttering when prompt to ignite or extinguish campfire appears, or any other prompt that uses `game.start_tutotial` function
  * Parallel GC will work only when level is fully loaded to prevent some bugs on loading
  * Added `_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR` macro to `openal` and `optick`, fixes crashes with certain PC configurations
  * damoldavskiy: HUD state switch callback (https://github.com/themrdemonized/xray-monolith/pull/448)
  * LVutner: Faster CBuffer updates (https://github.com/themrdemonized/xray-monolith/pull/449)
  * SaloEater:
    * Fix trade manager resupply sync (https://github.com/themrdemonized/xray-monolith/pull/442)
    * Imgui luadebug button (https://github.com/themrdemonized/xray-monolith/pull/443)

MT:
  * Fixes from IXRay repo, fixes possible stack overflow in Pripyat Outskirts (https://github.com/ixray-team/ixray-1.6-stcop/commit/d9f32e486a27f61ec3cc88a7a0cb87863def2284)
  * Fixed absent explosion particles with Molotov mod when `mt_level_call` is 0
  * Fixed absent spot light cone when light doesn't allow shadow casting
  * Thread safety for `combat_members()` access
  * Fixed possible freezes in Zaton due to NaN coordinates in spatial components 

**2026.02.22**

Main and MT:
  * Spawn Antifreeze more safety features
  * Remove stutter when raising PDA by iterating actual se_stalker and se_monster objects instead of whole alife in function `pda.calculate_rankings()`
  * damoldavskiy: Easing and correct scaling for .anms, custom pivot points (https://github.com/themrdemonized/xray-monolith/pull/436)

MT:
  * Fixed buggy lighting on objects in DX8
  * More safety features for visual memory manager
  * Light refactor of building and rendering `dsgraph` items
  * `mt_task_manager` console command to toggle Task Manager between main and second thread, default is 0, main thread
  * More safety in `phase_ssfx_sss_ext`, should be less crashy
  * Parallel GC runs in parallel to Physics and Sound Processing instead of after them, gives GC more time to work for more effectiveness
  * knallpsi: support for .peak volumetric lights by LVutner (https://github.com/themrdemonized/xray-monolith/pull/430)
    * How to enable .peak?
      0. Recommended to install SSS before this
      1. Download and install shaders from www.moddb.com/mods/stalker-anomaly/addons/peak-volumetrics-1-1
      2. Type `pfx_volumetric_mode 1` in console or enable .peak volumetrics in Modded Exes options -> Visual -> Graphics
      3. Optionally tune volumetric lights intensity in SSS MCM options

**2026.02.15**

MT:
  * Fixed bug with swapping textures

**2026.02.14**

Main:
  * Ported new GC procedure from MT branch
  * Ported critical section locking on resources creation and deletion from MT branch

Main and MT:
  * Spawn Antifreeze: Fixed possible `stack overflow` crash
  * Removed all dynamic thread affinity and process priority changes inside the engine, the game launches with normal priority
  * Removed 1 second pause on splash screen
  * `_mm_pause` spin count is `4096`
  * Simplified code when using separate key for underbarrel grenade launcher, should be less buggy
  * Minor performance increase by optimizing getting `R_constant` pointers
  * erepb:
    * various window related fixes, consistent window creation, center splash, multimonitor support, correct cursor limits (https://github.com/themrdemonized/xray-monolith/pull/418)
    * fix race in refcount (https://github.com/themrdemonized/xray-monolith/pull/426) (https://github.com/themrdemonized/xray-monolith/pull/429)
  * Tosox: Installation based instance mutex, Allow multiple game instances (https://github.com/themrdemonized/xray-monolith/pull/424)

MT:
  * Fixed volumetric lights rendering
  * Fixed flickering shadows from omni lights when using SSS
  * Fixed possible crash related to visual manager when entity is destroyed
  * Rain and Particle Manager uses less strict atomics
  * knallpsi: Console commands history, scroll console with mouse wheel, fixed browsing commands history with arrows (https://github.com/themrdemonized/xray-monolith/pull/423)

**2026.02.08**

Main and MT:
  * Preemptive trader update check are run on own timer, fixes stutters in Gamma from this side 
  * DLTX Refactor:
    * Duplicate section check now actually works
    * DLTX Cache: evaluated files and sections are stored permanently in RAM. Next file access by `CInifile` construction will quickly return data without ever accessing disk and DLTX parsing
    * `dltx_use_cache` variable to toggle the cache
    * Vanilla usage of cache is about 50MB, on GAMMA it is up to 150MB
  * Better Debug Inputs script (when calling F7). Spawner of items, objects and executor are affected
    * When you spawn an object from a list or by typing in input field, the input will be saved
    * You can cycle between saved inputs by pressing up and down arrows, much like through console commands. The input shouldnt be focused (no caret visible)
    * Maximum of 20 inputs can be saved
    * Execution of functions in executor is automatically wrapped into protected call, so you wont crash the game on some error. If error occured, you will see the message on the bottom or in the console
  * Spawn Antifreeze: fixed occasional crash when spawning, reorganized data to minimize multithreading lock time
  * erepb: super early luajit init (https://github.com/themrdemonized/xray-monolith/pull/419)
  * erepb: actually fix sound device selection and autoswitch (https://github.com/themrdemonized/xray-monolith/pull/416)
  * PrivatePirate97: decoupled horz recoil, non-linear inertia movement, additions to lua_help_ex.script (https://github.com/themrdemonized/xray-monolith/pull/417)
  * GhenTuong: Development work for level_graph, CExplosive, CGrenade, CWeaponStatMgun, CWeapon, script mutant movement. Export CGrenade functions (https://github.com/themrdemonized/xray-monolith/pull/420)
  * Tosox: Keyboard layout-aware text input & Caps Lock handling (https://github.com/themrdemonized/xray-monolith/pull/421)

MT:
  * Fixed crash when using Glowsticks mod
  * Fixed occasional crash when using SWM Visible Legs mod
  * Fixed occasional crash on exiting the game due to Renderer being destroyed before shader data
  * 3DSS, fixed rendering grass on top of stalkers when looking through the scope
  * Reduced memory footprint of `shared_str` and `str_container`
  * Prevent duplicates when calling `Instance_Register` in `ModelPool` 
  * Safer destruction of objects if `feel_vision_update` is running
  * Add self-model ignoring in feel vision (https://github.com/ixray-team/ixray-1.6-stcop/commit/c0a2540937708d029e4f7247e6cbcb929204eb5b)
  * Disabled blood decals on objects when shooting by default, can be enabled via `r__blood_decals_on_objects`
  * `mt_calc_bones` option to disable multithreaded bones calculation
  * Multithreaded bones calculation is slightly safer
  * Scheduler:
    * Split real-time objects on main thread, non RT on task group when MT Scheduler is enabled. Fixes Interaction Dot Marks mod
    * `scheduler_batch_size` to control max objects to `shedule_update` per frame
    * `scheduler_log` to log info
  * Idle Time Parallel Lua GC
    * When frame is prepared in Renderer, repeatedly call Lua GC with small step to keep it busy and reduce cleanup work later, reduces stutters
    * After frame is rendered, check if Lua memory usage is good, perform big GC with usual GC step if its not
    * `lua_parallel_gc` to enable feature
    * `lua_parallel_gcstep` controls GC step, default 75
    * `lua_parallel_gc_call_amount` sets the amount of times GC will be called, default 25
    * `lua_parallel_gc_debug` to print memory information

**2026.01.31**

Main and MT:
  * DLTX Refactor
    * Restored old `xr_vector` structure for KV pairs, increases stability of loading
    * Significantly optimized loading and merging sections' data with mods, reduced loading times esp. with DLTX heavy modpacks
    * More informative logging, print warnings when a malformed line encountered in files, warnings when section inherits from non-existent parent or a parent that was defined with `!` override
  * PrivatePirate97: fix inertia offset movement when using canted aim (https://github.com/themrdemonized/xray-monolith/pull/410)

MT:
  * Fixed constant motion blur due to incorrect motion vectors calculation placement in code
  * Fixed potential crash when trying to use `demo_record` and fly through geometry or in any situation where a lot of rendering sectors and portals is occured
  * Parallel execution of `CreateTimeEvent` and `AddUniqueCall` commands via `mt_level_call` command (disabled by default). Disabling partially fixes Interaction Dot Marks mod
  * `stat_memory` prints more info on `shared_str` container - current pool blocks used, load factor, max hash collisions
  * `stat_memory` prints Lua memory usage

**2026.01.25 (Pre-release)**
* First release of test version of MT branch 
* Expanded grass shadow settings in Modded Exes settings
* Refactored DLTX code to be more performant and readable for engine modders.
* Significantly reduced loading times on DLTX heavy modpacks by changing key-value storage from `xr_vector` to `xr_set`
* Removed obsolete code calls in `FPU` module
* VodoXleb: fixed level.set_music_volume() (https://github.com/themrdemonized/xray-monolith/pull/403)
* erepb: fix sound device autoswitch (https://github.com/themrdemonized/xray-monolith/pull/405)

**2025.12.30**
* Enabled Hot Reload configuration for `VerifiedDX11` configuration
* Removed double call to `calculateBones` in `Actor.cpp`
* tlnguyen-smu: Fix to Potential Vulnerability in Cloned Code in LuaJIT (https://github.com/themrdemonized/xray-monolith/pull/399)

**2025.12.23**
* Removed dependency on obsolete `Loki` library, reduced size of exe and pdb files (https://github.com/ixray-team/ixray-1.6-stcop/commit/4dc52310a94829c254ea03019399afac12f77450, https://github.com/ixray-team/ixray-1.6-stcop/commit/8e916f28fbc293628b61d8b3270e33b91294f31a)
* Crows will fly towards recently killed NPCs (https://github.com/ixray-team/ixray-1.6-stcop/commit/0392ea1ae4e7454dd8a7d38e1868e03558c42093)
* Fixed sound volume calculation from emitter to player (https://github.com/ixray-team/ixray-1.6-stcop/commit/dddeb09cb90190d151b23bcba15a389175ed686f)
* Print warnings in console if invalid fire_bone, fire_bone2 or shell_bone is provided
* Fixed compiled warnings for player_hud functions
* Fixed `g_target_temp_data_16` allocation size in xrSound
* GhenTuong: Development changes for CProjector, CWeaponStatMgun, bind_monster.script (https://github.com/themrdemonized/xray-monolith/pull/396)

**2025.12.02**
* Antglobes: Export Screenshot Func + variable resolution & encoding (https://github.com/themrdemonized/xray-monolith/pull/394)

**2025.11.17**
* Fixed `g_recon_maxdist`, minspeed and maxspeed commands to set proper variables
* GhenTuong: Small tweak and fix bugs for CCar, CExplosive, CWeaponStatMgun, SPATIAL_CHANGE.

**2025.10.28**
* Temporary revert https://github.com/themrdemonized/xray-monolith/pull/326 to fix issue https://github.com/themrdemonized/xray-monolith/issues/387#issuecomment-3453531244

**2025.10.23**
* Added `bolt` and `bolt_bullet` to spawn antifreeze ignore list
* SaloEater: World2UI With Depth lua method (https://github.com/themrdemonized/xray-monolith/pull/386)

**2025.09.28**
* Moved ubgl key to custom21, fix issue https://github.com/themrdemonized/xray-monolith/issues/380, you need to rebind UBGL key again in the settings
* RavenAscendant: additional 100 entries, will let the bind console command bind actions to values that won't get triggered by keyboard input but can be triggered from the `level.press_action` functions

**2025.09.26**
* Disabled `ZoomTexture` check in `set_scope_ui` Lua export
* Xottab-DUTY: Fixed loading of fast (shadow) geometry (https://github.com/themrdemonized/xray-monolith/pull/379)

**2025.09.19**
* Possible fix for issue https://github.com/themrdemonized/xray-monolith/issues/375

**2025.09.15**
* Various Luabind fixes and improvements
* Simplify `script_callback_ex` templates
* Fix `PHItemList` compile warning
* SaloEater: Update link to new wiki (https://github.com/themrdemonized/xray-monolith/pull/372)

**2025.09.12**
* OXR: Replace shared_str with xr_string for log and fs, fixed https://github.com/themrdemonized/xray-monolith/issues/366
* Fix https://github.com/themrdemonized/xray-monolith/issues/369

**2025.09.10**
* ProfLander: Fix CALifeMonsterBrain::process_task segfault (https://github.com/themrdemonized/xray-monolith/pull/364)

**2025.09.06**
* Replaced luabind with non-Boost version (https://github.com/ixray-team/ixray-1.6-stcop/commit/2f61f5f781130468c945720b76d23ce4bbea95b1)
* Disable `std::terminate` in luabind (https://github.com/ixray-team/ixray-1.6-stcop/commit/723fb65a8b9ebb89dda8f03cbb4b1bebceabacdc)
* Removed Boost library
* Added nullptr check in `CMonsterCorpseMemory::add_corpse`
* LVutner:
  * ADD: [Render] Added missing s_position samplers (https://github.com/themrdemonized/xray-monolith/pull/358)
  * UPD: [Render] r_ComputePass fix (https://github.com/themrdemonized/xray-monolith/pull/359)

**2025.08.30**
* MFB: Smart covers now have fixed enter min/max distances (https://github.com/themrdemonized/xray-monolith/pull/352)
* Lucy: Level Script Attachments (https://github.com/themrdemonized/xray-monolith/pull/353)
* ProfLander: ImGui: Implement grouping API (https://github.com/themrdemonized/xray-monolith/pull/354)

**2025.08.27**
* Fix https://github.com/themrdemonized/xray-monolith/issues/346
* NLTP_Ashes:
  * Export multiple CWeaponKnife related functions to Lua (https://github.com/themrdemonized/xray-monolith/pull/347)
  * Export HUD elements to Lua (https://github.com/themrdemonized/xray-monolith/pull/351)
* Ncenka: Fix for Random Music in Main Menu (https://github.com/themrdemonized/xray-monolith/pull/345)

**2025.08.23**
* Kutez: Callback Priority System (https://github.com/themrdemonized/xray-monolith/pull/339)
* Ncenka: PDA UI XML Setter (https://github.com/themrdemonized/xray-monolith/pull/343)

**2025.08.21**
* GhenTuong:
  * CCar CWeaponStatMgun changes (https://github.com/themrdemonized/xray-monolith/pull/310)
  * Export API functions and minor improvements (https://github.com/themrdemonized/xray-monolith/pull/340)
* Antglobes: Sun values (https://github.com/themrdemonized/xray-monolith/pull/341)

**2025.08.19**
* Removed `parallel_for` in HOM and `particle_actions_collection` in favor of single-threaded loop for less thread creation overhead
* Replace `unordered_map` implementation to `unordered_node_map`, same with set
* Removed double loop in volumetric lights code
* Disabled update of actor stamina while driving cars (https://github.com/ixray-team/ixray-1.6-stcop/commit/6c1ad01adffba180df8f47a58f33e66e69def949)
* Fix crash when NPC trying use destroyed object (https://github.com/ixray-team/ixray-1.6-stcop/commit/d34966c3e255568f60df7bd0e33d61bebfe98afa)
* ProfLander: Add string count to stat_memory and OOM handler (https://github.com/themrdemonized/xray-monolith/pull/337)
* Kutez: Added the new "volume_mult" property for HUD sound call back. Removed all indoor framework related engine side code (https://github.com/themrdemonized/xray-monolith/pull/338)

**2025.08.12u1**
* Fix issue https://github.com/themrdemonized/xray-monolith/issues/333
* v2v3v4: fix ctd when zooming into about to be destroyed object with detector scopes

**2025.08.12**
* Fix issue https://github.com/themrdemonized/xray-monolith/issues/332

**2025.08.11**
* Small reorganization of 3rd party files
* `g_interrupt_fire_on_aim_toggle` cvar to set stop firing when pressed aim, default enabled addresses issue https://github.com/themrdemonized/xray-monolith/issues/327
* v2v3v4: update all sound positions at once
* ProfLander: Launchers: Cartridge Ammo + Trajectory and Reload Options (https://github.com/themrdemonized/xray-monolith/pull/322)
* VodoXleb: Add `binoculars_dynamic_zoom_check` cvar for new Binoc zoom, default disabled (https://github.com/themrdemonized/xray-monolith/pull/328), addresses issue https://github.com/themrdemonized/xray-monolith/issues/325
* Lucy: Fix lua function to get/set shaders and textures of models (https://github.com/themrdemonized/xray-monolith/pull/329)
* LVutner:
  * Removed useless DSVs... (https://github.com/themrdemonized/xray-monolith/pull/330)
  * Possible fix for corrupted CBuffers [r_ComputePass] (https://github.com/themrdemonized/xray-monolith/pull/331)

**2025.08.09**
* Replace `smart_cast` with fast_dynamic_cast library (https://github.com/ixray-team/ixray-1.6-stcop/commit/2197a168bbd700f64df0fbcb5f0139a289a39102)
* Convert LuaJIT NMake to VS2022 project (https://github.com/themrdemonized/xray-monolith/pull/323)
* Reduced .pdb size

**2025.08.07**
* VodoXleb: Add scope_dynamic_zoom = off for binoculars
* Ncenka: Dynamic Devices turn on/off (https://github.com/themrdemonized/xray-monolith/pull/318)

**2025.08.04**
* Debug renderer supports strings as ids for primitives
* Moved new ammo aggregation script into item_weapon, directly replacing the old implementation

**2025.08.01**
* Fixed missing `ik_calc_ssa` setting in Modded Exes menu
* Fixed crash with certain mods that misuse ammo aggregation function

**2025.07.31**
* Use x64 toolchain for compiling
* Orleonn: Lua export: CUIDialogWnd::AllowWorkInPause and render_device:pause_ex (https://github.com/themrdemonized/xray-monolith/pull/308)
* NLTP_ASHES: Add fail-safe in CGameObject::net_Spawn to try to update a missing model (https://github.com/themrdemonized/xray-monolith/pull/311)

**2025.07.29**
* Bone calc optimizations
  * Usage of Screen Space Area (SSA) instead of distance check, works better with larger objects such as pseudogiants
  * `ik_calc_ssa` cvar to control the optimization strength, default is 0.006
* Reduced trader update radius 100 -> 30
* `bullet_on...` callbacks received `bullet.element` field. When bullet hits alife object, element will be a bone id. If its static geometry, then it will be a geometry triangle number. -1 if there was no hit.
* New ammo aggregation function, waits for game objects to be online and a different algorithm. Probably fixes https://github.com/themrdemonized/xray-monolith/issues/118

**2025.07.27**
* Lucy: Model Visbox Update and Script Attachment Fixes (https://github.com/themrdemonized/xray-monolith/pull/306)

**2025.07.26**
* Nearby traders' inventory will be updated in advance
* Sound:
  * Updated OpenAL version to 1.23.1
  * Integration of OpenAL EFX extensions, `snd_efx` works now (https://github.com/ixray-team/ixray-1.6-stcop/commit/e429c13023261623b5260c8e85b588d6d8535e44)
  * Added some sound environments where they were appropriate from (https://www.moddb.com/mods/doctorx-call-of-the-zone/addons/dead-air-spatial-sound-and-reverb).
  * Not all maps were added from that mod. If you want all of them, download it separately. Maps that have sound environments:
    * Underground maps
    * Swamps
    * Dark Valley
    * Rostok
    * Brain Scorcher
    * Red Forest
    * Hospital
    * Pripyat Outskirts
  * `snd_efx_environment_change_time` to change interpolation time between sound environments on a map, default 1.66 seconds

**2025.07.23**
* Various bug fixes and crash fixes
* ZoulKrystal: Gasmask performance edit and ltx aim fov (https://github.com/themrdemonized/xray-monolith/pull/304)

**2025.07.20**
* Fixed flickering bones when using `mt_update_weapon_sounds`
* VodoXleb: Callback for GAME path build fail (https://github.com/themrdemonized/xray-monolith/pull/301)

**2025.07.19**
* Optimizations
  * Updates to weapon sound positions moved to separate frame, can be toggled with `mt_update_weapon_sounds` cvar, default on. Slight performance gain depending on the amount of weapon game objects
  * Calculating bones optimization will be engaged only after fully loading the level
* Gameplay
  * Optional progressive stamina drain, stamina usage linearly depends on current weight instead of hard cutoff point, cvar `g_progressive_stamina_cost`, default off
  * Fix crows AI fly target position (https://github.com/ixray-team/ixray-1.6-stcop/commit/4e7de9844c1906749bb519f9c6ce350f42f02dea)
  * NPCs will turn their heads to look at actor when upclose (https://github.com/ixray-team/ixray-1.6-stcop/commit/e55a85f0d5b719e3cd9ce23ca7976b0ca2124b08)
    * cvar `g_npcs_look_at_actor` to enable the feature, default on
    * cvar `g_npcs_look_at_actor_min_distance` to control minimum distance when they start to look, default 3.5
    * callback `npc_on_before_look_at_actor` to control the behaviour of npcs
* Fixed https://github.com/themrdemonized/xray-monolith/issues/296
* VodoXleb: Fix `_G.get_object_squad` error (https://github.com/themrdemonized/xray-monolith/pull/299)

**2025.07.16**
* Fixed crash due to not clearing pointer to deleted IRenderable in bones calculations

**2025.07.15**
* Reducing updates of bones calculations instead of disabling them, fixes issues with T-posing corpses

**2025.07.14**
* DLTX: Allow DLTX's `>` to create the property if it doesn't exist (https://github.com/themrdemonized/xray-monolith/issues/289)
* Optimizations:
  * Skeleton models outside of view frustum won't have bones calculations, less CPU load
  * Additionally `r__optimize_calculate_bones` cvar allows to disable calculations for far away objects (default enabled)
  * `ik_calc_dist` acts as a distance, over which calculations stop (default 100)
  * `ik_always_calc_dist` is a distance, under which models will perform calculations even when not in frustum (default 20)
  * In heavily populated maps with loads of entities expect around 2ms less frame time if you are bound by CPU
* Sound:
  * Added distance based delay according to the normal 343m/s speed of sound. Console variables to tweak:
    * `snd_distance_based_delay_power` controls the delay strength. 0 will disable delay. Default 1
    * `snd_distance_based_delay_min_distance` controls minimum distance in meters to start noticing the delay. Default 50
  * Added optional pitch variation to sounds. Every time the sound is played it will have slightly different pitch. `snd_pitch_variation_power` controls the variation strength. Default 0

**2025.07.12**
* lulnope: expose `memory_remove_links` to lua scripts

**2025.07.08**
* Spawn antifreeze: fixed issue related to bolts, introduced in previous version: https://github.com/themrdemonized/xray-monolith/issues/287
* SaloEater: Debug scripts with luapanda (https://github.com/themrdemonized/xray-monolith/pull/251)

**2025.07.06**
* Spawn antifreeze:
  * Added `mod_system_spawn_antifreeze_ignore.ltx` file, lines in that file are sections that won't be processed by antifreeze. Partially addresses issue https://github.com/themrdemonized/xray-monolith/issues/283
  * Fixed CTD on a possible condition when trying to spawn child items while parent is already destroyed and not exists in ALife, fixes issue https://github.com/themrdemonized/xray-monolith/issues/284
* Replaced all occurences of `luabind` to `::luabind`
* Migrated projects to C++17 standard

**2025.07.05**
* Ascii1457: SSS 23.2 Update

**2025.07.03**
* Ascii1457: SSS 23.1 Update

**2025.07.02**
* Spawn Antifreeze: Don't prefetch helicopters, might fix issue https://github.com/themrdemonized/xray-monolith/issues/278

**2025.07.01**
* `hanging_lamp_ignore_match_configuration` cvar to circumvent the SSS23 + HF crash, related to https://github.com/themrdemonized/xray-monolith/issues/273
* Fix https://github.com/themrdemonized/xray-monolith/issues/276

**2025.06.30**
* Disable printing DLTX `!` warnings by default, toggle it with `print_dltx_warnings 1` cvar
* Lua GC step increased to 300
* OneMorePseudoCoder:
  * Don't apply rendering optimizations to cars and helicopters (https://github.com/themrdemonized/xray-monolith/pull/271)
  * Fix npc footsteps (https://github.com/themrdemonized/xray-monolith/pull/274)

**2025.06.28**
* Fixed crash to desktop in updateDiscordPresence function due to race condition
* Spawn antifreeze: do not prefetch G_RPG7 and G_FAKE objects, fixes "incorrect destroy sequence for object" error
* Ascii1457: SSS 23 Update
* LVutner: R11G11B10_FLOAT support

**2025.06.27**
* Disabled initial script prefetches, conflict with SSS

**2025.06.26**
* Spawn antifreeze: Fixed some random crashes to desktop due to race condition in shader creation
* LVutner: [Render] PIXEVENTs. Thanks to forserx and frowik

**2025.06.24**
* Hotfixes:
  * Spawn antifreeze: Fixed crashing to desktop if model hasn't been found for prefetching
  * Dynamic news manager: Check if the type of stuff in loot table is actual object, fixes crashes with existing moddb fixes
* Fixed "overriding /ob2 with /ob3" warning when building
* Fixed issue https://github.com/themrdemonized/xray-monolith/issues/267

**2025.06.23**
* Optimization pass:
  * Spawn Antifreeze: put offloading model and texture resources into separate thread before spawning entities. Enabled by default. If you notice some issues, please report them and turn it off in console `spawn_antifreeze 0` (https://github.com/themrdemonized/xray-monolith/pull/257)
  * Move Discord update into separate thread
  * Fixed `trans_outfit.transparent_gg()` function having whole alife loop, now using only game objects
  * Fix Dynamic News Manager loot table containing possible destroyed objects, leading to busy hands
  * Changed Lua garbage collection step 400 -> 160
* Moved project files outside of vs2022 folder, refactor solution file. Should fix broken precompiled headers
* ProfLander: Integrate optick profiler (https://github.com/themrdemonized/xray-monolith/pull/262)

**2025.06.20**
* Updated Github Action to use `softprops/action-gh-release`
* [Feature Request] Added an option to invert Mouse Wheel when changing weapons (https://github.com/themrdemonized/xray-monolith/issues/261)
* ProfLander: 3D Ballistics Fixes (https://github.com/themrdemonized/xray-monolith/pull/259)
* Kutez: Update v3 Indoor Gunsound Framework (https://github.com/themrdemonized/xray-monolith/pull/260)

**2025.06.18**
* Lucy: Script Attachment 3D UI Scale/Origin (https://github.com/themrdemonized/xray-monolith/pull/258)

**2025.06.15**
* Lucy: Some more changes (https://github.com/themrdemonized/xray-monolith/pull/254)

**2025.06.11**
* Lucy: ImGui implementation with script support (https://github.com/themrdemonized/xray-monolith/pull/250)

**2025.06.09**
* NLTP_ASHES: Exported `_keyboard`'s `key_name` and `key_local_name` to Lua (https://github.com/themrdemonized/xray-monolith/pull/248)

**2025.06.08**
* NLTP_ASHES: Add `actor_on_death` callback (https://github.com/themrdemonized/xray-monolith/pull/244)

**2025.06.05**
* The engine will crash if override sound failed to be initialised in `_G.COnBeforePlayHudSound` callback
* ProfLander: Fix black default crosshair (https://github.com/themrdemonized/xray-monolith/pull/242)
* NLTP_ASHES: Implement SetHudMode on CScriptParticles and CParticlesObject (https://github.com/themrdemonized/xray-monolith/pull/243/files)

**2025.06.03**
* NLTP_ASHES: Export color animation functions & implemented RemoveColorAnimation (https://github.com/themrdemonized/xray-monolith/pull/241)

**2025.05.29**
* lulnope: Fix popup positioning for tall popups in tasks/map tab of PDA (https://github.com/themrdemonized/xray-monolith/pull/237)

**2025.05.26**
* Fixed and cleaned up Modded Exes options
* Lucy: Script Attachment improvements (https://github.com/themrdemonized/xray-monolith/pull/234)

**2025.05.24**
* NLTP_ASHES: Export CurrentGameUI()->m_pMessagesWnd to Lua (https://github.com/themrdemonized/xray-monolith/pull/233)

**2025.05.23**
* damoldavskiy: Procedural move animations (https://github.com/themrdemonized/xray-monolith/pull/230)

**2025.05.19**
* damoldavskiy: MAS: min zoom handling (https://github.com/themrdemonized/xray-monolith/pull/228)

**2025.05.16**
* Fixed `get_ui_position` function for script attachments
* ProfLander:
  * Multiple crosshairs (https://github.com/themrdemonized/xray-monolith/pull/224)
  * Implement `g_aimpos_zoom` (https://github.com/themrdemonized/xray-monolith/pull/226)
  * Use correct trace for `g_get_target_position origin` and direction (https://github.com/themrdemonized/xray-monolith/pull/227)

**2025.05.12**
* NLTP_ASHES: Expose CUIProgressBar's m_UIProgressItem to LUA (https://github.com/themrdemonized/xray-monolith/pull/225)

**2025.05.10**
* added ScrollView instance in `on_news_received` callback
* Lucy:
  * Script attachments now use strings instead of IDs (https://github.com/themrdemonized/xray-monolith/pull/221)
  * [ReShade] Fix crash in fullscreen mode (https://github.com/themrdemonized/xray-monolith/pull/223)
* Lander: Expose wallmark range via options menu (https://github.com/themrdemonized/xray-monolith/pull/222)

**2025.05.07**
* Upgrade bug fix for MAS (https://github.com/themrdemonized/xray-monolith/pull/219)

**2025.05.06**
* `get_console():get_variable_bounds(cvar)` function to get bounds of cvar
* damoldavskiy:
  * MAS scope spawn fix (https://github.com/themrdemonized/xray-monolith/pull/217)
* ProfLander:
  * Fix scriptable near-wall jitter (https://github.com/themrdemonized/xray-monolith/pull/213)
  * Implement g_aimpos to govern bullet direction (https://github.com/themrdemonized/xray-monolith/pull/214)
  * Improved categorization and definition machinery for settings menu (https://github.com/themrdemonized/xray-monolith/pull/215)

**2025.05.03**
* `ray_pick:query()` will clear previous query result
* GhenTuong: Merge force-body-state, axr_beh improvements, vignette_control (https://github.com/themrdemonized/xray-monolith/pull/209)
* ProfLander:
  * Improved reticle interpolation (https://github.com/themrdemonized/xray-monolith/pull/206)
  * Crosshair Sizing Fixes Again (https://github.com/themrdemonized/xray-monolith/pull/210)

**2025.05.01**
* `g_draw_pickup_item_names` cvar for disabling item names on holding key (https://github.com/themrdemonized/xray-monolith/issues/191)
* ProfLander: 
  * Fix logspam in CWeapon::GetNearWallOffset (https://github.com/themrdemonized/xray-monolith/pull/192)
  * Fix option defaults for firepos settings (https://github.com/themrdemonized/xray-monolith/pull/193)
  * Fix crosshair readout jitter (https://github.com/themrdemonized/xray-monolith/pull/194)
  * Crosshair: Far size option (https://github.com/themrdemonized/xray-monolith/pull/197)
  * Lua trace API improvements (https://github.com/themrdemonized/xray-monolith/pull/198)
  * Fix animated HUD FOV (https://github.com/themrdemonized/xray-monolith/pull/199)
  * Scriptable Nearwall (https://github.com/themrdemonized/xray-monolith/pull/201)
  * Add 'Always Show Crosshair' option (https://github.com/themrdemonized/xray-monolith/pull/203)
* VodoXleb:  Callback to force set NPC body_state (https://github.com/themrdemonized/xray-monolith/pull/202)

**2025.04.28**
* Possibility to set multiple objects to ignore for `ray_pick` via multiple `set_ignore_object` calls: 
  ```lua
    ray:set_ignore_object(obj_1)
    ray:set_ignore_object(obj_2)
    ...
  ```
* ProfLander: Implement g_firepos (https://github.com/themrdemonized/xray-monolith/pull/190)

**2025.04.27**
* Removed duplicate stack traces on Lua-related CTD
* `obj:bounding_box(bool bHud)` method to get bounding box of an object
* ProfLander:
  * Shader-Based Crosshair (https://github.com/themrdemonized/xray-monolith/pull/183)
  * Positional Near-Wall Offset (https://github.com/themrdemonized/xray-monolith/pull/186)

**2025.04.25**
* `game.ui2world_offscreen(pos)` function for unprojecting from ui coordinates outside of screen
* Additional vector exports:
  * static functions:
    * `vector.generate_orthonormal_basis(Fvector dir, Fvector up, Fvector right)`
    * `vector.generate_orthonormal_basis_normalized(Fvector dir, Fvector up, Fvector right)`
  * `function project(Fvector u, Fvector v)`
  * `function project(Fvector v)`
* Additional matrix exports:
  * `function transform(Fvector, Fvector)`
  * `function transform(Fvector)`
  * `function transform_tiny(Fvector, Fvector)`
  * `function transform_tiny(Fvector)`
  * `function transform_dir(Fvector, Fvector)`
  * `function transform_dir(Fvector)`
  * `function hud_to_world()`
  * `function world_to_hud()`
* Kutez: Update v0.3 Indoor Gunsound Framework (https://github.com/themrdemonized/xray-monolith/pull/176)
* ProfLander:
  * HUD <-> World Transforms (https://github.com/themrdemonized/xray-monolith/pull/179)
  * Fix weapon particle projection in third-person (https://github.com/themrdemonized/xray-monolith/pull/180)
  * Reduce code duplication in bone_position / bone_direction (https://github.com/themrdemonized/xray-monolith/pull/182)

**2025.04.22**
* `on_before_play_hud_sound` callback, possibility to override hud sound with another one
* `obj:bone_transform` functions to get transform matrix of a bone
* `obj:xform` function to get transform matrix of an object
* New vector functions
  * `hud_to_world()` to translate vector from hud space to world space
  * `world_to_hud()` to vice versa
  * `hud_to_world_dir()` to translate direction vector from hud space to world space
  * `world_to_hud_dir()` to vice versa

**2025.04.21**
* NLTP_Ashes: Add support for distortion on HUD geometry (https://github.com/themrdemonized/xray-monolith/pull/174)
* Lucy: Updated ReShade Support (https://github.com/themrdemonized/xray-monolith/pull/175). **If you are updating from version older than 2025.04.21 and use Reshade, you need to update Reshade as well**

**2025.04.19**
* Lucy: (https://github.com/themrdemonized/xray-monolith/pull/171)
  * Removal of Model Pool caused a lot of unpredictable crashes, so now it's back but with a small change to work in line with the set_shader lua methods :)
  * Added `get_default_shaders` and `reset_shader` to easily revert a model to its vanilla shaders without the need to store default values in a table
  * Added support for getting/setting/resetting Script Attachment model shaders/textures

**2025.04.18**
* VodoXleb: Actor camera y offset (https://github.com/themrdemonized/xray-monolith/pull/170)

**2025.04.15**
* Fixes (https://github.com/themrdemonized/xray-monolith/issues/168)
* Lucy: Fixes (https://github.com/themrdemonized/xray-monolith/pull/167)

**2025.04.14**
* Lucy: Fixes (https://github.com/themrdemonized/xray-monolith/pull/165)

**2025.04.13**
* Kutez: Indoor Gunsound Framework (https://github.com/themrdemonized/xray-monolith/pull/161)

**2025.04.11**
* Lucy: Scripted game_object shader/texture changing (https://github.com/themrdemonized/xray-monolith/pull/157)

**2025.04.05**
* removed `FreeRoom_inBelt` check for attachments

**2025.04.04**
* Script fixes
  * Fixed `utils_item.has_scope` returning wrong addon items on some occassions

**2025.04.03**
* Script fixes
  * Fixed `axr_trade_manager.npc_trade_buy_sell` and `axr_trade_manager.npc_tech_upgrade_sell` functions
  * Fixed `item_weapon.detach_scope` giving wrong addon items on some occassions

**2025.03.29**
* Bence7661: Fix issue (https://github.com/themrdemonized/xray-monolith/issues/153)

**2025.03.22**
* Fix issue (https://github.com/themrdemonized/xray-monolith/issues/149)

**2025.03.21**
* `--dxgi-old` command line argument to fix Linux issue (https://github.com/themrdemonized/xray-monolith/issues/95)

**2025.03.18**
* Lucy: Remove hardcoded hud_mode for Flashlight class (https://github.com/themrdemonized/xray-monolith/pull/148), Fix of (https://github.com/themrdemonized/xray-monolith/issues/147)

**2025.03.17**
* Fix of (https://github.com/themrdemonized/xray-monolith/issues/146)

**2025.03.16**
* Lucy: Fix headlamp position (https://github.com/themrdemonized/xray-monolith/pull/145)

**2025.03.15**
* `obj:list_bones(bHud = false)` to return a table of all bones of an object in `[bone_id] = bone_name` form. If `bHud` is true, then use hud model
* damoldavskiy: Repaired MAS for Script Attachments (https://github.com/themrdemonized/xray-monolith/pull/144)

**2025.03.14**
* `use_separate_ubgl_keybind` to enable separate UBGL keybind or return to vanilla system
* damoldavskiy: Modular Attachment System: alt positions, zoom step count (https://github.com/themrdemonized/xray-monolith/pull/143)
* Lucy: Script Attachments (https://github.com/themrdemonized/xray-monolith/pull/140)

**2025.03.13**
* Bence7661: Enable freelook while reloading (https://github.com/themrdemonized/xray-monolith/pull/139)

**2025.03.12**
* Bence7661: Russian translation for UBGL/Alt aim separation feature (https://github.com/themrdemonized/xray-monolith/pull/138)

**2025.03.09**
* damoldavskiy: Fixes for Modular Attachment System (https://github.com/themrdemonized/xray-monolith/pull/136)
* ZoulKrystal: Pregenerated gasmask rain noise (https://github.com/themrdemonized/xray-monolith/pull/137)

**2025.03.07**
* damoldavskiy: Modular Attachment System (https://github.com/themrdemonized/xray-monolith/pull/135)

**2025.03.06**
* `actor_on_update_pickup` callback to check if there is an item possible to be picked up

**2025.03.05**
* Bence7661: Ability to separately set keybinds for UBGL (Under Barrel Grenade Launcher) and alternate aim switch (Canted, Laser, etc) (https://github.com/themrdemonized/xray-monolith/pull/134)

**2025.02.28**
* DXML:
  * Added `flags` parameter to `on_xml_read` callback. Currently supported flags:
    * cache - if `flags.cache = true` then the result of DXML will be cached and the callback will not be fired next time the XML file is processed by the engine
  * Fixed typos in `dxml_core.openXMLFile` utility function

**2025.02.25**
* New engine exports:
  * db.actor:get_actor_object_looking_at() - get object actor is looking at and in interaction radius
  * db.actor:get_actor_person_looking_at() - get inventory owner actor is looking at and in interaction radius
  * db.actor:get_actor_default_action_for_object() - get the interaction action for the object actor is looking at

**2025.02.22**
* Bence7661: Magazine fed grenade launcher class -> SSRS (allows modding in mag fed GLs) (https://github.com/themrdemonized/xray-monolith/pull/130)

**2025.02.19**
* VodoXleb: `g_firepos_zoom` to enable bullets firing from gun barrel while aiming (https://github.com/themrdemonized/xray-monolith/pull/129)

**2025.02.07**
* GAMEMTL_SUBITEM_COUNT increased 10 -> 20

**2025.01.26**
* privatepirate: Re-design of Modded Exes Options menu

**2025.01.25**
* `db.actor:get_actor_ui_luminosity()` to get luminosity as displayed on the HUD from 0 to 1

**2025.01.22**
* `allow_silencer_hide_tracer` cvar is 0 by default

**2025.01.19**
* TIHan: Stutter improvement, disable running 'net_Relcase' on scripts (https://github.com/themrdemonized/xray-monolith/pull/122)

**2025.01.17**
* Possibility to set `fire_point_silencer` for weapons. Adjust fire point when silencer is attached, defaults to the value of fire_point. Supports both HUD and world models

**2025.01.13**
* privatepirate: Fixed HDR Settings UI

**2024.12.11**
* Fix of "Fail to detach scope from some weapons" (https://github.com/themrdemonized/xray-monolith/issues/114)

**2024.12.10**
* momopate (https://github.com/themrdemonized/xray-monolith/pull/112):
  * Added console commands:
    * `fix_avelocity_spread` -> Bool; Fixes moving the camera not affecting accuracy.
    * `apply_pdm_to_ads` -> Bool; Makes it so that PDM values from the weapon are properly applied when aiming down sights.
    * `smooth_ads_transition` -> Bool; Makes it so that going in and out of ADS smoothly changes the accuracy bonus instead of changing only at full ADS.
    * `allow_silencer_hide_tracer` -> Bool; Optionally disables silencers being able to hide tracers completely. (Defaults to true)
  * Added `silenced_tracers` (bool) field to weapons sections. If the ammo type can be hidden when using a silenced weapon, setting this field to true makes the tracer visible again.
  * Added `GetZoomRotateTime()` and `SetZoomRotateTime(float val)`.
  * Added `senderweapon` to the table used for `level.add_bullet()` to improve compatibility with modding. Defaults to sender.
  * Added `bkeep_speed` to `SetActorPosition()`.
  * Added `SetMovementSpeed()`. Exported as `set_movement_speed(vector vel)` for scripting.

**2024.12.09**
* SSS 22 Update

**2024.11.30**
* Moved `_g.script` patches to separate script and added it into `script.ltx` load
* RavenAscendant: alife_clone_weapon fix

**2024.11.27**
* Possibilty to change vertical mouse sensitivity factor by typing `mouse_sens_vertical` in console or in Modded Exes settings. The number is a multiplicative factor to overall mouse sensitivity, default 1

**2024.11.10**
* New Zoom Delta Algorithm: Changes zoom levels of a scope to have same Visual zoom change between levels, to enable it type `new_zoom_delta_algorithm 1` in console (https://github.com/themrdemonized/xray-monolith/issues/98)

**2024.11.09**
* Temporary disabled cform_cache (https://github.com/themrdemonized/xray-monolith/issues/100)

**2024.11.08**
* Lucy, RavenAscendant, NLTP_ASHES: Transparency shader fix for detectors (https://github.com/nltp-ashes/Western-Goods/releases/tag/fix-vanilla-shader)

**2024.11.04**
* `level.map_get_object_spots_by_id(id)` to get all map object spots associated with `id`

**2024.11.02**
* Possibility to define `scope_texture` and `scope_texture_alt` in weapon upgrades

**2024.11.01**
* Poltergeists will always spawn visible corpse upon death. Previously flame ones and pseudogeists would spawn only collision mesh without visible mesh. To restore old behaviour, type `poltergeist_spawn_corpse_on_death 0` in console  

**2024.10.28**
* VodoXleb: Add explosive_item_on_explode callback (https://github.com/themrdemonized/xray-monolith/pull/97/files)

**2024.10.26**
* VodoXleb: Add torch force update function to use in Lua (https://github.com/themrdemonized/xray-monolith/pull/96)

**2024.10.22**
* `aaa_sound_object_patch.script` file: Rework of the `sound_object` class to have caching of playing sounds, fixing the issue of abrupt cutting of sounds due to LuaJIT GC

**2024.10.19**
* Ishmael: Allow addons to hide loading tips (https://github.com/themrdemonized/xray-monolith/pull/91)
* Added console command `print_bone_warnings` to toggle printing warnings when using bone_position and bone_direction functions and encounter invalid bones

**2024.10.18**
* Fixed wrong initial zoom level with dynamic zoom scopes
* `level.add_bullet` supports table as an argument to define bullet properties. Refer to `lua_help_ex.script` file for available table fields

**2024.10.14**
* deggua: Fix Grass Patterns due to Bad PRNG Seeding (https://github.com/themrdemonized/xray-monolith/pull/86)

**2024.10.10**
* damoldavskiy: Added hud_fov_aim_factor console parameter which which lowers hud_fov in ads. Useful for 3D scopes. Default is 0 (https://github.com/themrdemonized/xray-monolith/pull/83)

**2024.10.07**
* damoldavskiy: Min/max scope zoom fov params (https://github.com/themrdemonized/xray-monolith/pull/81)
* Reverted Fix HUD Shaking due to 3DSS compatibility issues (https://github.com/themrdemonized/xray-monolith/pull/75/commits/98022a2a2cd17204c0ffac215d541425940042ba)
* Fix Grenade launcher rounds issue when loading save (https://github.com/themrdemonized/xray-monolith/issues/79)

**2024.10.06**
* Hozar2002: Large levels support and fix HUD model rendering at large distances from map origin (https://github.com/themrdemonized/xray-monolith/pull/75)
* damoldavskiy: Zoom step count adjustment and slight refactor (https://github.com/themrdemonized/xray-monolith/pull/80)
* New `ltx_help_ex.script` file to describe additional LTX params supported by the engine

**2024.10.05**
* deggua: HDR10 Bloom, Lens Flares, Misc Updates (https://github.com/themrdemonized/xray-monolith/pull/78)
* Disabled screenshot capturing if HDR is enabled to prevent crashes
* Added HDR live editing in Modded Exes settings

**2024.10.01**
* Added game version check for ActorMenu_on_item_after_move fix
* DXML: add check for empty XMLs, fixed crash when trying to use `query` on them
* `r__3Dfakescope 0` by default

**2024.09.29 ([clean reinstall required](https://github.com/themrdemonized/xray-monolith?tab=readme-ov-file#troubleshooting))**
* Anomaly 1.5.3 Update

**2024.09.28**
* Reset mouse state on loading the game

**2024.09.25 (clean reinstall required)**
* Gamedata files are packed `00_modded_exes_gamedata.db0` archive during Github Action

**2024.09.23**
* Anomaly 1.5.3 Test3 Update

**2024.09.20**
* Anomaly 1.5.3 Test2 Update

**2024.09.19**
* Anomaly 1.5.3 Test1 Update

**2024.09.15**
* deggua: DX11 DWM vsync fix for windowed modes + fix for broken blur passes (https://github.com/themrdemonized/xray-monolith/pull/72)

**2024.09.14 (remove gamedata/shaders/r3 folder inside Anomaly folder before installation)**
* deggua: Fix HDR10 rendering (https://github.com/themrdemonized/xray-monolith/pull/71)

**2024.09.12**
* GhenTuong: 
  * Fix particles flicking by using single threading update.
  * Objects can use "ignore_collision" in section configs to ignore collision with another objects. Can be map geometry, other physic objects, and creatures.
  `ignore_collision = map,obj,npc`
  * Allow the use of "on_physic_contact" in section configs to run a script when collisions occur.
  * Allow the use of "on_explode" in section configs to run a script when explosive objects explode.

  * Fixed bug, when grenade launchers of all varieties spawn grenade objects to match the amount of ammo and don't release old grenade objects when unloading.
  * Allow the use of "ammo_grenade_vel" in section configs for grenade ammo to have a different velocity
* damoldavskiy: Option to make zombies invisible in heatvision (https://github.com/themrdemonized/xray-monolith/pull/69)

**2024.09.09**
* Ascii1457: fixed FPS drops with weapons without SSS

**2024.09.08**
* Redotix: 3DSS SSS21 Compatibility and Modded exes menu toggle (https://github.com/themrdemonized/xray-monolith/pull/67)

**2024.09.07**
* Ascii1457: fixed water rendering without SSS

**2024.09.06**
* deggua: Fix for D3D management issues in dx10HW.cpp (https://github.com/themrdemonized/xray-monolith/pull/66)

**2024.09.04**
* damoldavskiy: Support for upcoming updates of Shader 3D Scopes (https://github.com/themrdemonized/xray-monolith/pull/64)
* deggua: Fix HDR10 issues with HUD samplers and MSAA setting (https://github.com/themrdemonized/xray-monolith/pull/65)

**2024.09.02**
* deggua: HDR10 output support to the DX11 renderer (https://github.com/themrdemonized/xray-monolith/pull/63)
  * Added HDR parameters to the console variables.
  * `r4_hdr_on` command to enable HDR. Restart is required, default is disabled.
  * `r4_hdr_whitepoint_nits` to set the monitor's HDR whitepoint (max brightness).
  * `r4_hdr_ui_nits` to set the UI brightness in nits and should be set below the whitepoint at a level that is comfortable for the user.
  * `r4_hdr_colorspace` to set HDR colorspace
  * All options are available in Modded Exes options menu
  * Included shaders works with vanilla Anomaly. For compatibility with SSS and GAMMA, download GAMMA shaders from here https://github.com/deggua/xray-hdr10-shaders/releases/tag/v3
  
**2024.08.25**
* Redotix: 3D Shader scopes (3DSS) rendering adjustments (https://github.com/themrdemonized/xray-monolith/pull/62):
  * custom shader flags for 3DSS lenses with a custom render order
  * a render phase for rendering the scope reticle
  * a render target that samples the z buffer of the scene before hud rendering begins

**2024.08.20**
* SSS 21 Update

**2024.08.18**
* NLTP_ASHES (https://github.com/themrdemonized/xray-monolith/pull/61):
  * LTX based patrol paths definitions
  * Support for custom first shot sound effect in `CWeaponMagazined`

    Example:
    ```![wpn_pkm]
    snd_shoot_actor_first             = path\to\my_sfx
    snd_silncer_shoot_actor_first     = path\to\my_sln_sfx
    ```
  * Support for inventory boxes in `game_object:object("section")` function
  * Fix for `ActorMenu_on_item_after_move` callback sending nil values in obj argument
  * Fix for `actor_on_item_take_from_box` callback not firing when item is moved from a slot

**2024.08.10**
* Restored `VIEWPORT_NEAR` value to vanilla due to graphical issues

**2024.08.04**
* VodoXleb: Add viewport_near console command to change camera near value

**2024.07.21**
* VodoXleb: Add callbacks for getting artefact_count of outfit and getting belt_size in inventory.

**2024.07.06**
* Possible 0xffff address violation crash fix when using detector scopes on enemies

**2024.07.05**
* New Lua functions:
  * obj:get_scope_ui() Returns table containing this data
    * `name` - name of scope_texture weapon currently uses
    * `uiWindow` - CUIWindow instance of scope UI
    * `statics` - array of CUIStatic that CUIWindow scope UI instance uses
  * obj:set_scope_ui(string) to set scope UI by a texture name (for example `wpn_crosshair_mosin`)

**2024.06.12**
* GhenTuong: Fix crash related to PhraseDialog.cpp

**2024.05.31**
* Reverted "vegeta1k95: Change item `max_uses` and `remaining_uses` to the maximum of 65535 (u16)", the feature is incompatible with existing savefiles, breaking amount of uses and inventory weight calculation

**2024.05.30**
* NLTP_ASHES: `obj:character_dialogs()` to get a list of available dialogs of character

**2024.05.27**
* etapomom:
  * Added console commands:
    * `allow_outfit_control_inertion_factor` *
    * `allow_weapon_control_inertion_factor` *
    * `render_short_tracers` **
  ```
      (*) Weapon and outfit control_inertion_factor can affect mouse sens, toggleable with their respective commands.
      (**) Tracers will be capped to their minimum length instead of not rendering with this command enabled.
  ```

  * Explosive shrapnel (frags) customization:
    * `frags_ap`
    * `frags_air_resistance`
    * `frags_tracer`
    * `frags_4to1_tracer`
    * `frags_magnetic_beam_shot`
    * `frags_tracer_color_ID`

  * Outfit `control_inertion_factor` ltx field now read in-engine
  * Silencers can hide bullet tracers, toggleable per ammo with the `tracer_silenced` ltx field
  * Tracer length modifier added to the bullet_manager section, `tracer_length_k`
* vegeta1k95: Change item `max_uses` and `remaining_uses` to the maximum of 65535 (u16)

**2024.05.23**
* Fixed incorrect CUIListBox width if `complex_mode=1` is used in conjuction with colored text

**2024.05.21**
* Support for arbitrary-count burst fire with `rpm_mode_2` with working `cycle_down` parameter
* `cycle_down` and `rpm_mode_2` support for weapon upgrades
* `db.actor:get_talking_npc()` function to return the object player talks to

**2024.05.20**
* NLTP_ASHES, GhenTuong:
  * Fixed script_text node not working properly with parametrized dialogs (fix from GhenTuong);
  * Rewrote (more so reformatted) parts of PhraseDialog.cpp in an attempt to make it more readable.

**2024.05.15**
* NLTP_ASHES: hotfix(wpn-zoom-type-changed-with-gl): Fix On Weapon Zoom Type Changed With Grenade Launcher

**2024.05.11**
* Fixed faulty xrs_facer.hit_callback function
* VodoXleb: customizable `explode_effector` per grenade section with `explode_effector_sect_name = effector_sect` ltx parameter
* NLTP_ASHES: `actor_on_weapon_zoom_type_changed` Lua callback

**2024.05.07**
* Fixed CMovementManager parallel pathfinding that could lead to stuck monsters on the map
* Reduced delay between pathfinding computations from 1.5-2.5 to 1-1.5 secs
* Lua function `get_string_table()` to return a table of all translated string texts and corresponding string IDs

**2024.05.06**
* Fixes to `item_device.on_anomaly_touch` and `itms_manager.actor_on_item_before_use` callbacks that didn't respect input `flags.ret_value`
* strangerism: New `freeze_time` console command that allows to freeze time but the sounds can still play

**2024.05.03**
* vegeta1k95: `get_hud():GetWindow()` method

**2024.05.01**
* Fixed missing translation strings for some modded exes options
* New Lua functions
  * `db.actor:update_weight()` - Force update actor's inventory weight
  * `db.actor:get_total_weight_force_update()` - Force update actor's inventory weight and return updated weight

**2024.04.22**
* Added `complex_mode` attribute to `<list>` UI node (default is disabled), which allows for colored text of list items and other features. Example:
  ```xml
    <properties_box>
      <texture>ui_inGame2_demo_player_info_window</texture>
      <list x="0" y="0" width="10" height="10" item_height="18" always_show_scroll="0" can_select="1" bottom_indent="10" right_ident="10" left_ident="20" complex_mode="1">
          <font_s r="220" g="220" b="220" />
          <font r="150" g="150" b="150" font="letterica16" complex_mode="1" />
      </list>
  </properties_box>
  ```
* vegeta1k95:
  * Added an optional and configurable texture glowing for silencers after consecutive shots, visually looks like overheating.

**2024.04.17**
* Tosox:
  * Poltergeists and Burers can throw corpses, to enable it check modded exes options menu or type in console `telekinetic_objects_include_corpses 1`

**2024.04.12**
* Script hit `.bone` field can be assigned `s_hit.bone = "bip01_spine"`, as well as called `s_hit.bone("bip01_spine")`. This will potentially change behaviour of some vanilla scripts and mods to the authors' intended way.   
* vegeta1k95:
  * Added new optional telekinesis type for gravitational anomalies behavior: CTeleTrampolin
  * Can be used by mods, which set `tele_type 1` inside anomalies config sections.
  * CTeleTrampolin Launches objects caught in an anomaly very high to the sky. Now one can experiment with immersive lore-friendly "Springboard"/"Trampolin" anomalies :)


**2024.04.07**
* strangerism:
  * Fixed cursor staying visible when launching demo_record
  * Adds propagation of F12 keypress screenshot event (only available with cmd `demo_record_return_ctrl_inputs`)
  * Toggle cursor visibility when demo_record controls are switched to invoker script and back (only available with cmd `demo_record_return_ctrl_inputs`)
  * Added new API to enable camera bonduary check (limits the camera distance from the actor)
  * Added camera bonduary check and that the camera does not go below the actor ground (not really a ground collision check)
  * Update console cmd `demo_record_return_ctrl_inputs` to enable camera bonduary check
* DaimeneX: base_hud_offset_... params work with PDA

**2024.04.06**
* Safe reading of base_hud_offset_... params
* `on_loading_screen_key_prompt` callback works with `keypress_on_start 0` cvar

**2024.04.05**
* DaimeneX: Editable variables that allow weapon position adjustements that don't require aim tweaks
  * base_hud_offset_pos
  * base_hud_offset_pos_16x9
  * base_hud_offset_rot
  * base_hud_offset_rot_16x9

**2024.04.03**
* vegeta1k95: Fixed the problem where weapon muzzle flash and smoke particles were wrongly aligned wrt each other.

**2024.04.01**
* `npc:set_enable_movement_collision(true|false)` to enable or disable NPC's movement collision
* strangerism:
  * added to DemoRecord extra keybindings to support pc/laptops with no numpad
  * added new console command (demo_record_return_ctrl_inputs) to launch DemoRecord with the ability for client scripts to receive certain keystrokes and react to it while DemoRecord is still running

**2024.03.30**
* vegeta1k95: Fixed gravity anomalies not always playing particles/sounds of body tearing

**2024.03.26**
* vegeta1k95: Add optional "blowout_disable_idle" option for anomalies

**2024.03.24**
* Print Lua callstack when `level.object_by_id(nil)` is called

**2024.03.21**
* vegeta1k95:
  * Fix lens flares and sun/moon for heatvision for DX10 (forgot in the last request);
  * Make corpses heat decay configurable through new console parameters;
* Tosox:
  * New npc_on_item_before_pickup callback

**2024.03.18**
* vegeta1k95:
  * Fix issue when lens flares - from sun, etc - were rendered on top of infrared image.
  * Rendering sun/moon into heat RT - makes it possible for those to be displayed hot in infrared.

**2024.03.12**
* SSS 20.2 Update
* Fix water rendering on DX10

**2024.03.11**
* damoldavskiy: Mark Switch shader param

**2024.03.08**
* Lucy, VodoXleb: new `on_phrase_callback`

**2024.03.05**
* SSS 20 Update

**2024.02.28**
* Crash saving is enabled after loading screen and disabled on player net_destroy. This should fix fake `db.actor is nil` Lua errors
* MOUSEBUFFERSIZE is increased from 64 to 1024
* KEYBOARDBUFFERSIZE is increased from 64 to 128
* Adjustable mouse and keyboard buffer size via console commands `mouse_buffer_size` and `keyboard_buffer_size`

**2024.02.05**
* Xr_ini.cpp fixes
* Fixed incorrect bullet speed in `bullet_on_init` callback 

**2024.02.01**
* Raise limits of `mouse_sens_aim` console command to 0.01-5.0

**2024.01.31**
* LVutner: compute shaders fix

**2024.01.30**
* Hrust: fix volumetric fog

**2024.01.27**
* Fixed: `[error]Expression    : left_eye_bone_id != u16(-1) && right_eye_bone_id != u16(-1), CBaseMonster::update_eyes_visibility`
* Reenabled robinhood hashing

**2024.01.26**
* Temporary disabled robinhood hashing, reason: `[error]Expression    : left_eye_bone_id != u16(-1) && right_eye_bone_id != u16(-1), CBaseMonster::update_eyes_visibility`
* Print Lua stack on `you are trying to use a destroyed object` error
* Print error message for CAI_Stalker::net_Export in the log when crashing
* MagielBruntink: Print missing `.ogg` comments only in debug mode

**2024.01.18**
* MagielBruntink: Increased lua_gcstep default to 400 and allow console editing via `lua_gcstep` command 

**2024.01.13**
* Fixed typo in GameMtlLib.cpp
* OneMorePseudoCoder: Memory leaks fixes

**2024.01.11**
* Added possibility to override gamemtl.xr materials and define new ones via `materials.ltx` and `material_pairs.ltx` files. Please read the guide in those files in `gamedata/materials` folder
* Removed `pSettings->line_exist(sect_name,"fire_point")==pSettings->line_exist(sect_name,"fire_bone")` game crash check
* Raised `GAMEMTL_SUBITEM_COUNT` for material sounds and particles constant from 4 to 10
* Fixed rare unlocalizer crash and crashes on Linux because of this bug

**2024.01.04**
* New `db.actor` exports
  * `db.actor:get_actor_crouch_coef()`
  * `db.actor:set_actor_crouch_coef(float)`
  * `db.actor:get_actor_climb_coef()`
  * `db.actor:set_actor_climb_coef(float)`
  * `db.actor:get_actor_walk_strafe_coef()`
  * `db.actor:set_actor_walk_strafe_coef(float)`
  * `db.actor:get_actor_run_strafe_coef()`
  * `db.actor:set_actor_run_strafe_coef(float)`
  * `db.actor:get_actor_sprint_strafe_coef()`
  * `db.actor:set_actor_sprint_strafe_coef(float)`
* Added `hit_fraction_actor` field for helmet upgrades
* Added filename field for section items
* Added ini script methods for DLTX
  * dltx_print(string sec = nil, string line = nil): prints information about sections. If sec is nil, then whole file will be printed. If section is provided and line is nil, then the whole section will be printed. If both provided, only the provided line will be printed
  * dltx_get_filename_of_line(string sec, string line): returns filename that was used for a line in the section
  * dltx_get_section(string sec): returns a table with section information with structure:
  ```
    {
      [<section_1>] = {
        name = <section_1>
        value = <value_1>
        filename = <filename_1>
      }
      ...
    }
  ```
  * dltx_is_override(string sec, string line): returns true if line in section was overriden by DLTX mod

**2023.12.30**
* Added `bullet_id` field to script hit struct
* Restored behaviour of `r_bool_ex` and `r_value` functions to match vanilla code
* Filtered useless lines in stack trace if `pdb` file is not present

**2023.12.24**
* SSS update
* Minimap shape fix moved into option. Add `ratio_mode = "1"` in `zone_map_*.xml` files in `level_frame` node to enable the new code

**2023.12.23**
* DLTX: simplified possibility to delete whole section, now its just enough to write `!![section]` to completely delete it
* Longreed: added possibility for minimap frame to have a custom rectangular shape 

**2023.12.17**
* Fixed getting wrong values in ini cache due to robin_hood hashing, reenabled the library

**2023.12.16u1**
* Temporary disabled robin_hood hashing to fix crashes

**2023.12.16**
* `xr_unordered_map` type is replaced with robin_hood hashing from https://github.com/martinus/robin-hood-hashing. Additionaly introducing `xr_unordered_set` and `xr_pair` types based on this library
* Added engine-based caching ini values in CIniFile class
* Added Lua function `ini_file:get_filename()` to return the filename of file
* Replaced `ini_file_ex` class with implementation that is derived from `ini_file`, fully backwards compatible with existing `ini_file_ex`
* `level.iterate_nearest` will sort objects by ascending distance before executing Lua callback
* Added `game.update_pda_news_from_uiwindow(CUIWindow*)` function to update news in PDA from news window on the HUD
* Added printing of engine stack trace in the log via StackWalker library https://github.com/JochenKalmbach/StackWalker
  * To make it work you need to download `pdb` file for your DX/AVX version and put it into same place as `exe` file. PDB files are here: https://github.com/themrdemonized/xray-monolith/releases/latest

**2023.12.09u1**
* Fixed crash on failed `tonumber` conversion when using `SYS_GetParam`

**2023.12.09**
* Disabled get_console warning for bool variables
* Lua unlocalizer supports `_g.script`
* Lua unlocalizer filename check is insensitive, ie `[_G]` is the same as `[_g]`
* Disabled Lua caches for ini files when using `SYS_GetParam` and reading variables such as `r_string_ex` in favor of engine functions and to reduce memory footprint and possible GC calls
* Added undocumented Lua extensions exports into `lua_help_ex.script`
* Added new callback `on_news_received` to manipulate game news when they are displayed
* Added `game.change_game_news_show_time` function to postpone fadeout of game news
* Added `throttle(func, time)` function to throttle calls to `func` by `time` in milliseconds


**2023.12.06**
* Added gun object and gun owner params to `actor_on_hud_animation_mark` and `
actor_on_hud_animation_play` callbacks
* Warnings when script fails to get console variables due to incorrect type

**2023.12.02**
* Added possibility to edit freelook angle limit with console command `freelook_cam_limit`
* Using /Ob3 flag for compiling, theoretically should increase performance a bit

**2023.11.28**
* Lucy: add a motion mark to the draw/holster animations of the flashlight to trigger when the light turns on or off

**2023.11.25**
* Added functions for manipulating map spot graphics
  * `level.map_get_object_spot_static(u16 id, LPCSTR spot_type)` will return CUIStatic object of the spot on the map
  * `level.map_get_object_minimap_spot_static(u16 id, LPCSTR spot_type)` will return CUIStatic object of the spot on the minimap
* If DXML query is invalid, the game will crash with an error message and callstack

**2023.11.22**
* GhenTuong: parametrized functions in dialogs (precondition, action, script_text tags)
  * '=' and '!' can be used and work same as condlist. `=` is true condition, `!` is false
  * () can be used to input parameters. Parameters are separated by ':'
  * Example: `<precondition>!file.func(dolg:1:false)</precondition>`
  ```lua
    function func(a,b,dialog_id,phrase_id,next_id,p)
      printf("%s %s %s %s %s %s", a, b, dialog_id, phrase_id, next_id, table.concat(p, " "))
    end
  ```
* Added option to write timestamps in console and log file. Type `log_timestamps 1` in console to turn on the feature
* DXML query function will always return table even with invalid queries

**2023.11.15**
* Fixed crash when trying to use alt aim on Mosin-like scopes

**2023.11.14**
* Fixed issue when having grenade launcher attached will trigger shader scopes

**2023.11.12**
* Fixed physics initialization when obj:force_set_position(pos, true) is used
* Fixed not working shader scopes when alternative sight is an optic
* Removed (DLTX, DXML, ...) string from version title in the main menu screen

**2023.10.27**
* Reverted change to mouse wheel, its not inverted now
* Toggle inverted mouse wheel with console command `mouse_wheel_invert_zoom 1` 

**2023.10.20**
* Removed monster stuck fix due to big fps loses it causes. You can turn it back via console command `monster_stuck_fix 1`

**2023.10.17**
* SSS 18 update
* Lucy: specifying a custom UI bone for Svarog/Veles in the hud sections of detectors via `detector_ui_bone` property
* `string_table_error_msg` console command to print missing translation strings 

**2023.10.05**
* Fixed occasional crash when right click on PDA map
* `db.actor:set_actor_direction` supports roll as third argument

**2023.09.30**
* Added possibility to disable weapon switch with mouse wheel, use console command `mouse_wheel_change_weapon 0`
* Fixed invisible console bug when there are invisible debug gizmos being used
* `db.actor:set_actor_direction` supports pitch as a second argument
* Minor fixes of right click PDA callback
* Added print of stack when incorrect bone id provided for bone functions
* Moved LuaJIT to VS2022 toolchain and applied optimization flags for build 

**2023.09.22**
* Added `on_map_right_click` callback, which allows to right click anywhere on the map and fire user functions at projected real world position. Refer to `callbacks_gameobject.script`
* Added possibility to zoom in and out relative to cursor on the map, instead of always center
* Fixed possible crash when using script functions for getting bone properties
* Added temporary fix for view NPC PDA's function not working correctly if 2D PDA is used
* Added `player_hud` functions documentation in `lua_help_ex.script`
* Added global functions
  * `nextTick(f, n)` will execute function f on the next n-th game tick. Your function should return true if you want to fire it once, just like with time events
  * `print_tip(s)` will print information in news feed as well as in console 

**2023.09.06**
* Moved build procedure to Github Actions in https://github.com/themrdemonized/xray-monolith
* Apart from exes themselves, https://github.com/themrdemonized/xray-monolith repo will contain PDB files if you want to debug the code yourself
* Added `angle` and `force_set_angle` functions for game objects by GhenTuong
* Added `get_modded_exes_version` function

**2023.09.02**
* Performance improvements when calling level.object_by_id function, thanks Lucy for the hint

**2023.08.29**
* Added `apply_torque` function to `get_physics_shell()` Lua object to apply rotational force to models
* Fixes by ![clayne](https://github.com/clayne)
  * https://github.com/themrdemonized/xray-monolith/pull/14

**2023.08.19**
* Removed diff files, please fork XRay-Monolith repo to work on engine and submit changes via pull requests: https://github.com/themrdemonized/xray-monolith
* Added boneId parameter to `on_before_hit_after_calcs` callback
* More descriptive error message on crashes if it was caused by Lua
* Fixes by ![clayne](https://github.com/clayne)
  * https://github.com/themrdemonized/xray-monolith/pull/1
  * https://github.com/themrdemonized/xray-monolith/pull/2
  * https://github.com/themrdemonized/xray-monolith/pull/3
  * https://github.com/themrdemonized/xray-monolith/pull/4
  * https://github.com/themrdemonized/xray-monolith/pull/5
  * https://github.com/themrdemonized/xray-monolith/pull/6
  * https://github.com/themrdemonized/xray-monolith/pull/7
  * https://github.com/themrdemonized/xray-monolith/pull/8
  * https://github.com/themrdemonized/xray-monolith/pull/9
  * https://github.com/themrdemonized/xray-monolith/pull/10
  * https://github.com/themrdemonized/xray-monolith/pull/11
  * https://github.com/themrdemonized/xray-monolith/pull/12
  * https://github.com/themrdemonized/xray-monolith/pull/13
  
**2023.08.09**
* Reduced size of exes
* Changes to build procedure to solve not starting game with certain CPU configurations
* Restored original behaviour of `bone_position` and `bone_direction` functions to solve issues with some mods. If incorrect bone_id is specified for them, the warning will be printed in the console

**2023.08.08**
* SSS update
* Attempt to resolve not starting game with certain CPU configurations. Huge thanks to ![clayne](https://github.com/clayne) for contributing time and efforts to solve the problem

**2023.08.07**
* Lucy: Fix of broken left hand animations

**2023.08.06**
* SSS update

**2023.08.05**
* Fixed possible malfunction of shader scopes by enforcing `r__fakescope 1` on first update
* New callback `on_before_hit_after_calcs` that will fire just before applying hit to an entity, refer to `callbacks_gameobject.script`
* Possibility to keep hud drawed and affected when using `level.set_cam_custom_position_direction` function, refer to `lua_help_ex.script`
* Features and fixes by Lucy:
  * Device/Detector animations can now use the lead_gun bone if their hud section has lh_lead_gun = true (script animations can also use this config toggle in their section)
  * Device/Detector amera animations will now play even if there's something in your right hand
  * Cleaned up player_hud animation code

**2023.07.28**
* Features by LVutner:
  * Added support for Shader Scopes on DX9
  * Added support for Heatvision on DX10
* New file `aaaa_script_fixes_mp.script` that will contain monkeypatches for fixing some vanilla scripts
  * Fixed incorrect behaviour of `actor_on_item_before_pickup` callback
* Fixed unable to switch to russian language when typing text
* Added Troubleshooting section in readme file

**2023.07.24**
* Exes now are built with Visual Studio 2022. In case you have problems, make sure you installed the latest Visual C++ Redistributables. You can find them here: https://www.techpowerup.com/download/visual-c-redistributable-runtime-package-all-in-one/
* Fixes and features by Lucy
  * Reshade shaders won't affect UI, full addon support version of Reshade is required
  * fix for hands exported from blender (you no longer have to reassign the motion references)
  * fix for silent error / script freeze when getting player accuracy in scripts
  * animation fixes (shotgun shell didn't sync with add cartridge and close anims)
  * game no longer crashes on missing item section when loading a save (should be possible to uninstall most mods mid-game now)
  * fix for mutants stuck running in place (many thanks to Arszi for finding it)
  * fix for two handed detector/device animations (swaying is now applied to both arms instead of only the left one)
  * it's now possible to play script particle effects in hud_mode with :play(true) / :play_at_pos(pos, true)
  * the game will now display a crash message when crashing due to empty translation string
  * Scripted Debug Render functions, drawing debug boxes, spheres and lines 
  * Debug renderer works on DX10/11
    * Many thanks to OpenXRay and OGSR authors:
    * https://github.com/OpenXRay/xray-16/commit/752cfddc09989b1f6545f420a5c76a3baf3004d7
    * https://github.com/OpenXRay/xray-16/commit/a1e581285c21f2d5fd59ffe8afb089fb7b2da154
    * https://github.com/OpenXRay/xray-16/commit/c17a38abf6318f1a8b8c09e9e68c188fe7b821c1
    * https://github.com/OGSR/OGSR-Engine/commit/d359bde2f1e5548a053faf0c5361520a55b0552c
  * Exported a few more vector and matrix functions to lua
  * Optional third argument for world2ui to return position even if it's off screen instead of -9999
  * Unified bone lua functions and made them safer
  * It's now possible to get player first person hands bone data in lua
* All new functions are added in `lua_help_ex.script`

**2023.07.21**
* Added `level.get_target_pos()` and `level.get_target_result()` functions, refer to lua_help_ex.script
* Added `actor_on_changed_slot` callback, refer to `callbacks_gameobject.script`
* Hotfix of possible mouse unfocus from game window by Lucy

**2023.07.09**
* Added `anomaly_on_before_activate` event for NPCs in callbacks_gameobject.script to make them less vurnerable to anomalies if pathfinding is enabled
* Added possibility to set custom rotation angle of the wallmark, please refer to lua_help_ex.script in wallmarks_manager class
* 3D UI no longer clip with the sky and visibility in the dark fix by Lucy

**2023.07.07**
* Added `bullet_on_update` callback, please refer to callbacks_gameobject.script file for available info about new callback
* Added `life_time` field to bullet table

**2023.07.04**
* `ai_die_in_anomalies` command now works in real time
* Revised the code that described behaviour of NPCs and monsters when they are near anomalies
  * Anomalies are always visible for AI on engine level, previously it was defined by console variable `ai_die_in_anomalies`
  * Monsters will always try to evade anomalies
  * NPCs behaviour works this way
    * if `ai_die_in_anomalies` is 1, they will try to evade anomalies and will receive damage if they cant
    * otherwise its defined per NPC (by default its vanilla behaviour - no pathfinding and no damage) that can be changed via scripts
* New game object functions for NPCs:
  * npc:get_enable_anomalies_pathfinding() - get the state of anomalies pathfinding
  * npc:set_enable_anomalies_pathfinding(bool) - enable or disable anomalies pathfinding
  * npc:get_enable_anomalies_damage() -  get the state of anomalies damage
  * npc:set_enable_anomalies_damage(bool) -  enable or disable anomalies damage

**2023.07.03**
* Added level.map_remove_all_object_spots(id) function

**2023.07.01**
* Mouse wheel callback `on_mouse_wheel` can consume input if flags.ret_value is false, please refer to callbacks_gameobject.script file for available info about new callback
* Added global variable MODDED_EXES_VERSION

**2023.06.30**
* Added mouse wheel callback `on_mouse_wheel`, please refer to callbacks_gameobject.script file for available info about new callback
* Added `db.actor:get_actor_lookout_coef()` and `db.actor:set_actor_lookout_coef(float)` functions for manipulating maximum lean angle
* Fix of gun disappearing when switching between 1st and 3rd person view

**2023.06.17**
* Added `bullet_on_init` callback, please refer to callbacks_gameobject.script file for available info about new callback
* Small cleanup of dxml_core.script

**2023.06.04**
  * SSS update

**2023.05.29**
  * In case of missing translation for a string, the engine will fallback to english text for this string. To disable the behaviour, use console command `use_english_text_for_missing_translations 0`

**2023.05.27**
  * `local res, obj_id = game.ui2world(pos)` for unprojecting from ui coordinates (ie. mouse cursor) to the world

**2023.05.22**
  * `alife():iterate_object(function(se_obj))` function to iterate all objects that are in the game
  * `level.set_cam_custom_position_direction(Fvector position, Fvector direction)` and `level.remove_cam_custom_position_direction()` to manipulate camera in world coordinates
  * CWeapon additional methods: world model on stalkers adjustments
    * function Set_mOffset(Fvector position, Fvector orientation)
    * function Set_mStrapOffset(Fvector position, Fvector orientation)
    * function Set_mFirePoint(Fvector position)
    * function Set_mFirePoint2(Fvector position)
    * function Set_mShellPoint(Fvector position) 

**2023.04.27**
* SSS update

**2023.04.15**
* Added `get_artefact_additional_inventory_weight()` and `set_artefact_additional_inventory_weight(float)` for artefacts

**2023.04.09**:
* Put extra shader files in 000_shader_placeholder.db0, CLEAN SHADER CACHE IS REQUIRED

**2023.04.08**:

* SSS update
* Smooth Particles with configurable update rate by vegeta1k95
  * To change update rate use console command `particle_update_mod` which takes values from 0.04 to 10.0 (default is 1.0). 1.0 corresponds to 30hz, 0.5 - 60hz and so on. The setting is also available in the options menu in "Modded Exes" group
  * Possibility to set particle update delta in milliseconds in .pe files for fine tuning with `update_step` field

**2023.03.31**:

* SSS update
* Removed maximum engine limit of 5 artefacts on belt

**2023.03.25**:

* Potential fix for stuck monsters from OGSR Engine repo in `control_animation_base_accel.cpp`
* Added `bullet_on_impact` and `bullet_on_remove` callbacks, please refer to callbacks_gameobject.script file for available info about new callbacks

**2023.03.19**:

* Added true first person death camera (enabled by default), that will stay with player when he dies and will react accordingly to player's head transforms. Additional console commands
  * `first_person_death` // Enable First Person Death Camera
  * `first_person_death_direction_offset` // FPD Camera Direction Offset (in DEGREES)
  * `first_person_death_position_offset` // FPD Camera Position Offset (x, y, z)
  * `first_person_death_position_smoothing` // FPD Camera Position Change Smoothing
  * `first_person_death_direction_smoothing` // FPD Camera Direction Change Smoothing
  * `first_person_death_near_plane_offset` // FPD Camera Near Plane Offset

[![Watch the video](https://img.youtube.com/vi/Jm-DRNqnak0/default.jpg)](https://youtu.be/Jm-DRNqnak0)

* Fixed heatvision effects not applied to the player hands
* Added options menu for modded exes settings
  * From options menu you can adjust all added modded exes parameters, such as for Shader Scopes, Sound Doppler, FPD Camera and so on

![image](http://puu.sh/JC40Y/9315119150.jpg)

* Added `level.get_music_volume()` and `level.set_music_volume()` Lua functions to adjust music volume in runtime without messing with console commands

**2023.03.15**:

* Stability updates to heatvision sources
* DXML 3.0 update:
  * DXML now uses own storage for callbacks to ensure they are fired accordingly to registering order
  * Added `insertFromXMLFile` function to read contents of xml file to insert into xml_obj
  * Added optional parameter useRootNode to `insertFromXMLString` and `insertFromXMLFile` functions that will hint DXML to insert contents from a root node of parsed XML instead of the whole file (default: false) 

**2023.03.11**:

* Added heatvision support by vegeta1k95 (https://www.moddb.com/mods/stalker-anomaly/addons/heatvision-v02-extension-for-beefs-nvg-dx11engine-mod/)
* Fixed too big FOV when using shader scopes with `new_zoom_enable` command enabled

**2023.03.09**:

* Added possibility to unlocalize Lua variables in scripts before loading, making them global to the script namespace
  * For unlocalizing a variable in the script, please refer to documentation in test file in `gamedata/configs/unlocalizers` folder
* Fixed the bug where `scope_factor` settings were applied to disabled shader scopes or scopes without defined radius for shader effect
* Fixed non-working adjustable scopes upgrade for weapons

**2023.03.05**:

* Fixed the bug where the weapon with attached adjustable scope and grenade launcher will allow to zoom in with GL. To explicitly enable zooming with active grenade launcher, for whatever reason, add `scope_dynamic_zoom_gl = true` in weapon section in its .ltx file
* Possibility to add shader scopes to alternative sights
  * `scope_dynamic_zoom_alt = true` will enable adjustable scope for alt. sight
  * `scope_texture_alt = <path to texture>` will allow to specify what crosshair to use for alt. sight
* Correct zoom_factor calculation for adjustable scopes with shader scopes enabled, you wont get any extra zoom from shader on top of engine FOV
* `scope_factor` console command that changes zoom by shader scopes now works in real time
* Lowered chromatic abberation and scope blur effect, increasing the quality of image  

**2023.02.20**:

* New SSS update
* New demo-record.diff that contains these changes

  * New console commands:
    * `demo_record_blocked_input 1` will start demo_record but you won't be available to move it or stop it, its intended for manipulation via scripts executing console commands below. The console and Esc key are available
    * `demo_record_stop` will stop all launched `demo_record` commands, including with blocked input ones 
    * `demo_set_cam_direction <head, pitch, roll>` will set the direction the camera is facing and its roll. The parameters are in RADIANS, beware. Use this with `demo_set_cam_position <x, y, z>` to manipulate camera via scripts

**2023.02.18**:

* New SSS update

**2023.02.16**:

* Added `gameobjects_registry` table in `callbacks_gameobject.script` that contains all online game objects and updates accordingly. Additionally, a global iterator `game_objects_iter` added that will go through all online game objects

  ```lua
  for obj in game_objects_iter() do
    printf(obj:section())
  end
  ```

* Pseudogiant stomps now can kill and damage any object, stalker or mutant, instead of only actor. New callbacks provided for pseudogiants attacks in callbacks_gameobject.script

  * To disable new functionality, type in console `pseudogiant_can_damage_objects_on_stomp 0`

**2023.01.28**:

* DLTX received possibility to create section if it doesn't exists and override section if it does with the same symbol `@`.
Below is the example for `newsection` that wasn't defined. Firstly its created with one param `override = false`, then its overriden with `override = true`

```
@[newsection]
override = false

@[newsection]
override = true

```
* Added `bone_direction()` function for game objects
* Updated `lua_help_ex.script` with new functions available

**2023.01.23**:

* MAX_TRIS const increased from 1024 to 16384

**2023.01.13**:

* Fix corrupted print of duplicate section

**2023.01.06**:

* In case of typical first person model/animation errors, the game will print the section that has defined model

**2023.01.03**:

* Added CGameObject::NetSpawn and NetDestroy callbacks to Lua (file callbacks_gameobject.script), to register callback use

  ```lua
  RegisterScriptCallback("game_object_on_net_spawn", function(obj))
  RegisterScriptCallback("game_object_on_net_destroy", function(obj))
  ```

* DXML will no longer process translation strings of non eng/rus languages, they aren't supported yet
* New lua_help_ex.script file where new engine exports will be described
* Exported additional CWeapon functions considering weapon's RPM, handling and recoil
* Exported functions to get and set actors walk accel and walkback coeff

  ```lua
  db.actor:get_actor_walk_accel()
  db.actor:set_actor_walk_accel(float)
  db.actor:get_actor_walk_back_coef()
  db.actor:set_actor_walk_back_coef(float)
  ```
  * DLTX received possibility to add items to parameter's list if the parameter has structure like 
  
  ```name = item1, item2, item3```
  
    * `>name = item4, item5` will add item4 and item5 to list, the result would be `name = item1, item2, item3, item4, item5`
    * `<name = item3` will remove item3 from the list, the result would be `name = item1, item2`
    * example for mod_system_...ltx: 
    
    ```
      ![info_portions]
      >files                                    = ah_info, info_hidden_threat

      ![dialogs]
      >files                                    = AH_dialogs, dialogs_hidden_threat
      
      ![profiles]
      >files                                    = npc_profile_ah, npc_profile_hidden_threat
      >specific_characters_files                = character_desc_ah, character_desc_hidden_threat
    ```

* Exported distance_to_xz_sqr() function of Fvector
* Redesigned duplicate section error, it will additionally print what file adds the section in the first place in addition to the file that has the duplicate
