#include "stdafx.h"
#include "../../xrEngine/igame_persistent.h"
#include "../xrRender/FBasicVisual.h"
#include "../../xrEngine/customhud.h"
#include "../../xrEngine/xr_object.h"
#include "../xrRender/SkeletonCustom.h"
#include "../xrRender/QueryHelper.h"
#include "../../Include/xrAPI/xrAPI.h"          // pip DRender, the debug-line backend for the scope_debug world overlay
#include "../../Include/xrRender/DebugRender.h" // pip IDebugRender::add_lines
#include "../xrRender/SkeletonX.h"              // pip CSkeletonX for the skinned lens bone transform
#include "../../xrEngine/svp_crash_context.h"   // pip svp state ring for tester crash reports
#include "../../xrEngine/xr_ioconsole.h"         // pip Console registry for the [SVP-CFG] fingerprint
#include "../../xrEngine/xr_ioc_cmd.h"           // pip IConsole_Command Name/Status/TStatus
#include "svp_camera.h"

// pip scope_debug >= 2 world overlay, eyepiece (blue) objective (yellow) camera (white)
// via DRender->add_lines, flushed by the stock debug render
void debug_scope(Fmatrix scope_camera, float vfov, float aspect)
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
	// a culled weapon leaves the live radii 0 this frame, fall back to the held debug radii
	CRenderDevice::CSecondVPParams::Lens eye = p.eyepiece;
	if (eye.radius <= EPS)
		eye.radius = p.dbg_eyepiece_r;
	draw_lens(eye, 0xff0000ff);          // eyepiece blue
	// objective yellow at the CAMERA (the real entrance the scope views from), the stored p.objective.m_W
	// is a forward math intermediate (it derives the camera pull-back d), not the visible front lens
	CRenderDevice::CSecondVPParams::Lens objAtCam = p.objective;
	if (objAtCam.radius <= EPS)
		objAtCam.radius = p.dbg_objective_r;
	objAtCam.m_W = scope_camera;
	draw_lens(objAtCam, 0xffffff00);     // objective yellow (at the camera/entrance)
	// orange disc at the true derived objective, its gap from the yellow camera disc shows the pull-back
	CRenderDevice::CSecondVPParams::Lens objTrue = p.objective;
	if (objTrue.radius <= EPS)
		objTrue.radius = p.dbg_objective_r;
	draw_circle(Fmatrix(objTrue.m_W).mulB_43(Fmatrix().scale(objTrue.radius, objTrue.radius, 0.f)), 0xffff8000, true);
	draw_camera(0xffffffff);             // scope cam white
	// pip magenta cube at the live SVP camera position, updates each frame so the camera move between
	// svpscope 1 (eyepiece) and 2 (objective) is obvious at a glance
	draw_cube(scope_camera, eye.radius * 0.6f, 0xffff00ff);

	// green world-view frustum from the SVP camera, the exact cone the scope image renders
	if (vfov > EPS)
	{
		const float d = 30.f;
		const float hh = tanf(vfov * 0.5f) * d;
		const float hw = hh * ((aspect > EPS) ? aspect : 1.f);
		Fvector corner[4];
		for (int i = 0; i < 4; i++)
		{
			const float sx = (i == 0 || i == 3) ? -1.f : 1.f;
			const float sy = (i < 2) ? 1.f : -1.f;
			corner[i].mad(scope_camera.c, scope_camera.k, d);
			corner[i].mad(scope_camera.i, sx * hw);
			corner[i].mad(scope_camera.j, sy * hh);
			dbg_line(scope_camera.c, corner[i], 0xff00ff00, true);
		}
		for (int i = 0; i < 4; i++)
			dbg_line(corner[i], corner[(i + 1) & 3], 0xff00ff00, true);
	}

	// cyan front-plane disc on the optical axis, mode 2 drops pieces wholly behind it
	extern float g_svp_hud_front_m;
	{
		Fvector ax; ax.sub(p.objective.m_W.c, p.eyepiece.m_W.c);
		const float tube = ax.magnitude();
		if (tube > EPS)
		{
			ax.div(tube);
			const float front = (g_svp_hud_front_m > EPS) ? g_svp_hud_front_m : tube;
			Fmatrix fm; fm.identity();
			fm.k.set(ax);
			Fvector seed = (_abs(ax.y) < 0.9f) ? Fvector{0, 1, 0} : Fvector{1, 0, 0};
			fm.i.crossproduct(seed, ax); fm.i.normalize();
			fm.j.crossproduct(ax, fm.i);
			fm.c.mad(p.eyepiece.m_W.c, ax, front);
			const float fr = std::max(eye.radius, objAtCam.radius) * 1.75f;
			draw_circle(Fmatrix(fm).mulB_43(Fmatrix().scale(fr, fr, 0.f)), 0xff00ffff, true);
			extern int ps_r__svp_cop_diag;
			static u32 s_cam_ms = 0;
			if (ps_r__svp_cop_diag && Device.dwTimeGlobal - s_cam_ms > 1000)
			{
				s_cam_ms = Device.dwTimeGlobal;
				Fvector ec; ec.sub(scope_camera.c, p.eyepiece.m_W.c);
				extern Fvector4 ps_s3ds_param_3;
				extern int ps_r__svp_hud_full;
				PipMsg("[SVP-CAM] pos=(%.2f,%.2f,%.2f) fwd=(%.2f,%.2f,%.2f) vfov=%.2fdeg cam2eye=%.1fcm tube=%.1fcm frontplane=%.1fcm it=%.0f full=%d",
					scope_camera.c.x, scope_camera.c.y, scope_camera.c.z,
					scope_camera.k.x, scope_camera.k.y, scope_camera.k.z,
					rad2deg(vfov), ec.magnitude() * 100.f, tube * 100.f, front * 100.f,
					ps_s3ds_param_3.x, ps_r__svp_hud_full);
			}
		}
	}
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

// pip [3DB] ballistics overlay core, has_sight draws the captured red sight line and gaps the fire
// axis against it (else the camera axis), draw_lens adds the eyepiece/objective markers + zeroed ray
static void svp_3db_overlay(float fNearPlane, bool has_sight, const Fvector& sight_org, const Fvector& sight_axis,
	bool draw_lens, const Fvector& eyepiece_pos, bool has_objective, const Fvector& objective_pos)
{
	extern int ps_r__3db_debug;
	auto& vp = Device.m_SecondViewport;
	// display distance, the ranged zero re-picks every tick and would teleport every
	// endpoint while panning, settle it for drawing, the log keeps the live number
	const float D_live = (vp.fire_ray_zero > 0.f) ? vp.fire_ray_zero : 100.f;
	static float s_disp_D = 0.f;
	static u32 s_disp_frame = 0;
	if (Device.dwFrame != s_disp_frame + 1 || s_disp_D <= 0.f)
		s_disp_D = D_live;
	else
		s_disp_D += (D_live - s_disp_D) * (1.f - expf(-Device.fTimeDelta / 0.25f));
	s_disp_frame = Device.dwFrame;
	const float D = s_disp_D;
	Fmatrix eyeW2; eyeW2.invert(Device.matrices[0].mView);
	// clip to the near plane, a behind-plane endpoint rasterizes as a screen streak
	const float znear = fNearPlane + 0.01f;
	auto line = [&](const Fvector& a, const Fvector& b, u32 color) {
		Fvector ca; ca.sub(a, eyeW2.c);
		Fvector cb; cb.sub(b, eyeW2.c);
		const float za = ca.dotproduct(eyeW2.k), zb = cb.dotproduct(eyeW2.k);
		if (za < znear && zb < znear)
			return;
		Fvector aa = a, bb = b;
		if (za < znear)
			aa.lerp(a, b, (znear - za) / (zb - za));
		else if (zb < znear)
			bb.lerp(a, b, (znear - za) / (zb - za));
		Fvector v[2] = { aa, bb }; u16 i[2] = { 0, 1 };
		DRender->add_lines(v, 2, i, 1, color, true);
	};
	auto cross = [&](const Fvector& p, float s, u32 color) {
		Fvector a, b;
		a.set(p); a.mad(eyeW2.i, -s); b.set(p); b.mad(eyeW2.i, s);
		line(a, b, color);
		a.set(p); a.mad(eyeW2.j, -s); b.set(p); b.mad(eyeW2.j, s);
		line(a, b, color);
	};

	// yellow muzzle marker, red eyepiece and cyan objective only with a captured lens
	cross(vp.muzzle_pos, 0.01f, 0xffffff00);
	if (draw_lens)
	{
		cross(eyepiece_pos, 0.01f, 0xffff0000);
		if (has_objective)
			cross(objective_pos, 0.01f, 0xff00ffff);
	}

	// the sight line to the zero distance, drawn from the stable published copy ballistics converge on
	if (has_sight)
	{
		Fvector sight; sight.mad(sight_org, sight_axis, D);
		line(sight_org, sight, 0xffff0000);
		cross(sight, D * 0.02f, 0xffff0000);
	}

	// the camera crosshair ray from the mirrored actor eye, the ballistic truth when aimpos
	// is off, stays the shooter's eye while demo_record flies the device camera
	Fvector cpos, cfwd;
	if (vp.eye_ray_dir.square_magnitude() > EPS)
	{
		cpos.set(vp.eye_ray_pos);
		cfwd.set(vp.eye_ray_dir);
	}
	else
	{
		cpos.set(eyeW2.c);
		cfwd.set(eyeW2.k);
	}
	cfwd.normalize_safe();
	Fvector chp; chp.mad(cpos, cfwd, D);
	line(cpos, chp, 0xff4080ff);
	cross(chp, D * 0.02f, 0xff4080ff);

	// the raw fire axis, where bullets go with g_svp_zero 0
	if (Device.dwFrame - vp.fire_ray_frame < 8)
	{
		Fvector faxis; faxis.set(vp.fire_ray_dir); faxis.normalize_safe();
		Fvector fire; fire.mad(vp.fire_ray_pos, faxis, D);
		line(vp.fire_ray_pos, fire, 0xff00ff00);
		cross(fire, D * 0.02f, 0xff00ff00);
		// the gap reads the fire axis against the sight line when captured, else the aim axis
		float c = has_sight ? sight_axis.dotproduct(faxis) : cfwd.dotproduct(faxis);
		clamp(c, -1.f, 1.f);
		static u32 s_aim_ms = 0;
		if (Device.dwTimeGlobal - s_aim_ms > 1000)
		{
			s_aim_ms = Device.dwTimeGlobal;
			if (has_sight)
				PipMsg("[3DB] axis gap %.2f mrad at %.0fm (zero %.0f)", acosf(c) * 1000.f, D, vp.fire_ray_zero);
			else
				PipMsg("[3DB] fire-aim gap %.2f mrad at %.0fm (zero %.0f)", acosf(c) * 1000.f, D, vp.fire_ray_zero);
		}

		// the zeroed departure ray the shot actually flies, only with a captured sight line
		if (has_sight && ps_r__3db_debug >= 2 && vp.fire_ray_zero > 0.f)
		{
			Fvector zp; zp.mad(sight_org, sight_axis, D);
			line(vp.fire_ray_pos, zp, 0xffffffff);
		}
	}

	// fading shot tracers, brightness decays over 5s
	if (ps_r__3db_debug >= 3)
	{
		for (const auto& tr : vp.fire_traces)
		{
			const u32 age = Device.dwTimeGlobal - tr.time_ms;
			if (!tr.time_ms || age >= 5000)
				continue;
			const u32 k = 255u - age * 255u / 5000u;
			const u32 color = 0xff000000 | (k << 16) | (k << 8);
			Fvector end; end.mad(tr.pos, tr.dir, D * 2.f);
			line(tr.pos, end, color);
		}
	}
}

// pip [3DB] overlay for sights without a captured pip lens, reflex and irons keep 3d
// ballistics so the sight independent markers draw here, the pip overlay owns the rest
void ballistics_debug_overlay()
{
	float _, fov, fNearPlane, fFarPlane;
	Device.matrices[0].mProject.decompose_projection(fov, _, fNearPlane, fFarPlane);
	const Fvector zero = { 0.f, 0.f, 0.f };
	svp_3db_overlay(fNearPlane, false, zero, zero, false, zero, false, zero);
}

// pip build the SVP camera (fills Device.matrices[1]) from the captured lens + the weapon
// zoom factor, called after the lens derives so TargetSVP->SetActive reads it ready
void svpCamera()
{
	// the published zoom is raise transient free, unset falls back to the shader constant
	const float zoom_src = (Device.m_SecondViewport.svp_zoom_pub > 1.f)
		? Device.m_SecondViewport.svp_zoom_pub
		: g_pGamePersistent->m_pGShaderConstants->hud_params.y;
	// the scale rides the live fov so the scope keeps its fov-75 look at any user fov
	float svp_fov = zoom_src * 0.75f * Device.m_SecondViewport.svp_fov_scale;
	float _, fov, fNearPlane, fFarPlane;
	Device.matrices[0].mProject.decompose_projection(fov, _, fNearPlane, fFarPlane);
	// the mag reads the steady wide aim fov (punch free from the weapon publish), the live decomposed
	// fov keeps feeding the vFov/projection so the scope image tracks the actual main view
	const float aim_fov_pub = Device.m_SecondViewport.svp_aim_fov;
	const float fov_aim = (aim_fov_pub > 1.f) ? deg2rad(aim_fov_pub) : fov;

	// a zoom-0 tube sight (1x thermal/nv) has no zoom fov and re-images at 1x, the near-0 value
	// would also blow up the vFov/offset tan() math
	if (svp_fov < 1.0f) svp_fov = rad2deg(fov_aim);


	auto mm = Device.matrices[0];
	auto& params = Device.m_SecondViewport;

	// analytic eyepiece fit, a disc of radius r at view depth d projects to ndc height 2*r*_22/d
	// under mProjectHud, the fit is screen height 2 over that. depth on the view forward, exact off axis
	Fmatrix eyeW0; eyeW0.invert(mm.mView);
	Fvector camfwd; camfwd.set(eyeW0.k); camfwd.normalize();
	Fvector eyed; eyed.sub(params.svp_sight_pos, eyeW0.c);
	const float lens_depth = eyed.dotproduct(camfwd);
	const float ndc_height = (lens_depth > EPS)
		? (2.f * params.svp_lens_r * mm.mProjectHud._22 / lens_depth) : 0.f;
	const bool analytic_ok = params.eyepiece.radius > EPS && params.svp_lens_r > EPS
		&& lens_depth > EPS && _valid(ndc_height) && ndc_height > 1e-4f;
	const float ratio_analytic = analytic_ok ? (2.f / ndc_height) : 0.f;
	float ratio_magnification = 1.f;

	// the magnification of the scope (1X 4X etc)
	float scope_magnification = fov_aim / deg2rad(svp_fov);

	// flat screen optic (binocular), the panel is a see-through window so the svp fov is the
	// panel's angular subtense at the weapon zoom, magnification then tracks the stock look
	extern int ps_r__svp_flat_window;
	extern Fvector4 ps_s3ds_param_3;
	const bool flat_window = ps_r__svp_flat_window && (int)ps_s3ds_param_3.y == 8
		&& Device.m_SecondViewport.svp_disc_px > 1.f;
	auto window_fov = [&](float zdeg) {
		const float p = _min(Device.m_SecondViewport.svp_disc_px / (float)Device.dwHeight, 1.5f);
		return 2.f * atanf(p * tanf(deg2rad(zdeg) * 0.5f));
	};
	if (flat_window)
	{
		// authored mag flat optics keep the clean optical mag, only the see-through panel takes the subtense ratio
		if (!params.svp_authored_mag)
			scope_magnification = fov_aim / window_fov(g_pGamePersistent->m_pGShaderConstants->hud_params.y > 1.f
				? g_pGamePersistent->m_pGShaderConstants->hud_params.y : svp_fov);
		ratio_magnification = 1.f;
	}
	else
	{
		// coast the analytic fit through the raise and one pole it while settled, the raw lens pose
		// bobs but the along axis depth barely moves so an unsettled aim or alt sight holds the value
		const float aim_rot = g_pGamePersistent->m_pGShaderConstants->hud_params.x;
		const bool alt_sight = Device.m_SecondViewport.svp_alt_sight;
		static float s_ratio = 0.f;
		static u32 s_ratio_frame = 0;
		static u32 s_ratio_epoch = 0;
		const bool gap = (Device.dwFrame != s_ratio_frame + 1);
		s_ratio_frame = Device.dwFrame;
		// subscribe to the optic epoch, a magnifier flip or scope swap bumps it with no frame gap so
		// the fit drops the old optic's value and reseeds at the next settle
		const bool epoch = (params.svp_optic_epoch != s_ratio_epoch);
		s_ratio_epoch = params.svp_optic_epoch;
		const bool can_track = (aim_rot > 0.999f) && !alt_sight;
		if (analytic_ok)
		{
			// a session gap or an optic swap drops the latch, the raise value is meaningless so the
			// seed waits for the first settled frame, then the one pole eases residual pose noise
			if (gap || epoch || !_valid(s_ratio) || s_ratio <= 0.f)
				s_ratio = can_track ? ratio_analytic : 0.f;
			else if (can_track)
				s_ratio += (ratio_analytic - s_ratio) * (1.f - expf(-Device.fTimeDelta / 0.15f));
		}
		ratio_magnification = (s_ratio > 0.f) ? s_ratio : ratio_analytic;
	}

	// magnification ceiling, the optic's authored config max zoom (hud_fov_params.x, the same source
	// as g_pip_scope_max_mag), held through section churn so a flip cannot push the mag past the optic
	{
		const Fvector4& fovp_c = g_pGamePersistent->m_pGShaderConstants->hud_fov_params;
		const float fscale_c = rad2deg(fov_aim) / 75.f;
		const float cfg_max = (fovp_c.x > EPS) ? fov_aim / deg2rad(fovp_c.x * 0.75f * fscale_c) : 0.f;
		static float s_mag_ceiling = 0.f;
		static u32 s_ceiling_frame = 0;
		if (Device.dwFrame != s_ceiling_frame + 1) s_mag_ceiling = 0.f; // session gap drops the hold
		s_ceiling_frame = Device.dwFrame;
		const float rot_c = g_pGamePersistent->m_pGShaderConstants->hud_params.x;
		if (cfg_max > EPS && rot_c > 0.999f) s_mag_ceiling = cfg_max; // latch the settled config max
		const float ceiling = (s_mag_ceiling > EPS) ? s_mag_ceiling : cfg_max;
		if (ceiling > EPS && scope_magnification > ceiling) scope_magnification = ceiling;
	}

	// pip ratio pipeline diag ([SVP-RATIO]), cross checks the analytic fit against the measured
	// screen ndc extent, they agree for a settled on axis pose, measured lives here for the check
	{
		extern int ps_r__svp_diag;
		if (ps_r__svp_diag)
		{
			static u32 s_rat_ms = 0;
			if (Device.dwTimeGlobal - s_rat_ms > 1000)
			{
				s_rat_ms = Device.dwTimeGlobal;
				Fvector4 top, bot;
				Fmatrix m_WVP = Fmatrix().mul(mm.mProjectHud, Fmatrix().mul(mm.mView, params.eyepiece.m_W));
				m_WVP.transform(top, {0, params.eyepiece.radius, 0, 1});
				m_WVP.transform(bot, {0, -params.eyepiece.radius, 0, 1});
				top.div(top.w); bot.div(bot.w);
				float meas_h = abs(top.y - bot.y);
				if (!_valid(meas_h) || meas_h < 0.001f) meas_h = 0.001f;
				float hf, ha, hn, hff;
				mm.mProjectHud.decompose_projection(hf, ha, hn, hff);
				PipMsg("[SVP-RATIO] meas %.3f analytic %.3f final %.3f flat %d p3y %.1f measH %.3f ndcH %.3f depth %.1fcm r %.2fcm aim %.2f alt %d ok %d hfov %.1f",
					2.f / meas_h, ratio_analytic, ratio_magnification, (int)flat_window,
					ps_s3ds_param_3.y, meas_h, ndc_height,
					lens_depth * 100.f, params.svp_lens_r * 100.f,
					g_pGamePersistent->m_pGShaderConstants->hud_params.x,
					(int)Device.m_SecondViewport.svp_alt_sight, (int)analytic_ok,
					rad2deg(hf));
			}
		}
	}

	// expose engine magnification so the shader has a curMag with no 3DSS config
	extern float g_pip_scope_magnification;
	extern float g_pip_scope_min_mag;
	extern float g_pip_scope_max_mag;
	extern float g_pip_scope_ratio;
	// eyepiece-fit factor, rated on-screen magnification = ratio * scope, clamped for degenerate geometry
	const float ratio_use = (ratio_magnification > 0.5f) ? ((ratio_magnification < 8.f) ? ratio_magnification : 8.f) : 1.f;
	if (svp_fov > EPS)
	{
		g_pip_scope_magnification = scope_magnification;
		g_pip_scope_ratio = ratio_use;
		// derive min/max mag from hud_fov_params for variable reticles (fixed scope: x == y)
		const Fvector4& fovp = g_pGamePersistent->m_pGShaderConstants->hud_fov_params;
		// the 75-base bounds rescale to the aim fov, authored mins are 75-base too,
		// only the legacy optical-model min already rides the aim fov and passes through
		const float fscale = rad2deg(fov_aim) / 75.f;
		const float yscale = (_abs(fovp.y - fovp.x) < 0.01f || params.svp_min_75base) ? fscale : 1.f;
		if (flat_window && !params.svp_authored_mag)
		{
			g_pip_scope_max_mag = (fovp.x > EPS) ? fov_aim / window_fov(fovp.x * fscale) : scope_magnification;
			g_pip_scope_min_mag = (fovp.y > EPS) ? fov_aim / window_fov(fovp.y * yscale) : scope_magnification;
		}
		else
		{
			g_pip_scope_max_mag = (fovp.x > EPS) ? fov_aim / deg2rad(fovp.x * 0.75f * fscale) : scope_magnification;
			g_pip_scope_min_mag = (fovp.y > EPS) ? fov_aim / deg2rad(fovp.y * 0.75f * yscale) : scope_magnification;
		}
	}

	// the fov we render at to get the correct zoom, the eyepiece-fit ratio scales the vFov
	float vFov = 2.0f * atan(tan(fov * 0.5f) / (ratio_use * scope_magnification));
	// flat window renders exactly the panel subtense (tan-correct, the mag division is not)
	if (flat_window)
	{
		if (params.svp_authored_mag)
		{
			// authored mag flat optic renders the floor panel subtense then the optical mag crops in
			const Fvector4& fovp = g_pGamePersistent->m_pGShaderConstants->hud_fov_params;
			const float fscale = rad2deg(fov_aim) / 75.f;
			const float yscale = (_abs(fovp.y - fovp.x) < 0.01f) ? fscale : 1.f;
			vFov = window_fov(fovp.y * yscale) / scope_magnification;
		}
		else
			vFov = fov / scope_magnification;
	}

	auto near_plane = fNearPlane;
	auto m_W_svpcam = params.eyepiece.m_W; // svpscope 1 places the camera on the eyepiece
	// svpscope 2 renders the world from the entrance pupil, the eye slid up the optical
	// axis to the measured front plane
	if (scope_svp_enabled >= 2)
	{
		Fmatrix eyeW; eyeW.invert(Device.matrices[0].mView);
		m_W_svpcam.c.set(eyeW.c);
		extern float g_svp_hud_front_m;
		// latch the front plane at the settle edge, the script push lands frames after zoom in so a
		// live per-frame read would step the camera mid aim, hold the settled value for the aim
		static float s_front_hold = 0.f;
		static u32 s_front_frame = 0;
		static bool s_front_latched = false;
		const float aim_r = g_pGamePersistent ? g_pGamePersistent->m_pGShaderConstants->hud_params.x : 0.f;
		if (Device.dwFrame != s_front_frame + 1) s_front_latched = false; // session gap re-latches
		s_front_frame = Device.dwFrame;
		if (aim_r > 0.999f) { if (!s_front_latched) { s_front_hold = g_svp_hud_front_m; s_front_latched = true; } }
		else s_front_latched = false; // unsettled, re-latch at the next settle
		const float front_use = s_front_latched ? s_front_hold : g_svp_hud_front_m;
		if (front_use > EPS)
		{
			// the entrance pupil sits ON the axis, no eye lateral residue in the formed image
			// (eye movement belongs to the exit-pupil crescent, not the camera)
			Fvector ax; ax.set(params.eyepiece.m_W.k); ax.normalize();
			m_W_svpcam.c.mad(params.eyepiece.m_W.c, ax, front_use);
			// side/top mounted optics view the world from the objective's real lateral position
			const Fvector4& off = params.svp_opt_offset;
			if (params.eyepiece.radius > EPS && (_abs(off.x) > EPS || _abs(off.y) > EPS))
			{
				Fvector ri; ri.set(params.eyepiece.m_W.i); ri.normalize();
				Fvector up; up.set(params.eyepiece.m_W.j); up.normalize();
				m_W_svpcam.c.mad(ri, off.x * params.eyepiece.radius);
				m_W_svpcam.c.mad(up, off.y * params.eyepiece.radius);
			}
			// mode 2 pushes the scope near clip to the pupil front plane so behind-pupil barrel
			// falls behind it, world geometry sits far past this
			extern int ps_r__svp_near_pupil;
			if (ps_r__svp_near_pupil && front_use > near_plane)
				near_plane = front_use;
		}
		extern int ps_r__svp_cop_diag;
		static u32 s_frontcam_ms = 0;
		if (ps_r__svp_cop_diag && Device.dwTimeGlobal - s_frontcam_ms > 500)
		{
			s_frontcam_ms = Device.dwTimeGlobal;
			PipMsg("[SVP-CAM] frontplane hold=%.1fcm live=%.1fcm aim=%.3f latched=%d",
				front_use * 100.f, g_svp_hud_front_m * 100.f, aim_r, (int)s_front_latched);
		}
	}

	// pip roll_stabilize aligns the SVP camera up to the view up, dropping mount cant/flip
	// (0 = raw mesh tilt)
	extern int ps_r__svp_roll_stabilize;
	if (ps_r__svp_roll_stabilize)
	{
		Fvector fwd, wup, right, up;
		fwd.set(m_W_svpcam.k.x, m_W_svpcam.k.y, m_W_svpcam.k.z);
		fwd.normalize();
		wup.set(Device.vCameraTop.x, Device.vCameraTop.y, Device.vCameraTop.z);
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


	// pip lens flip diagnostic ([SVP-ORIENT]), the mesh basis vs the final svp camera basis
	{
		extern int ps_r__svp_cop_diag;
		if (ps_r__svp_cop_diag && params.eyepiece.radius > EPS)
		{
			static u32 s_orient_ms = 0;
			if (Device.dwTimeGlobal - s_orient_ms > 500)
			{
				s_orient_ms = Device.dwTimeGlobal;
				PipMsg("[SVP-ORIENT] meshUp=(%.2f,%.2f,%.2f) meshFwd=(%.2f,%.2f,%.2f) camUp=(%.2f,%.2f,%.2f) rollStab=%d camUp_out=(%.2f,%.2f,%.2f) camRight_out=(%.2f,%.2f,%.2f) camFwd_out=(%.2f,%.2f,%.2f)",
					params.eyepiece.m_W.j.x, params.eyepiece.m_W.j.y, params.eyepiece.m_W.j.z,
					params.eyepiece.m_W.k.x, params.eyepiece.m_W.k.y, params.eyepiece.m_W.k.z,
					Device.vCameraTop.x, Device.vCameraTop.y, Device.vCameraTop.z,
					ps_r__svp_roll_stabilize,
					m_W_svpcam.j.x, m_W_svpcam.j.y, m_W_svpcam.j.z,
					m_W_svpcam.i.x, m_W_svpcam.i.y, m_W_svpcam.i.z,
					m_W_svpcam.k.x, m_W_svpcam.k.y, m_W_svpcam.k.z);
			}
		}
	}

	// aspect = height/width (engine convention, matches SetActive's fASPECT); square == 1
	// derived from the same policy that sizes the target this frame, not the pre-frame target shape
	extern void svp_target_wh(u32&, u32&);
	u32 tw, th;
	svp_target_wh(tw, th);
	float aspect = (float)th / (float)tw;
	// a non-square SVP keeps the horizontal window subtense at vFov, the vertical follows the aspect
	float vfov_use = (aspect < 0.999f) ? (2.f * atanf(aspect * tanf(vFov * 0.5f))) : vFov;

	float fNearPlane_hud, fFarPlane_hud;
	Device.matrices[0].mProject.decompose_projection(_, _, fNearPlane_hud, fFarPlane_hud);
	auto svp_proj = Fmatrix().build_projection(vfov_use, aspect, near_plane, fFarPlane);
	auto svp_proj_hud = Fmatrix().build_projection(vfov_use, aspect, near_plane, fFarPlane_hud);

	// pip DLSS jitter the SVP scene projection (gated), {0,0} otherwise, applied to mProject only
	Device.m_SecondViewport.svp_jitter_px.set(0, 0);
	if (ps_r__svp_dlss != 0)
	{
		const u32 jf = Device.dwFrame; // latch once, stable on the render thread this frame
		Fvector2 jpx = svp_jitter_offset(jf);
		svp_apply_jitter(svp_proj, jpx, (float)RImplementation.TargetSVP->Width, (float)RImplementation.TargetSVP->Height);
		Device.m_SecondViewport.svp_jitter_px = jpx;
	}

	// the held dbg radius keeps the lines through a culled weapon, it zeroes on unscope
	if (scope_debug >= 2 && (params.eyepiece.radius > EPS || params.dbg_eyepiece_r > EPS))
		debug_scope(m_W_svpcam, vFov, (float)aspect);

	Device.matrices[1].mView.invert(m_W_svpcam);
	Device.matrices[1].mProject = svp_proj;
	Device.matrices[1].mProjectHud = svp_proj_hud;

	// pip cache the SVP scene constants for the DLSS eval inputs and the defocus bind (render thread,
	// written then read the same frame). svp_fov is radians from the projection, the basis is the camera world
	{
		auto& vp = Device.m_SecondViewport;
		Device.matrices[1].mProject.decompose_projection(vp.svp_fov, vp.svp_aspect, vp.svp_near, vp.svp_far);
		vp.svp_cam_pos = m_W_svpcam.c;
		vp.svp_right = m_W_svpcam.i;
		vp.svp_up = m_W_svpcam.j;
		vp.svp_fwd = m_W_svpcam.k;
	}

	// pip snapshot the svp runtime state into the crash-context ring for tester crash logs
	{
		extern float g_svp_hud_front_m;
		auto& vp = Device.m_SecondViewport;
		SvpCrashFrame cf;
		cf.frame = Device.dwFrame;
		cf.mode = scope_svp_enabled;
		cf.active = vp.IsSVPActive();
		cf.render_pass_is_svp = vp.m_render_pass_is_svp;
		cf.hud_front_m = g_svp_hud_front_m;
		cf.mag = vp.svp_mag;
		cf.fov_deg = rad2deg(vp.svp_fov);
		cf.disc_px = vp.svp_disc_px;
		svp_crash_context_push(cf);
	}

	// pip optics diagnostic: throttled [SVPCOP] log of the camera center-of-projection offset from the
	// eye, settled frames only (ADS transitions blow up ratio_magnification)
	extern int ps_r__svp_cop_diag;
	if (ps_r__svp_cop_diag && params.eyepiece.radius > EPS
		&& ratio_magnification > 1.0f && ratio_magnification < 8.0f)
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
			PipMsg("[SVPCOP] mode=%d mag=%.3f eff=%.3f min=%.3f max=%.3f ratio=%.3f svpfov=%.2f vfov=%.2f cop_cm=%.2f fwd_cm=%.2f lat_cm=%.2f eye_r_cm=%.2f obj_fwd_cm=%.2f obj_r_cm=%.2f cant=%.1f",
				scope_svp_enabled, scope_magnification, eff_mag, g_pip_scope_min_mag, g_pip_scope_max_mag, ratio_magnification,
				svp_fov, rad2deg(vFov), d.magnitude() * 100.f, fwd * 100.f, lat_v.magnitude() * 100.f,
				params.eyepiece.radius * 100.f, od.dotproduct(eyefwd) * 100.f, params.objective.radius * 100.f,
				cant);

			// barrel-continuity probe: one weapon-fixed point (the objective) through both pipelines,
			// a steady delta while sway swings = static projection mismatch, delta tracking sway = lag
			const float disc_px_probe = (params.svp_disc_px > 1.f)
				? params.svp_disc_px : (float)Device.dwHeight / _max(ratio_magnification, 1.f);
			if (params.svp_fov > EPS)
			{
				auto to_px = [](const Fvector& w, const Fmatrix& v, const Fmatrix& pr, float W, float H, Fvector2& o) -> bool {
					Fmatrix vpm; vpm.mul(pr, v);
					Fvector4 c; vpm.transform(c, {w.x, w.y, w.z, 1});
					if (!_valid(c.w) || c.w < EPS) return false;
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
					const float bx = px_disc.x + (uv_in.x - 0.5f) * disc_px_probe;
					const float by = px_disc.y + (uv_in.y - 0.5f) * disc_px_probe;
					// near-field coc at the objective probe from the near-blur thin-lens formula, in svp px
					// covered means the blur radius spans the pipeline mismatch at the disc rim
					extern float ps_r__svp_near_blur, ps_r__svp_focus_m;
					const float omm = Device.m_SecondViewport.svp_opt_obj_mm;
					const float A = (omm > 0.01f) ? omm * 0.001f : 0.024f;
					const float vfov = (Device.m_SecondViewport.svp_fov > 0.01f) ? Device.m_SecondViewport.svp_fov : 0.35f;
					const float k = A * (float)Device.svp_height() / vfov * _min(ps_r__svp_near_blur, 3.f);
					const float z = od.dotproduct(eyefwd);
					const float coc = k * _max(1.f / _max(z, 0.05f) - 1.f / _max(ps_r__svp_focus_m, 1.f), 0.f);
					Fvector2 rdir; rdir.set(uv_in.x - 0.5f, uv_in.y - 0.5f);
					const float rlen = sqrtf(rdir.x * rdir.x + rdir.y * rdir.y);
					Fvector2 rim;
					if (rlen > EPS)
						rim.set(px_disc.x + rdir.x / rlen * 0.5f * disc_px_probe, px_disc.y + rdir.y / rlen * 0.5f * disc_px_probe);
					else
						rim.set(px_disc.x, px_disc.y);
					const float mismatch = sqrtf((bx - px_out.x) * (bx - px_out.x) + (by - px_out.y) * (by - px_out.y));
					const int covered = (coc >= mismatch) ? 1 : 0;
					PipMsg("[SVP-BARREL] sway=%.2fdeg fovmatch=%d out=(%.0f,%.0f) in=(%.0f,%.0f) delta=(%.0f,%.0f)px rim=(%.0f,%.0f) coc=%.0fpx mismatch=%.0fpx covered=%d",
						sway, ps_r__svp_hud_fov_match, px_out.x, px_out.y, bx, by, bx - px_out.x, by - px_out.y,
						rim.x, rim.y, coc, mismatch, covered);
				}
			}
		}
	}

	// pip [SVP-AIM] composite-centering log (r__svp_cop_diag 2): the scope disc projected on screen
	// vs the screen center, the lens compositing alignment, not a ballistic statement
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
			PipMsg("[SVP-AIM] disc=(%.1f,%.1f) d=(%.1f,%.1f)px |d|=%.1f", rx, ry, dx, dy, sqrtf(dx * dx + dy * dy));
		}
	}

	// pip [3DB] overlay, 1 = markers + fire (green) camera (blue) sight (red) rays,
	// 2 = zeroed departure ray (white), 3 = fading shot tracers
	{
		extern int ps_r__3db_debug;
		if (ps_r__3db_debug > 0 && (params.eyepiece.radius > EPS || params.dbg_eyepiece_r > EPS))
		{
			auto& vp = Device.m_SecondViewport;
			// the sight line from the stable published copy, else the eyepiece axis
			Fvector s_org, saxis;
			if (vp.svp_sight_ok)
			{
				s_org.set(vp.svp_sight_pos);
				saxis.set(vp.svp_sight_dir);
			}
			else
			{
				s_org.set(params.eyepiece.m_W.c);
				saxis.set(params.eyepiece.m_W.k);
			}
			saxis.normalize_safe();
			const bool has_obj = (params.objective.radius > EPS || params.dbg_objective_r > EPS);
			svp_3db_overlay(fNearPlane, true, s_org, saxis, true, params.eyepiece.m_W.c, has_obj, params.objective.m_W.c);
		}
	}

	// pip one-shot config fingerprint on the first scoped frame so any tester log diffs against ours
	{
		extern int ps_r__svp_report;
		static bool s_cfg_logged = false;
		if (!s_cfg_logged || ps_r__svp_report)
		{
			s_cfg_logged = true;
			ps_r__svp_report = 0;
			// build fingerprint header first, testers diff their log against this
			PipMsg("[SVP-CFG] build %s mode=%d", __DATE__, scope_svp_enabled);
			// generate the cvar list from the console registry so it never drifts from the registered
			// set, every r__svp_/g_svp_/s3ds_ command with its live value grouped about eight per line
			if (Console)
			{
				char line[1024]; line[0] = 0; u32 grp = 0;
				for (const auto& c : Console->Commands)
				{
					const char* nm = c.second->Name();
					if (!(0 == strncmp(nm, "r__svp_", 7) || 0 == strncmp(nm, "g_svp_", 6) || 0 == strncmp(nm, "s3ds_", 5)))
						continue;
					IConsole_Command::TStatus st; st[0] = 0; c.second->Status(st);
					char tok[288]; xr_sprintf(tok, "%s%s=%s", grp ? " " : "", nm, st);
					xr_strcat(line, tok);
					if (++grp == 8) { PipMsg("[SVP-CFG] %s", line); line[0] = 0; grp = 0; }
				}
				if (line[0]) PipMsg("[SVP-CFG] %s", line);
				// aa state read the same registry way, ssfx_taa is the script-driven taa control
				IConsole_Command::TStatus a_taa, a_aa, a_ker; a_taa[0] = a_aa[0] = a_ker[0] = 0;
				auto st_of = [](const char* name, IConsole_Command::TStatus& out) {
					auto it = Console->Commands.find(name);
					if (it != Console->Commands.end()) it->second->Status(out);
				};
				st_of("ssfx_taa", a_taa); st_of("r2_aa", a_aa); st_of("r2_aa_kernel", a_ker);
				PipMsg("[SVP-CFG] aa ssfx_taa=%s r2_aa=%s r2_aa_kernel=%s", a_taa, a_aa, a_ker);
			}

			// pip [SVP-FILES] shader source truth: where each scope file resolves from (loose or which
			// archive), size and source crc, plus the precompiled-blob and mounted-archive taints
			{
				static const char* s_scope_files[] = {
					"scope_color_write.ps", "scope_3dss_common.h", "scope_common.h",
					"scope_custom_image.h", "scope_custom_reticle.h", "scope_custom_shadow.h",
					"scope_custom_lens.h", "mark_adjust.h",
					"svp_hooks_common.h", "svp_hooks_image.h",
					"svp_hooks_reticle.h", "svp_hooks_lens.h", "svp_hooks_shadow.h",
					"models_scope_reticle.ps", "models_scope_reticle_precise.ps",
					"models_reflex_reticle.ps", "models_reflex_reticle_3db.ps",
					"models_reflex_reticle_simple.ps", "models_reflex_reticle_simple_3db.ps",
					"models_reflex_lens.ps", "models_scope_zwrite.ps",
					"models_scope_reticle.s", "models_scope_reticle_precise.s",
					"models_reflex_reticle.s", "models_reflex_reticle_3db.s",
					"models_reflex_reticle_simple.s", "models_reflex_reticle_simple_3db.s",
					"models_reflex_lens.s", "models_scope_zwrite.s", "models_scope_back.s",
					"models_scope_reticle.vs", "models_reflex_reticle.vs", "models_reflex_lens.vs",
					"scope_vertex.vs", "scope_defines.h", "svp_nearblur.ps",
					"gbuffer_stage.h", "nv_utils.h", "thermal_utils.h" };
				for (const char* fn : s_scope_files)
				{
					string_path rel;
					strconcat(sizeof(rel), rel, "r3\\", fn);
					const CLocatorAPI::file* f = FS.exist("$game_shaders$", rel);
					if (!f)
					{
						PipMsg("[SVP-FILES] %-32s MISSING", fn);
						continue;
					}
					u32 crc = 0;
					char ver[96] = "unmarked";
					if (IReader* r = FS.r_open("$game_shaders$", rel))
					{
						crc = crc32(r->pointer(), r->length());
						// the resolved source carries our PIP_COMPAT_PATCH tag, a foreign shader that
						// shadowed ours or a stale pre-marker file reads unmarked
						const char* p = (const char*)r->pointer();
						const u32 n = r->length();
						const char* key = "PIP_COMPAT_PATCH";
						const u32 klen = (u32)xr_strlen(key);
						for (u32 i = 0; n > klen && i <= n - klen; ++i)
							if (0 == strncmp(p + i, key, klen))
							{
								u32 j = i + klen, k = 0;
								while (j < n && p[j] != '\r' && p[j] != '\n' && k < sizeof(ver) - 1)
									ver[k++] = p[j++];
								ver[k] = 0;
								break;
							}
						FS.r_close(r);
					}
					const char* src = "loose";
					if (f->vfs != 0xffffffff && f->vfs < FS.m_archives.size())
						src = FS.m_archives[f->vfs].path.c_str();
					PipMsg("[SVP-FILES] %-32s %6u bytes crc %08x ver[%s] %s", fn, f->size_real, crc, ver, src);
				}
				// shipped precompiled blobs load with no source crc and shadow every source edit
				FS_FileSet blobs;
				string_path bdir;
				FS.update_path(bdir, "$game_shaders$", "r3\objects\r4\scope_color_write.ps\\");
				FS.file_list(blobs, bdir, FS_ListFiles | FS_RootOnly, "*");
				PipMsg("[SVP-FILES] dispatcher precompiled blobs %u, rs_precompiled_shaders %d",
					(u32)blobs.size(), psDeviceFlags2.test(rsPrecompiledShaders) ? 1 : 0);
				u32 n_arch = 0;
				for (const auto& A : FS.m_archives)
					if (strstr(A.path.c_str(), "mods"))
					{
						PipMsg("[SVP-FILES] mounted mods archive %s", A.path.c_str());
						++n_arch;
					}
				PipMsg("[SVP-FILES] mods archives mounted %u", n_arch);
			}
		}
	}

	// pip [SVP-LEDGER] once-per-session inert-path sweep, after enough scoped time to exercise the
	// gated paths, flags any instrumented counter still zero while its gate cvar is on
	{
		extern void svp_ledger_sweep();
		const float SVP_LEDGER_SETTLE_S = 60.f; // diagnostic patience window in scoped seconds, not a rendering threshold
		static float s_ledger_scoped_s = 0.f;
		static bool s_ledger_swept = false;
		if (!s_ledger_swept)
		{
			s_ledger_scoped_s += Device.fTimeDelta;
			if (s_ledger_scoped_s >= SVP_LEDGER_SETTLE_S)
			{
				s_ledger_swept = true;
				svp_ledger_sweep();
			}
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
		// no objective captured, place one at the authored-set front distance in eyepiece radii
		// (mod_system_3dss_objective_lenses median 12.7 mean 14.0, same 14r as the geomscan fallback)
		float distance = e.radius * 14.0f;
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

	// each focal anchor sits one eyepiece focal length inside its end of the tube (f_e ~ the
	// authored eye relief, symmetric relay), the old 0.2/0.8 split stays as the cvar fallback
	float t1 = 0.2f, t2 = 0.8f;
	{
		extern int ps_r__svp_focal_derive;
		extern Fvector4 ps_s3ds_param_1;
		const float L = p_d.magnitude();
		const float fe = ps_s3ds_param_1.y * 0.01f;
		if (ps_r__svp_focal_derive && L > EPS && fe > 0.01f)
		{
			t1 = clampr(fe / L, 0.05f, 0.45f);
			t2 = clampr(1.f - fe / L, 0.55f, 0.95f);
		}
	}
	Device.m_SecondViewport.w_ffp = Fvector(p_d).mul(t1).add(p_e);
	Device.m_SecondViewport.w_sfp = Fvector(p_d).mul(t2).add(p_e);
}

// pip derive the eyepiece/objective from the captured scope-lens meshes, consumed by
// svpCamera and the activation gate (GetSVPCameraMatrix)
static xr_vector<Fvector4> g_pip_hud_geom; // pip diag: snapshot of HUD geometry centers (xyz) + radius (w), captured before render_hud clears the lists

// pip snapshot HUD geometry centers before render_hud clears the lists, deriveScopeLens
// geomscans them after the clear, the call site keeps its gate inline
void svp_snapshot_hud_geom()
{
	g_pip_hud_geom.clear();
	auto snap = [](auto& lst) { for (auto& H : lst) { if (!H.pVisual || !H.pMatrix) continue; auto& VV = H.pVisual->getVisData(); Fvector w; H.pMatrix->transform_tiny(w, VV.sphere.P); Fvector4 e; e.set(w.x, w.y, w.z, VV.sphere.R); g_pip_hud_geom.push_back(e); } };
	snap(RImplementation.GMBase.RGraph.mapHUDSorted.Sorted);
	snap(RImplementation.GMBase.RGraph.mapHUD);
}

// pip one owner of the current optic identity, the lens visual and sphere radius mark the aimed
// optic, a magnifier flip or scope swap changes them with no frame gap so the subscribers reseed
static u32 svp_epoch_update(const void* lens_visual, float lens_radius)
{
	// relative eyepiece-radius jump marking a different optic, migrated from the ratio reseed seed
	const float optic_change = 0.05f;
	static const void* s_prev_vis = nullptr;
	static float s_prev_r = 0.f;
	static u32 s_epoch = 0;
	if (lens_radius <= EPS)
		return s_epoch; // no valid lens this frame holds the identity
	const bool have_prev = (s_prev_vis != nullptr) && (s_prev_r > EPS);
	const bool vis_change = have_prev && (lens_visual != s_prev_vis);
	const bool r_change = have_prev && (_abs(lens_radius - s_prev_r) > s_prev_r * optic_change);
	if (vis_change || r_change)
	{
		++s_epoch;
		extern int ps_r__svp_cop_diag;
		if (ps_r__svp_cop_diag)
			PipMsg("[SVP-EPOCH] %u %s r %.2f->%.2fcm", s_epoch,
				vis_change ? (r_change ? "both" : "visual") : "radius",
				s_prev_r * 100.f, lens_radius * 100.f);
	}
	s_prev_vis = lens_visual;
	s_prev_r = lens_radius;
	return s_epoch;
}

// pip the optics bus, resolve the per-optic inputs once from the fresh eyepiece so one precedence
// and one eps gate feed every consumer, authored_optics folds into the offset, mm from spec then w
static void svp_optics_resolve(CSecondVPParams* p, float er)
{
	extern int ps_r__svp_authored_optics;
	extern Fvector4 scope_objective_lens_offset;
	extern float ps_s3ds_objective_mm;
	Fvector4 off;
	if (ps_r__svp_authored_optics) off = scope_objective_lens_offset;
	else off.set(0.f, 0.f, 0.f, 0.f);
	p->svp_opt_offset = off;
	float mm = 0.f;
	if (ps_s3ds_objective_mm > EPS)
		mm = ps_s3ds_objective_mm; // spec-sheet clear aperture
	else if (off.w > EPS)
		mm = 2000.f * off.w * er; // authored w radius in eyepiece radii to mm
	p->svp_opt_obj_mm = mm;
	extern int ps_r__svp_stats; extern u32 svp_stats_optic_resolve;
	if (ps_r__svp_stats) ++svp_stats_optic_resolve;
	extern int ps_r__svp_cop_diag;
	if (ps_r__svp_cop_diag)
	{
		static u32 s_opt_ms = 0;
		if (Device.dwTimeGlobal - s_opt_ms > 1000)
		{
			s_opt_ms = Device.dwTimeGlobal;
			PipMsg("[SVP-OPTICS] epoch %u off %.2f,%.2f,%.2f,%.2f mm %.1f src %s", p->svp_optic_epoch,
				off.x, off.y, off.z, off.w, mm,
				(ps_s3ds_objective_mm > EPS) ? "spec" : ((off.w > EPS) ? "w" : "none"));
		}
	}
}

void CRender::deriveScopeLens()
{
	// pip clear the flat-panel publish, re-set below when a flat window lens is the eyepiece
	Device.m_SecondViewport.svp_panel_flat = false;

	// multi-lens weapons carry several scope-lens meshes, pick the aimed one, a visible
	// lens bone nearest the camera ray
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
			Fmatrix lensX = *GMBase.svp_pose_of(N.pMatrix);
			bool bone_vis = true;
			CSkeletonX* sk = fast_dynamic_cast<CSkeletonX*>(N.pVisual);
			if (sk)
			{
				bone_vis = sk->SVP_LensBoneVisible();
				Fmatrix boneR;
				if (GMBase.svp_lens_bone_of(N.pVisual, boneR) || sk->SVP_LensBoneXform(boneR))
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
				Fvector bd; V.box.getsize(bd);
				PipMsg("[SVP-LENS] %s dist=%.1fcm fwd=%.3f r=%.1fcm box=%.1fx%.1fx%.1fcm %s",
					bone_vis ? "vis " : "HIDE", dist * 100.f, fwd, V.sphere.R * 100.f,
					bd.x * 100.f, bd.y * 100.f, bd.z * 100.f,
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

	// pip diag, an aimed frame that derives no lens (hip frames also derive, gate on the raise)
	if (!best)
	{
		extern int ps_r__svp_diag;
		if (ps_r__svp_diag && g_pGamePersistent
			&& g_pGamePersistent->m_pGShaderConstants->hud_params.x > 0.05f)
		{
			static u32 s_nb_ms = 0;
			if (Device.dwTimeGlobal - s_nb_ms > 500)
			{
				s_nb_ms = Device.dwTimeGlobal;
				PipMsg("[SVP-LENS] no aimed lens picked, nodes=%u stale_r=%.1fcm",
					(u32)GMBase.RGraph.mapScopeHUDSorted.size(),
					Device.m_SecondViewport.dbg_eyepiece_r * 100.f);
			}
		}
	}

	for (auto& N : GMBase.RGraph.mapScopeHUDSorted)
	{
		if (&N != best)
			continue;

		// a skinned scope lens is positioned by its bone, the captured matrix is only the kinematics
		// root, fold in the lens bone skinning matrix so the eyepiece follows the glass on ADS and sway
		Fmatrix lensX = *GMBase.svp_pose_of(N.pMatrix);
		CSkeletonX* sk = fast_dynamic_cast<CSkeletonX*>(N.pVisual);
		if (sk)
		{
			Fmatrix boneR;
			if (GMBase.svp_lens_bone_of(N.pVisual, boneR) || sk->SVP_LensBoneXform(boneR))
				lensX.mulB_43(boneR);
		}

		auto& V = N.pVisual->getVisData();
		Fvector c;
		V.box.getcenter(c); // AABB center fits a flat lens disc tighter than the bounding sphere center
		Fmatrix m_W = lensX;
		m_W.mulB_43(Fmatrix().translate(c));
		float radius = V.sphere.R;

		auto* p = &Device.m_SecondViewport;

		// publish the optic epoch from the fresh lens identity, before the pose hold so a swap the
		// hold would reject still bumps it, the visual and radius are pose invariant per optic
		p->svp_optic_epoch = svp_epoch_update(N.pVisual, radius);

		// while aimed the eyepiece cannot lurch far from the camera, a stale or exo-arm pose reads
		// double the distance and whips the fit, hold the last good pose instead
		extern int ps_r__svp_lens_reject;
		static Fmatrix s_eyep_hold; static float s_eyep_holdr = 0.f, s_eyep_holdd = 0.f;
		static u32 s_eyep_frame = 0; static bool s_eyep_ok = false;
		static u32 s_eyep_epoch = 0;
		// a session gap or an optic swap drops the hold so the new optic's fresh pose is taken
		if (Device.dwFrame != s_eyep_frame + 1 || p->svp_optic_epoch != s_eyep_epoch) s_eyep_ok = false;
		s_eyep_frame = Device.dwFrame;
		s_eyep_epoch = p->svp_optic_epoch;
		const float aim_x = g_pGamePersistent ? g_pGamePersistent->m_pGShaderConstants->hud_params.x : 0.f;
		const float new_d = m_W.c.distance_to(Device.vCameraPosition);
		if (ps_r__svp_lens_reject && s_eyep_ok && aim_x > 0.9f && new_d > s_eyep_holdd * 1.6f)
		{
			m_W = s_eyep_hold;
			radius = s_eyep_holdr;
		}
		else
		{
			s_eyep_hold = m_W; s_eyep_holdr = radius; s_eyep_holdd = new_d; s_eyep_ok = true;
		}

		p->eyepiece.m_W = m_W;
		p->eyepiece.radius = radius;

		// resolve the optics bus from the fresh eyepiece, the consumers below and downstream read
		// the record instead of the raw cvars
		svp_optics_resolve(p, radius);

		// ballistics sight line, a whole stable copy, logic fires while this frame re-derives
		if (radius > EPS)
		{
			p->svp_sight_pos.set(m_W.c);
			Fvector sk;
			sk.set(m_W.k);
			sk.normalize_safe();
			p->svp_sight_dir.set(sk);
			p->svp_lens_r = radius;
			p->svp_sight_frame = Device.dwFrame;
			p->svp_sight_ok = true;
		}

		// panel aspect from the lens AABB, the two largest extents are the plane W:H (the ~0 axis is
		// the depth), ~1 for a round lens so a conventional scope keeps its square SVP
		Fvector bsz; V.box.getsize(bsz);
		float ea[3] = { _abs(bsz.x), _abs(bsz.y), _abs(bsz.z) };
		if (ea[0] < ea[1]) { float t = ea[0]; ea[0] = ea[1]; ea[1] = t; }
		if (ea[1] < ea[2]) { float t = ea[1]; ea[1] = ea[2]; ea[2] = t; }
		if (ea[0] < ea[1]) { float t = ea[0]; ea[0] = ea[1]; ea[1] = t; }
		p->svp_panel_aspect = (ea[1] > EPS) ? (ea[0] / ea[1]) : 1.f;

		// pip flat-panel on-screen quad for the binocular brackets, plane half-extent world vectors
		// (mesh local x = width, y = height, z = normal)
		{
			extern int ps_r__svp_flat_window;
			extern Fvector4 ps_s3ds_param_3;
			if (ps_r__svp_flat_window && (int)ps_s3ds_param_3.y == 8)
			{
				Fvector we, he;
				m_W.transform_tiny(we, Fvector().set(bsz.x * 0.5f, 0.f, 0.f));
				m_W.transform_tiny(he, Fvector().set(0.f, bsz.y * 0.5f, 0.f));
				p->svp_panel_ax_w.sub(we, m_W.c);
				p->svp_panel_ax_h.sub(he, m_W.c);
				p->svp_panel_flat = true;
			}
		}

		if (p->eyepiece.radius > EPS)
		{
			// pip objective, prefer the real captured front lens on the optical axis, the
			// geomscan finds the forward-most on-axis node as the fallback plane
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
			// authored objective wins, place the front lens at the resolved z along the optical axis
			// with the lateral x/y offset and w radius, all in eyepiece radii
			const Fvector4& off = p->svp_opt_offset;
			if (off.z > EPS)
			{
				const float er = p->eyepiece.radius;
				Fvector fwd; fwd.set(p->eyepiece.m_W.k); fwd.normalize();
				Fvector ri;  ri.set(p->eyepiece.m_W.i);  ri.normalize();
				Fvector up;  up.set(p->eyepiece.m_W.j);  up.normalize();
				p->objective.m_W = p->eyepiece.m_W;
				p->objective.m_W.c.mad(fwd, off.z * er);
				p->objective.m_W.c.mad(ri,  off.x * er);
				p->objective.m_W.c.mad(up,  off.y * er);
				p->objective.radius = (off.w > EPS)
					? off.w * er : p->eyepiece.radius * ps_r__svp_obj_size;
				have_obj = true;
			}
			for (auto& N : GMBase.RGraph.mapScopeHUDObjective)
			{
				if (have_obj)
					break; // authored objective already placed, mesh detection is the next fallback
				if (!N.pVisual || !N.pMatrix)
					break;
				Fmatrix oX = *GMBase.svp_pose_of(N.pMatrix);
				if (CSkeletonX* sk = fast_dynamic_cast<CSkeletonX*>(N.pVisual))
				{
					Fmatrix boneR;
					if (GMBase.svp_lens_bone_of(N.pVisual, boneR) || sk->SVP_LensBoneXform(boneR))
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
				// no distinct objective mesh, derive it geometrically along the optical axis,
				// the eyepiece radius is the only mesh-scale-robust unit
				Fvector fwd; fwd.set(p->eyepiece.m_W.k); fwd.normalize();
				p->objective.m_W = p->eyepiece.m_W;
				// 14r fallback = the authored-set front distance (mod_system_3dss_objective_lenses mean, median 12.7)
				const float dist_r = (geom_front > 0.f ? geom_front : 14.0f) * ps_r__svp_obj_dist;
				p->objective.m_W.c.mad(fwd, p->eyepiece.radius * dist_r);
				p->objective.radius = p->eyepiece.radius * ps_r__svp_obj_size; // eyepiece-relative objective radius (one global knob): the front-node geomscan swung ~6x conflating glass vs bell housing, eyepiece radius is the only mesh-scale-stable unit
			}
			ffp_sfp(); // focal-plane points for the scope shader

			// pip [SVP-DETECT]/[SVP-EYEDIV] one-shot a/b, measured mesh geometry vs authored ltx vs
			// the live derivation, off unless r__svp_diag, latched per aimed optic
			{
				extern int ps_r__svp_diag;
				if (ps_r__svp_diag)
				{
					static const void* s_detect_last = nullptr;
					if (N.pVisual != s_detect_last)
					{
						extern int ps_r__svp_measured_optics;
						const float er = p->eyepiece.radius;
						// authored objective resolved by the optics bus, no second precedence copy
						const float auth_w = p->svp_opt_offset.w;
						const float auth_mm = p->svp_opt_obj_mm;
						// live objective this frame, forward distance and radius in eyepiece radii
						Fvector le; le.sub(p->objective.m_W.c, p->eyepiece.m_W.c);
						const float live_z = (er > EPS) ? le.magnitude() / er : 0.f;
						const float live_w = (er > EPS) ? p->objective.radius / er : 0.f;
						// a weapon transition frame can carry a garbage pose, nan or inf skips the
						// latch so the next frame retries with a real one
						if (_valid(er) && _valid(live_z) && _valid(live_w))
						{
							s_detect_last = N.pVisual;
							SLensDetection d;
							const bool have = sk && sk->SVP_GetLensDetection(d);
							PipMsg("[SVP-DETECT] switch=%d er=%.4f | authored z=%.2f w=%.3f mm=%.1f | live z=%.2f w=%.3f | detected ok=%d z=%.2f w=%.3f mm=%.1f src=%d",
								ps_r__svp_measured_optics, er,
								p->svp_opt_offset.z, auth_w, auth_mm,
								live_z, live_w,
								(int)(have && d.ok),
								(have && d.ok) ? d.offset.z : 0.f,
								(have && d.ok) ? d.offset.w : 0.f,
								(have && d.ok) ? d.mm : 0.f,
								(have && d.ok) ? d.source : -1);
							// live vs detected eyepiece radius, surfaces the near-bone contamination that skews
							// the analytic zoom ratio, read-only, the override stays deferred
							const float det_r = (have && d.ok) ? d.eye_radius : 0.f;
							const float eratio = (det_r > EPS && er > EPS) ? er / det_r : 0.f;
							PipMsg("[SVP-EYEDIV] live_r=%.4f detected_r=%.4f ratio=%.2f src=%d %s",
								er, det_r, eratio, (have && d.ok) ? d.source : -1,
								(eratio > 1.4f || (eratio > EPS && eratio < 0.7f)) ? "DIVERGE" : "agree");
						}
					}
				}
			}
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
