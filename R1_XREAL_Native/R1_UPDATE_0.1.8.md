# R1 XREAL Native 0.1.8 — remembered stereo intent

RU: первый запуск автоматически включает стерео. F10 переключает желаемый режим, а не текущее состояние рендера. Ручное выключение запрещает восстановление; ручное включение разрешает восстановление после загрузок, паузы и временных сбросов. Выбор сохраняется между запусками.

EN: first use defaults to automatic stereo. F10 toggles desired state rather than the currently rendered state. Manual OFF suppresses recovery; manual ON permits recovery after loads, pause and temporary resets. Intent persists across sessions.

## Implementation

Only the Lua loader changes behavior; the native DLL is byte-identical to 0.1.7 (its native log still reports 0.1.7). `wanted` is separate from `active`. Existing lifecycle `stop()` calls suspend rendering without changing intent. F10 writes `0` or `1` to `appdata/R1_XREAL_enabled.txt`; this is installation-wide, independent of savegames. Missing preference defaults to ON. Write failures are reported and preserve the session choice. Remove that file to reset the preference.

Recovery requires a living actor, an unpaused device and first-person camera. `level.get_active_cam()` is verified in `src/xrGame/level_script.cpp`; `eacFirstEye = 0` in `Actor_defs.h`. An unavailable view entity returns 255 and cannot enable stereo. A two-second retry interval limits repeated activation; clock rollback resets the deadline. Error messages are deduplicated. Unknown host/DLL installation failure remains latched until restart; native renderer guards are not bypassed. No callback retries a game crash or hung process.

MCM settings are reapplied when enabling. Duplicate callback registration in one script instance is prevented. Main-menu SBS composition remains independent of the world-stereo preference.

## Verification

`tests/R1_loader_test.lua` failed against 0.1.7 at the initial automatic-enable assertion, then passed after implementation. Mock scenarios: initial enable, manual OFF across load, ON after load/options/pause, third-person suspension and first-person recovery, native error/reset recovery, retry throttling, F10 while suspended, persistence across script reloads, Escape/inventory and UI ratio. This verifies Lua state transitions, not actual gameplay.

ZIP CRC, unique paths and exact packaged bytes are checked by the package builder. No new in-game acceptance run was completed. The existing intermittent loading stalls, heap-corruption reports and incomplete UI/optics remain unresolved; this is an experimental candidate.

Install the full ZIP through MO2, disabling the older package. No automatic modification of the active player profile. Rollback: restore the previous ZIP; the preference text file may be removed separately. No EXE or save changes.
