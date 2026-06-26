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
	// objective yellow at the CAMERA (the real entrance the scope views from). the stored p.objective.m_W
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
	// pip floor svp_fov: a near-0 value blows up the vFov/offset tan() math
	if (svp_fov < 1.0f) svp_fov = 1.0f;
	float _, fov, fNearPlane, fFarPlane;
	Device.matrices[0].mProject.decompose_projection(fov, _, fNearPlane, fFarPlane);


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
	float screen_height_NDC = 2.0f;
	float ratio_magnification = screen_height_NDC / scope_height_NDC;

	// the magnification of the scope (1X 4X etc)
	float scope_magnification = fov / deg2rad(svp_fov);

	// expose engine magnification so the shader has a curMag with no 3DSS config
	extern float g_pip_scope_magnification;
	extern float g_pip_scope_min_mag;
	extern float g_pip_scope_max_mag;
	if (svp_fov > EPS)
	{
		g_pip_scope_magnification = scope_magnification;
		// derive min/max mag from hud_fov_params for variable reticles (fixed scope: x == y)
		const Fvector4& fovp = g_pGamePersistent->m_pGShaderConstants->hud_fov_params;
		g_pip_scope_max_mag = (fovp.x > EPS) ? fov / deg2rad(fovp.x * 0.75f) : scope_magnification;
		g_pip_scope_min_mag = (fovp.y > EPS) ? fov / deg2rad(fovp.y * 0.75f) : scope_magnification;
			// pip drive the 3DSS reticle magnification (shader_scope_params curMag/minMag/maxMag) from the
			// engine so the FFP reticle scales with zoom even when the 3DSS zoom script stops updating it
			extern int ps_r__svp_reticle_mag;
			if (ps_r__svp_reticle_mag)
			{
				extern Fvector4 ps_shader_scope_params;
				ps_shader_scope_params.x = g_pip_scope_magnification;
				ps_shader_scope_params.y = g_pip_scope_min_mag;
				ps_shader_scope_params.z = g_pip_scope_max_mag;
			}
	}

	// the fov we render at to get the correct zoom
	float vFov = 2.0f * atan(tan(fov * 0.5f) / (ratio_magnification * scope_magnification));

	// the fov for camera placement
	float vFovMagOnly = 2.0f * atan(tan(fov * 0.5f) / scope_magnification);

	auto camera_offset_from_vfov_and_radius = [](float vFov, float radius) -> float {
		return radius / tan(vFov / 2.0f);
	};

	auto near_plane = fNearPlane;
	auto m_W_svpcam = params.eyepiece.m_W; // default place the camera on the eyepiece
	if (scope_svp_enabled >= 2 && params.objective.radius > EPS)
	{
		// place the camera for the objective lens
		auto d = camera_offset_from_vfov_and_radius(vFovMagOnly, params.eyepiece.radius * 1.4f);
		m_W_svpcam = Fmatrix().mul(params.objective.m_W, Fmatrix().translate(0, 0, -d));
		near_plane = d;
	}


	// pip force the SVP camera up to world up so a canted scope renders upright (optical axis k is kept)
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

	// pip stabilization/recoil-comp run in deriveScopeLens so the camera and disc sampling stay consistent

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

		// pip eye-box drift, the true bore vs the aim as a screen-space offset (tan units). project the
		// captured true bore into the main view, perspective-divide -> NDC offset from center (0 on aim)
		Fvector bv; bv.set(vp.svp_bore_fwd);
		Device.matrices[0].mView.transform_dir(bv); // world -> view space
		const float bz = (bv.z > EPS) ? bv.z : EPS;
		vp.svp_eyebox.set(bv.x / bz, bv.y / bz, ps_r__svp_eyebox, 0.f);
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
	for (auto& N : GMBase.RGraph.mapScopeHUDSorted)
	{
		if (!N.pVisual || !N.pMatrix)
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
			// pip capture the true bore before stabilization reduces it (for the eye-box shadow)
			p->svp_bore_fwd.set(p->eyepiece.m_W.k); p->svp_bore_fwd.normalize();

			// pip steady-scope: blend the lens orientation toward aim to reduce magnified sway (off = 1:1)
			if (ps_r__svp_stabilize > EPS)
			{
				const float keep = 1.0f - ps_r__svp_stabilize; // fraction of the real sway retained
				Fvector aim; aim.set(Device.vCameraDirection); aim.normalize();
				Fvector f; f.set(p->eyepiece.m_W.k); f.normalize();
				f.lerp(aim, f, keep); f.normalize(); // blend bone forward toward aim
				Fvector wup = {0.f, 1.f, 0.f}, right, up;
				right.crossproduct(wup, f);
				if (right.magnitude() > EPS_S)
				{
					right.normalize();
					up.crossproduct(f, right); up.normalize();
					p->eyepiece.m_W.i.set(right);
					p->eyepiece.m_W.j.set(up);
					p->eyepiece.m_W.k.set(f);
				}
			}

			// pip objective: prefer the REAL front lens captured from the mesh (mapScopeHUDObjective).
			// place it at the real front-lens position but along the (stabilized) optical axis so the
			// orientation stays consistent with the eyepiece. fall back to the legacy fixed offset only
			// when the scope flags a single lens surface (objective == ocular)
			// automatic objective distance (geomscan): scan the HUD geometry snapshot (taken before
			// render_hud cleared the lists) for the forward-most on-axis node + its radius = the
			// objective glass plane, in eyepiece radii. clamped, fed to the geometric fallback below
			float geom_front = -1.f;
			{
				const Fvector eye = p->eyepiece.m_W.c;
				Fvector axis; axis.set(p->svp_bore_fwd); axis.normalize();
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
			float dbg_cand_dist = -1.f; // objective-capture distance test (kept: used by the capture gate below)
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
				// relative to it. eyepiece radius is the only mesh-scale-robust unit, refined per scope
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

	// pip double-pass, the captured graph renders once for the main viewport and, when a scope drives
	// the SVP, a second time into TargetSVP, true_pip off keeps the single stock main pass
	const bool svp = Device.true_pip_on && Device.m_SecondViewport.IsSVPActive();
	// pip allocate the SVP target before the main pass derives the camera into it (lazy, only while a
	// PiP scope is aimed, never when off)
	if (Device.true_pip_on && g_pGamePersistent &&
		g_pGamePersistent->m_pGShaderConstants->hud_params.y > 0.005f)
		EnsureTargetSVP();
	if (Device.true_pip_on)
		TargetMain->SetActive();
	renderGBuffer(!svp); // keep the priority-0 graph when an SVP pass follows
	if (svp)
	{
		EnsureTargetSVP();
		TargetSVP->SetActive();
		renderGBuffer(true);
		TargetMain->SetActive(); // shadow generation + main accumulation run on the main target

		// pip install the dual-accumulate hook, each shadow unit builds its map once on the main atlas
		// then this re-accumulates it into the SVP with no second shadow render
		Device.m_SecondViewport.dual_accum = [this](const std::function<void()>& accum)
		{
			TargetSVP->SetActive();   // SVP gbuffer + accumulator (and, for now, the SVP shadow atlas)
			share_main_smaps();       // re-point the shadow atlas at the main maps the generation built
			extern int ps_r__svp_sss_sun;
			Device.m_SecondViewport.force_svp_sss = (ps_r__svp_sss_sun != 0); // sun keeps the SSS contact term
			accum();                  // accumulate this unit into the SVP, reading the shared maps
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
			GMBase.r_dsgraph_render_hud();

			// pip derive the scope lens from the captured HUD, then build the SVP camera (matrices[1])
			// so TargetSVP->SetActive can read it before the SVP pass, only while a PiP scope is aimed
			if (scope_svp_enabled && g_pGamePersistent &&
				g_pGamePersistent->m_pGShaderConstants->hud_params.y > 0.005f)
			{
				deriveScopeLens();
				if (Device.m_SecondViewport.eyepiece.radius > EPS && TargetSVP)
					svpCamera();
			}
		}
		GMBase.r_dsgraph_render_lods(true, clearGraph);
		// pip r__svp_skip_grass drops the near-grass field on the scope pass (mostly off a zoomed cone)
		if (Details && !(svp_pass && ps_r__svp_skip_grass)) Details->Render();
		Target->phase_scene_end();
	}

	if (svp_cull || svp_cull_grass)
		CDSGraphManager::svp_cull_end(); // pip end SVP cull, the shared shadow/light passes below are unaffected

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
	{
		// level
		phase = PHASE_NORMAL;
		//	Igor: we don't want to render old lods on next frame.
		GMBase.r_dsgraph_render_static(1); // normal level, secondary priority
		CParticlesAsync::Wait();
		GMBase.r_dsgraph_render_dynamic(1);
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
