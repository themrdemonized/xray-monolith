# R1 XREAL Native 0.1.6 — stereo volume sampling candidate

## What changed / Что изменилось

Added an optional, stereo-only cap on local volumetric-light sample density. Default 1.5 gives 36 slices instead of the host default 3.0 / 72 slices. MCM provides an enable checkbox and density 1..5; disable the checkbox to use the original setting. The cap never increases a lower user setting. It does not disable dynamic lights, shadow maps, volumetric lights or sun shafts. This is a quality/performance tradeoff, not a lossless renderer optimization: fewer slices may reveal bands or flicker in light shafts, especially in motion.

RU: добавлено ограничение плотности объёмного света только для стерео. По умолчанию 1.5: 36 слоёв вместо штатных 72. MCM позволяет выключить ограничение или выбрать плотность 1..5. Меньше слоёв — быстрее, но возможны полосы/мерцание лучей. Свет и тени остаются включены. Настройка игры восстанавливается после каждого вызова и не записывается в user.ltx.

## Exact implementation

September MT AnomalyDX11.exe SHA256 `C43701F5822950172C9D13A9CD4C92CBDA3E94060CAE794FBA81DEDFD5BA43D0`, matching PDB age 1. No support is implied for AVX or other builds.

- `CRenderTarget::accum_volumetric`, RVA `0xc7e390`: exact 24-byte entry signature checked before MinHook installation.
- `ps_ssfx_volumetric`, RVA `0x143fd68`, size 16: x selects the slice method; z at +8 is quality. For `x <= 0` the original vanilla per-light method is untouched.
- `native/R1_volume_quality.h`: scoped cap, only inside the two-eye render pair. Original value restored even on C++ exception. `r1st_volume_quality(float)` accepts 0 (original) or 1..5; rejects NaN, infinity, wrong thread and calls inside a render pair.
- Original engine code in `src/Layers/xrRenderPC_R4/r4_rendertarget_accum_spot.cpp` computes `iNumSlices = int(24 * quality)`, normalizes intensity by `1 / quality`, and adjusts slice spacing through `vMaxBounds`. The DLL calls that original implementation; it does not substitute an unlit image.
- The cap changes sampling, not resolution. Full-width intermediate eye targets remain; true per-eye target sizing is still future work.
- Also disable the existing decompression hook explicitly during exported shutdown, alongside the new volume hook. Normal F10 OFF does not unload hooks.

For a source-engine implementation, keep a stereo setting separate from `ps_ssfx_volumetric` and select a local `fQuality = min(ps_ssfx_volumetric.z, stereoCap)` only in the SSS slice branch while rendering a stereo eye. Preserve the existing intensity/spacing calculations. The DLL uses a scoped global substitution because that local calculation is inside the verified original function. Do not port RVAs to another host.

## Controlled measurements

User quicksave_2 bunker view, isolated installation, RTX 3070 Ti, actual windowed buffer 3824x1041. User's game closed. Each stage: 4-second warmup, 10.015-second sample. Frame count from Device.dwFrame matched actor callbacks; GetTickCount64 wall time. These are frame rates, not GPU event timings. Mono has a different field of view and is context only.

| Stereo setting | Frames | FPS |
|---|---:|---:|
| Original 3.0 / 72 slices | 640 | 63.904 |
| Balanced 1.5 / 36 slices | 878 | 87.668 |
| Fast 1.0 / 24 slices | 948 | 94.658 |
| Original restored | 644 | 64.304 |

Balanced gained about 37% here. No general FPS guarantee. Benchmark process exit 0. Mono context: 137.194 FPS. One earlier launch never reached the benchmark and was stopped; it is not counted as a successful run.

Repeat using the final packaged DLL and 0.1.6 player loader, with captures removed from the timing harness: original 63.399, balanced 90.156, fast 97.843, original restored 65.595 FPS (10016 ms per stage), process exit 0. Mono context 137.580. Variation between runs is retained rather than presented as an exact promised gain. Raw measurements: `evidence/R1_volume_quality_benchmark.log`.

Earlier isolation: keeping local shadows on but toggling `r2_volumetric_lights` yielded 65.202 -> 133.300 -> 65.502 FPS (on/off/on), exit 0. This narrows the previous shadow-flag test: that flag also bypassed volumetric rendering. Glass itself has not been proven the cause.

The same-frame shadow-atlas reuse experiment yielded 66.494 FPS uncached, 64.896 cached, 65.795 restored. It provided no speedup. Its hooks, copied shadow textures and replay logic are NOT in the 0.1.6 player DLL.

## Verification and remaining limits

Build, camera math, render-list restoration, volume guard and Lua loader tests passed. WARP compositor test checked 4096 pixels, eye order, edge coverage and viewport restoration. These unit/mock checks are distinct from in-game proof.

Final player DLL SHA256 `E72FAA1D3A32D4C15231E5B1BEFBB873F32D58E04C8550C200DB30BB602757A8`. A fresh visual run with the production 0.1.6 loader completed 977 pairs, native error 0, exit 0. Both-eye captures at density 1.5, original, and 1.0 were inspected: no obvious new lighting bands in this stationary bunker view. This does not certify motion, visible light shafts elsewhere, or comfort in glasses. A preceding visual launch also stalled before its test callback and was stopped; intermittent fixture loading remains unresolved, not counted as a pass. `tests/R1_volume_visual.script` reproduces the capture sequence.

The 3D PDA issue, complete native UI layout, optics/dominant-eye masking, concussion handling, full-resolution efficiency and all-scene lighting correctness remain unresolved. Earlier shutdown heap corruption is not declared solved. No automatic edits to the user's working game or MO2 profile. Install the complete candidate ZIP over/after disabling the previous addon; rollback by reinstalling 0.1.5. No new game required.
