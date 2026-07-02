# PIP_IMPROVEMENT_REPORT — full-engine sweep for PiP/SVP betterment

Branch: `improve/pip-engine-sweep-2026-07-02` (from `audit/pip-bulletproof-2026-07-02`; lineage:
`truepip-realign-mt`@`37a20fb9` → baseline WIP commit → 4 audit fixes → this sweep's commits).
Date: 2026-07-02, fully autonomous. Method: four Opus quadrant-survey agents (lighting/shadows/
materials, post/AA/exposure, screen-space/atmosphere, engine-services/camera/perf) + own geometry-
pipeline section + own verification of every Tier-1 claim before implementation. Companion:
`PIP_AUDIT_REPORT.md` (same-day hardening pass), `pip-research/SCOPE-SYSTEM-AUDIT.md`,
`pip-research/PIP-FULL-ANALYSIS.md`.

## 1. EXECUTIVE SUMMARY

- Subsystems reviewed: **all renderer + engine subsystems that touch the screen** (coverage
  checklist §3): geometry/culling/LOD/details, sun+cascades, point/spot lights, shadow maps,
  static lighting, materials (bump/POM/specular/emissive/wallmarks/forward), tonemap/exposure,
  bloom, LUT/grade, AA/TAA/SMAA, motion blur, DOF, distortion/psi, AO, SSS contact shadows,
  SSR/water, wet surfaces, sky/sun disk/flares, fog, volumetrics, rain, particles/billboards,
  HUD/weapon interaction, camera/projection machinery, RT management/VRAM, update-rate machinery,
  profiling, screenshot/capture, MT job system, console/LTX/Lua surface, second-camera precedents.
- Improvements found: **3 implemented tonight (Tier 1)**, **10 execution-ready blueprints
  (Tier 2)**, **6 ideas (Tier 3)** — plus 2 claims from the survey **disproven during
  verification** (documented, not implemented).
- **The three highest-impact things to do next:** (1) flip `r__svp_skip_lut 1` + `r__svp_skip_dof 1`
  after one visual A/B — the scope pixels currently get the LUT grade twice and (for DOF users)
  focal blur computed for the wrong camera; (2) execute the **scope-cone light capture** blueprint
  (IMP-005) — lights culled by the main view's frustum/fog-distance vanish from the magnified
  image, the most user-visible parity gap found; (3) execute the **VRAM slim** blueprint (IMP-008)
  — ~50 MB of the SVP's ~180 MB footprint is provably wasted (an unfilled second shadow atlas +
  two full-screen-sized RTs on a 720² target).

## 2. PiP BASELINE (Phase 0)
See `PIP_AUDIT_REPORT.md` §2 for the full territory map (identical baseline, same day). In one
line: scoped frames render the scene twice (main, then SVP into a square per-viewport
`CRenderTarget` with a complete `$svp` RT set), share shadow maps via the `dual_accum`
re-accumulate, run a full per-viewport `phase_combine` (own AO/SSR/volumetrics/TAA), capture to
`rt_secondVP` pre-tonemap, and composite onto the lens in the main combine through the 3DSS shader
stack. **Confirmed frame-order spine (post agent, verified):** capture happens after the SVP's
DOF/LUT but before combine_2's tonemap/grade/vignette → scope pixels are tonemapped/graded/
vignetted exactly once by the MAIN pass — but DOF'd and LUT'd twice (hence IMP-001).

## 3. COVERAGE CHECKLIST (verdict per subsystem)
FULL parity (verified, no action): static lighting/lightmaps/hemi; env cubemaps; sky/clouds/sun
disk/flares (per-target, SVP-camera-correct); materials bump/POM/specular; wallmarks;
forward/translucent/particles (per-pass drained, billboards face the SVP camera via the
SetMatrices basis rebuild); anomaly distortion (mask+resolve per-target); volumetrics (per-target,
skippable); rain streaks; AO (per-target, runs for true-pip SVP); wet surfaces (gbuffer
property); sun cascades (all 3 replayed, correct selection); shadowed point/spot replay (no light
type skipped); tonemap/grade/vignette (exactly once, main exposure); screenshot path (composited
scope included trivially); square-aspect handling (matrix-driven frustum, aspect==1 exact);
mid-frame `Device.fFOV` swap (every SVP-pass consumer wants the scope FOV; main-FOV consumers run
once-per-frame before the passes).
DEGRADED (catalog entries): far-cascade texel density under magnification (IMP-006); light-list
capture (IMP-005); SSS sun contact shadows default-off in scope (IMP-004); water reflections
tunable-flat (documented, `r__svp_skip_ssr`); eye adaptation uses main exposure (IMP-007);
bloom sourced from main-res lens pixels (IMP-009); scope pixels double-LUT/double-DOF (IMP-001,
fixed-gated); capture-time SSA/LOD thresholds are main-angular (IMP-010/011).
ABSENT (catalog): emissive/self-illum in scope (IMP-002, implemented-gated); SVP pass timing
instrumentation (IMP-003, implemented); muzzle flash through glass (Tier 3, prior sessions).
NOT APPLICABLE: second-camera precedents (none exist — the SVP is the engine's first; the two
applicable patterns, 3DSS depth-copy and the shadow-atlas share, are already used); MT offload of
SVP prep (proven not-worth-it: sub-ms producer→consumer chain on the render thread); update-rate
cadence for the scope image (exists as the legacy `frameDelay`, correctly bypassed — TAA/MV
coupling forbids stale frames).
Not reviewed: R1/R2/R3 renderer variants (not shipped by this fork's configs); editor paths.

## 4. PARITY MATRIX
The four quadrant tables with full `file:line` evidence are preserved verbatim in the agent
outputs and synthesized above (§3) — key rows with evidence anchors:
sun cascades `R_sun.cpp:42-79/364-380`; light capture main-only `r4_R_render.cpp:790-798`,
`Light_DB.cpp:200` (fog-distance cull), `light_vis.cpp:38-82` (main OCCQ); emissive main-only
`r4_R_render.cpp:989-1017`; AO gate `phase_combine.cpp:107-129`; SSS gate `r4_R_render.cpp:
920-957` + `r__svp_sss_sun` default 0; SSR/water levels `phase_combine.cpp:339-394`; fog binders
`Blender_Recorder_StandartBinding.cpp:144-211` (see §7 disproof); billboards
`ParticleEffect.cpp:641/645` + basis rebuild `r4_R_render.cpp:29-32`; capture-order spine
`phase_combine.cpp:472/541/563/586/589/655` + `nightvision.cpp:310`; SMAA/async RT oversize
`r4_rendertarget.cpp:1007-1011/1129`; unused SVP smap atlas `:830-834` vs `share_main_smaps`
`r4_R_render.cpp:963`; VRAM inventory table (agent D, ~175-185 MB @1440p).

## 5. IMPROVEMENT CATALOG

### Tier 1 — implemented tonight
- **IMP-001** `engine:` `f986b448` — **once-only DOF/LUT gates** (`r__svp_skip_dof`,
  `r__svp_skip_lut`, both default 0 = current behavior). Defect: `phase_dof()`/`phase_lut()` run
  ungated in the SVP combine (`phase_combine.cpp:586/589`, verified) and again over the composited
  lens → double grade; SVP-internal DOF uses main-view focus distances against the magnified
  scene. Blast radius: two lines gated on `true_pip_on && m_render_pass_is_svp && cvar`; off = 
  byte-identical. **Recommend flipping both to 1 after one visual A/B.**
- **IMP-002** `pip:` `968ff1d2` — **gated emissive replay** (`r__svp_emissive`, default 0).
  Self-illum geometry (signs, lamps, glowing anomalies) was main-only by design → dark through the
  scope at night. Replays `r_dsgraph_render_emissive(false)` into the SVP accumulator before the
  main drain, mirroring the stock block's stencil/cull setup; list-clear semantics preserved for
  both ssfx-bloom states. Off = byte-identical.
- **IMP-003** `pip:` `3b07d6c1` — **[SVP-PERF] cost probe** (gated `r__svp_diag`). No timer
  existed for the SVP pass; now a throttled 1/s log of CPU submit ms + draw-call/vert deltas via
  the existing `RCache.stat` counters. Limitation: CPU-side (GPU timestamp queries = Tier 2).

### Tier 2 — execution-ready blueprints (full sketches in the quadrant reports; condensed)
- **IMP-004 SVP sun contact shadows default**: all machinery exists (`r__svp_sss_sun`); flipping
  the default violates the no-altered-defaults rule tonight → recommend the user flips it (cheap,
  per-target RT, contact shadows are exactly what magnification exaggerates).
- **IMP-005 scope-cone light capture** (the missing-light parity bug): union the capture frustum
  with the SVP cone + relax the fog-distance cull when scoped; fail-open OCCQ for cone-only
  lights. Files: `r__dsgraph_render.cpp` capture, `Light_DB.cpp:200`, call site
  `r4_R_render.cpp:790`. cvar `r__svp_light_capture`. Risk: medium (flicker discipline).
- **IMP-006 tight-frustum far sun cascade for the scope cone**: dedicated small smap sized from
  the SVP frustum corners; substitute only the FAR sub-phase in the replay. Sharp magnified
  shadows. Risk: medium (bias seams).
- **IMP-007 scope-local eye adaptation**: the SVP already measures its own luminance
  (per-target `rt_LUM_pool`) and discards it; bind it as `s_tonemap_svp` and apply local exposure
  in the IMAGE phase (the unused `tonemap()` in `scope_3dss_common.h` is the exact shape). Fixes
  "dark room → bright outdoors = blown out." cvars `r__svp_local_exposure`, `r__svp_exposure_bias`.
- **IMP-008 VRAM slim (~50 MB)**: skip the SVP's never-filled `rt_smap_depth` (+minmax); size
  `rt_smaa_*` and `t_ss_async` to the target's own dims (currently full-screen on a 720² target).
  Needs one runtime confirmation (no pre-share bind; SMAA-on-SVP tolerance).
- **IMP-009 SVP bloom before capture**: run `phase_ssfx_bloom` for the SVP (per-target buffers
  exist; the `:574` gate) so magnified bright sources flare appropriately. cvar `r__svp_bloom`.
- **IMP-010 magnified-cull parity (capture SSA)**: objects below the main view's SSA discard are
  missing from the shared graph → invisible through the scope where they'd subtend N× the pixels.
  Scale the capture-time discard by 1/mag when scoped; keep the main drain's own threshold.
  Risk: medium-high (shared capture path).
- **IMP-011 scope LOD bias**: select finer LODs for scope-cone objects at magnification (the
  `svp_lod`/`svp_ssa` hooks already scale the other way). Companion to IMP-010.
- **IMP-012 lens-masked main TAA/SMAA**: the composited lens is TAA'd a second time by the main
  pass with main-view MVs → ghosting on scope pan (a possible residual smear contributor). Mask
  via the CUSTOM_DEPTH lens depth or scope stencil. cvar `r__svp_lens_skip_main_taa`.
- **IMP-013 svp_dump debug capture + GPU timers**: console command dumping `rt_secondVP$svp` /
  MV/position buffers via the existing `D3DX11SaveTextureToMemory` + staging pattern
  (`r__screenshot.cpp:176/196`); D3D11 timestamp queries upgrade for IMP-003.

### Tier 3 — ideas (one paragraph each in the quadrant outputs)
DOF-over-scope focus-depth parameterization (`scope_depth_value` 1.0f vs `NO_BLUR=100.0`
reconciliation); sun-glare boost through magnification (`r__svp_sun_glare_boost`, eye-comfort
risk); psi/controller warp inside the scope image; magnification-gated near-plane on the
objective-camera path only (z-precision); per-scope LTX homes for the 3.0 weapon-mag gate and
exit-pupil fallback + cvars for the 0.75 hud-fov factor and 14.0 objective base; muzzle flash
through the glass (defer-drain `mapHUDSorted.Sorted`, prior sessions).

## 6. ENGINE & SHADER CHANGE REGISTER
| Change | Files | Why | Backward-compat | Commit |
|---|---|---|---|---|
| once-only DOF/LUT gates | r4_rendertarget_phase_combine.cpp; xrRender_console.cpp | double grade / wrong-camera focal blur on scope pixels | both cvars default 0 → bit-identical; gate reads two existing flags | f986b448 |
| emissive replay | r4_R_render.cpp; xrRender_console.cpp | self-illum absent in scope | default 0 → block unreachable; list semantics untouched when off | 968ff1d2 |
| [SVP-PERF] probe | r4_R_render.cpp | no SVP timing existed | inside `if(svp)`; log gated `r__svp_diag`; measurement-only | 3b07d6c1 |
No shader files changed by this sweep.

## 7. DECISIONS MADE ON YOUR BEHALF
1. **Fog-plane "staleness" finding DISPROVEN during verification** (the survey's top-ranked fix):
  `cl_fog_plane`'s `marker` is **never assigned** (`Blender_Recorder_StandartBinding.cpp:149-169`,
  read in full) — the cache never latches, the plane recomputes from live `mFullTransform` every
  draw and is already per-viewport-correct by accident. No change made; documented so nobody
  "fixes" the latch and breaks the SVP.
2. **`r__svp_sss_sun` default flip rejected tonight** (no-altered-defaults rule) → Tier 2
  recommendation instead.
3. Tier-1 gates default to CURRENT behavior even where the current behavior is arguably wrong
  (double LUT), because the user's tuned look (eyebox dark 0.9 etc.) was calibrated on today's
  pixels — flipping while they sleep would silently change a validated look.
4. The SVP cost probe shipped as a throttled log rather than an on-screen HUD line (the log path
  is verifiable by reading; the HUD render path was not exercised tonight).
5. Commit prefixes: `engine:` where the touched line executes for non-PiP paths (the combine
  gates), `pip:` for additions unreachable without PiP.
6. Same carried-WIP/baseline handling as the audit mission (see PIP_AUDIT_REPORT §6).

## 8. RUNTIME TEST PLAN
- **IMP-001:** ADS a scope with `r__svp_skip_lut 0↔1` (grade shift on scope pixels = the double
  was real); with a DOF-enabled config, `r__svp_skip_dof 0↔1` (interior sharpness). rdc: capture
  the SVP combine, confirm the LUT/DOF draws absent when gated.
- **IMP-002:** night map, glowing sign/anomaly; `r__svp_emissive 0↔1` through a scope — the object
  lights up at 1. rdc: SVP accumulator gains the emissive draws.
- **IMP-003:** `r__svp_diag 1`, scope in grass vs indoors — [SVP-PERF] deltas in the log; sanity:
  calls delta drops with `r__svp_cull_ssa` raised.
- Tier-2 blueprints each carry their own verification steps in the quadrant reports (rdc bindings
  for `s_image`, cascade texel inspection, LUM pool dumps, VRAM before/after for IMP-008).
- The standing in-game queue from prior sessions (barrel fast-pan, grass-in-scope, roll lean test)
  remains in `PIP-FULL-ANALYSIS.md`.

## 9. RESIDUAL RISK LIST
1. IMP-001's gates are OFF — the double-LUT/DOF defect remains live until the user A/Bs and flips.
2. The missing-light gap (IMP-005) is unaddressed in code — most visible at night.
3. The stock-engine `CopyResource(surface_get(), surface_get())` leak class (audit residual #1)
   still exists outside the PiP path.
4. Emissive replay (IMP-002) is code-verified but not eye-verified; first enablement should check
   bloom interaction on ssfx-bloom configs.
5. All Tier-2 items are blueprints, not code — each carries its own risk note.
