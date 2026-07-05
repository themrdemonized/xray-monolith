# IMP-006 — Scope-Fitted Far Sun Cascade: Implementation Guide

**Status: scouted + source-verified, NOT implemented.**
Scouting: 6-agent parallel sweep (2026-07-05) + first-hand verification of every load-bearing
claim. All line numbers checked against `truepip-exp-mt` @ `caeb3740`.

---

## 1. The problem

The SVP scope reuses the main view's three sun shadow cascades (sizes **20 / 40 / 160 m**,
`ps_ssfx_shadow_cascades`, R_sun.cpp:42-65). Two consequences at magnification:

1. **Texel starvation.** The far cascade spends `smapsize²` texels on a 160 m box around the
   camera sized for a naked-eye FOV. A 4× scope looking at a target 150 m out magnifies those
   texels 4×; shadows in the scope image go blocky/swimmy exactly where the player is looking.
2. **Range cutoff.** Beyond ~160 m there is no sun shadow at all. The naked eye barely notices;
   a 10× scope stares straight at it.

Fix shape: render **one extra sun shadow map fitted to the SVP camera's view cone** and use it
for the **far-cascade accumulate of the SVP viewport only**. Main viewport untouched.

---

## 2. Verified architecture (how sun shadows flow today)

### 2.1 The cascade loop is sequential, single-threaded
`render_sun_cascades()` (R_sun.cpp:67-79) is a plain `for` loop calling
`render_sun_cascade(i)` on the render thread. Call chain:
`CRender::Render` → `renderSceneLighting` (r4_R_render.cpp:790) → `render_sun_cascades`
(r4_R_render.cpp:1139). **`sun_cascades_task` (r4.h:204) is declared but never used — dead
code, not a hazard.** No parallelism to coordinate with.

### 2.2 One smap surface, reused per cascade
Each cascade: fit ortho (R_sun.cpp:92-283) → traverse+capture its own `cascade.GMCascade`
(:290-291) → `Target->phase_smap_direct(fuckingsun, SE_SUN_FAR)` + render graph (:307-326) →
accumulate (:351-359). The next cascade **overwrites the same smap**. The atlas only ever holds
the current cascade's map at accumulate time.

### 2.3 The SVP replay (dual_accum)
r4_R_render.cpp:776-785. For each shadow unit the hook runs:
```
TargetSVP->SetActive();     // SVP gbuffer/accumulator/matrices — Device.matrices[1] loaded
share_main_smaps();         // re-point "$user$smap_depth" texture at the MAIN surface
accum();                    // the same accumulate lambda, now on the SVP target
TargetMain->SetActive();    // restore
```
Inside `render_sun_cascade` the far-cascade replay is R_sun.cpp:364-379 (`svp_accum` lambda,
FAR branch :375-377).

### 2.4 The three keys that make the substitution clean

**KEY 1 — the sampler matrix is `X.D.combine`, not the `xform` parameter.**
`accum_direct_cascade(sub_phase, xform, xform_prev, fBias)`
(r4_rendertarget_accum_direct.cpp:344) builds the shadow-lookup matrix as
```
xf_project.mul(m_TexelAdjust, fuckingsun->X.D.combine);   // :497
m_shadow.mul(xf_project, Device.mInvView);                 // :498  ← SVP mInvView during replay
```
`X.D.combine` is written during the smap render (R_sun.cpp:298: `= cull_xform`). For
**SE_SUN_FAR the `xform` parameter is completely unused** — the band cuboid is built from
`inv(xform_prev)` (:567-571, the middle cascade's box). So: *whatever `X.D.combine` holds when
the accumulate runs is what the shader samples with.* Substituting the scope map = set
`X.D.combine = scope_xform` for the replay, restore after.

**KEY 2 — the SVP target already owns an unused smap.**
`createUnique` (r4_rendertarget.cpp:552-570) allocates **every** RT per target with a `$svp`
name suffix; the SVP target has `rt_smap_depth` ("$user$smap_depth$svp", same size/format as
main) that is **never rendered to and never read** today — `share_main_smaps` exists precisely
to bypass it. `phase_smap_direct` (r4_rendertarget_phase_smap_D.cpp:7,30) binds **its own
member** `rt_smap_depth` — so `TargetSVP->phase_smap_direct(...)` renders into the SVP's smap
with zero new allocation. **The main atlas is never written → every "atlas overwrite" hazard
from the original plan evaporates** (volumetric, sunshafts, later spot-light smaps: all
unaffected).

**KEY 3 — texture re-pointing is symmetric and cheap.**
`SetActive` re-applies the active target's `RenderTargetRemaps` (surface_set + explicit
`RCache.Invalidate`, r4_rendertarget.cpp:364-368, 391-395). `share_main_smaps`
(r4_R_render.cpp:1122-1129) is a 2-entry surface_set + Invalidate. The mirror helper
(`point_smaps_at_svp()`) is the same loop over `TargetSVP->RenderTargetRemaps`. After the
replay, the hook's `TargetMain->SetActive()` restores main remaps automatically.

### 2.5 Volumetric sun — self-consistent, not a blocker
`accum_direct_volumetric` runs **inside** `accum_direct_cascade` (:732-733), gated
`o.advancedpp && ps_sunshafts_mode && sub_phase == SE_SUN_FAR`, and receives the composed
`m_shadow`. During the substituted SVP replay it samples the scope map with the scope matrix —
consistent by construction. (A scout claimed volumetric runs later inside `render_lights`
reading the far atlas; **verified false** — the call sites are :340/:733, both inside the
direct-sun accumulates.)

### 2.6 The stencil band logic survives substitution
Near/middle replays already ran normally on the SVP target and zeroed the SVP stencil in their
bands (st_pass=ZERO, mask 0xFE; FAR uses st_mask=0, KEEP — :652-659). The far pass draws the
middle cascade's cuboid from `inv(xform_prev)` under the active (SVP) view/proj
(:541-542 read `Device.mView/mProject` = SVP during replay). Passing the **same `xform_prev`**
keeps band selection identical; only the sampled map changes.

---

## 3. Implementation plan

New cvars (`xrRender_console.cpp/.h`, registered CMD4):
- `ps_r__svp_sun` int 0/1, **default 1** — master gate.
- `ps_r__svp_sun_range` float, default **400**, range [160, 1000] — scope shadow reach (m).

All code gated `svpscope ≥ 1` via the existing `Device.true_pip_on && IsSVPActive()` frame
condition (the hook only exists when svp is true anyway) — zero effect at `svpscope 0`.

### Step A — mirror helper (r4_R_render.cpp, beside share_main_smaps)
```cpp
void CRender::point_smaps_at_svp()   // decl in r4.h next to share_main_smaps
{
    for (auto& r : TargetSVP->RenderTargetRemaps)
        if (r.second == TargetSVP->rt_smap_depth ||
            (TargetSVP->rt_smap_depth_minmax && r.second == TargetSVP->rt_smap_depth_minmax))
            r.first->surface_set(r.second->pSurface);
    RCache.Invalidate();
}
```
(minmax is irrelevant for FAR — element switch is NEAR-only, accum_direct.cpp:356-357 — but
re-pointing both keeps the state model simple.)

### Step B — scope-cascade fit (new function, R_sun.cpp or a small new file)
Do **not** reuse the main fit block: it chains rays across cascades via a **`static`
`light_cuboid`** (R_sun.cpp:134) and `compute_caster_model_fixed` — main-view semantics and
shared static state. The scope fit is a direct AABB fit, ~50 lines:

1. **Receiver volume**: 8 corners of the SVP frustum, truncated at
   `R = min(ps_r__svp_sun_range, CurrentEnv->far_plane)`. Build from
   `Device.matrices[1]` (mView/mProject): invert `mProject*mView`, transform the ±1 NDC cube,
   re-scale the 4 far corners along near→far rays so depth = R.
2. **Sun basis**: exactly the main code's L_dir/L_up/L_right + `build_camera_dir`
   (R_sun.cpp:117-124).
3. **Ortho box**: transform the 8 corners into light view space, take the XY AABB;
   `D3DXMatrixOrthoOffCenterLH(minX,maxX,minY,maxY, 0.1, dist+extent)` with the same
   `dist = light_top_plane.classify(camera)` caster-headroom pattern as :166-172 (this is what
   pulls casters *between the sun and the receiver box* into the map).
4. **Texel snap**: quantize the light-view-space AABB min-corner to
   `texel_world = max_extent / o.smapsize` granules before building the ortho. The box is
   aim-driven — without snapping, shadows crawl at high mag. (Same purpose as the main block
   :237-271; a min-corner quantize is sufficient because the extent is also quantized.)
5. **Extent floor**: clamp XY extent ≥ ~40 m. At low mag the fitted box approaches the main far
   box (no win, no harm); at 10× on a 400 m cone the footprint is ~50-120 m vs 160 m plus
   properly centered on the aim point — a 2-4× density win *plus* 400 m reach vs 160.
6. **Cull frustum**: `CFrustum::CreateFromMatrix(scope_cull_xform, FRUSTUM_P_LRTB|FRUSTUM_P_FAR)`
   (omit NEAR so casters toward the sun aren't clipped) + `cull_COP` mirroring :112.
7. **Bias**: `scope_bias = max_extent * -0.0000025f` (the init_cascades formula, :47-57).

### Step C — the seam (R_sun.cpp:364-379, the svp_accum lambda)
```cpp
auto svp_accum = [&]
{
    Target->phase_accumulator();
    if (cascade_ind == 0)
        Target->accum_direct_cascade(SE_SUN_NEAR, cascade.xform, cascade.xform, cascade.bias);
    else if (cascade_ind < m_sun_cascades.size() - 1)
        Target->accum_direct_cascade(SE_SUN_MIDDLE, cascade.xform,
                                     m_sun_cascades[cascade_ind - 1].xform, cascade.bias);
    else if (!svp_far_scope_accum(cascade_ind))          // NEW: scope-fitted far, else fall back
        Target->accum_direct_cascade(SE_SUN_FAR, cascade.xform,
                                     m_sun_cascades[cascade_ind - 1].xform, cascade.bias);
};
```
`svp_far_scope_accum` (returns false → stock fallback if gate off / fit degenerate / sunfilter):
```cpp
bool CRender::svp_far_scope_accum(u32 cascade_ind)
{
    if (!ps_r__svp_sun || o.sunfilter) return false;        // sunfilter path unverified: fall back
    Fmatrix scope_xform; CFrustum scope_frustum; Fvector scope_cop; float scope_bias, extent;
    if (!svp_fit_sun_cascade(scope_xform, scope_frustum, scope_cop, scope_bias, extent))
        return false;

    light* sun = (light*)Lights.sun_adapted._get();
    Fmatrix saved_combine = sun->X.D.combine;

    // render the scope smap into the SVP's own rt_smap_depth (TargetSVP is active here)
    phase = PHASE_SMAP;
    m_svp_sun_graph.clear();                                  // fresh CDSGraphManager member
    m_svp_sun_graph.traverse(pOutdoorSector, scope_frustum, scope_cop, scope_xform);
    m_svp_sun_graph.r_dsgraph_capture(false, true);
    Target->phase_smap_direct(sun, SE_SUN_FAR);               // binds+clears TargetSVP smap
    sun->X.D.combine = scope_xform;
    RCache.set_xform_world(Fidentity);
    RCache.set_xform_view(Fidentity);
    RCache.set_xform_project(scope_xform);
    m_svp_sun_graph.r_dsgraph_render_graph(0);
    // (skip translucent-shadow + grass: far cascade skips grass already, tsh optional later)

    point_smaps_at_svp();                                     // s_smap → the scope map
    Target->phase_accumulator();
    Target->accum_direct_cascade(SE_SUN_FAR, scope_xform,     // xform unused for FAR (KEY 1)
                                 m_sun_cascades[cascade_ind - 1].xform, scope_bias);

    sun->X.D.combine = saved_combine;                         // main state back
    // hook epilogue TargetMain->SetActive() restores main remaps + matrices
    return true;
}
```
Context guarantees at the call: the hook already did `TargetSVP->SetActive()` (SVP matrices,
Device.dwWidth/Height = SVP dims — correct for the accumulate quad) **and** `share_main_smaps()`
(harmless; overridden by `point_smaps_at_svp`). The smap render is view-independent (identity
view + ortho proj). Restore of xforms after the replay: the existing tail R_sun.cpp:382-386
runs after the hook returns, same as stock.

### Step D — the graph manager member
`CDSGraphManager m_svp_sun_graph;` on CRender (r4.h, beside `m_sun_cascades`), constructed with
the same flags as `sun::cascade::GMCascade` (r_sun_cascades.h:38). Call `.clear()` before each
traverse (scout-verified safe/idempotent, r__dsgraph_manager.h:129-146). Do **not** reuse
`cascade.GMCascade` — its graph was consumed by the main render and clearing it would be
order-fragile.

---

## 4. Hazard register (scout claims, adjudicated)

| # | Hazard | Verdict | Disposition |
|---|--------|---------|-------------|
| 1 | Overwriting the main atlas breaks volumetric/sunshafts/rain | **MOOT** | Design never writes the main atlas (KEY 2). Volumetric claim was also factually wrong (§2.5). |
| 2 | `sun_cascades_task` MT races | **FALSE** | Dead code; loop is sequential (§2.1). |
| 3 | `static light_cuboid` corruption | **REAL, avoided** | Scope fit doesn't touch it (Step B). |
| 4 | Wrong xform → misaligned sampling | **REAL, solved** | `X.D.combine` swap + restore is the whole contract (KEY 1). The `xform` arg is dead for FAR. |
| 5 | minmax SM staleness | **MOOT for FAR** | minmax element is NEAR-only (accum_direct.cpp:356-357). Never regenerate it here. |
| 6 | Band logic breaks with smaller footprint | **FALSE** | Band = stencil (already written by near/middle SVP replays) + `inv(xform_prev)` cuboid; we pass the same `xform_prev`. Out-of-box samples hit the smap border = **lit** (border-white sampler), i.e. graceful falloff outside the scope cone — which is off-screen in the SVP anyway. |
| 7 | `phase_accumulator` binds wrong target | **FALSE** | It binds the active target's member RTs; TargetSVP is active (scout-6 §4). |
| 8 | Shimmer from aim-driven fit | **REAL** | Texel snap (Step B4). Accept residual crawl during fast pans; it settles at rest. |
| 9 | `rmNormal()` viewport under SVP dims | **VERIFY at build** | `phase_smap_direct` calls `u_setrt` (RT-sized viewport) then `rmNormal()`. Stock main-path already renders smaps at smapsize ≠ screen dims, so rmNormal must track the bound RT — confirm once in-engine with `[SVP-SUN]` probe (smap VP = smapsize²). |
| 10 | VRAM | **NOTE** | Uses the *already-allocated* SVP smap — zero new VRAM. This retires IMP-008's "free the SVP smap" idea; can't have both. |
| 11 | Perf | **NOTE** | One extra smap render per frame while aiming, cone-culled (small caster set). Wire the existing `[SVP-PERF]` pattern: traverse+render ms + draw calls, throttled, under `r__svp_diag`. |

---

## 5. Verification plan

1. **Build gate** then `[SVP-SUN]` probe (gated `r__svp_diag`): extent, corner count, caster
   draw-calls, fit-degenerate fallbacks per second.
2. **svpscope 0**: zero change (code unreachable — no hook).
3. **`r__svp_sun 0`**: byte-identical to today's replay (fallback path).
4. **Visual A/B** (day map, 4-10× scope, target at 100-300 m): toggle `r__svp_sun` — shadow
   edges in-scope should sharpen visibly; at 300+ m shadows exist only at 1.
5. **Main-view regression**: shadows outside the scope unchanged while aiming (main atlas
   untouched); volumetric shafts unchanged.
6. **Crawl test**: slow pan at 10× — shadow edges must not swim (snap working); check rest
   stability.
7. **Sun-angle sweep**: dawn/dusk (long shadows, low sun) — the `dist+extent` Z headroom must
   catch tall off-cone casters; missing-shadow streaks here mean the caster margin needs the
   full main-style `tweak_COP_initial_offs` treatment.
8. **RenderDoc**: one capture; confirm the scope smap render sits inside the far-cascade SVP
   replay and samples `$user$smap_depth$svp`.

## 6. Effort + rollout

- **Size**: ~150-200 new lines (fit ~60, seam ~50, helper ~10, cvars/probe ~30), 4 files
  (R_sun.cpp, r4_R_render.cpp, r4.h, xrRender_console.cpp/.h). No shader changes, no Compat
  Patch changes, no allocation changes.
- **Order**: helper → fit (probe-only, log extents while aiming, no render) → seam → visual A/B.
  The fit can be validated by logs *before* any pixel changes.
- **Risk posture**: every failure mode has a fallback to the stock replay inside the same frame;
  `r__svp_sun 0` is a total kill switch. Ship default **1** after the sun-angle sweep passes,
  else default 0 for one RC.
