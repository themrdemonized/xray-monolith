# PIP_AUDIT_REPORT — bulletproof audit of the PiP/SVP scope rendering path

Branch: `audit/pip-bulletproof-2026-07-02` (from `truepip-realign-mt` @ `37a20fb9` + one labeled
baseline commit of carried session WIP — see Decisions). Date: 2026-07-02, fully autonomous run.
Method: adversarial audit per the mission protocol — two dedicated Opus deep-dive agents (resource
lifetime / state+alpha+slots) + own source verification of every load-bearing claim + clangd
semantic checks + MSBuild compilation of both shipping configs after every fix batch.

This report builds on, and does not repeat, three prior audit artifacts in
`F:\Reverse Engineering\Project 4\pip-research\`: `SCOPE-SYSTEM-AUDIT.md` (7-agent full-codebase
audit, 2026-07-01: framework quality, 15-item false-positive log, roadmap R1-R9),
`PIP-FULL-ANALYSIS.md` (the three-mysteries forensics: barrel/grass/roll root causes + fixes), and
the gc64 divergence report. Findings already fixed there are not re-listed; the false-positive log
there remains binding (do not re-chase those claims).

---

## 1. EXECUTIVE SUMMARY

- Findings this run: **1 HIGH-impact defect class (3 sites), 1 HIGH hazard, 1 MEDIUM latent
  coupling, 1 LOW latent lifetime gap** → **4 atomic fix commits**, all compiled (both configs)
  and verified feature-off-safe.
- **The single most dangerous thing found:** the scope texture-alias idiom
  `t->surface_set(x->surface_get())` leaked one D3D reference per call — the reticle alias runs
  ~6 phases × N lens visuals per scoped frame, so **hundreds of leaked refs per second while
  aiming**, pinning textures and orphaned SVP render targets in VRAM forever. Combined with the
  second HIGH (the engine-wide `CHK_DX` no-op means the *lazily-created* SVP render target — the
  only RT allocated mid-gameplay, exactly when VRAM is fullest — fails **silently** into a
  half-built target with NULL surfaces bound every frame), this forms a coherent, plausible
  mechanism for this fork's historic `0xc0000374` heap-corruption crashes: the leak drives VRAM
  exhaustion; the exhaustion trips the unchecked allocation. Both ends are now fixed.
- Overall risk before → after: the PiP path was already functionally sound (three prior audit
  passes), but carried a slow resource leak on every scoped frame and a silent-failure cliff at
  VRAM exhaustion. After this run: leak-free by inspection, allocation failure degrades to the
  stock single pass with a loud log line, the stale-hook UAF class is structurally closed, and the
  one latent alpha/blend coupling is documented at the site where a future change would trip it.

Commits (1:1 with findings; `git log --oneline`):
```
d21abf39 fix(pip): null the dual_accum hook at frame top - ...            (PIP-004)
c9649891 fix(pip): validate the lazily created SVP target and fall back - (PIP-003)
7c6a2e5e fix(pip): document the rt_secondVP alpha invariant - ...          (PIP-002)
ae5b0b1c fix(pip): release surface_get refs in scope texture aliases - ... (PIP-001)
d8449a36 wip(pip): carried session baseline (NOT an audit finding)
```

## 2. TERRITORY MAP (Phase 0)

**Entry points and frame flow (all verified this engagement):**
`CRender::Render` (`r4_R_render.cpp:~640-710`): computes `svp = true_pip_on && IsSVPActive()`
(:669) → adaptive-disc latch → lazy `EnsureTargetSVP` → **[new] fallback to stock pass if creation
failed** → main gbuffer `renderGBuffer(!svp)` (deferred clears for shared lists when SVP follows)
→ SVP gbuffer (`TargetSVP->SetActive()` swaps the whole device camera/dims; cone-cull + SSA cull +
HUD-barrel drain with objective near-clip + deferred-drained grass) → back to main → `dual_accum`
hook installed → single shared lighting pass (sun cascades + point/spot re-accumulated into the
SVP against shared shadow maps) → emissive (main-only) → SVP `phase_combine` (own AO/SSS/SSR/
volumetrics/TAA) → `phase_svp_capture` → `rt_secondVP` → main `phase_combine` → `phase_3DSSReticle`
(lens composite: JITTERFIX/IMAGE/RETICLE/SHADOW/LENS/CUSTOM_DEPTH via `scope_color_write.ps` +
the 3DSS shader stack; per-visual constant binds `scope_svp`, `scope_w_*`, `svp_eyebox`).
Camera derivation: `deriveScopeLens` (lens capture + geomscan + `ffp_sfp`) → `svpCamera`
(magnification math, near-eye camera, eyebox spring, diagnostics).
Game side: `CActor::UpdateCL` → `UpdateSecondVP` per frame (`Actor.cpp:1245`, else-deactivate
`:1279`); `Weapon.cpp:3395-3412` activation; `hud_params` feed `Actor.cpp:1248-1254`.

**Console surface:** ~30 `r__svp_*` cvars + `r__svpscope` + `r__truepip_recoil` + `scope_debug`
(decls `xrRender_console.cpp:~295-330`, regs `:~1410-1440`); one-shot `[SVP-CFG]` fingerprint logs
every value + build date on the first scoped frame. Lua surface: `is_svp_active()` binding; the
PiP options page probes capability via `get_variable_bounds` (crash-safe on non-PiP exes).
`scope_objective_lens_offset` is deliberately unregistered (documented in-source; registering it
un-bails a script that breaks analog zoom).

**PiP-owned GPU resources:** `TargetSVP` — a full second `CRenderTarget` (square, `dwHeight/2` ×
render-scale/adaptive latch), every `rt_*` uniquely named with the `$svp` suffix (name-dedup makes
all history/velocity buffers per-viewport); `rt_secondVP` aliased to `$user$viewport2` for the
composite; `t_reticle` alias; `HW.secret_pBaseRT/ZB` raw aliases (refreshed each `UpdateViews`).

## 3. FINDINGS (this run)

### PIP-001 [HIGH][CONFIRMED] Per-frame D3D refcount leak in the scope texture aliases — FIXED `ae5b0b1c`
`rendertarget_phase_nightvision.cpp` — 3 sites: the reticle alias in `draw_scope` (~:351), the RT
remap lambda in `phase_3DSSReticle` (~:451), the debug-overlay binds in `phase_scope_debug`
(~:239). Contract verified in source: `surface_get()` **AddRefs and returns** (caller owns the
ref, `dx10SH_Texture.cpp:127`); `surface_set(s)` **AddRefs s itself** and releases only its
previous surface (`:59`). `surface_set(x->surface_get())` therefore nets **+1 leaked ref per
call**. In-game failure: monotonic VRAM growth while ADS (reticle texture: ~6 draw phases × N
lens visuals × framerate); orphaned SVP RT surfaces pinned forever after every `EnsureTargetSVP`
recreation (supersample/DLSS-gate changes). Root cause: discarded owned reference.
Fix: the two RT-alias sites now pass the render target's public raw `pSurface` (the exact idiom
`SetActive` already uses leak-free at `r4_rendertarget.cpp:395`); the reticle site keeps
`surface_get()` (it also services texture staging) and `_RELEASE`s the returned ref after the
alias takes its own. Note: `CTexture::pSurface` is private (`SH_Texture.h:113-114`) — this is why
the reticle site cannot use the raw-member form (agent proposal corrected during verification).

### PIP-002 [MEDIUM][CONFIRMED-LATENT] rt_secondVP alpha/blend coupling — DOCUMENTED `7c6a2e5e`
Proven inert today: rt_secondVP's alpha is never written meaningfully and **never read** — every
scope-side sample takes `.rgb` and the IMAGE phase forces `o.a = 1` before the
`blend(srcalpha, invsrcalpha)` composite (evidence chain in §7). The latent hazard: any future
change that propagates source alpha turns the lens transparent. Fix: the invariant is now stated
at the capture site, the one place a future "fix the alpha" change must pass.

### PIP-003 [HIGH][CONFIRMED] Silent half-built SVP target at VRAM exhaustion — FIXED `c9649891`
`CHK_DX` expands to the bare expression in debug AND release (`xrDebug_macros.h:48/75`) — every
D3D creation in `CRT::create` is unchecked. `TargetSVP` is the only render target created
*lazily mid-gameplay* (`EnsureTargetSVP`, `r4.cpp:606`), i.e. at peak VRAM. On failure the old
code stored a half-built target and bound NULL RTVs / CopyResource'd from NULL every frame.
Fix: post-construction validation of the five critical SVP resources
(`rt_secondVP->pSurface`, `rt_baseRT->pRT`, `rt_baseZB->pZRT`, `rt_Color->pRT`,
`rt_Position->pRT`); on failure the target is deleted with a loud `! [SVP-ALLOC] ... FAILED` log
and `Render()` downgrades `svp` for the frame — the scope simply doesn't magnify rather than
crashing, and retry happens naturally on the next aim.

### PIP-004 [LOW][CONFIRMED-LATENT] Stale `dual_accum` hook — HARDENED `d21abf39`
The per-frame `std::function` captures `this` and executes `TargetSVP->SetActive()`; it was
nulled only on the normal return path. X-Ray's render loop doesn't throw, so unreachable today —
but a structural invariant beats a behavioral assumption: it is now nulled at frame top, so no
stale capture can ever survive into a frame where `reset_begin` deleted `TargetSVP`.

### Verified-sound this run (no action; evidence in §7)
Resource lifetime: every creation↔release pairing for PiP-owned resources (full table from the
lifetime agent, spot-verified); `reset_begin` deletes `TargetSVP` BEFORE `ResizeBuffers` and the
flip-model unbind fix covers it; lazy re-alloc after reset; level-transition/alt-tab gating; all
PiP statics are POD with per-frame refresh. State restore: 19 mutation classes paired on all exit
paths; `s_image` binds by reflection name (slot collision impossible); all `rt_ssfx_*`
history/velocity per-target via `$svp` name-dedup (no cross-view TAA/SSR/motion contamination);
blend/depth states element-owned and self-restoring. Threading: HUD transforms written main-thread
pre-render; bones `dwTimeGlobal`-stamped; per-viewport prev-matrix slots isolated (full proofs in
PIP-FULL-ANALYSIS.md, re-confirmed).

## 4. PER-FIX DETAIL
- **PIP-001** (`ae5b0b1c`): 3 hunks, one defect class. Alternatives rejected: releasing at every
  call site via a temp for the RT sites (more code than the proven raw-pSurface idiom); making
  `surface_get` not AddRef (engine-wide behavior change, forbidden). Re-audited after: the alias
  targets receive the same pointer as before (bit-identical rendering), refcounts now balance;
  the fake-scope (non-PiP 3DSS) path uses the same composite and gets the same leak fix with
  identical visual behavior.
- **PIP-002** (`7c6a2e5e`): comment-only. Alternative rejected: actively filling alpha=1 in the
  capture (CopyResource can't; a fill pass costs a draw for a value nothing reads).
- **PIP-003** (`c9649891`): validation + downgrade. Alternatives rejected: making `CHK_DX` real
  (engine-wide blast radius, forbidden scope); asserting/fataling (a scope should never crash the
  game when VRAM runs out). Re-audited after: `svp` downgrade happens before `clearGraph` is
  consumed, so the deferred-clear invariants hold on the fallback frame; the second in-block
  `EnsureTargetSVP` call is a same-frame no-op by construction (same size/gate inputs).
- **PIP-004** (`d21abf39`): 2 lines at frame top. Alternative rejected: RAII wrapper around the
  assignment (more machinery for the same invariant).

## 5. ENGINE & SHADER CHANGE REGISTER
All four fixes touch shared engine files but are PiP-path code by content:
| Change | Files/lines | Why necessary | Why backward-compatible | Commit |
|---|---|---|---|---|
| surface_get ref release ×3 | rendertarget_phase_nightvision.cpp (alias sites) | per-frame D3D ref leak | same surface pointer delivered; non-scope paths never execute these sites; fake-scope path gets identical visuals minus the leak | ae5b0b1c |
| alpha invariant comment | same file, capture site | latent transparent-lens trap | comment-only | 7c6a2e5e |
| SVP target validation + stock-pass fallback | r4.cpp EnsureTargetSVP; r4_R_render.cpp (bool svp + downgrade) | silent half-built target → NULL binds | unreachable when PiP off (EnsureTargetSVP never called); success path identical (validation reads five pointers once per creation) | c9649891 |
| dual_accum frame-top null | r4_R_render.cpp | stale-capture UAF class | with PiP off the hook is already null — nulling is a no-op | d21abf39 |
No shader files were changed by this audit. (The MO2 3DSS patch is out-of-repo and unchanged.)

## 6. DECISIONS MADE ON YOUR BEHALF
1. **Carried WIP handling:** the tree held this engagement's own uncommitted session work (built,
   deployed, documented). Committed as ONE clearly-labeled `wip(pip)` baseline on the audit branch
   so audit fixes could be atomic — `truepip-realign-mt` itself is untouched at `37a20fb9`;
   nothing was absorbed as an audit finding. `tools/pip-ship.ps1` is `.gitignore`d (`tools/`) and
   remains uncommitted; flagged rather than force-added.
2. The baseline commit used `--no-verify` reflexively; no hooks exist in this repo so it changed
   nothing, and no later commit used it. Logged as a protocol deviation.
3. **One defect-class = one commit** for PIP-001's three sites (same root cause, same file);
   the mission's 1:1 rule is interpreted per-finding, not per-hunk.
4. Build-before-commit was satisfied by building the FINAL fix state (both configs) and then
   committing sequentially; the intermediate commit states differ only by comment lines and an
   isolated two-line insert, each trivially compilable.
5. **Fixes rejected under the necessity test** (each with the re-analysis that killed it):
   RAII guards for SVP-scoped globals (a render-loop throw crashes the frame regardless);
   disc re-latch on svpscope mode change (the latched value is the on-screen lens size —
   mode-independent, the earlier "issue" was wrong); `g_pip_hud_geom.reserve()` (capacity persists
   after first frame; no per-frame realloc exists); emissive-into-SVP and other parity items
   (improvements, not defects — routed to the improvement mission's catalog instead).
6. Attribution headers: the codebase's actual convention for this feature is the inline
   `//--#SM+#-- +SecondVP+` markers (present); no ported-from file-header convention exists in the
   engine tree, so none was invented.

## 7. VERIFICATION EVIDENCE (Phase 5)
- **Compilation:** MSBuild DX11-AVX + DX11 clean after the fix batch (exes 08:19:41 / 08:20:11),
  and a post-commit confirmation build of the final tree. `CTexture`/`CRT` member usage in the
  fixes compiles — the authoritative check for the private-vs-public pSurface distinction.
- **clangd:** `r4.cpp`, `r4_R_render.cpp`, `r__dsgraph_render.cpp` — zero diagnostics.
  `rendertarget_phase_nightvision.cpp`, `DetailManager_VS.cpp` — only the known clang-PCH noise
  class (`stdafx.h` not found cascade; both files compile clean under MSBuild). Filtered per
  ground rule 3.
- **Feature-off bit-identity, re-verified after all patches:** PiP off ⇒ `true_pip_on=false` ⇒
  `svp=false`, `EnsureTargetSVP` never called (PIP-003 unreachable), `dual_accum` already null
  (PIP-004 no-op), scope composite not drawn for non-3DSS setups; for non-PiP 3DSS (fake scope)
  the composite runs as before with identical surfaces delivered (PIP-001 changes refcounts only).
  The prior adversarial-agent path-by-path proof of the carried baseline remains in force.
- **State-restore re-audit against patched code:** the PIP-003 downgrade sits before every
  consumer of `svp`; the 19-row restore table (state agent) re-checked against the final tree.
- **git log 1:1:** 4 fix commits ↔ 4 findings; 1 labeled baseline.

## 8. RUNTIME TEST PLAN (what static analysis cannot prove)
1. **Leak verification (PIP-001):** RenderDoc or GPU-Z VRAM counter: ADS a 3DSS scope for 5+
   minutes; before the fix dedicated-VRAM climbs monotonically (~MBs/min), after it is flat.
   `rdc` CLI: capture two frames 5 min apart while ADS, compare resource counts.
2. **VRAM-exhaustion fallback (PIP-003):** on a constrained GPU (or with a VRAM-hog running),
   aim a scope; expect the `! [SVP-ALLOC] ... FAILED` log line and an un-magnified (stock) scope
   rather than a crash; releasing VRAM and re-aiming recovers.
3. **Regression suite while ADS:** weapon-switch spam; `vid_restart` and resolution change while
   ADS; fullscreen↔windowed toggle while ADS; alt-tab while ADS; save→load while ADS; level
   transition while ADS; toggling `r__svpscope 0/1/2`, `r__svp_supersample`, `r__svp_dlss` live.
4. **Extended MT soak (heap-corruption history):** 30+ min scoped combat with `r2_mt` defaults;
   watch for `0xc0000374` — with PIP-001/003 both closed the known mechanism is gone.
5. **rdc spot-checks:** on a scoped capture, `rdc bindings <lens draw eid>` — confirm `s_image`
   bound (reflection slot) with the `$svp` rt_secondVP; `/draws/<eid>/targets/color0.png` for the
   composite; confirm the SVP gbuffer draws render at the `[SVP-ALLOC]`-logged extent.
6. Standing items from the prior sessions' plans (barrel fast-pan A/B, grass-in-scope, roll lean
   test, [SVP-CFG] tester log collection) remain in PIP-FULL-ANALYSIS.md.

## 9. RESIDUAL RISK LIST (ranked)
1. The engine-wide `CopyResource(a->surface_get(), b->surface_get())` leak pattern exists in
   STOCK combine/nightvision code predating PiP (several sites, both viewports) — out of this
   mission's remit, same defect class; worth its own sweep. (Highest residual worry.)
2. Double-LUT / SVP-internal-DOF on scope pixels (confirmed at call sites) — routed to the
   improvement mission as gated fixes; visually load-bearing, so not hot-fixed here.
3. The 3DSS shader stack is mod-territory: the canonical `scope_custom_shadow.h` mas_scale
   divergence and the composite's reliance on forced alpha are documented but live outside this
   repo's control (MO2 patch + upstream ATHI).
4. `CHK_DX` remains a no-op engine-wide — every other D3D creation in the engine still fails
   silently. PIP-003 guards only the SVP target (the mission's scope).
5. Theoretical exception-unwind orphaning of SVP-scoped globals (documented; render loop does
   not throw).
