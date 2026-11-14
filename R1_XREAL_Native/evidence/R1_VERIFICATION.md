# R1 verification record — 2026-09-29

Native source was unchanged between the recorded live tests and the subsequent portable-build-script adjustment.

- Live-tested DLL SHA256: `636a2dc3e0eb0678ef12f8580d2bd4e8a3d143e182883fdb621e1b49e3bee048`.
- Rebuilt DLL SHA256 after build-script validation: `74048aa92739fe77b600a64bb867f5a08cb12790e907189ae5a9fb78b8232d1e`. This exact rebuilt binary has not received a fresh game launch. MSVC output is not configured for deterministic binary reproduction.
- `R1_test_math.cmd`: exit 0, camera geometry/right-eye anchor/convergence/vertical alignment/rotation/input guard PASS.
- `R1_test_gpu.cmd`: exit 0, D3D11 WARP 4096-pixel SBS/eye order/edge coverage/caller viewport restoration PASS.
- `R1_build.cmd`: exit 0, native DLL linked.
- `R1_world_runtime.log`: 1172 pairs / 2344 passes / 1172 restore-path executions / error 0; shutdown PASS.
- `R1_ads_runtime.log`: contains the earlier run plus a second run; second checkpoint 1139 pairs / 2278 passes / 1139 restore-path executions / error 0. wpn_fort ADS assertion passed, ammunition 12 -> 11, shutdown PASS.
- `R1_ads.png`: 2544x681 native SBS capture converted from PPM without image edits. Right eye sees the aligned sight; left eye sees its side. This is qualitative observation, not a calibrated hit-accuracy measurement.

Not tested: live AVX, unknown-host rejection, matrix byte equality, GPU allocation-failure recovery, long sessions, all MT races, portal-boundary culling, optical scopes, separate temporal histories, UI/menus, save reload, transition, head tracking, full GAMMA stack, perceived comfort in glasses.

Game executables, PDBs, assets, original saves and complete game logs are not part of this source handoff.
