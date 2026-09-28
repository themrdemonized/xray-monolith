# R1 XREAL Native — developer experiment

Status, September 29, 2026: a separate x64 DLL renders world geometry from two eye positions within one simulation frame and composes left/right SBS. This is not depth-buffer image conversion. The EXE file remains unchanged on disk; MinHook modifies three code sites in process memory. This is not a complete playable release.

## Evidence

- An isolated September MT DX11 run recorded 1172 pairs / 2344 passes. A second run with a pistol recorded 1139 pairs / 2278 passes at its checkpoint. These are counters, not an FPS benchmark.
- The captured `wpn_fort` ADS image shows the sight straight-on in the right eye and from the side in the left eye. `IsZoomed()` passed and firing reduced ammunition from 12 to 11. This does not establish impact accuracy.
- Disable and hook shutdown completed normally, using separate appdata, settings and a copied save.
- Camera reference math and a real D3D11 WARP composition test passed. The math test does not cover every live engine matrix.

## Unfinished work

Final composition overwrites 2D HUD. Menus are not SBS-ready. There is no finished player loader, MCM, or Phoenix/head-tracking integration. Impact alignment, optical scopes, transitions, save reload and long sessions are unverified. Code rejects HDR, MSAA and active SecondViewport. TAA must be disabled manually; the DLL does not yet enforce this. SSR/AO/other temporal histories are not separated. AVX has an identity profile but no live test in this evidence set. GAMMA compatibility is not claimed.

Do not install `R1_live.script` or `modxml_r1_stereo_test.script` into a normal profile: they control the test actor, request ADS/fire and quit the game.

## Build and use

Read [the developer guide](R1_DEVELOPER_EN.md). In an x64 Native Tools Command Prompt for VS 2022, run `R1_build.cmd`, `R1_test_math.cmd`, and `R1_test_gpu.cmd`. The build uses local source, bundled MinHook, MSVC and Windows SDK. It does not need Python, the original author's toolkit, or a full engine checkout.

Output: `package/gamedata/plugins/R1_XREAL_Native/R1_XREAL_Native.dll`. Copying the DLL does not load it automatically: the test calls `ffi.load`, `r1st_install`, then continuously refreshes `r1st_set`. Game assets, executable binaries, PDBs and save files are not distributed. You need your own Anomaly 1.5.3 and an exact MT executable listed in the guide.

`native` contains source, `tests` contains harnesses, and `evidence` contains results. `R1_prepare.py` and `R1_prepare_live.py` are historical, machine-specific setup scripts, not portable installation tools. They are not needed to build the checked-in native profiles.

Rollback: exit the isolated test process and disable its test scripts/DLL. No EXE restoration is required. A custom MO2 loader can be disabled with its own mod; restart the game because the loaded DLL is pinned until process exit.

Русский: [R1_README_RU.md](R1_README_RU.md).
