# R1 PDA Map API validation

Date: 2026-10-12. Author of checks: Codex in the isolated R1 PDA Map API source component.

## Baselines

- Ordinary: `c57fe5b9fab4fe0506659db03137c52d2500eb2f`, upstream tag `2026.10.11`, branch `all-in-one-vs2022-wpo`.
- MT: `871e2491228a64f0d5e257c7a7e0255feb841ac8`, upstream branch `all-in-one-vs2022-wpo-mt`.
- Reference ZIP: `R1_Map_Pencil_v0.4.0_ALPHA.zip`, read-only. Its README claims are not engine/runtime validation for this fork.

## Executed checks

- Ordinary source: MSVC `14.44.35207`, Windows SDK `10.0.26100.0`, x64, C++17, Release-style engine defines. `R1_PdaMapApi.cpp`, `UIMapWnd.cpp`, `ui_export_script.cpp` compiled to real object files; all exit codes 0. Precompiled headers were disabled so fresh headers and Lua exports were compiled.
- Shared production math: `tests/R1_Run_PdaMapMath.ps1` compiled and ran `R1_PdaMapMath_Test.cpp`: 308 checks passed. Covers global/level coordinate inversion, nonzero bounds/origins, four aspect factors, four zoom values, pan, invalid zoom/aspect/NaN, line quad width/winding and degenerate dots. These tests call the same header used by the API.
- MT source: the same three changed translation units compiled separately against the MT source tree, using the same MSVC/SDK and Release-style defines; all exit codes 0. The feature cherry-pick preserved MT-specific UI/registration/project changes.
- Lua syntax: the bundled LuaJIT host/minilua.c (Lua 5.1.5 parser) compiled as an isolated checker; loadfile accepted callbacks_gameobject.script, lua_help_ex.script and the example. Their chunks were not executed. New callbacks, module registration, project source entries and documentation links were inspected; git diff --check passed. Existing tracked files retained UTF-8 without BOM and CRLF checkout line endings; upstream license and authorship files were not modified.

## Not executed

Full engine linking and execution; game tests; 2D/3D PDA display and clipping; input callback/consumption behavior; save/load/death/transition scenarios; MT UI multithreading; renderer/AVX/modpack acceptance; performance measurement. No game/MO2 installation or laboratory reservation was made.

This is a source/API development fork. It contains no claim of a game-tested universal binary, no migrated R1 Map Pencil addon and no engine-side annotation persistence. The documented game acceptance scenarios remain required.

RU: выполнены отдельная компиляция изменённых единиц и 308 проверок общей математики. Полная линковка и проверки в игре не выполнялись. Детальные локальные логи и хэши находятся в `R1_Checks` компонента; они не входят в игровые файлы.
