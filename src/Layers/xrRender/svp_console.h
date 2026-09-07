#ifndef svp_consoleH
#define svp_consoleH
#pragma once

// true PiP second viewport (SVP) console vars, the complete extern set
// definitions and registration live in svp_console.cpp

extern ECORE_API int scope_svp_enabled; // true PiP scope mode (0 off, 1 eyepiece, 2 objective)
extern ECORE_API int scope_debug; // scope debug overlay level
extern ECORE_API int ps_r__3db_debug; // ballistics overlay level

// render / perf
extern ECORE_API float ps_r__svp_render_scale;
extern ECORE_API float ps_r__svp_supersample;
extern ECORE_API int ps_r__svp_diag;
extern ECORE_API int ps_r__svp_cop_diag;
extern ECORE_API int ps_r__svp_report;
extern ECORE_API int ps_r__svp_stats; // per-viewport render stats overlay (0 off, 1 compact, 2 breakdown)
extern ECORE_API u32 svp_stats_ssa_culled; // svp small-object cull tally read by the overlay, incremented in r__dsgraph_render
extern ECORE_API u32 svp_stats_cull_reject; // svp off-cone frustum-reject tally read by the overlay, incremented in r__dsgraph_render
extern ECORE_API u32 svp_stats_cull_reject_ident; // svp identity-matrix sorted world statics the cone rejects, incremented in svp_cull_reject
extern ECORE_API u32 svp_stats_lights_mirrored; // svp lights the cone cull mirrors into the scope, incremented in lights_render
extern ECORE_API u32 svp_stats_lights_skipped; // svp lights the cone cull drops, incremented in lights_render
extern ECORE_API u32 svp_stats_taa_stamp; // svp taa sovereignty stamp fires read by the overlay, incremented in phase_ssfx_taa
extern ECORE_API u32 svp_stats_nvg_split; // svp nvg tube split fires read by the overlay, incremented in phase_combine
extern ECORE_API u32 svp_stats_lod_scale; // svp lod scale armed frames read by the overlay, incremented in svp_set_lod_scale
extern ECORE_API u32 svp_stats_hud_cull_reject; // svp hud drain cone rejects read by the overlay, incremented in svp_hud_latch
extern ECORE_API u32 svp_stats_grass_cull_reject; // svp grass cone rejects read by the overlay, incremented in the detail manager
extern ECORE_API u32 svp_stats_reflex_proxy; // svp reflex proxy draws read by the overlay, incremented in draw_reflex_proxy
extern ECORE_API u32 svp_stats_distort_guard; // svp distort guard stamps read by the overlay, incremented in phase_combine
extern ECORE_API u32 svp_stats_nvg_sky; // svp nvg sky lum remaps read by the overlay, incremented in phase_combine
extern ECORE_API u32 svp_stats_disc_latch; // svp adaptive-res disc latch moves read by the overlay, incremented in phase_3DSSReticle
extern ECORE_API u32 svp_stats_fwd_keep; // svp forward-list keeps read by the overlay, incremented in render_forward
// session-lifetime ever-fired ledger latches, set at the gated call sites, swept once per session
extern ECORE_API u32 svp_ledger_cull_reject;
extern ECORE_API u32 svp_ledger_ssa_culled;
extern ECORE_API u32 svp_ledger_cull_reject_ident;
extern ECORE_API u32 svp_ledger_lights_mirrored;
extern ECORE_API u32 svp_ledger_lights_skipped;
extern ECORE_API u32 svp_ledger_lod_scale;
extern ECORE_API u32 svp_ledger_hud_cull_reject;
extern ECORE_API u32 svp_ledger_grass_cull_reject;
extern ECORE_API u32 svp_ledger_reflex_proxy;
extern ECORE_API u32 svp_ledger_distort_guard;
extern ECORE_API u32 svp_ledger_nvg_sky;
extern ECORE_API u32 svp_ledger_disc_latch;
extern ECORE_API u32 svp_ledger_fwd_keep;
extern ECORE_API float ps_r__svp_adaptive_res;
extern ECORE_API float ps_r__svp_lod;
extern ECORE_API float ps_r__svp_cull_ssa;
extern ECORE_API int ps_r__svp_dlss;
extern ECORE_API int ps_r__svp_adaptive_grow;
extern ECORE_API int ps_r__svp_cull;
extern ECORE_API int ps_r__svp_cull_grass;
extern ECORE_API int ps_r__svp_light_cull;
extern ECORE_API int ps_r__svp_corner_mask;
extern ECORE_API int ps_r__svp_skip_motionblur;
extern ECORE_API int ps_r__svp_skip_hud_distort;
extern ECORE_API int ps_r__svp_skip_dof;
extern ECORE_API int ps_r__svp_wpn_dof;
extern ECORE_API int ps_r__svp_skip_lut;
extern ECORE_API int ps_r__svp_emissive;
extern ECORE_API int ps_r__svp_skip_ssr;
extern ECORE_API int ps_r__svp_skip_volumetric;
extern ECORE_API int ps_r__svp_skip_grass;
extern ECORE_API int ps_r__svp_sss_sun;

// optics derivation
extern ECORE_API float ps_r__svp_obj_dist;
extern ECORE_API float ps_r__svp_obj_size;
extern ECORE_API int ps_r__svp_focal_derive;
extern ECORE_API int ps_r__svp_glare_model;
extern ECORE_API int ps_r__svp_photo_model;
extern ECORE_API int ps_r__svp_drain_anchor;
extern ECORE_API int ps_r__svp_settle_derive;
extern ECORE_API int ps_r__svp_ratio_derive;
extern ECORE_API int ps_r__svp_lens_reject;
extern ECORE_API int ps_r__svp_recoil_hold;
extern ECORE_API int ps_r__svp_roll_stabilize;
extern ECORE_API int ps_r__svp_clean_optics;
extern ECORE_API int ps_r__svp_distort_guard;
extern ECORE_API int ps_r__svp_jitterfix;
extern ECORE_API int ps_r__svp_taa_mask;
extern ECORE_API int ps_r__svp_hud_fov_match;
extern ECORE_API int ps_r__svp_bloom;
extern ECORE_API int ps_r__svp_local_exposure;
extern ECORE_API float ps_r__svp_exposure_bias;
extern ECORE_API int ps_r__svp_light_capture;
extern ECORE_API int ps_r__svp_npc_detail;
extern ECORE_API int ps_r__svp_thermal_sim;
extern ECORE_API float ps_r__svp_twilight;
extern ECORE_API float ps_r__svp_parallax;
extern ECORE_API float ps_r__svp_near_blur;
extern ECORE_API float ps_r__svp_focus_m;
extern ECORE_API int ps_r__svp_authored_optics;
extern ECORE_API float ps_r__svp_eyebox;
extern ECORE_API int ps_r__svp_reflex_capture;
extern ECORE_API int ps_r__svp_measured_optics; // measured lens geometry fills unauthored optics, registered in svp_console.cpp

// per-scope overrides pushed by zzz_extra_scope_features.script
extern ECORE_API float ps_s3ds_objective_mm;
extern ECORE_API float ps_s3ds_middle_grey;
extern ECORE_API float ps_s3ds_adapt_speed;

// glass / reticle cosmetics
extern ECORE_API int ps_r__svp_chroma;
extern ECORE_API float ps_r__svp_reticle_washout;
extern ECORE_API float ps_r__svp_field_curve;
extern ECORE_API int ps_r__svp_acog_fiber;
extern ECORE_API float ps_r__svp_veiling_glare;
extern ECORE_API float ps_r__svp_rain_optic;
extern ECORE_API float ps_r__svp_rain_debug;
extern ECORE_API int ps_r__svp_uv_debug;
extern ECORE_API int ps_r__svp_autoflip;
extern ECORE_API int ps_r__svp_autoflip_reticle;
extern ECORE_API float ps_r__svp_coating;
extern ECORE_API float ps_r__svp_mirage;
extern ECORE_API int ps_r__svp_flat_window;
extern ECORE_API float ps_r__svp_sharpen;
extern ECORE_API float ps_r__svp_sharpen_falloff;
extern ECORE_API float ps_r__svp_sharpen_inner;
extern ECORE_API float ps_r__svp_nvg_bleach;
extern ECORE_API float ps_r__svp_nvg_sensitivity;
extern ECORE_API int ps_r__svp_hud_full;

// registers every svp console command, called once from xrRender_initconsole
extern void svp_console_init();

// prints the [SVP-LEDGER] inert-path sweep, called once per session from svpCamera
extern void svp_ledger_sweep();

#endif
