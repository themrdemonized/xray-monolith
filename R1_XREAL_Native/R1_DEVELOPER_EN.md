# R1 XREAL Native: build, architecture and continuation guide

## 1. Scope

This is a reproducible renderer-hook experiment, not a complete VR integration. It renders separate geometric views without modifying the executable file. A test PASS does not establish comfortable use in glasses or full game compatibility.

The original game camera is the **right eye**; the left eye is one IPD to its left. Mouse input and ballistics remain in the original game path. This permits initial iron-sight evaluation without moving the firing ray. The implied head center is offset from the original camera; future head tracking must account for that.

## 2. Exact baseline

Inspected MT source: [e189f080525335d4b9fbb282f8467b4968b13372](https://github.com/themrdemonized/xray-monolith/tree/e189f080525335d4b9fbb282f8467b4968b13372), `all-in-one-vs2022-wpo-mt`, September 12, 2026. This is a source candidate, not a proven byte-reproducible build of the installed executable. Matching PDBs supplied the ABI evidence.

| Host | EXE SHA256 | Evidence |
|---|---|---|
| MT DX11 | `c43701f5822950172c9d13a9cd4c92cbda3e94060cae794fba81dedfd5ba43d0` | PE/PDB and live world/ADS |
| MT DX11AVX | `8c2c508c500cc52ef70bd97f56338bec35483681cde93d01fdf7eebae13fc7b3` | PE/PDB only; no live AVX test |

DX11 PDB GUID: `52941685-9551-4677-9d46-42dca45b5d67`, age 1. See `evidence/R1_*_identity.json` for identities. A release month or executable filename is insufficient. Check with `Get-FileHash .\bin\AnomalyDX11.exe -Algorithm SHA256`.

## 3. Independent build

Use Windows x64, VS2022 C++ Build Tools and Windows SDK providing D3D11, DirectXMath, BCrypt and PPL. MinHook is bundled under `vendor/minhook`; retain its LICENSE.txt. Open an x64 Native Tools Command Prompt for VS2022 in this directory:

```bat
R1_test_math.cmd
R1_test_gpu.cmd
R1_build.cmd
```

`R1_env.cmd` checks that an x64 Developer Prompt is active. The DLL uses C++17, x64 and `/MT`. `sizeof(Concurrency::task_group)==232` is an enforced ABI constraint, not proof of complete PPL compatibility across toolsets. Audit task waiting when changing toolsets.

Output: `package/gamedata/plugins/R1_XREAL_Native/R1_XREAL_Native.dll`. Building this checked-in profile requires neither Python nor a complete engine checkout. Historical `R1_prepare.py` and `R1_prepare_live.py` use author-local dependencies and are not portable installation steps. Do not redistribute game EXEs, PDBs, assets or saves with the addon.

## 4. Loader contract

These are exports of this DLL, not built-in Anomaly APIs:

```c
typedef struct {
    unsigned size, installed, enabled, pairs, passes, frame, restored;
    int error;
} R1Status;
int r1st_install(const char* log_path);
int r1st_set(unsigned enabled, float eye_distance_m, float convergence_m);
int r1st_status(R1Status* out);
int r1st_capture(const char* ppm_path);
int r1st_shutdown(void);
```

`tests/R1_live.script` demonstrates LuaJIT FFI loading with `getFS():update_path('$game_data$', 'plugins\\R1_XREAL_Native\\R1_XREAL_Native.dll')`. Install, control and shutdown must use the same thread. Set status.size to `ffi.sizeof('R1Status')`. Most successful calls return 1. Installation is default-off; call `r1st_set(1,0.064,2)` more frequently than once per 1500 ms to maintain stereo. `r1st_set(0,...)` disables it. Expired heartbeat stops stereo rendering, but the status enabled field is not a separate heartbeat-freshness indicator.

IPD accepts 0..0.085 metres and convergence 0.5..10000 metres. These are implementation bounds, not comfort recommendations. Zero IPD is useful for view comparisons. Capture queues a PPM write for the next composed frame and currently supports RGBA8 backbuffers only; verify the file and log, not merely the request return value.

Errors: `-10` unknown host/changed hook bytes; `-11..-14` MinHook installation/pinning; `-2/-3` invalid thread/state/arguments; `-20` HDR/MSAA/SecondViewport; `-21` D3D targets; `-22` simulation frame changed between eyes; `-23` composition; `-24` unsuitable backbuffer aspect. Installation errors are not all mirrored into status.error: inspect return values.

## 5. Frame flow

1. `identify()` hashes the on-disk EXE, validates x64 PE timestamp/image size and compares 24 in-memory bytes at each of three hook sites. Unknown hosts are refused.
2. MinHook intercepts `CRender::Render`, `CRenderDevice::End` and `CHOM::MT_RENDER`. Addresses use module base plus RVA; ASLR remains enabled.
3. `onRender` validates level readiness, menu, owner thread, heartbeat and modes. Refused frames use the original renderer.
4. Before camera writes, it temporarily clears Device.isRendering, waits for secondary_tasks and restores the flag. This handles existing MT jobs and the idle Lua GC loop. Synchronization still needs review; successful runs do not prove all races absent.
5. Snapshot captures camera/inverse matrices and aspect, and records Device.dwFrame. HOM is disabled during the pair; the HOM hook prevents reuse of the same-frame occlusion result for both eyes.
6. Set left matrices, invoke original Render and copy the backbuffer to left. Set right matrices, invoke Render and copy to right. Simulation, input and fire are not intentionally rerun by this loop.
7. Restore camera/render cache/HOM and verify unchanged frame. The restored counter records this path executing; it is not an independent byte comparison of every engine field.
8. Before original End, a full-screen triangle packs the images into left/right halves. A deferred D3D context plus `ExecuteCommandList(..., TRUE)` preserves immediate-context state.

Each temporary eye target is currently full backbuffer size and is compressed horizontally during composition. This is expensive and is not single-pass stereo. Eye-sized render targets are future work; pair counters are not a performance benchmark.

## 6. Camera math and ADS

For original position P, right basis R and offset s: `Peye=P+R*s`. Right s=0, left s=-IPD. In row-vector convention the view matrix is multiplied by translation `(-s,0,0)`. Projection `_11` is doubled; `_31 += s*_11/convergence`. Cameras are parallel with off-axis projection, not toe-in.

X-Ray stores aspect as **height/width**, so it doubles for a half-width eye. World, HUD and Cam view/project/full matrices, inverses and render caches are updated. Previous matrices are set to current; this is not a replacement for separate temporal histories.

3840x1080 SBS would provide 1920x1080 per eye. The local windowed test requested 2560x720 but actually captured **2544x681**. Inspect the actual backbuffer rather than assuming the selected mode equals client-area dimensions.

`R1_stereo_math.h` provides reference make_pair/project tests. Runtime `eye()` separately applies engine matrices and does not call make_pair. A next test should capture actual matrices and compare control-point projections against the reference.

## 7. ABI map

Only valid for the two exact hosts above. Complete profiles, including global RVAs and signatures, are in `native/R1_hosts.h`.

| Field | Byte offset |
|---|---:|
| Device position/direction/up/right | 64 / 76 / 88 / 100 |
| world/HUD/Cam view | 112 / 176 / 240 |
| world/HUD/Cam projection | 304 / 368 / 432 |
| world/HUD/Cam full | 496 / 560 / 624 |
| Previous matrices | corresponding current +576 |
| FOV / aspect | 1500 / 1504 |
| m_pRender | 2064 |
| isRendering | 2264 |
| inverse view/project/HUD project/full | 2320 / 2384 / 2448 / 2512 |
| SecondViewport / secondary_tasks | 2576 / 2584 |
| level bReady / persistent main menu | 524912 / 2608 |
| CHW device/context/base RTV | 48 / 56 / 72 |
| CRender HOM / CHOM enabled | 528 / 56 |

DX11 Render/End/HOM RVAs: `0xb9dc70 / 0x30e00 / 0xb88040`. AVX: `0xb90290 / 0x30aa0 / 0xb7ad70`. Globals, caches and menu addresses also differ; never combine profile fields across hosts.

## 8. Porting to another executable

1. Preserve the old profile and create a separate game/appdata/save fixture.
2. Obtain a matching EXE/PDB pair. Compare CodeView RSDS GUID/age with the PDB using DIA/DbgHelp. Stop if symbols report unmatched.
3. Resolve Render, End, MT_RENDER, transform-cache functions and menu check. Subtract module base for RVAs. Inspect disassembly, calling conventions, overloads and function boundaries.
4. Verify every layout, field type/size, PPL ABI and global address. Read the matching engine code to identify changed side effects.
5. Add a distinct profile with SHA256, PE timestamp/image size, PDB GUID/age and 24 original hook bytes. Do not disable validation or merely replace a hash to make a new EXE run.
6. Test unknown-host refusal and disabled mode first; then zero IPD and actual IPD. Test DX11/AVX independently and verify refusal when another hook already changes a site.
7. Include source, commands, fresh logs, captures and DLL hash, with untested items named explicitly. Symbol lookup and compilation are not runtime acceptance.

The historical profile generator depends on an earlier local toolkit. An independent port requires its own DIA/DbgHelp dump or a portable extractor; this prototype does not include such an extractor. The checked-in header is sufficient to rebuild for the two existing profiles.

## 9. Reproducing the game harness

Prepare a separate game root, fsgame mapping, appdata and copied save with an actor and ordinary pistol. Provide your own game data and exact executable. Disable TAA (`ssfx_taa (0,0,0,0)`), MSAA and HDR; avoid SecondViewport and simultaneous ReShade depth stereo. Select a wide SBS backbuffer. The initial fixture run with `-nosound` produced repeated missing-sound errors; the successful fixture used normal audio initialization with muted/null output.

Place the DLL at its documented path and both test `.script` files in this fixture's gamedata/scripts only. The DXML bootstrap clears menu/pause for automation. `R1_live.script` registers actor_on_update, loads the DLL, selects a slotted weapon, enables stereo, captures frames, presses zoom/fire, disables hooks and quits. It assumes a suitable saved game; it is not an asset-free integration test.

Save R1_live.log, R1_native.log, fresh xray_*.log, R1_stereo.ppm and R1_ads.ppm. Check normal exit, no FATAL ERROR, counters, image size, eye order and vertical alignment. Compare near/middle/far surfaces and disocclusion between eyes. The evidence PNG is only a format conversion of the native PPM.

Further acceptance matrix: zero IPD; physical IPD; unknown host; existing hook; 16:9 refusal; heartbeat expiry; enable/disable; menu; inventory/PDA; death; save reload; level transition; AVX; targets at three distances; different HUD FOV; iron sights/collimator/3DSS/SecondViewport; 30-minute session. Impact validation needs the original aim ray, right-eye projection and actual hit position, not only ammunition expenditure.

## 10. Prioritized remaining engineering

1. UI: End currently overwrites late 2D HUD. Provide a separate UI target and correct per-eye layout/cursor; define menu behavior. Do not rerun simulation to draw UI twice.
2. State: add RAII for HOM/in_pair/isRendering and fix partial GPU setup. Currently an existing vs/left resource can incorrectly imply complete initialization after another allocation failed. This is a known error-path defect, not a completed fix.
3. Temporal/culling: enforce unsupported-TAA refusal and separate histories. Calculate and some sector/light selection precede the hook, so HOM-off alone does not prove portal-boundary correctness. Audit actual draw calls per eye.
4. Aiming: measure impacts and HUD transforms, then implement optical/SecondViewport handling. Do not alter ballistics to hide projection errors.
5. Controls: default-off MCM titled `R1 XREAL Native`, explicit unsupported-mode handling, save/transition lifecycle and an accessible exit key. Phoenix UDP and independent head rotation are not integrated yet.
6. Performance: use eye-sized targets once correct, then measure CPU/GPU time.

## 11. Direct engine integration

Start at `src/xrEngine/device.cpp`, `src/Layers/xrRenderPC_R4/r4_R_render.cpp`, `src/Layers/xrRender/CHudInitializer.cpp`, `src/xrGame/HUDManager.cpp`, and `src/xrGame/Actor_Weapon.cpp`.

Separate simulation update from render-view. After required MT jobs complete, create two explicit view-state objects containing world/HUD matrices, targets, culling and histories. Render each, compose UI separately, Present once. Preserve the original aim ray before camera changes. Treat optics as an additional view rather than blindly reusing global SecondViewport. This is an integration design, not an implemented engine patch.

For a full engine build follow its README: initialize recursive submodules, install required C++/MFC/ATL components and build engine-vs2022.sln. Use the result in a separate test installation. The addon build does not require this step.

## 12. Handoff and review

Submit DLL source, guarded profiles, MinHook with its license, build commands, tests, RU/EN docs and small relevant evidence. Exclude game db, saves, complete appdata and secrets. Export a committed change using `git format-patch -1 HEAD --stdout > R1_XREAL_review.patch`; apply on a separate branch using `git am R1_XREAL_review.patch`. An addon-source patch is not a finished engine stereo patch.

A draft PR must state its experimental status and missing UI/optics/temporal support, ask for review of render/MT ordering, and explain the explicit per-eye integration path. Update the test matrix, DLL hash and evidence with every revision. A fork or submission does not imply upstream acceptance.
