#include	"stdafx.h"
#pragma		hdrstop

#include	"svp_console.h"

#include	"../../xrEngine/xr_ioconsole.h"
#include	"../../xrEngine/xr_ioc_cmd.h"

int scope_svp_enabled = 0; // true PiP second viewport scope (0 off, 1 eyepiece, 2 objective)
float ps_r__svp_render_scale = 1.0f; // SVP render scale, 1.0 keeps the dwHeight/2 per side
float ps_r__svp_supersample = 1.0f; // SVP supersample: render the SVP square larger so the eyepiece downsamples it (SSAA), 1.0 = off, 2.0 = 4x SVP pixels
int ps_r__svp_diag = 0; // SVP perf diagnostics: throttled log of [SVP-RES] over-render ratio + [SVP-ALLOC] target size while scoped, 0 = off
int ps_r__svp_cop_diag = 0; // svp optics diagnostics, 1 = throttled [SVPCOP]/[SVP-HUD]/[SVP-BARREL], 2 = adds the per-frame [SVP-AIM] reticle-vs-screen-center delta
int ps_r__svp_report = 0; // svp one-shot report, 1 re-dumps [SVP-CFG] + [SVP-FILES] on the next scoped frame then clears itself
int ps_r__svp_stats = 0; // per-viewport render stats overlay, 0 off, 1 compact table, 2 adds the per-section breakdown
u32 svp_stats_ssa_culled = 0; // svp small-object cull tally the overlay reads, incremented in r__dsgraph_render while the overlay is on
u32 svp_stats_cull_reject = 0; // svp off-cone frustum-reject tally the overlay reads, incremented in r__dsgraph_render
u32 svp_stats_cull_reject_ident = 0; // svp identity-matrix sorted world statics the cone rejects, incremented in svp_cull_reject
u32 svp_stats_lights_mirrored = 0; // svp lights the cone cull mirrors into the scope, incremented in lights_render
u32 svp_stats_lights_skipped = 0; // svp lights the cone cull drops, incremented in lights_render
u32 svp_stats_taa_stamp = 0; // svp taa sovereignty stamp fires the overlay reads, incremented in phase_ssfx_taa
u32 svp_stats_nvg_split = 0; // svp nvg tube split fires the overlay reads, incremented in phase_combine
u32 svp_stats_lod_scale = 0; // svp lod scale armed frames the overlay reads, incremented in svp_set_lod_scale
u32 svp_stats_hud_cull_reject = 0; // svp hud drain cone rejects the overlay reads, incremented in svp_hud_latch
u32 svp_stats_grass_cull_reject = 0; // svp grass cone rejects the overlay reads, incremented in the detail manager
u32 svp_stats_reflex_proxy = 0; // svp reflex proxy draws the overlay reads, incremented in draw_reflex_proxy
u32 svp_stats_distort_guard = 0; // svp distort guard stamps the overlay reads, incremented in phase_combine
u32 svp_stats_nvg_sky = 0; // svp nvg sky lum remaps the overlay reads, incremented in phase_combine
u32 svp_stats_disc_latch = 0; // svp adaptive-res disc latch moves the overlay reads, incremented in phase_3DSSReticle
u32 svp_stats_fwd_keep = 0; // svp forward-list keeps the overlay reads, incremented in render_forward
u32 svp_stats_optic_resolve = 0; // svp optics bus resolve fires the overlay reads, incremented in svp_optics_resolve
// session-lifetime ever-fired ledger latches, one per instrumented gated path, never per-frame
// reset, the [SVP-LEDGER] sweep flags any still zero while its gate cvar is on
u32 svp_ledger_cull_reject = 0;
u32 svp_ledger_ssa_culled = 0;
u32 svp_ledger_cull_reject_ident = 0;
u32 svp_ledger_lights_mirrored = 0;
u32 svp_ledger_lights_skipped = 0;
u32 svp_ledger_lod_scale = 0;
u32 svp_ledger_hud_cull_reject = 0;
u32 svp_ledger_grass_cull_reject = 0;
u32 svp_ledger_reflex_proxy = 0;
u32 svp_ledger_distort_guard = 0;
u32 svp_ledger_nvg_sky = 0;
u32 svp_ledger_disc_latch = 0;
u32 svp_ledger_fwd_keep = 0;
int ps_r__3db_debug = 0; // 3db ballistics overlay, 1 = bone markers + fire axis + crosshair ray + sight line + mrad log, 2 = adds the zeroed ray, 3 = adds fading shot tracers
float ps_r__svp_adaptive_res = 1.2f; // adaptive SVP resolution: size the SVP render to the on-screen eyepiece disc * this margin. 0 = off (full svp_height), 1.0 = render exactly at the disc (sharpest, mild pan shimmer), 1.2 = keep ~1.2x SSAA. big oculars clamp to no-op
float ps_r__svp_lod = 0.0f; // SVP LOD reduction strength [0..1]: scale the scope's LOD selection to its true pixel coverage (coarser at low mag, capped at the main view so zoomed detail is never worse). 0 = off
float ps_r__svp_cull_ssa = 4.0f; // SVP small-object cull strength: skip scope geometry below this * the LOD-out ssa, scaled by magnification (tiny distant clutter at low mag). 0 = off, higher = more aggressive
int ps_r__svp_dlss = 0; // SVP DLSS-SR master gate, 0 = stock (render_scale inert), nonzero = scaffolding active
// svpscope 2 geometric objective (single-lens scopes have no distinct front lens to capture, derive it
// along the optical axis from the eyepiece), lengths are in eyepiece radii (the only mesh-scale-robust
// unit), refined per scope from real objective_mm later
float ps_r__svp_obj_dist = 1.0f;     // svpscope 2 objective: scale on the AUTO geomscan front distance (1.0 = raw auto, fixed 14r fallback when geomscan finds nothing)
float ps_r__svp_obj_size = 1.0f;     // svpscope 2 objective radius = eyepiece_radius * this (authored-set median, mod_system_3dss_objective_lenses n=67, 0.65 = the old tuned value)
int ps_r__svp_focal_derive = 1;      // svpscope 2 focal anchors derive from eye relief + tube length (0 = the original 0.4/0.6 split)
int ps_r__svp_glare_model = 1;       // veiling glare falloff: 1 = Stiles-Holladay 1/theta^2 vs the scope's half fov, 0 = legacy pow6 cone
int ps_r__svp_photo_model = 1;       // eye photometrics: 1 = Moon-Spencer pupil + squared relative brightness, 0 = legacy linear models
int ps_r__svp_adaptive_grow = 0;     // 1 sizes the SVP up to the disc past the half-height base, costs up to 4x the pixels on large oculars (0 = shrink-only, stock cost)
int ps_r__svp_drain_anchor = 1;      // the drain clip-on window widens to the authored front plane (0 = the fixed 1.4-2.6 window)
int ps_r__svp_settle_derive = 1;     // eyebox settle + recoil windows derive from the weapon's authored timing (0 = flat 400/250/1000ms)
int ps_r__svp_ratio_derive = 1;      // ratio latch band derives from the measured noise, never looser than the fixed band (0 = fixed 25%/2%)
int ps_r__svp_lens_reject = 1;       // while aimed, reject an eyepiece pose that jumps far (a stale or exo-arm pose) and hold the last good one (0 = take every pose)
int ps_r__svp_recoil_hold = 1;       // the scope optics derivation (fov reference + fit band) holds through the post-shot window (0 = live)
int ps_r__svp_roll_stabilize = 0; // svp level the scope world on lean/cant (0 = realistic image tilts with the cant, default; 1 = leveled)
int ps_r__svp_clean_optics = 1; // svp strip the 3DSS fake cosmetics (parallax shadow, chromatism, nvg blur, fisheye) for a clean scope (1 = stripped, default; 0 = full 3DSS look)
int ps_r__svp_distort_guard = 1; // svp stamp the distort mask neutral over the composited lens so the combine warp is identity there (0 = let the lens warp with the main view)
int ps_r__svp_jitterfix = 1; // svp lens edge jitter pre-pass, likely superseded by the sentinel jitter, 0 skips it for the a/b
int ps_r__svp_taa_mask = 1; // svp stamp the taa mask over the composited lens so the main resolve keeps the svp viewport taa there (0 = let the main taa re-resolve the lens)
int ps_r__svp_hud_fov_match = 2; // svp barrel mapping: 2 magnifies with the wheel, 1 fixed 1:1 window, 0 legacy
int ps_r__svp_bloom = 1; // svp bloom on the scope pass so magnified bright sources flare (0 = off)
int ps_r__svp_local_exposure = 1; // svp scope-local eye adaptation, the image grades by the scope's own measured exposure (0 = main exposure)
float ps_r__svp_exposure_bias = 0.0f; // svp local-exposure bias in stops
int ps_r__svp_light_capture = 1; // svp capture lights for the scope cone past the main frustum/fog cull (0 = main-view lights only)
int ps_r__svp_npc_detail = 1; // svp keep dynamic parts the main view discards as subpixel (distant NPC heads), the scope resolves them (0 = stock discard)
int ps_r__svp_thermal_sim = 1; // svp digital-sensor sim on thermal displays: sensor cell grid + per-cell noise (0 = clean optical image)
float ps_r__svp_twilight = 1.0f; // svp exit-pupil twilight dimming: zooming shrinks the exit pupil below the dark-adapted eye and the image dims, day scenes unaffected (0 = off)
float ps_r__svp_parallax = 0.0f; // svp reticle parallax, 0 = pinned center (default), 1 = the real eye deflection response
float ps_r__svp_near_blur = 1.0f; // svp near-field defocus strength on the scope image (svpscope 2, 0 = off)
int ps_r__svp_nearblur_scatter = 0; // svp near-blur composite, 0 = gather default look, 1 = scatter accumulator
float ps_r__svp_focus_m = 100.0f; // svp parallax focus distance in meters, objects off this plane defocus by the thin lens law
int ps_r__svp_authored_optics = 1; // svp use the authored per-scope scope_objective_lens_offset for the front plane + defocus aperture (0 = measured geometry only)
int ps_r__svp_measured_optics = 0; // svp fill the objective offset + mm from mesh detection when authored ltx is absent (0 = authored/live only, byte-identical)
float ps_r__svp_eyebox = 1.0f; // svp eyebox strength, console unregistered, 1.0 keeps the anchor bound publish alive
int ps_r__svp_reflex_capture = 0; // svp reflex/holo dot path, 0 = legacy 1x main-view overlay, 1 = collimated proxy inside the magnifier viewport (default 0 until in-game confirms the proxy)
float ps_s3ds_objective_mm = 0.f; // per-scope objective clear aperture mm from spec sheets, pushed by zzz_extra_scope_features (0 = mesh-relative fallback)
float ps_s3ds_middle_grey = 0.f; // per-scope SVP tonemap middle-grey override, pushed by zzz_extra_scope_features (0 = inherit main)
float ps_s3ds_adapt_speed = 0.f; // per-scope SVP tonemap adaptation speed override (0 = inherit main)
int ps_r__svp_chroma = 1; // svp keep the authored per-scope chromatic aberration on glass under true PiP, scaled by zoom (0 = stripped with clean optics)
float ps_r__svp_reticle_washout = 0.0f; // svp illuminated reticle wash-out vs a bright background, glow only (0 = off)
float ps_r__svp_field_curve = 1.0f; // svp field curvature edge softness, outer field blurs like a real non-flat-field scope (0 = flat)
int ps_r__svp_acog_fiber = 0; // svp ACOG fiber reticle brightness source, 1 = sun visibility (fiber gathers sunlight), 0 = scene luminance
float ps_r__svp_veiling_glare = 0.0f; // svp veiling glare strength, off-axis sun scatter washes the image near the sun (0 = off)
float ps_r__svp_rain_optic = 1.0f; // svp rain droplets on the objective glass, scaled by rain density (0 = off)
float ps_r__svp_rain_debug = 0.0f; // svp forces the scope rain regardless of weather, the value stands in for rain density (0 = live weather)
int ps_r__svp_uv_debug = 0; // svp debug: paint the scope by the sample-coord V (green top red bottom) to spot a flipped lens UV
int ps_r__svp_autoflip = 1; // svp auto-correct a mesh-inverted lens (world upside down) by detecting the flipped sample-coord V (1 = on)
int ps_r__svp_autoflip_reticle = 1; // svp auto-correct a mesh-inverted reticle to match the auto-flipped world (1 = on)
float ps_r__svp_coating = 0.0f; // svp lens coating, typical multi-coated glass transmission loss + faint warm tint (0 = off, 1 = physical)
float ps_r__svp_mirage = 0.0f; // svp heat mirage on distant ground, grows with sun heat and magnification (0 = off)
int ps_r__svp_flat_window = 1; // svp flat-screen optics render the panel's see-through window subtense so magnification tracks the stock look (1 = on)
float ps_r__svp_sharpen = 0.35f; // svp contrast-adaptive sharpen on the scope image (0 = off, 1 = max)
float ps_r__svp_sharpen_falloff = 0.0f; // svp sharpen radial falloff toward the disc rim, crisp center soft edge (0 = uniform)
float ps_r__svp_sharpen_inner = 0.0f; // svp sharpen inner crisp-zone radius before the falloff starts (0 = from center)
float ps_r__svp_nvg_bleach = 0.0f; // svp NVG highlight bleach roll-off, replaces the hard clamp so bright sources compress not clip (0 = off, stock)
float ps_r__svp_nvg_sensitivity = 1.0f; // svp NVG bleach onset sensitivity, higher rolls off dimmer sources
int ps_r__svp_hud_full = 2; // svpscope 1 body skip only, mode 2 clips at the pupil camera near plane instead renders (default), 1 = full barrel from the eye + near-blur, 0 = objective clip with the body rendered
int ps_r__svp_near_pupil = 0; // svp mode 2 push the scope near clip to the pupil front plane so behind-pupil barrel falls behind it (0 = main-view near plane)
int ps_r__svp_drain_clip = 0; // svp clip the drained barrel wholly behind the authored objective plane, w2 near-blur dissolves the surviving sliver (0 = drain the whole barrel)
int ps_r__svp_cull = 1; // svp cull the scope geometry to the scope frustum, the SVP re-submits the whole main-frustum world otherwise (1 = on)
int ps_r__svp_skip_motionblur = 0; // svp skip motion blur on the scope pass, magnified blur is an artifact and a small cost (0 = keep)
int ps_r__svp_skip_hud_distort = 1; // svp skip hud-layer distortion on the scope pass, the muzzle heat billboard sits on the entrance pupil and warps the image (1 = skip)
// note r__svp_distort_guard owns the lens footprint in the main view, 0 here shows warp there only with the guard also 0
int ps_r__svp_skip_dof = 0; // svp skip the scope-internal dof pass, main dof still covers the composited lens (0 = current doubled behavior)
int ps_r__svp_wpn_dof = 1; // svp pass the sss weapon dof constants through while scoped, 0 zeroes them like before
int ps_r__svp_skip_lut = 0; // svp skip the scope-internal lut grade, main grade still covers the composited lens (0 = current doubled behavior)
int ps_r__svp_emissive = 1; // svp replay self-illum geometry into the scope image so lamps glow through the scope (0 = main-only emissive)
int ps_r__svp_skip_ssr = 1; // svp scope reflections, 0 reflective water + SSR, 1 flat water + SSR (default), 2 flat water + no SSR
int ps_r__svp_skip_volumetric = 0; // svp skip volumetric lights on the scope pass, subtle at magnification (0 = keep)
int ps_r__svp_skip_grass = 0; // svp skip grass/details on the scope pass, near grass is mostly off a zoomed cone (0 = keep)
int ps_r__svp_sss_sun = 0; // svp compute the scope SSS pass and keep the sun contact shadow term on the scope, expensive (0 = off)
int ps_r__svp_cull_grass = 1; // svp cull grass instances to the scope cone instead of replaying the whole main field (1 = on)
int ps_r__svp_light_cull = 1; // svp cone-cull the mirrored light blends, skip a light whose sphere never meets the scope cone (1 = on, 0 = mirror everything)
int ps_r__svp_corner_mask = 1; // svp stencil the dead corners outside the eyepiece disc so the lighting + combine passes skip them (1 = on)
int scope_debug = 0;

void svp_console_init()
{
#if defined(USE_DX11)
	// true PiP scope cvars are DX11-only, do not register them on the DX10/9/8 renderers (the backing
	// vars keep their 0 defaults so the shared code still reads them as off)
	CMD4(CCC_Integer, "r__svpscope", &scope_svp_enabled, 0, 2);
	CMD4(CCC_Float, "r__svp_render_scale", &ps_r__svp_render_scale, 0.5f, 1.0f); // takes effect on vid_restart, floor matches the runtime clamp
	CMD4(CCC_Float, "r__svp_supersample", &ps_r__svp_supersample, 1.0f, 2.0f); // SSAA the magnified scope image, 1.0 = off (4x SVP cost at 2.0)
	CMD4(CCC_Integer, "r__svp_diag", &ps_r__svp_diag, 0, 1); // SVP perf diagnostics log (0 = off)
	CMD4(CCC_Integer, "r__svp_cop_diag", &ps_r__svp_cop_diag, 0, 2); // svp optics log (1 = throttled, 2 = + per-frame [SVP-AIM])
	CMD4(CCC_Integer, "r__svp_report", &ps_r__svp_report, 0, 1); // svp one-shot [SVP-CFG]+[SVP-FILES] re-dump, self-clearing
	CMD4(CCC_Integer, "r__svp_stats", &ps_r__svp_stats, 0, 2); // per-viewport render stats overlay (1 = compact, 2 = per-section breakdown)
	CMD4(CCC_Integer, "r__3db_debug", &ps_r__3db_debug, 0, 3); // 3db overlay (1 = markers + axes, 2 = + zeroed ray, 3 = + tracers)
	CMD4(CCC_Float, "r__svp_adaptive_res", &ps_r__svp_adaptive_res, 0.0f, 2.0f); // size SVP render to the eyepiece disc * margin (0 = off, 1.2 recommended)
	CMD4(CCC_Float, "r__svp_lod", &ps_r__svp_lod, 0.0f, 1.0f); // SVP LOD reduction strength (0 = off)
	CMD4(CCC_Float, "r__svp_cull_ssa", &ps_r__svp_cull_ssa, 0.0f, 8.0f); // SVP small-object cull strength (0 = off)
	CMD4(CCC_Integer, "r__svp_dlss", &ps_r__svp_dlss, 0, 1); // SVP DLSS-SR scaffolding gate, 0 = stock
	CMD4(CCC_Float, "r__svp_obj_dist", &ps_r__svp_obj_dist, 0.0f, 3.0f); // svpscope 2 objective: scale on the auto geomscan front distance
	CMD4(CCC_Float, "r__svp_obj_size", &ps_r__svp_obj_size, 0.1f, 6.0f); // svpscope 2 geometric objective: objective radius (eyepiece radii)
	CMD4(CCC_Integer, "r__svp_focal_derive", &ps_r__svp_focal_derive, 0, 1); // svpscope 2 focal anchors from eye relief + tube length (0 = 0.4/0.6 split)
	CMD4(CCC_Integer, "r__svp_glare_model", &ps_r__svp_glare_model, 0, 1); // veiling glare falloff model (0 = legacy pow6)
	CMD4(CCC_Integer, "r__svp_photo_model", &ps_r__svp_photo_model, 0, 1); // eye photometric models (0 = legacy linear)
	CMD4(CCC_Integer, "r__svp_adaptive_grow", &ps_r__svp_adaptive_grow, 0, 1); // adaptive res grows to the disc (0 = shrink-only)
	CMD4(CCC_Integer, "r__svp_drain_anchor", &ps_r__svp_drain_anchor, 0, 1); // drain clip-on window anchors on the authored front (0 = fixed window)
	CMD4(CCC_Integer, "r__svp_settle_derive", &ps_r__svp_settle_derive, 0, 1); // eyebox windows from authored weapon timing (0 = flat ms)
	CMD4(CCC_Integer, "r__svp_ratio_derive", &ps_r__svp_ratio_derive, 0, 1); // ratio latch band from measured noise (0 = fixed band)
	CMD4(CCC_Integer, "r__svp_lens_reject", &ps_r__svp_lens_reject, 0, 1); // while aimed, reject a far-jumping eyepiece pose and hold the last good (0 = take every pose)
	CMD4(CCC_Integer, "r__svp_recoil_hold", &ps_r__svp_recoil_hold, 0, 1); // scope optics derivation holds through the post-shot window (0 = live)
	CMD4(CCC_Integer, "r__svp_roll_stabilize", &ps_r__svp_roll_stabilize, 0, 1); // svp keep the scope world level on lean/cant (0 = realistic image-tilts-with-cant)
	CMD4(CCC_Integer, "r__svp_clean_optics", &ps_r__svp_clean_optics, 0, 1); // strip 3DSS fake cosmetics (parallax shadow/chromatism/nvg blur/fisheye), 0 = full look
	CMD4(CCC_Integer, "r__svp_distort_guard", &ps_r__svp_distort_guard, 0, 1); // stamp distort-mask neutral over the composited lens (0 = lens warps with main view)
	CMD4(CCC_Integer, "r__svp_jitterfix", &ps_r__svp_jitterfix, 0, 1); // svp lens-edge jitter pre-pass (0 skips, superseded-by-sentinel a/b)
	CMD4(CCC_Integer, "r__svp_taa_mask", &ps_r__svp_taa_mask, 0, 1); // stamp the taa mask over the composited lens (0 = let the main taa re-resolve it)
	CMD4(CCC_Integer, "r__svp_hud_fov_match", &ps_r__svp_hud_fov_match, 0, 2); // svp barrel mapping: 2 magnified, 1 window, 0 legacy
	CMD4(CCC_Integer, "r__svp_bloom", &ps_r__svp_bloom, 0, 1); // svp bloom on the scope pass
	CMD4(CCC_Integer, "r__svp_local_exposure", &ps_r__svp_local_exposure, 0, 1); // svp scope-local eye adaptation
	CMD4(CCC_Float, "r__svp_exposure_bias", &ps_r__svp_exposure_bias, -3.0f, 3.0f); // svp local-exposure bias in stops
	CMD4(CCC_Integer, "r__svp_light_capture", &ps_r__svp_light_capture, 0, 1); // svp scope-cone light capture
	CMD4(CCC_Integer, "r__svp_npc_detail", &ps_r__svp_npc_detail, 0, 1); // svp keep subpixel dynamic parts for the scope (0 = stock)
	CMD4(CCC_Integer, "r__svp_thermal_sim", &ps_r__svp_thermal_sim, 0, 1); // svp thermal digital-sensor sim (0 = clean)
	CMD4(CCC_Float, "r__svp_twilight", &ps_r__svp_twilight, 0.0f, 1.0f); // svp exit-pupil twilight dimming strength (0 = off)
	CMD4(CCC_Float, "r__svp_parallax", &ps_r__svp_parallax, 0.0f, 10.0f); // svp true-scale reticle parallax, 1 = physical sub-pixel (0 = pinned)
	CMD4(CCC_Float, "r__svp_near_blur", &ps_r__svp_near_blur, 0.0f, 3.0f); // svp near-field defocus strength (0 = off)
	CMD4(CCC_Integer, "r__svp_nearblur_scatter", &ps_r__svp_nearblur_scatter, 0, 1); // svp near-blur composite (0 = gather default, 1 = scatter)
	CMD4(CCC_Float, "r__svp_focus_m", &ps_r__svp_focus_m, 10.0f, 1000.0f); // svp parallax focus distance m (fixed-parallax scopes sit at 100)
	CMD4(CCC_Integer, "r__svp_authored_optics", &ps_r__svp_authored_optics, 0, 1); // svp use authored per-scope objective offset for front plane + aperture (0 = measured only)
		CMD4(CCC_Integer, "r__svp_measured_optics", &ps_r__svp_measured_optics, 0, 1); // measured lens geometry fills unauthored optics (0 = off, authored/live only)
	CMD4(CCC_Integer, "r__svp_reflex_capture", &ps_r__svp_reflex_capture, 0, 1); // svp reflex/holo dot path (0 = legacy 1x main-view overlay, 1 = collimated proxy in the magnifier viewport)
	CMD4(CCC_Float, "s3ds_objective_mm", &ps_s3ds_objective_mm, 0.0f, 200.0f); // per-scope objective clear aperture mm for the true exit pupil
	CMD4(CCC_Float, "s3ds_middle_grey", &ps_s3ds_middle_grey, 0.0f, 2.0f); // per-scope SVP tonemap middle-grey (0 = inherit main)
	CMD4(CCC_Float, "s3ds_adapt_speed", &ps_s3ds_adapt_speed, 0.0f, 20.0f); // per-scope SVP tonemap adaptation speed (0 = inherit main)
	CMD4(CCC_Integer, "r__svp_chroma", &ps_r__svp_chroma, 0, 1); // svp keep authored chromatic aberration on glass, zoom scaled (0 = stripped)
	CMD4(CCC_Float, "r__svp_reticle_washout", &ps_r__svp_reticle_washout, 0.0f, 2.0f); // svp illuminated reticle wash-out strength (0 = off)
	CMD4(CCC_Float, "r__svp_field_curve", &ps_r__svp_field_curve, 0.0f, 3.0f); // svp field curvature edge softness (0 = flat field)
	CMD4(CCC_Integer, "r__svp_acog_fiber", &ps_r__svp_acog_fiber, 0, 1); // svp ACOG fiber brightness source (1 = sun visibility, 0 = scene luminance)
	CMD4(CCC_Float, "r__svp_veiling_glare", &ps_r__svp_veiling_glare, 0.0f, 3.0f); // svp veiling glare near the sun (0 = off)
	CMD4(CCC_Float, "r__svp_rain_optic", &ps_r__svp_rain_optic, 0.0f, 3.0f); // svp rain droplets on the objective (0 = off)
	CMD4(CCC_Float, "r__svp_rain_debug", &ps_r__svp_rain_debug, 0.0f, 3.0f); // svp force scope rain, value = density stand-in (0 = live weather)
	CMD4(CCC_Integer, "r__svp_uv_debug", &ps_r__svp_uv_debug, 0, 1); // svp debug: paint sample-coord V to spot a flipped lens UV
	CMD4(CCC_Integer, "r__svp_autoflip", &ps_r__svp_autoflip, 0, 1); // svp auto-correct a mesh-inverted lens (1 = on)
	CMD4(CCC_Integer, "r__svp_autoflip_reticle", &ps_r__svp_autoflip_reticle, 0, 1); // svp auto-correct a mesh-inverted reticle (1 = on)
	CMD4(CCC_Float, "r__svp_coating", &ps_r__svp_coating, 0.0f, 1.0f); // svp lens coating transmission loss + warm tint (0 = off, 1 = physical)
	CMD4(CCC_Float, "r__svp_mirage", &ps_r__svp_mirage, 0.0f, 3.0f); // svp heat mirage on distant ground, sun + magnification driven (0 = off)
	CMD4(CCC_Integer, "r__svp_flat_window", &ps_r__svp_flat_window, 0, 1); // svp flat-screen window subtense fov (1 = on)
	CMD4(CCC_Float, "r__svp_sharpen", &ps_r__svp_sharpen, 0.0f, 1.0f); // svp contrast-adaptive sharpen (0 = off)
	CMD4(CCC_Float, "r__svp_sharpen_falloff", &ps_r__svp_sharpen_falloff, 0.0f, 1.0f); // svp sharpen radial falloff (0 = uniform)
	CMD4(CCC_Float, "r__svp_sharpen_inner", &ps_r__svp_sharpen_inner, 0.0f, 1.0f); // svp sharpen inner crisp radius (0 = from center)
	CMD4(CCC_Float, "r__svp_nvg_bleach", &ps_r__svp_nvg_bleach, 0.0f, 1.0f); // svp NVG highlight bleach roll-off (0 = off, stock clamp)
	CMD4(CCC_Float, "r__svp_nvg_sensitivity", &ps_r__svp_nvg_sensitivity, 0.1f, 4.0f); // svp NVG bleach onset sensitivity
	CMD4(CCC_Integer, "r__svp_hud_full", &ps_r__svp_hud_full, 0, 2); // svp barrel: 2 front-plane clip (default), 1 full barrel from the eye, 0 objective clip + body
	CMD4(CCC_Integer, "r__svp_near_pupil", &ps_r__svp_near_pupil, 0, 1); // svp mode 2 push the near clip to the pupil front plane (0 = main-view near)
	CMD4(CCC_Integer, "r__svp_drain_clip", &ps_r__svp_drain_clip, 0, 1); // svp clip the drained barrel behind the authored objective plane (0 = whole barrel)
	CMD4(CCC_Integer, "r__svp_cull", &ps_r__svp_cull, 0, 1); // svp frustum cull the scope geometry (1 = on)
	CMD4(CCC_Integer, "r__svp_skip_motionblur", &ps_r__svp_skip_motionblur, 0, 1); // svp skip motion blur on the scope
	CMD4(CCC_Integer, "r__svp_skip_hud_distort", &ps_r__svp_skip_hud_distort, 0, 1); // svp skip hud-layer distortion on the scope
	CMD4(CCC_Integer, "r__svp_skip_dof", &ps_r__svp_skip_dof, 0, 1); // svp scope-internal dof once-only gate
	CMD4(CCC_Integer, "r__svp_wpn_dof", &ps_r__svp_wpn_dof, 0, 1); // svp sss weapon dof while scoped
	CMD4(CCC_Integer, "r__svp_skip_lut", &ps_r__svp_skip_lut, 0, 1); // svp scope-internal lut once-only gate
	CMD4(CCC_Integer, "r__svp_emissive", &ps_r__svp_emissive, 0, 1); // svp self-illum in the scope image
	CMD4(CCC_Integer, "r__svp_skip_ssr", &ps_r__svp_skip_ssr, 0, 2); // svp scope reflections level (0 expensive, 1 regular, 2 cheapest)
	CMD4(CCC_Integer, "r__svp_skip_volumetric", &ps_r__svp_skip_volumetric, 0, 1); // svp skip volumetric lights on the scope
	CMD4(CCC_Integer, "r__svp_skip_grass", &ps_r__svp_skip_grass, 0, 1); // svp skip grass/details on the scope
	CMD4(CCC_Integer, "r__svp_sss_sun", &ps_r__svp_sss_sun, 0, 1); // svp SSS sun contact shadows on the scope
	CMD4(CCC_Integer, "r__svp_cull_grass", &ps_r__svp_cull_grass, 0, 1); // svp cull grass to the scope cone
	CMD4(CCC_Integer, "r__svp_light_cull", &ps_r__svp_light_cull, 0, 1); // svp cone-cull the mirrored light blends (1 = on)
	CMD4(CCC_Integer, "r__svp_corner_mask", &ps_r__svp_corner_mask, 0, 1); // svp stencil-skip the dead disc corners (1 = on)
	CMD4(CCC_Integer, "r__scope_debug", &scope_debug, 0, 4);
#endif
}

// [SVP-LEDGER] {counter, gate} table, only rows where a gate on means the counter should tick in
// normal scoped play, the situational latches (hud drain, reflex proxy, nvg sky) stay out of it
namespace
{
	struct svp_ledger_row
	{
		const char*  name;
		const u32*   counter;  // session latch
		const int*   gate_i;   // int cvar gate, nonzero = on (null skips this test)
		const float* gate_f;   // float cvar gate, > thr = on (null skips this test)
		float        gate_thr;
	};
	static const svp_ledger_row s_svp_ledger[] = {
		{ "cull_reject",       &svp_ledger_cull_reject,       &ps_r__svp_cull,          nullptr,                 0.f },
		{ "ssa_culled",        &svp_ledger_ssa_culled,        nullptr,                  &ps_r__svp_cull_ssa,     0.f },
		{ "cull_reject_ident", &svp_ledger_cull_reject_ident, &ps_r__svp_cull,          nullptr,                 0.f },
		{ "lights_mirrored",   &svp_ledger_lights_mirrored,   &ps_r__svp_light_cull,    nullptr,                 0.f },
		{ "lights_skipped",    &svp_ledger_lights_skipped,    &ps_r__svp_light_cull,    nullptr,                 0.f },
		{ "lod_scale",         &svp_ledger_lod_scale,         nullptr,                  &ps_r__svp_lod,          0.f },
		{ "grass_cull_reject", &svp_ledger_grass_cull_reject, &ps_r__svp_cull_grass,    nullptr,                 0.f },
		{ "distort_guard",     &svp_ledger_distort_guard,     &ps_r__svp_distort_guard, nullptr,                 0.f },
		{ "disc_latch",        &svp_ledger_disc_latch,        nullptr,                  &ps_r__svp_adaptive_res, 0.f },
		{ "fwd_keep",          &svp_ledger_fwd_keep,          nullptr,                  nullptr,                 0.f },
	};
}

// prints [SVP-LEDGER] once per session, a gate on with its latch still zero is a maybe-inert path
void svp_ledger_sweep()
{
	u32 checked = 0, inert = 0;
	for (const svp_ledger_row& r : s_svp_ledger)
	{
		const bool on = (r.gate_i ? (*r.gate_i != 0) : true) && (r.gate_f ? (*r.gate_f > r.gate_thr) : true);
		if (!on)
			continue;
		++checked;
		if (*r.counter == 0)
		{
			PipMsg("[SVP-LEDGER] INERT? %s gate on counter zero", r.name);
			++inert;
		}
	}
	if (inert == 0)
		PipMsg("[SVP-LEDGER] ledger ok (%u checked)", checked);
	else
		PipMsg("[SVP-LEDGER] %u inert of %u checked", inert, checked);
}
