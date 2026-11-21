# R1 local-shadow A/B/A, 2026-09-29

**Follow-up:** the more selective volume-light test and 0.1.6 sampling option are documented in `R1_UPDATE_0.1.6.md`. Shadow-map reuse did not improve FPS. Turning off only volumetric lights while retaining shadows reproduced most of the speedup; the historical conclusion below must not be read as proof that shadow-map generation itself is the bottleneck.

User quicksave_2 bunker viewpoint; exact September MT DX11 executable C43701F5822950172C9D13A9CD4C92CBDA3E94060CAE794FBA81DEDFD5BA43D0. Separate fixture, 3824x1041 windowed SBS, production 0.1.5 Lua, diagnostic derivative of 0.1.4 native code. The user closed their game before measurement. Sun and local lighting remained enabled.

| Stage | Frames | Measured milliseconds | FPS |
|---|---:|---:|---:|
| Mono, combined-width view | 1383 | 10015 | 138.093 |
| Stereo, normal local shadows | 672 | 10015 | 67.099 |
| Stereo, local shadows disabled | 1417 | 10015 | 141.488 |
| Stereo, local shadows restored | 673 | 10015 | 67.199 |

Four-second warmup before each ten-second sample. Read-only Device.dwFrame and Windows GetTickCount64 counters; actor update counts agreed with frame counts. Completed process exit 0. These are observed frame rates, not isolated GPU timestamps. Mono sees a different horizontal field of view and is only contextual; the meaningful controlled comparison is the stereo A/B/A sequence.

Diagnostic intervention: after joining renderer workers and capturing the existing light registry, save each light's flags, clear only bShadow (bit 6, exact PDB field +24) for both eye passes, and restore flags by scope exit. No light colour or intensity was disabled. This changes the shadowed-point-light path to an unshadowed path, including its associated passes; it does not isolate only shadow-map rasterization. `tests/R1_lighting_probe.cpp` and `tests/R1_lighting_benchmark.script` reproduce the diagnostic, not a player feature. Build the probe to a separate build directory using the same native libraries as R1_build.cmd; do not replace a player ZIP with this DLL.

Conclusion: local shadow processing is a major performance contributor in this specific bunker view. Average frame interval falls from about 14.90 ms to 7.07 ms when these shadows are absent and returns to about 14.88 ms afterward. This does not prove a glass material itself is the cause, nor establish general performance on other scenes/GPUs.

No production FPS fix was implemented. A proper optimization needs consistent shadow-map reuse across eyes (light identity, shadow atlas placement/LOD, visibility, and dynamic casters), with visual correctness verified. Simply disabling shadows is not shipped as a fix. The diagnostic DLL was removed from the fixture afterward; restored player DLL SHA256 121BB9D425A32B295ED5EB706CFB4B1FAF040956D9809717047B4A7F41323CFB.

The first harness initialized during early actor loading and never produced measured samples; it was stopped and is not counted. The successful run delays initialization until three seconds after the actor starts updating.

RU: в этом бункере подтверждён большой вклад теней локальных ламп: 67.1 -> 141.5 -> 67.2 FPS при отключении и возврате теней. Сам свет оставался включён. Это измерение причины, не исправление FPS. Рабочая игра и ZIP не получали диагностическую DLL или отключение теней.

GPU reported by the fresh engine log: NVIDIA GeForce RTX 3070 Ti.
