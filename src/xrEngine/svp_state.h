#ifndef svp_stateH
#define svp_stateH
#pragma once

#include <functional>
#include <atomic>

// the SVP (PiP second viewport) cross-thread data bus, logic publishes and render consumes by
// field name, device.h includes this just before CRenderDevice and aliases it back inside
class ENGINE_API CSecondVPParams //--#SM+#-- +SecondVP+
{
	bool isActive; // Oeaa aeoeaaoee ?aiaa?a ai aoi?ie au?ii?o
	u8 frameDelay;  // Ia eaeii eaa?a n iiiaioa i?ioeiai ?aiaa?a ai aoi?ie au?ii?o iu ia?i?i iiaue
					  //(ia ii?ao auou iaiuoa 2 - ea?aue aoi?ie eaa?, ?ai aieuoa oai aieaa ieceee FPS ai aoi?ii au?ii?oa)

public:
	bool isCamReady; // Oeaa aioiaiinoe eaia?u (FOV, iiceoey, e o.i) e ?aiaa?o aoi?iai au?ii?oa

	IC bool IsSVPActive() { return isActive; }
	void SetSVPActive(bool bState);
	bool    IsSVPFrame();

	IC u8 GetSVPFrameDelay() { return frameDelay; }
	void  SetSVPFrameDelay(u8 iDelay)
	{
		frameDelay = iDelay;
		clamp<u8>(frameDelay, 2, u8(-1));
	}

	// true PiP additions
	struct Lens { Fmatrix m_W; float radius; };
	Lens eyepiece;
	Lens objective;
	Fvector3 w_ffp;
	Fvector3 w_sfp;
	// pip held lens radii for the debug overlays, cleared when the svp deactivates
	float dbg_eyepiece_r = 0.f;
	float dbg_objective_r = 0.f;

	// pip DLSS-SR scaffolding, all inert at gate 0. cached SVP scene constants refreshed at the
	// svpCamera tail (render thread, written then read same frame) for the eval inputs
	float svp_near = 0.f, svp_far = 0.f, svp_fov = 0.f, svp_aspect = 1.f;
	Fvector svp_cam_pos = {}, svp_up = {}, svp_right = {}, svp_fwd = {};
	Fvector2 svp_jitter_px = {}; // raw sub-pixel jitter baked into matrices[1].mProject, {0,0} at gate 0
	bool m_lens_prev_valid = false; // render-thread edge state for the lens-appears reset trigger

	float svp_disc_px = 0.f; // pip on-screen eyepiece disc diameter (px), learned in the lens composite
	float svp_disc_applied = 0.f; // pip disc px the SVP target is sized to, locked at ADS-in so it never resizes mid-ADS
	float svp_panel_aspect = 1.f; // pip flat-panel lens W:H from the lens AABB (1 = round/square scope)

	// pip flat-panel on-screen quad for the binocular target brackets, plane half-extent world vectors
	// (logic projects them through the hud transform), the shader V-crop, and the active-panel flag
	Fvector svp_panel_ax_w = {}; // panel center -> width edge (svp ndc +x)
	Fvector svp_panel_ax_h = {}; // panel center -> height edge (svp ndc -y)
	float svp_panel_vcrop = 1.f; // svp_glass2.w flat-panel V-crop (1 = svp matches the panel)
	bool svp_panel_flat = false; // a reticle_type 8 flat window drives the svp this frame

	// pip stable sight line for ballistics, published whole by deriveScopeLens. the eyepiece
	// fields are render scratch that zero at frame start while logic fires concurrently
	Fvector svp_sight_pos = {};
	Fvector svp_sight_dir = {};
	bool svp_sight_ok = false;
	u32 svp_sight_frame = 0; // publish frame, staleness gates the svp readiness
	float svp_lens_r = 0.f; // stable lens radius published with the sight line, logic reads it for activation
	u32 svp_optic_epoch = 0; // pip optic identity counter, bumps on a lens visual or radius change, subscribers reseed
	// pip resolved per-optic optics inputs, the bus fills these once at the lens derive so one
	// precedence and one eps gate govern every consumer instead of each re-reading the raw cvars
	Fvector4 svp_opt_offset = { 0.f, 0.f, 0.f, 0.f }; // xy lateral zw front/radius (eyepiece radii), authored_optics gated, 0 = none
	float svp_opt_obj_mm = 0.f; // objective clear aperture mm, the spec cvar then the authored w fallback, 0 = none
	bool svp_alt_sight = false; // aimed on a non scope sight, the fit latch freezes on the wrong pose
	float svp_recoil_relax_s = 0.f; // authored one shot recovery, dispersion over relax speed
	float svp_zoom_pub = 0.f; // raise transient free zoom for the svp camera, 0 = unset falls back to hud_params.y
	float svp_eyebox_rad = 0.f; // eyebox half angle (rad) from the real exit pupil and eye relief
	float svp_shadow_gain = 0.f; // swing envelope 0..1, hard weapon motion sweeps the crescent in
	u32 svp_lever_ms = 0; // pip lever throw stamp, a click flip pulses the transition shadow for the throw
	float svp_swing_x = 0.f, svp_swing_y = 0.f; // latched swing side unit vector for the crescent
	float svp_mag = 0.f; // current scope magnification for the zoom scaled trigger, 0 unknown
	float svp_fov_scale = 1.f; // config zoom factors ride the 75 base, this rescales them to the live fov, 1 for script authored
	float svp_aim_fov = 0.f; // steady wide main view fov (deg) through a pip scope, the mag reads it so recoil fov punches never wobble it, 0 unset
	bool svp_authored_mag = false; // pip flat optic carries authored mags, keep the clean optical mag not the panel subtense ratio
	bool svp_min_75base = false; // pip the min zoom bound was computed in the 75 base so it rescales to the aim fov

	// pip live ballistic ray of the active weapon (logic thread writes, render diag reads)
	Fvector fire_ray_pos = {};
	Fvector fire_ray_dir = {};
	float fire_ray_zero = 0.f; // meters, mirrors g_svp_zero for the render-side overlay
	u32 fire_ray_frame = 0;
	Fvector muzzle_pos = {}; // pip fire bone + fire_point of the active weapon, for the [3DB] markers
	Fvector eye_ray_pos = {}; // pip actor first-eye mirror (logic writes), stays the shooter's
	Fvector eye_ray_dir = {}; // eye while demo_record flies the device camera

	// pip [3DB] shot tracer ring, one entry per fired bullet (logic writes, overlay reads + fades)
	struct FireTrace { Fvector pos; Fvector dir; u32 time_ms; };
	FireTrace fire_traces[16] = {};
	u32 fire_trace_head = 0;

	// history reset for the eval, set by the triggers (logic + render threads), consumed render-side
	// at the seam via exchange, atomic because the logic-thread writers race the render-thread read
	std::atomic<bool> dlss_reset_next{ false };

	// set by the double-pass, read by the hybrid IsSVPFrame when true_pip is on
	bool m_render_pass_is_svp = false;

	// pip set around the disc pass of the nvg tube split, the dev_param_8 binder centers the
	// offset tube only while it holds (render thread only)
	bool svp_nvg_disc_pass = false;

	// pip set true only when the collimated reflex proxy drew into rt_secondVP this frame
	bool svp_reflex_proxy_ok = false;

	// pip set only around the SVP water surface draw, makes ssfx_issvp read 0 so the SSS water shader
	// uses the SVP reflection instead of its flat-scope fallback (ssfx_water.ps reflection = turbidity)
	bool force_water_reflect = false;

	// pip set around the SVP sun accum, makes ssfx_issvp read 0 so the sun keeps the SSS contact term
	bool force_svp_sss = false;

	// pip shared-shadow hook, called as accum(); if (dual_accum) dual_accum(accum), the lambda
	// re-accumulates the shadow unit into the SVP, null for R2/R3 and when PiP is off
	std::function<void(const std::function<void()>&)> dual_accum;

};

#endif
