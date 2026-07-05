#include "stdafx.h"
#include "../../xrEngine/igame_persistent.h"
#include "../xrRender/FBasicVisual.h"
#include "../../xrEngine/customhud.h"
#include "../../xrEngine/xr_object.h"
#include "../xrRender/SkeletonCustom.h"
#include "../../xrParticles/ParticlesAsyncManager.h"

#include "../xrRender/QueryHelper.h"
#include "../../Include/xrAPI/xrAPI.h"          // pip DRender, the debug-line backend for the scope_debug world overlay
#include "../../Include/xrRender/DebugRender.h" // pip IDebugRender::add_lines
#include "../xrRender/SkeletonX.h"              // pip CSkeletonX for the skinned lens bone transform

// set all per-viewport camera globals from an explicit view/proj/proj_hud
// called by SetActive, the main path still uses on_idle
void CRender::SetMatrices(Fmatrix view, Fmatrix projection, Fmatrix projection_hud)
{
	Device.mView.set(view);
	Device.mProject.set(projection);
	Device.mProjectHud.set(projection_hud);
	Device.mFullTransform.mul(Device.mProject, Device.mView);
	Device.mFullTransformHud.mul(Device.mProjectHud, Device.mView);

	Device.mInvView.invert(view);
	Device.mInvProject.invert(projection);
	Device.mInvProjectHud.invert(projection_hud);
	D3DXMatrixInverse((D3DXMATRIX*)&Device.mInvFullTransform, 0, (D3DXMATRIX*)&Device.mFullTransform);

	Device.mInvView.transform(Device.vCameraPosition.set(0, 0, 0));
	Device.mInvView.transform_dir(Device.vCameraDirection.set(0, 0, 1));
	Device.mInvView.transform_dir(Device.vCameraTop.set(0, 1, 0));
	Device.mInvView.transform_dir(Device.vCameraRight.set(1, 0, 0));

	Device.vCameraPosition_saved.set(Device.vCameraPosition);
	Device.mView_saved.set(Device.mView);
	Device.mProject_saved.set(Device.mProject);
	Device.mFullTransform_saved.set(Device.mFullTransform);

	float fFov, fAspect, _;
	projection.decompose_projection(fFov, fAspect, _, _);
	Device.fFOV = rad2deg(fFov);
	Device.fASPECT = fAspect;

	Device.m_pRender->SetCacheXform(Device.mView, Device.mProject);
	Device.prepare_matrices();
}

// pip scope_debug >= 2 world overlay, draws the eyepiece (blue), objective (yellow) and camera
// (white) lenses in the world via DRender->add_lines so the render DLL needs no xrGame link, the
// lines are flushed by the stock CLevel::OnRender debug render
void debug_scope(Fmatrix scope_camera)
{
	auto dbg_line = [](const Fvector& a, const Fvector& b, u32 color, bool bHud) {
		Fvector v[2] = { a, b };
		u16 idx[2] = { 0, 1 };
		DRender->add_lines(v, 2, idx, 1, color, bHud);
	};

	auto draw_circle = [&](Fmatrix m, u32 color, bool bHud) {
		const int n = 100;
		Fvector v0 = { 0, 0, 0 };
		for (int i = 0; i <= n; i++) {
			float angle = float(i) / float(n) * PI * 2.0f;
			Fvector v1 = { cosf(angle), sinf(angle), 0.f };
			m.transform(v1);
			if (i > 0) dbg_line(v0, v1, color, bHud);
			v0 = v1;
		}
	};

	auto draw_lens = [&](CRenderDevice::CSecondVPParams::Lens lens, u32 color) {
		draw_circle(Fmatrix(lens.m_W).mulB_43(Fmatrix().scale(lens.radius, lens.radius, 0.f)), color, true);
		Fvector v0 = { 0, 0, 0 }, v1 = { 0, 0, 100 };
		lens.m_W.transform(v0);
		lens.m_W.transform(v1);
		dbg_line(v0, v1, color, true);
		// up-spoke, a circle + optical axis can't show a ROLL about the axis (rotationally symmetric),
		// so draw the lens up-vector as a radial spoke, a rolled lens shows the spoke pointing off-up
		Fvector u0 = { 0, 0, 0 }, u1 = { 0, lens.radius, 0 };
		lens.m_W.transform(u0);
		lens.m_W.transform(u1);
		dbg_line(u0, u1, color, true);
	};

	auto draw_camera = [&](u32 color) {
		const float cm = 1.0f / 100.0f;
		draw_circle(Fmatrix(scope_camera).mulB_43(Fmatrix().scale(.25f * cm, .25f * cm, 0.f)), color, true);
	};

	// pip wireframe cube at a transform (orientation + position), half-extent h, 12 edges
	auto draw_cube = [&](const Fmatrix& m, float h, u32 color) {
		Fvector c[8];
		for (int i = 0; i < 8; i++) {
			Fvector q; q.set((i & 1) ? h : -h, (i & 2) ? h : -h, (i & 4) ? h : -h);
			m.transform_tiny(c[i], q);
		}
		static const int e[12][2] = { {0,1},{2,3},{4,5},{6,7}, {0,2},{1,3},{4,6},{5,7}, {0,4},{1,5},{2,6},{3,7} };
		for (int k = 0; k < 12; k++)
			dbg_line(c[e[k][0]], c[e[k][1]], color, true);
	};

	auto& p = Device.m_SecondViewport;
	draw_lens(p.eyepiece, 0xff0000ff);   // eyepiece blue
	// objective yellow at the CAMERA (the real entrance the scope views from), the stored p.objective.m_W
	// is a forward math intermediate (it derives the camera pull-back d), not the visible front lens
	CRenderDevice::CSecondVPParams::Lens objAtCam = p.objective;
	objAtCam.m_W = scope_camera;
	draw_lens(objAtCam, 0xffffff00);     // objective yellow (at the camera/entrance)
	draw_camera(0xffffffff);             // scope cam white
	// pip magenta cube at the live SVP camera position, updates each frame so the camera move between
	// svpscope 1 (eyepiece) and 2 (objective) is obvious at a glance
	draw_cube(scope_camera, p.eyepiece.radius * 0.6f, 0xffff00ff);

}

// pip STUB sub-pixel jitter for the SVP scene projection (DLSS scaffolding), swap for Ascii's
// shared helper. Halton(2,3), 16-sample phase, returns a centered offset in [-0.5,0.5] px
static float svp_halton(u32 i, u32 b)
{
	float f = 1.0f, r = 0.0f;
	while (i > 0) { f /= (float)b; r += f * (float)(i % b); i /= b; }
	return r;
}
static Fvector2 svp_jitter_offset(u32 frame)
{
	const u32 phase = 16; // TODO match Ascii's confirmed sequence length
	u32 i = (frame % phase) + 1; // Halton is 1-based
	Fvector2 o;
	o.set(svp_halton(i, 2) - 0.5f, svp_halton(i, 3) - 0.5f);
	return o;
}
// apply a pixel jitter to a projection by shifting the post-perspective NDC center, the axis/sign/
// remap convention lives here so the eval swap is one line
static void svp_apply_jitter(Fmatrix& proj, Fvector2 px, float w, float h)
{
	proj.m[2][0] += 2.0f * px.x / w;  // NDC x, clip.x gains z * this, the w-divide cancels z
	proj.m[2][1] -= 2.0f * px.y / h;  // NDC y, negated for texture-down
}

// pip build the SVP camera (fills Device.matrices[1]) from the eyepiece/objective lens + the
// weapon's SVP zoom factor (hud_params.y), called during the MAIN renderGBuffer after the lens
// depth is derived so matrices[1] is ready before TargetSVP->SetActive() reads it
void svpCamera()
{
	float svp_fov = g_pGamePersistent->m_pGShaderConstants->hud_params.y * 0.75f;
	float _, fov, fNearPlane, fFarPlane;
	Device.matrices[0].mProject.decompose_projection(fov, _, fNearPlane, fFarPlane);
	// a zoom-0 tube sight (1x thermal/nv) has no zoom fov and re-images at 1x, the near-0 value
	// would also blow up the vFov/offset tan() math
	if (svp_fov < 1.0f) svp_fov = rad2deg(fov);


	auto mm = Device.matrices[0];
	auto& params = Device.m_SecondViewport;

	// project the eyepiece top/bottom into NDC to find the extra magnification that corrects
	// the on-screen scope size
	Fvector4 top, bot;
	Fmatrix m_WVP = Fmatrix().mul(mm.mProjectHud, Fmatrix().mul(mm.mView, params.eyepiece.m_W));
	m_WVP.transform(top, {0, params.eyepiece.radius, 0, 1});
	m_WVP.transform(bot, {0, -params.eyepiece.radius, 0, 1});
	top.div(top.w);
	bot.div(bot.w);
	float scope_height_NDC = abs(top.y - bot.y);
	// ADS transition frames can project the eyepiece to a degenerate height, floor it
	if (scope_height_NDC < 0.001f) scope_height_NDC = 0.001f;
	float screen_height_NDC = 2.0f;
	float ratio_magnification = screen_height_NDC / scope_height_NDC;

	// the magnification of the scope (1X 4X etc)
	float scope_magnification = fov / deg2rad(svp_fov);

	// expose engine magnification so the shader has a curMag with no 3DSS config
	extern float g_pip_scope_magnification;
	extern float g_pip_scope_min_mag;
	extern float g_pip_scope_max_mag;
	extern float g_pip_scope_ratio;
	if (svp_fov > EPS)
	{
		g_pip_scope_magnification = scope_magnification;
		// eyepiece-fit factor, rated on-screen magnification = ratio * scope, clamped for degenerate geometry
		g_pip_scope_ratio = (ratio_magnification > 0.5f) ? ((ratio_magnification < 8.f) ? ratio_magnification : 8.f) : 1.f;
		// derive min/max mag from hud_fov_params for variable reticles (fixed scope: x == y)
		const Fvector4& fovp = g_pGamePersistent->m_pGShaderConstants->hud_fov_params;
		g_pip_scope_max_mag = (fovp.x > EPS) ? fov / deg2rad(fovp.x * 0.75f) : scope_magnification;
		g_pip_scope_min_mag = (fovp.y > EPS) ? fov / deg2rad(fovp.y * 0.75f) : scope_magnification;
	}

	// the fov we render at to get the correct zoom
	float vFov = 2.0f * atan(tan(fov * 0.5f) / (ratio_magnification * scope_magnification));

	auto near_plane = fNearPlane;
	auto m_W_svpcam = params.eyepiece.m_W; // svpscope 1 places the camera on the eyepiece
	// svpscope 2: scope rendered from the eye center of projection, orientation stays the
	// optical axis, near-field parallax matches the outside view
	if (scope_svp_enabled >= 2)
	{
		Fmatrix eyeW; eyeW.invert(Device.matrices[0].mView);
		m_W_svpcam.c.set(eyeW.c);
	}

	// pip roll_stabilize: level the SVP camera to world up so a canted scope renders upright (0 = realistic tilt)
	extern int ps_r__svp_roll_stabilize;
	if (ps_r__svp_roll_stabilize)
	{
		Fvector fwd, wup, right, up;
		fwd.set(m_W_svpcam.k.x, m_W_svpcam.k.y, m_W_svpcam.k.z);
		fwd.normalize();
		wup.set(0.f, 1.f, 0.f);
		right.crossproduct(wup, fwd);
		if (right.magnitude() > EPS_S)
		{
			right.normalize();
			up.crossproduct(fwd, right);
			up.normalize();
			m_W_svpcam.i.x = right.x; m_W_svpcam.i.y = right.y; m_W_svpcam.i.z = right.z;
			m_W_svpcam.j.x = up.x;    m_W_svpcam.j.y = up.y;    m_W_svpcam.j.z = up.z;
			m_W_svpcam.k.x = fwd.x;   m_W_svpcam.k.y = fwd.y;   m_W_svpcam.k.z = fwd.z;
		}
	}


	auto aspect = RImplementation.TargetSVP->Width / RImplementation.TargetSVP->Height; // square == 1

	float fNearPlane_hud, fFarPlane_hud;
	Device.matrices[0].mProject.decompose_projection(_, _, fNearPlane_hud, fFarPlane_hud);
	auto svp_proj = Fmatrix().build_projection(vFov, aspect, near_plane, fFarPlane);
	auto svp_proj_hud = Fmatrix().build_projection(vFov, aspect, near_plane, fFarPlane_hud);

	// pip DLSS jitter the SVP scene projection (gated), {0,0} otherwise, applied to mProject only
	Device.m_SecondViewport.svp_jitter_px.set(0, 0);
	if (ps_r__svp_dlss != 0)
	{
		const u32 jf = Device.dwFrame; // latch once, stable on the render thread this frame
		Fvector2 jpx = svp_jitter_offset(jf);
		svp_apply_jitter(svp_proj, jpx, (float)RImplementation.TargetSVP->Width, (float)RImplementation.TargetSVP->Height);
		Device.m_SecondViewport.svp_jitter_px = jpx;
	}

	// the eyepiece.radius > EPS gate is the fix for the overlay lines lingering after the scope is
	// removed, an un-scoped frame zeroes the radius so debug_scope is skipped and the lines clear
	if (scope_debug >= 2 && params.eyepiece.radius > EPS)
		debug_scope(m_W_svpcam);

	Device.matrices[1].mView.invert(m_W_svpcam);
	Device.matrices[1].mProject = svp_proj;
	Device.matrices[1].mProjectHud = svp_proj_hud;

	// pip cache the SVP scene constants for the DLSS eval inputs (render thread, written then read the
	// same frame, inert at gate 0). svp_fov is radians from the projection, the basis is the camera world
	{
		auto& vp = Device.m_SecondViewport;
		Device.matrices[1].mProject.decompose_projection(vp.svp_fov, vp.svp_aspect, vp.svp_near, vp.svp_far);
		vp.svp_cam_pos = m_W_svpcam.c;
		vp.svp_right = m_W_svpcam.i;
		vp.svp_up = m_W_svpcam.j;
		vp.svp_fwd = m_W_svpcam.k;
	}

	// pip optics diagnostic: throttled [SVPCOP] log of the camera center-of-projection offset from the
	// eye, settled frames only (ADS transitions blow up ratio_magnification)
	extern int ps_r__svp_cop_diag;
	if (ps_r__svp_cop_diag && params.eyepiece.radius > EPS
		&& ratio_magnification > 1.0f && ratio_magnification < 4.0f)
	{
		static u32 s_last_ms = 0;
		static float s_last_mag = 0.f;
		const float eff_mag = ratio_magnification * scope_magnification;
		const bool mag_moved = (s_last_mag < EPS) || (fabsf(eff_mag - s_last_mag) > 0.03f * s_last_mag);
		if (Device.dwTimeGlobal - s_last_ms > 400 || mag_moved)
		{
			s_last_ms = Device.dwTimeGlobal;
			s_last_mag = eff_mag;
			Fmatrix eyeW; eyeW.invert(Device.matrices[0].mView);
			Fvector camdir; camdir.set(eyeW.k); camdir.normalize();
			Fvector d; d.sub(m_W_svpcam.c, eyeW.c);
			const float fwd = d.dotproduct(camdir);
			Fvector fwd_v; fwd_v.set(camdir); fwd_v.mul(fwd);
			Fvector lat_v; lat_v.sub(d, fwd_v);
			Fvector eyefwd; eyefwd.set(params.eyepiece.m_W.k); eyefwd.normalize();
			Fvector od; od.sub(params.objective.m_W.c, params.eyepiece.m_W.c);
			// signed scope cant vs world up, proves whether lean actually rolls the weapon on a rig
			float cant = 0.f;
			{
				Fvector jup; jup.set(params.eyepiece.m_W.j); jup.normalize();
				Fvector lvl; lvl.set(0.f, 1.f, 0.f); lvl.mad(eyefwd, -lvl.dotproduct(eyefwd));
				if (lvl.magnitude() > EPS)
				{
					lvl.normalize();
					Fvector cx; cx.crossproduct(lvl, jup);
					cant = rad2deg(atan2f(cx.dotproduct(eyefwd), lvl.dotproduct(jup)));
				}
			}
			Msg("[SVPCOP] mode=%d mag=%.3f eff=%.3f min=%.3f max=%.3f ratio=%.3f svpfov=%.2f vfov=%.2f cop_cm=%.2f fwd_cm=%.2f lat_cm=%.2f eye_r_cm=%.2f obj_fwd_cm=%.2f obj_r_cm=%.2f cant=%.1f",
				scope_svp_enabled, scope_magnification, eff_mag, g_pip_scope_min_mag, g_pip_scope_max_mag, ratio_magnification,
				svp_fov, rad2deg(vFov), d.magnitude() * 100.f, fwd * 100.f, lat_v.magnitude() * 100.f,
				params.eyepiece.radius * 100.f, od.dotproduct(eyefwd) * 100.f, params.objective.radius * 100.f,
				cant);

			// barrel-continuity probe: one weapon-fixed point (the objective) through both pipelines,
			// a steady delta while sway swings = static projection mismatch, delta tracking sway = lag
			if (params.svp_disc_px > 1.f && params.svp_fov > EPS)
			{
				auto to_px = [](const Fvector& w, const Fmatrix& v, const Fmatrix& pr, float W, float H, Fvector2& o) -> bool {
					Fmatrix vpm; vpm.mul(pr, v);
					Fvector4 c; vpm.transform(c, {w.x, w.y, w.z, 1});
					if (c.w < EPS) return false;
					o.set((c.x / c.w * 0.5f + 0.5f) * W, (0.5f - c.y / c.w * 0.5f) * H);
					return true;
				};
				float dt_cd = camdir.dotproduct(eyefwd);
				clamp(dt_cd, -1.f, 1.f);
				const float sway = rad2deg(acosf(dt_cd));
				float hud_fov_d = params.svp_fov;
				extern int ps_r__svp_hud_fov_match;
				if (ps_r__svp_hud_fov_match == 1 && g_pip_scope_ratio > EPS)
				{
					float hf, _a2, _n2, _f2;
					Device.matrices[0].mProjectHud.decompose_projection(hf, _a2, _n2, _f2);
					if (hf > EPS) hud_fov_d = 2.f * atanf(tanf(hf * 0.5f) / g_pip_scope_ratio);
				}
				Fmatrix hp; hp.build_projection(hud_fov_d, params.svp_aspect, 0.10f, params.svp_far);
				Fmatrix hv = Device.matrices[1].mView;
				Fvector hvd; hvd.sub(params.eyepiece.m_W.c, m_W_svpcam.c);
				if (ps_r__svp_hud_fov_match && hvd.magnitude() > 0.01f)
					hv.build_camera(m_W_svpcam.c, params.eyepiece.m_W.c, params.eyepiece.m_W.j);
				Fvector2 px_out, uv_in, px_disc;
				if (to_px(params.objective.m_W.c, Device.matrices[0].mView, Device.matrices[0].mProjectHud, float(Device.dwWidth), float(Device.dwHeight), px_out)
					&& to_px(params.objective.m_W.c, hv, hp, 1.f, 1.f, uv_in)
					&& to_px(params.eyepiece.m_W.c, Device.matrices[0].mView, Device.matrices[0].mProjectHud, float(Device.dwWidth), float(Device.dwHeight), px_disc))
				{
					const float bx = px_disc.x + (uv_in.x - 0.5f) * params.svp_disc_px;
					const float by = px_disc.y + (uv_in.y - 0.5f) * params.svp_disc_px;
					Msg("[SVP-BARREL] sway=%.2fdeg fovmatch=%d out=(%.0f,%.0f) in=(%.0f,%.0f) delta=(%.0f,%.0f)px",
						sway, ps_r__svp_hud_fov_match, px_out.x, px_out.y, bx, by, bx - px_out.x, by - px_out.y);
				}
			}
		}
	}

	// pip [SVP-AIM] per-frame reticle-vs-screen-center delta (r__svp_cop_diag 2): the reticle rides
	// the optic (disc center), bullets follow the camera ray to the screen center, d = the live gap
	if (ps_r__svp_cop_diag >= 2 && params.eyepiece.radius > EPS)
	{
		Fmatrix vpm; vpm.mul(Device.matrices[0].mProjectHud, Device.matrices[0].mView);
		Fvector4 rc; vpm.transform(rc, {params.eyepiece.m_W.c.x, params.eyepiece.m_W.c.y, params.eyepiece.m_W.c.z, 1});
		if (rc.w > EPS)
		{
			const float rx = (rc.x / rc.w * 0.5f + 0.5f) * float(Device.dwWidth);
			const float ry = (0.5f - rc.y / rc.w * 0.5f) * float(Device.dwHeight);
			const float dx = rx - float(Device.dwWidth) * 0.5f;
			const float dy = ry - float(Device.dwHeight) * 0.5f;
			Msg("[SVP-AIM] reticle=(%.1f,%.1f) d=(%.1f,%.1f)px |d|=%.1f", rx, ry, dx, dy, sqrtf(dx * dx + dy * dy));
		}

		// visual confirm for the same question: GREEN cross = the ballistic point (the camera ray,
		// where bullets go), RED cross = the scope reticle center (the optic), both hud-projected
		{
			Fmatrix eyeW2; eyeW2.invert(Device.matrices[0].mView);
			Fvector fwd; fwd.set(eyeW2.k); fwd.normalize();
			Fvector ex; ex.sub(params.eyepiece.m_W.c, eyeW2.c);
			const float depth = ex.magnitude();
			Fvector ctr; ctr.set(eyeW2.c); ctr.mad(fwd, depth);
			const float s = depth * 0.02f;
			auto cross = [&](const Fvector& p, u32 color) {
				Fvector a, b;
				a.set(p); a.mad(eyeW2.i, -s); b.set(p); b.mad(eyeW2.i, s);
				Fvector v1[2] = { a, b }; u16 i1[2] = { 0, 1 };
				DRender->add_lines(v1, 2, i1, 1, color, true);
				a.set(p); a.mad(eyeW2.j, -s); b.set(p); b.mad(eyeW2.j, s);
				Fvector v2[2] = { a, b }; u16 i2[2] = { 0, 1 };
				DRender->add_lines(v2, 2, i2, 1, color, true);
			};
			cross(ctr, 0xff00ff00);
			cross(params.eyepiece.m_W.c, 0xffff0000);
		}
	}

	// pip one-shot config fingerprint on the first scoped frame so any tester log diffs against ours
	{
		static bool s_cfg_logged = false;
		if (!s_cfg_logged)
		{
			s_cfg_logged = true;
			extern float ps_r__svp_render_scale, ps_r__svp_supersample, ps_r__svp_adaptive_res, ps_r__svp_lod,
				ps_r__svp_cull_ssa, ps_r__svp_obj_dist, ps_r__svp_obj_size,
				ps_r__svp_near_blur, ps_r__svp_hud_sway_comp;
			extern int ps_r__svp_dlss, ps_r__svp_cull, ps_r__svp_cull_grass, ps_r__svp_skip_grass,
				ps_r__svp_skip_motionblur, ps_r__svp_skip_ssr, ps_r__svp_skip_volumetric, ps_r__svp_sss_sun,
				ps_r__svp_clean_optics, ps_r__truepip_recoil;
			extern int ps_r__svp_roll_stabilize;
			extern int ps_r__svp_hud_fov_match;
			extern int ps_r__svp_hud_full;
			extern float ps_r__svp_twilight, ps_r__svp_parallax;
			Msg("[SVP-CFG] build %s mode=%d fovm=%d hfull=%d nblur=%.1f swc=%.1f clean=%d bs=%d roll=%d scale=%.2f ss=%.2f ares=%.2f lod=%.2f cull=%d ssa=%.1f cullgrass=%d skipgrass=%d skipmb=%d skipssr=%d skipvol=%d sss=%d objd=%.2f objs=%.2f dlss=%d recoil=%d twl=%.2f par=%.2f",
				__DATE__, scope_svp_enabled, ps_r__svp_hud_fov_match, ps_r__svp_hud_full, ps_r__svp_near_blur, ps_r__svp_hud_sway_comp, ps_r__svp_clean_optics, ps_r__svp_boresight, ps_r__svp_roll_stabilize,
				ps_r__svp_render_scale, ps_r__svp_supersample, ps_r__svp_adaptive_res,
				ps_r__svp_lod, ps_r__svp_cull, ps_r__svp_cull_ssa, ps_r__svp_cull_grass, ps_r__svp_skip_grass,
				ps_r__svp_skip_motionblur, ps_r__svp_skip_ssr, ps_r__svp_skip_volumetric, ps_r__svp_sss_sun,
				ps_r__svp_obj_dist, ps_r__svp_obj_size,
				ps_r__svp_dlss, ps_r__truepip_recoil, ps_r__svp_twilight, ps_r__svp_parallax);
		}
	}
}

// pip front/second focal-plane world points (scope_w_ffp/sfp); the scope shader projects the SVP image through them
void ffp_sfp()
{
	auto e = Device.m_SecondViewport.eyepiece;
	auto o = Device.m_SecondViewport.objective;

	Fvector p_e = {0, 0, 0}; e.m_W.transform(p_e);
	Fvector p_o = {0, 0, 0}; o.m_W.transform(p_o);

	if (o.radius < EPS)
	{
		// no objective captured, make one up a scope-like distance in front of the eyepiece
		float distance = 10 * 0.01f;
		o.radius = e.radius;
		p_o = {0, 0, distance};
		e.m_W.transform(p_o);
	}

	{
		// reproject the objective directly in front of the eyepiece (not all scopes are inline)
		float distance = p_o.distance_to(p_e);
		Fvector dir = {0, 0, 1};
		o.m_W.transform_dir(dir);
		p_o.set(dir.mul(distance).add(p_e));
	}

	Fvector p_d = Fvector(p_o).sub(p_e);
	Fvector p_c1 = Fvector(p_d).mul(0.4f).add(p_e);
	Fvector p_c2 = Fvector(p_d).mul(0.6f).add(p_e);

	Device.m_SecondViewport.w_ffp = Fvector(p_c1).add(p_e).mul(0.5f);
	Device.m_SecondViewport.w_sfp = Fvector(p_c2).add(p_o).mul(0.5f);
}

// pip derive the scope eyepiece (and the objective lens from the offset cvar) from the captured
// scope-lens meshes, sets Device.m_SecondViewport.eyepiece/objective which svpCamera and the weapon
// SVP activation gate (GetSVPCameraMatrix) consume, called on the main pass after the HUD is captured
static xr_vector<Fvector4> g_pip_hud_geom; // pip diag: snapshot of HUD geometry centers (xyz) + radius (w), captured before render_hud clears the lists

void CRender::deriveScopeLens()
{
	// multi-lens weapons carry several scope-lens meshes (markswitch variants, addon + builtin),
	// pick the aimed one: visible lens bone, nearest the camera ray. hidden markswitch lenses sit
	// on the same axis closer to the eye and must never win
	const void* best = nullptr;
	{
		extern int ps_r__svp_cop_diag;
		static u32 s_lens_diag_ms = 0;
		const bool diag = ps_r__svp_cop_diag && (Device.dwTimeGlobal - s_lens_diag_ms > 3000);
		if (diag)
			s_lens_diag_ms = Device.dwTimeGlobal;
		float best_score = 1e9f;
		const Fvector cam_p = Device.vCameraPosition;
		const Fvector cam_f = Device.vCameraDirection;
		for (auto& N : GMBase.RGraph.mapScopeHUDSorted)
		{
			if (!N.pVisual || !N.pMatrix)
				continue;
			Fmatrix lensX = *N.pMatrix;
			bool bone_vis = true;
			CSkeletonX* sk = fast_dynamic_cast<CSkeletonX*>(N.pVisual);
			if (sk)
			{
				bone_vis = sk->SVP_LensBoneVisible();
				Fmatrix boneR;
				if (sk->SVP_LensBoneXform(boneR))
					lensX.mulB_43(boneR);
			}
			auto& V = N.pVisual->getVisData();
			Fvector c; V.box.getcenter(c);
			Fvector cw; lensX.transform_tiny(cw, c);
			Fvector d; d.sub(cw, cam_p);
			const float dist = d.magnitude();
			if (dist < 0.01f)
				continue;
			d.div(dist);
			const float fwd = d.dotproduct(cam_f);
			if (diag)
			{
				auto tx = N.pVisual->GetTexture();
				Msg("[SVP-LENS] %s dist=%.1fcm fwd=%.3f r=%.1fcm %s",
					bone_vis ? "vis " : "HIDE", dist * 100.f, fwd, V.sphere.R * 100.f,
					tx ? tx->cName.c_str() : "?");
			}
			if (!bone_vis || fwd < 0.2f)
				continue;
			const float score = (1.f - fwd) + dist * 0.02f;
			if (score < best_score)
			{
				best_score = score;
				best = &N;
			}
		}
	}

	for (auto& N : GMBase.RGraph.mapScopeHUDSorted)
	{
		if (&N != best)
			continue;

		// a skinned scope lens is positioned by its bone, the captured matrix is only the kinematics
		// root, fold in the lens bone skinning matrix so the eyepiece follows the glass on ADS and sway
		Fmatrix lensX = *N.pMatrix;
		CSkeletonX* sk = fast_dynamic_cast<CSkeletonX*>(N.pVisual);
		if (sk)
		{
			Fmatrix boneR;
			if (sk->SVP_LensBoneXform(boneR))
				lensX.mulB_43(boneR);
		}

		auto& V = N.pVisual->getVisData();
		Fvector c;
		V.box.getcenter(c); // AABB center fits a flat lens disc tighter than the bounding sphere center
		Fmatrix m_W = lensX;
		m_W.mulB_43(Fmatrix().translate(c));

		auto* p = &Device.m_SecondViewport;
		p->eyepiece.m_W = m_W;
		p->eyepiece.radius = V.sphere.R;

		if (p->eyepiece.radius > EPS)
		{
			// pip objective: prefer the REAL front lens captured from the mesh (mapScopeHUDObjective)
			// place it at the real front-lens position but along the optical axis so the
			// orientation stays consistent with the eyepiece, fall back to the legacy fixed offset only
			// when the scope flags a single lens surface (objective == ocular)
			// automatic objective distance (geomscan): scan the HUD geometry snapshot (taken before
			// render_hud cleared the lists) for the forward-most on-axis node + its radius = the
			// objective glass plane, in eyepiece radii, clamped, fed to the geometric fallback below
			float geom_front = -1.f;
			{
				const Fvector eye = p->eyepiece.m_W.c;
				Fvector axis; axis.set(p->eyepiece.m_W.k); axis.normalize();
				const float rr = p->eyepiece.radius;
				if (rr > EPS)
				{
					float fr = -1e9f;
					for (auto& g : g_pip_hud_geom)
					{
						Fvector wc; wc.set(g.x, g.y, g.z);
						Fvector d; d.sub(wc, eye);
						const float fwd = d.dotproduct(axis);
						if (fwd <= 0.f) continue;
						Fvector proj; proj.mad(eye, axis, fwd);
						const float perp = wc.distance_to(proj);
						if (perp < rr * 2.0f && (fwd + g.w) > fr) fr = fwd + g.w;
					}
					if (fr > 0.f) { geom_front = fr / rr; if (geom_front < 4.f) geom_front = 4.f; else if (geom_front > 30.f) geom_front = 30.f; }
				}
			}

			bool have_obj = false;
			float dbg_cand_dist = -1.f; // objective-capture distance, used by the capture gate below
			for (auto& N : GMBase.RGraph.mapScopeHUDObjective)
			{
				if (!N.pVisual || !N.pMatrix)
					break;
				Fmatrix oX = *N.pMatrix;
				if (CSkeletonX* sk = fast_dynamic_cast<CSkeletonX*>(N.pVisual))
				{
					Fmatrix boneR;
					if (sk->SVP_LensBoneXform(boneR))
						oX.mulB_43(boneR);
				}
				auto& OV = N.pVisual->getVisData();
				Fvector oc; OV.box.getcenter(oc);
				Fvector ow; oX.transform_tiny(ow, oc);
				dbg_cand_dist = ow.distance_to(p->eyepiece.m_W.c);
				// distinct from the eyepiece (a single-lens scope captures the same disc for both)
				if (OV.sphere.R > EPS && dbg_cand_dist > p->eyepiece.radius * 0.5f)
				{
					p->objective.m_W = p->eyepiece.m_W; // stabilized optical axis
					p->objective.m_W.c.set(ow);          // real front-lens world position
					p->objective.radius = OV.sphere.R;
					have_obj = true;
				}
				break;
			}
			if (!have_obj)
			{
				// no distinct objective lens in the mesh (single-lens scope, the common case), derive it
				// geometrically along the optical axis: a scope length forward of the eyepiece, sized
				// relative to it, eyepiece radius is the only mesh-scale-robust unit, refined per scope
				// from real objective_mm later
				Fvector fwd; fwd.set(p->eyepiece.m_W.k); fwd.normalize();
				p->objective.m_W = p->eyepiece.m_W;
				const float dist_r = (geom_front > 0.f ? geom_front : 14.0f) * ps_r__svp_obj_dist;
				p->objective.m_W.c.mad(fwd, p->eyepiece.radius * dist_r);
				p->objective.radius = p->eyepiece.radius * ps_r__svp_obj_size; // eyepiece-relative objective radius (one global knob): the front-node geomscan swung ~6x conflating glass vs bell housing, eyepiece radius is the only mesh-scale-stable unit
			}
			ffp_sfp(); // focal-plane points for the scope shader
		}
		break; // the first captured lens is the eyepiece
	}

	// pip DLSS reset when the lens first becomes valid (active can flip a frame before the capture),
	// render-thread edge state, inert at gate 0
	bool lens_valid = (Device.m_SecondViewport.eyepiece.radius > EPS);
	if (lens_valid && !Device.m_SecondViewport.m_lens_prev_valid)
		Device.m_SecondViewport.dlss_reset_next = true;
	Device.m_SecondViewport.m_lens_prev_valid = lens_valid;
}

void CRender::render_menu()
{
	PIX_EVENT(render_menu);
	//	Globals
	RCache.set_CullMode(CULL_CCW);
	RCache.set_Stencil(FALSE);
	RCache.set_ColorWriteEnable();

	// Main Render
	{
		Target->u_setrt(Target->rt_Generic_0, 0, 0, HW.pBaseZB); // LDR RT
		g_pGamePersistent->OnRenderPPUI_main(); // PP-UI
	}

	// Distort
	{
		FLOAT ColorRGBA[4] = {127.0f / 255.0f, 127.0f / 255.0f, 0.0f, 127.0f / 255.0f};
		Target->u_setrt(Target->rt_Generic_1, 0, 0, HW.pBaseZB); // Now RT is a distortion mask
		HW.pContext->ClearRenderTargetView(Target->rt_Generic_1->pRT, ColorRGBA);
		g_pGamePersistent->OnRenderPPUI_PP(); // PP-UI
	}

	// Actual Display
	Target->u_setrt(Device.dwWidth, Device.dwHeight, HW.pBaseRT,NULL,NULL, HW.pBaseZB);
	RCache.set_Shader(Target->s_menu);
	RCache.set_Geometry(Target->g_menu);

	Fvector2 p0, p1;
	u32 Offset;
	auto C = color_rgba(255, 255, 255, 255);
	float _w = float(Device.dwWidth);
	float _h = float(Device.dwHeight);
	float d_Z = EPS_S;
	float d_W = 1.f;
	p0.set(.5f / _w, .5f / _h);
	p1.set((_w + .5f) / _w, (_h + .5f) / _h);

	FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, Target->g_menu->vb_stride, Offset);
	pv->set(EPS, float(_h + EPS), d_Z, d_W, C, p0.x, p1.y);
	pv++;
	pv->set(EPS, EPS, d_Z, d_W, C, p0.x, p0.y);
	pv++;
	pv->set(float(_w + EPS), float(_h + EPS), d_Z, d_W, C, p1.x, p1.y);
	pv++;
	pv->set(float(_w + EPS), EPS, d_Z, d_W, C, p1.x, p0.y);
	pv++;
	RCache.Vertex.Unlock(4, Target->g_menu->vb_stride);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
}

extern u32 g_r;

void CRender::Render()
{
	PIX_EVENT(CRender_Render);

	rmNormal();

	bool _menu_pp = g_pGamePersistent ? g_pGamePersistent->OnRenderPPUI_query() : false;
	if (_menu_pp)
	{
		render_menu();
		return;
	};

	IMainMenu* pMainMenu = g_pGamePersistent ? g_pGamePersistent->m_pMainMenu : 0;
	bool bMenu = pMainMenu ? pMainMenu->CanSkipSceneRendering() : false;

	if (!(g_pGameLevel && g_hud)
		|| bMenu)
	{
		Target->u_setrt(Device.dwWidth, Device.dwHeight, HW.pBaseRT,NULL,NULL, HW.pBaseZB);
		return;
	}

	if (m_bFirstFrameAfterReset)
	{
		for (light* L : v_all_lights)//critical!!!
			L->m_moving_frames = 0;

		m_bFirstFrameAfterReset = false;
		return;
	}

	//.	VERIFY					(g_pGameLevel && g_pGameLevel->pHUD);

	// Configure
	RImplementation.o.distortion = FALSE; // disable distorion
	Fcolor sun_color = ((light*)Lights.sun_adapted._get())->color;
	BOOL bSUN = ps_r2_ls_flags.test(R2FLAG_SUN) && (u_diffuse2s(sun_color.r, sun_color.g, sun_color.b)>EPS) && !Core.ParamsData.test(ECoreParams::r4_dev);
	if (o.sunstatic) bSUN = FALSE;
	// Msg						("sstatic: %s, sun: %s",o.sunstatic?;"true":"false", bSUN?"true":"false");

	// HOM
	ViewBase.CreateFromMatrix(Device.mFullTransform, FRUSTUM_P_LRTB + FRUSTUM_P_FAR);
    HOM.MT_RENDER();

	//******* Main calc - DEFERRER RENDERER
	phase = PHASE_NORMAL;
	
	/*if (RImplementation.o.ssfx_core) // SSS23: DEPRECATED
	{
		// HUD Masking rendering
		FLOAT ColorRGBA[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
		HW.pContext->ClearRenderTargetView(Target->rt_ssfx_hud->pRT, ColorRGBA);

		Target->u_setrt(Target->rt_ssfx_hud, NULL, NULL, HW.pBaseZB);
		r_dsgraph_render_hud(true);

		// Reset Depth
		HW.pContext->ClearDepthStencilView(HW.pBaseZB, D3D_CLEAR_DEPTH, 1.0f, 0);
	}*/

    GMBase.traverse(RImplementation.pLastSector, ViewBase, Device.vCameraPosition, Device.mFullTransform);
    GMBase.r_dsgraph_capture_static();
    GMBase.r_dsgraph_capture_dynamic();

	// pip reset the per-frame lens, it is re-derived each frame so an un-scoped frame leaves radius 0
	Device.m_SecondViewport.eyepiece.radius = 0;
	Device.m_SecondViewport.objective.radius = 0;

	// pip a stale hook must never survive into this frame (it captures this + TargetSVP)
	Device.m_SecondViewport.dual_accum = nullptr;
	// pip double-pass, the captured graph renders once for the main viewport and, when a scope drives
	// the SVP, a second time into TargetSVP, true_pip off keeps the single stock main pass
	bool svp = Device.true_pip_on && Device.m_SecondViewport.IsSVPActive();
	// pip lock the adaptive SVP size at ADS-in (svp false -> true) from the disc learned on the last aim, so the
	// target is sized once per aim and never resized mid-ADS (the live disc keeps learning for next time)
	static bool s_prev_svp = false;
	if (svp && !s_prev_svp)
		Device.m_SecondViewport.svp_disc_applied = Device.m_SecondViewport.svp_disc_px;
	s_prev_svp = svp;
	// pip allocate the SVP target before the main pass derives the camera into it (lazy, only while a
	// PiP scope is aimed, never when off)
	if (Device.true_pip_on && g_pGamePersistent &&
		g_pGamePersistent->m_pGShaderConstants->hud_params.y > 0.005f)
		EnsureTargetSVP();
	// creation can fail at VRAM exhaustion, fall back to the single stock pass this frame
	if (svp && !TargetSVP)
		svp = false;
	if (Device.true_pip_on)
		TargetMain->SetActive();
	renderGBuffer(!svp); // keep the priority-0 graph when an SVP pass follows
	if (svp)
	{
		EnsureTargetSVP();
		// pip CPU-side cost probe for the SVP gbuffer, throttled log while r__svp_diag is on
		extern int ps_r__svp_diag;
		CTimer svp_t; svp_t.Start();
		const u32 calls0 = RCache.stat.calls;
		const u32 verts0 = RCache.stat.verts;
		TargetSVP->SetActive();
		renderGBuffer(true);
		if (ps_r__svp_diag)
		{
			static u32 s_perf_ms = 0;
			if (Device.dwTimeGlobal - s_perf_ms > 1000)
			{
				s_perf_ms = Device.dwTimeGlobal;
				Msg("[SVP-PERF] gbuffer %.2fms calls %u verts %uk", svp_t.GetElapsed_sec() * 1000.f,
					RCache.stat.calls - calls0, (RCache.stat.verts - verts0) / 1000);
			}
		}
		TargetMain->SetActive(); // shadow generation + main accumulation run on the main target

		// pip install the dual-accumulate hook, each shadow unit builds its map once on the main atlas
		// then this re-accumulates it into the SVP with no second shadow render
		Device.m_SecondViewport.dual_accum = [this](const std::function<void()>& accum)
		{
			TargetSVP->SetActive();   // SVP gbuffer + accumulator (and, for now, the SVP shadow atlas)
			share_main_smaps();       // re-point the shadow atlas at the main maps the generation built
			extern int ps_r__svp_sss_sun;
			Device.m_SecondViewport.force_svp_sss = (ps_r__svp_sss_sun != 0); // sun keeps the SSS contact term
			{ PIX_EVENT(SVP_ACCUM); accum(); } // SVP marginal lighting cost: accumulate this unit into the SVP (shared maps)
			Device.m_SecondViewport.force_svp_sss = false;
			TargetMain->SetActive();  // restore for the next unit's generation on the main atlas
		};
	}

	// single shared lighting pass, when svp the render_sun_cascades/render_lights dual-accumulate via
	// the hook and the SVP is combined + captured before the main combine composites it into the lens
	renderSceneLighting(bSUN, svp);
	Device.m_SecondViewport.dual_accum = nullptr;
}

void CRender::renderGBuffer(bool clearGraph)
{
	// label the pass so the SVP (scope) gbuffer is distinguishable from the main one in a capture
	PIX_EVENT_F("RENDER_GBUFFER[%s]", Target == TargetMain ? "MAIN" : "SVP");
	Device.dwViewport++; // pip per-viewport cache counter

	// pip cull the SVP geometry to the scope cone, the captured graph is main frustum so the SVP would
	// otherwise resubmit the whole world through a cone that sees a fraction
	extern int ps_r__svp_cull, ps_r__svp_skip_grass, ps_r__svp_cull_grass;
	const bool svp_pass = (Target == TargetSVP) && Device.true_pip_on;
	const bool svp_cull = svp_pass && ps_r__svp_cull;
	const bool svp_cull_grass = svp_pass && ps_r__svp_cull_grass && !ps_r__svp_skip_grass;
	if (svp_cull || svp_cull_grass)
	{
		Fmatrix svp_full;
		svp_full.mul(Device.matrices[1].mProject, Device.matrices[1].mView);
		CDSGraphManager::svp_cull_begin(svp_full, svp_cull);
	}
	// pip SVP coverage = (mag * svp_side / main_height)^2, capped at 1: the fraction of the main view's
	// pixel area an object covers in the scope. feeds the LOD scale and the small-object cull threshold
	extern float ps_r__svp_lod, ps_r__svp_cull_ssa;
	if (svp_pass && TargetSVP && (ps_r__svp_lod > 0.f || ps_r__svp_cull_ssa > 0.f))
	{
		extern float g_pip_scope_magnification;
		const float mh = (float)Device.dwHeight;
		float cov = (mh > 1.f) ? (g_pip_scope_magnification * (float)TargetSVP->Width / mh) : 1.f;
		cov *= cov;
		if (cov > 1.f) cov = 1.f;
		if (ps_r__svp_lod > 0.f)
			CDSGraphManager::svp_set_lod_scale(1.f + (cov - 1.f) * ps_r__svp_lod);
		if (ps_r__svp_cull_ssa > 0.f)
			CDSGraphManager::svp_set_ssa_cull(ps_r__svp_cull_ssa, cov);
	}

	phase = PHASE_NORMAL;
	Target->phase_scene_prepare(); // clears + binds this viewport's gbuffer and depth

    if (RImplementation.o.ssfx_motionvectors)
    {
        Target->u_setrt(Device.dwWidth, Device.dwHeight, 0, 0, Target->rt_ssfx_motion_vectors->pRT, 0);

        FLOAT ColorRGBA[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        HW.pContext->ClearRenderTargetView(Target->rt_ssfx_motion_vectors->pRT, ColorRGBA);

        RCache.set_Stencil(FALSE);
        g_pGamePersistent->Environment().RenderSky(true);

        RCache.Index.Flush();
        RCache.Vertex.Flush();

        RCache.set_xform_world(Fidentity);
    }

	if (ps_r2_ls_flags.test(R2FLAG_TERRAIN_PREPASS))
	{
		Target->u_setrt(Device.dwWidth, Device.dwHeight, NULL, NULL, NULL, !RImplementation.o.dx10_msaa ? HW.pBaseZB : Target->rt_MSAADepth->pZRT);
	}

	//******* Main render :: PART-0	-- first
	{
		PIX_EVENT(DEFER_PART0_SPLIT);
		// level, SPLIT
		Target->phase_scene_begin();
		GMBase.r_dsgraph_render_static(0, clearGraph);
		GMBase.r_dsgraph_render_dynamic(0, clearGraph);
		Target->disable_aniso();
	}

	//  Redotix99: for 3D Shader Based Scopes
	if (scope_3D_fake_enabled && Target == TargetMain) // pip legacy fallback, main view only
	{
		ID3D11Resource* zbuffer_res;
		HW.pBaseZB->GetResource(&zbuffer_res);
		HW.pContext->CopyResource(RImplementation.Target->rt_tempzb->pSurface, zbuffer_res);
	}

	if (RImplementation.o.dx10_msaa)
		RCache.set_ZB(RImplementation.Target->rt_MSAADepth->pZRT);

	if (Target == TargetMain) // pip lights captured once on main, the SVP reuses them
	{
		PIX_EVENT(DEFER_TEST_LIGHT_VIS);
		//******* Occlusion testing of volume-limited light-sources
		Target->phase_occq();
		LP_normal.clear();
		LP_pending.clear();
		GMBase.r_dsgraph_capture_lights();
	}

	//******* Main render :: PART-1 (second)
	{
		PIX_EVENT(DEFER_PART1_SPLIT);
		// level
		Target->phase_scene_begin();
		if (Target == TargetMain) // pip weapon HUD only in the main view, not the scope image
		{
			GMBase.r_dsgraph_capture_hud();
			// pip snapshot HUD geometry centers before render_hud clears the lists, so the geomscan (in
			// deriveScopeLens, after the clear) can auto-derive the objective distance against the optical axis
			if (scope_svp_enabled || scope_debug >= 2)
			{
				g_pip_hud_geom.clear();
				auto snap = [](auto& lst) { for (auto& H : lst) { if (!H.pVisual || !H.pMatrix) continue; auto& VV = H.pVisual->getVisData(); Fvector w; H.pMatrix->transform_tiny(w, VV.sphere.P); Fvector4 e; e.set(w.x, w.y, w.z, VV.sphere.R); g_pip_hud_geom.push_back(e); } };
				snap(GMBase.RGraph.mapHUDSorted.Sorted);
				snap(GMBase.RGraph.mapHUD);
			}
			// keep the weapon list when an SVP pass follows, the scope image drains it second
			GMBase.r_dsgraph_render_hud(clearGraph);

			// pip derive the scope lens then build the SVP camera (matrices[1]) while a PiP scope
			// is aimed, zoom-0 tube sights have no zoom fov so ADS + a captured lens also qualifies
			if (scope_svp_enabled && g_pGamePersistent &&
				(g_pGamePersistent->m_pGShaderConstants->hud_params.y > 0.005f
					|| (g_pGamePersistent->m_pGShaderConstants->hud_params.x > 0.05f
						&& !GMBase.RGraph.mapScopeHUDSorted.empty())))
			{
				deriveScopeLens();
				if (Device.m_SecondViewport.eyepiece.radius > EPS && TargetSVP)
					svpCamera();
			}
		}
		else if (svp_pass) // pip the weapon renders through the scope at low mag, one unit inside and out
		{
			extern float g_pip_scope_magnification;
			extern float g_pip_scope_ratio;
			extern int ps_r__svp_hud_fov_match;
			auto& vp = Device.m_SecondViewport;
			// svpscope 2 only. mode 2 leaves the narrowing cone naturally at mag, the window
			// modes need the low-power gate (a 1:1 barrel never leaves)
			const float eff_mag = g_pip_scope_ratio * g_pip_scope_magnification;
			const bool barrel_mag = (ps_r__svp_hud_fov_match >= 2);
			if (scope_svp_enabled >= 2 && (barrel_mag || eff_mag < 3.0f) && vp.eyepiece.radius > EPS)
			{
				// a scope only sees forward of its entrance pupil, the near plane at the objective
				// clips the tube/receiver/hands and leaves the barrel and attachments
				Fvector od; od.sub(vp.objective.m_W.c, vp.svp_cam_pos);
				float near_obj = od.magnitude();
				if (near_obj < 0.10f) near_obj = 0.10f;
				// full-barrel: skip the scope body meshes and pull the near plane to the eye, the
				// near-blur eats the close mass. thermals read the gbuffer, keep the objective clip
				extern int ps_r__svp_hud_full;
				extern float ps_r__svp_near_blur;
				extern bool g_svp_hud_skip_scope;
				extern Fvector4 ps_s3ds_param_3;
				const bool thermal = ps_s3ds_param_3.x >= 1.5f;
				const bool hud_full = ps_r__svp_hud_full && !thermal && ps_r__svp_near_blur > 0.01f
					&& vp.objective.radius > EPS;
				if (hud_full)
					near_obj = 0.08f;
				// mode 2 = the world fov (barrel magnifies with the wheel), mode 1 window =
				// tan(hud/2)/ratio, a 1:1 continuation of the outside hud render
				float hud_fov = vp.svp_fov;
				if (ps_r__svp_hud_fov_match == 1 && g_pip_scope_ratio > EPS)
				{
					float hf, _a, _n, _f;
					Device.matrices[0].mProjectHud.decompose_projection(hf, _a, _n, _f);
					if (hf > EPS)
						hud_fov = 2.f * atanf(tanf(hf * 0.5f) / g_pip_scope_ratio);
				}
				// anchor on the live ocular: look from the eye through the eyepiece center so the image
				// center lands on the disc's screen position for any eye-off-axis sway
				Fmatrix hud_view = Device.matrices[1].mView;
				if (ps_r__svp_hud_fov_match >= 1)
				{
					Fvector ed; ed.sub(vp.eyepiece.m_W.c, vp.svp_cam_pos);
					if (ed.magnitude() > 0.01f)
					{
						Fvector at = vp.eyepiece.m_W.c;
						// sway comp: steer the camera through (M-1)/M of the transient so fast motion
						// maps 1:1 against the outside barrel, the settled pose keeps full magnification
						extern float ps_r__svp_hud_sway_comp;
						if (barrel_mag && ps_r__svp_hud_sway_comp > 0.001f && near_obj > 0.11f
							&& g_pip_scope_ratio > EPS && vp.svp_fov > EPS)
						{
							Fvector adir = ed; adir.normalize();
							Fvector bdir = od; bdir.normalize();
							Fvector rel; rel.sub(bdir, adir);
							static Fvector s_rel = {0, 0, 0};
							static u32 s_relframe = 0;
							const bool gap = (Device.dwFrame != s_relframe + 1);
							s_relframe = Device.dwFrame;
							float dt = Device.fTimeDelta;
							if (dt > 0.1f) dt = 0.1f;
							if (gap || dt <= 0.f)
								s_rel.set(rel);
							else
							{
								Fvector d; d.sub(rel, s_rel);
								s_rel.mad(d, 1.f - expf(-dt / 0.06f));
							}
							Fvector r; r.sub(rel, s_rel);
							float hf2, _a3, _n3, _f3;
							Device.matrices[0].mProjectHud.decompose_projection(hf2, _a3, _n3, _f3);
							// barrel on-screen magnification vs the outside hud render
							float me = tanf(hf2 * 0.5f) / (g_pip_scope_ratio * tanf(vp.svp_fov * 0.5f));
							clamp(me, 0.25f, 8.f);
							const float k = (me - 1.f) / me * ps_r__svp_hud_sway_comp;
							Fvector nudge; nudge.set(r); nudge.mul(k * ed.magnitude());
							at.add(nudge);
						}
						hud_view.build_camera(vp.svp_cam_pos, at, vp.eyepiece.m_W.j);
					}
				}
				Fmatrix hud_proj;
				hud_proj.build_projection(hud_fov, vp.svp_aspect, near_obj, vp.svp_far);
				RCache.set_xform_view(hud_view);
				RCache.set_xform_project(hud_proj);
				g_svp_hud_skip_scope = hud_full;
				GMBase.r_dsgraph_render_hud_svp();
				g_svp_hud_skip_scope = false;
				RCache.set_xform_view(Device.matrices[1].mView);
				RCache.set_xform_project(Device.matrices[1].mProject);
			}
			else
				GMBase.RGraph.mapHUD.clear(); // consume the deferred main-pass clear
		}
		GMBase.r_dsgraph_render_lods(true, clearGraph);
		// pip r__svp_skip_grass drops the near-grass field on the scope pass (mostly off a zoomed cone)
		if (Details && !(svp_pass && ps_r__svp_skip_grass))
		{
			// keep the grass visible set on the main drain when the SVP pass draws it second
			extern bool g_svp_defer_detail_clear;
			g_svp_defer_detail_clear = (!svp_pass && !clearGraph && !ps_r__svp_skip_grass);
			Details->Render();
			g_svp_defer_detail_clear = false;
		}
		Target->phase_scene_end();
	}

	if (svp_cull || svp_cull_grass)
		CDSGraphManager::svp_cull_end(); // pip end SVP cull, the shared shadow/light passes below are unaffected
	if (svp_pass)
	{
		CDSGraphManager::svp_set_lod_scale(1.f); // pip restore full LOD + no cull before the shared light passes
		CDSGraphManager::svp_set_ssa_cull(0.f, 1.f);
	}

	// Wall marks
	if (Wallmarks)
	{
		PIX_EVENT(DEFER_WALLMARKS);
		Target->phase_wallmarks();

		Wallmarks->Render(); // wallmarks has priority as normal geometry
	}

	// full screen pass to mark msaa-edge pixels in highest stencil bit
	if (RImplementation.o.dx10_msaa)
	{
		PIX_EVENT(MARK_MSAA_EDGES);
		Target->mark_msaa_edges();
	}

	//	TODO: DX10: Implement DX10 rain.
	if (ps_r2_ls_flags.test(R3FLAG_DYN_WET_SURF))
	{
		PIX_EVENT(DEFER_RAIN);
		render_rain();
	}

	{
		// Save previus and current matrices
		{
			static Fmatrix mm_saved_viewproj;

			if (!Device.m_SecondViewport.IsSVPFrame())
			{
				Target->Matrix_previous.mul(mm_saved_viewproj, Device.mInvView);
				Target->Matrix_current.set(Device.mProject);
				mm_saved_viewproj.set(Device.mFullTransform);
			}
			else if (svp_pass)
			{
				// pip the scope water SSR and SSDO reproject against these, the setter blocks are main only so
				// the SVP would accumulate against stale matrices, build them from the per viewport matrices
				Fmatrix svp_prev_full, svp_inv_view, svp_prev_inv_view;
				svp_prev_full.mul(Device.matrices_previous[1].mProject, Device.matrices_previous[1].mView);
				svp_inv_view.invert(Device.matrices[1].mView);
				Target->Matrix_previous.mul(svp_prev_full, svp_inv_view);
				Target->Matrix_current.set(Device.matrices[1].mProject);
				svp_prev_inv_view.invert(Device.matrices_previous[1].mView);
				Target->Position_previous.set(svp_prev_inv_view.c);
			}
		}

		// pip the SVP skips its own SSS pass, r__svp_sss_sun computes it so the scope sun keeps the contact term
		extern int ps_r__svp_sss_sun;
		if (RImplementation.o.ssfx_sss && (!Device.m_SecondViewport.IsSVPFrame() || (svp_pass && ps_r__svp_sss_sun)))
		{
			static bool sss_rendered, sss_extended_rendered;

			// SSS Shadows
			if (ps_ssfx_sss_quality.z > 0)
			{
				Target->phase_ssfx_sss();
				sss_rendered = true;
			}
			else
			{
				if (sss_rendered) // Clear buffer
				{
					sss_rendered = false;
					FLOAT ColorRGBA[4] = { 1,1,1,1 };
					HW.pContext->ClearRenderTargetView(Target->rt_ssfx_sss->pRT, ColorRGBA);
				}
			}

			if (ps_ssfx_sss_quality.w > 0)
			{
				// Extra lights
				Target->phase_ssfx_sss_ext(RImplementation.LP_normal);
				sss_extended_rendered = true;
			}
			else
			{
				if (sss_extended_rendered) // Clear buffer
				{
					sss_extended_rendered = false;
					FLOAT ColorRGBA[4] = { 1,1,1,1 };
					HW.pContext->ClearRenderTargetView(Target->rt_ssfx_sss_tmp->pRT, ColorRGBA);
				}
			}
		}
	}
}

// pip re-point the shadow-atlas textures at the main maps so the SVP accumulation reads the shared
// shadow maps with no second shadow render (SetActive(SVP) had pointed them at the SVP's empty atlas)
void CRender::share_main_smaps()
{
	for (auto& r : TargetMain->RenderTargetRemaps)
		if (r.second == TargetMain->rt_smap_depth ||
			(TargetMain->rt_smap_depth_minmax && r.second == TargetMain->rt_smap_depth_minmax))
			r.first->surface_set(r.second->pSurface);
	RCache.Invalidate();
}

void CRender::renderSceneLighting(BOOL bSUN, bool svp)
{
	// Directional light - fucking sun, cascades build their shadow map once on the main atlas and the
	// accumulate is replayed into the SVP by the dual_accum hook (inside render_sun_cascade + below)
	if (bSUN) //bSUN && Device.dwFrame & 1 --Delayed sun update. Worth to check it in future
	{
		PIX_EVENT(DEFER_SUN);
		RImplementation.stats.l_visible ++;
		render_sun_cascades();
		auto sun_blend = [this] { Target->increment_light_marker(); Target->accum_direct_blend(); };
		sun_blend();
		if (Device.m_SecondViewport.dual_accum)
			Device.m_SecondViewport.dual_accum(sun_blend);
	}

	phase = PHASE_NORMAL;

	// pip gated self-illum replay into the SVP before the main drain touches the shared list
	extern int ps_r__svp_emissive;
	if (svp && ps_r__svp_emissive)
	{
		PIX_EVENT(SVP_SELF_ILLUM);
		TargetSVP->SetActive();
		TargetSVP->phase_accumulator();
		RCache.set_xform_project(Device.mProject);
		RCache.set_xform_view(Device.mView);
		if (!RImplementation.o.dx10_msaa)
			RCache.set_Stencil(TRUE, D3DCMP_ALWAYS, 0x01, 0xff, 0xff, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE,
			                   D3DSTENCILOP_KEEP);
		else
			RCache.set_Stencil(TRUE, D3DCMP_ALWAYS, 0x01, 0xff, 0x7f, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE,
			                   D3DSTENCILOP_KEEP);
		RCache.set_CullMode(CULL_CCW);
		RCache.set_ColorWriteEnable();
		GMBase.r_dsgraph_render_emissive(false);
		TargetMain->SetActive();
	}

	// emissive runs on the main viewport only (active here), the SVP skips self-illum (minor) so it does
	// not re-render + clear the shared GMBase emissive list that the main pass still needs
	{
		PIX_EVENT(DEFER_SELF_ILLUM);
		Target->phase_accumulator();
		// Render emissive geometry, stencil - write 0x0 at pixel pos
		RCache.set_xform_project(Device.mProject);
		RCache.set_xform_view(Device.mView);
		// Stencil - write 0x1 at pixel pos -
		if (!RImplementation.o.dx10_msaa)
			RCache.set_Stencil(TRUE, D3DCMP_ALWAYS, 0x01, 0xff, 0xff, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE,
			                   D3DSTENCILOP_KEEP);
		else
			RCache.set_Stencil(TRUE, D3DCMP_ALWAYS, 0x01, 0xff, 0x7f, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE,
			                   D3DSTENCILOP_KEEP);
		//RCache.set_Stencil				(TRUE,D3DCMP_ALWAYS,0x00,0xff,0xff,D3DSTENCILOP_KEEP,D3DSTENCILOP_REPLACE,D3DSTENCILOP_KEEP);
		RCache.set_CullMode(CULL_CCW);
		RCache.set_ColorWriteEnable();
		GMBase.r_dsgraph_render_emissive(RImplementation.o.ssfx_bloom ? false : true);
	}

	if (RImplementation.o.ssfx_bloom)
	{
		// Render Emissive on `rt_ssfx_bloom_emissive`
		FLOAT ColorRGBA[4] = { 0,0,0,0 };
		HW.pContext->ClearRenderTargetView(Target->rt_ssfx_bloom_emissive->pRT, ColorRGBA);
		Target->u_setrt(Target->rt_ssfx_bloom_emissive, NULL, NULL, !RImplementation.o.dx10_msaa ? HW.pBaseZB : Target->rt_MSAADepth->pZRT);
		GMBase.r_dsgraph_render_emissive(true, true);
	}

	// Lighting, shadow maps build once on the main atlas, render_lights accumulates per viewport
	// (it replays the accumulate into the SVP via dual_accum, sharing the maps), non dependant on OCCQ
	{
		PIX_EVENT(DEFER_LIGHT_NO_OCCQ);
		Target->phase_accumulator();
		render_lights(LP_normal);
	}

	// Lighting, dependant on OCCQ
	{
		PIX_EVENT(DEFER_LIGHT_OCCQ);
		render_lights(LP_pending);
	}

	// pip volumetric blur + combine per viewport, the SVP is combined and captured first so the main
	// combine can composite the ready SVP image into the lens
	if (svp)
	{
		TargetSVP->SetActive();
		if (RImplementation.o.ssfx_volumetric)
			Target->phase_ssfx_volumetric_blur();
		phase = PHASE_NORMAL;
		{
			PIX_EVENT(COMBINE_SVP);
			Target->phase_combine();
		}

		TargetSVP->phase_svp_capture(); // SVP combined color -> rt_secondVP, ready for the main lens
		TargetMain->SetActive();
	}

	{
		if (RImplementation.o.ssfx_volumetric)
			Target->phase_ssfx_volumetric_blur();
	}

	phase = PHASE_NORMAL;

	// Postprocess
	{
		PIX_EVENT(DEFER_LIGHT_COMBINE);
		Target->phase_combine();
	}

	// pip r__scope_debug on-screen inspector overlay (gated + main-only inside the call)
	Target->phase_scope_debug();

	// detail-clear + HUD UI on the main viewport, the scope image has no separate HUD UI
	if (Details)
		Details->details_clear();

	if (g_hud)
	{
		PROF_EVENT("render_hud");
		if (g_hud->RenderActiveItemUIQuery())
			GMBase.r_dsgraph_render_hud_ui();
		if (g_hud->RenderCamAttachedUIQuery())
			GMBase.r_dsgraph_render_cam_ui();
	}
}
#include "../xrRender/CHudInitializer.h"

void CRender::render_forward()
{
	RImplementation.o.distortion = RImplementation.o.distortion_enabled; // enable distorion

	//******* Main render - second order geometry (the one, that doesn't support deffering)
	//.todo: should be done inside "combine" with estimation of of luminance, tone-mapping, etc.
	// combine order, main clears the shared priority-1 forward lists last (keeps smoke + blended fx)
	const bool fwd_clear = svp_clear_shared_list(true);
	{
		// level
		phase = PHASE_NORMAL;
		//	Igor: we don't want to render old lods on next frame.
		GMBase.r_dsgraph_render_static(1, fwd_clear); // normal level, secondary priority
		CParticlesAsync::Wait();
		GMBase.r_dsgraph_render_dynamic(1, fwd_clear);
		GMBase.fade_render(); // faded-portals
		GMBase.r_dsgraph_render_sorted(false); // strict-sorted geoms
		g_pGamePersistent->Environment().RenderLast(); // rain/thunder-bolts
		GMBase.r_dsgraph_render_sorted_hud();
	}

	RImplementation.o.distortion = FALSE; // disable distorion
}

// Redotix99: for 3D Shader Based Scopes
void CRender::render_Reticle()
{
	VERIFY(0 == GMBase.RGraph.mapHUDSorted.Distort.size() + GMBase.RGraph.mapStaticSorted.Distort.size() + GMBase.RGraph.mapDynamicSorted.Distort.size());
	RImplementation.o.distortion = RImplementation.o.distortion_enabled;

	GMBase.r_dsgraph_render_ScopeSorted();

	RImplementation.o.distortion = FALSE;
}

void CRender::RenderToTarget(RRT target)
{
	ref_rt* RT = nullptr;

	switch (target)
	{
	case rtPDA:
		RT = &Target->rt_ui_pda;
		break;
	case rtSVP:
		RT = &Target->rt_secondVP;
		break;
	default:
		Debug.fatal(DEBUG_INFO, "None or wrong Target specified: %i", target);
		break;
	}

	ID3DTexture2D* pBuffer = nullptr;
	HW.m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBuffer);
	HW.pContext->CopyResource((*RT)->pSurface, pBuffer);
	pBuffer->Release();
}
