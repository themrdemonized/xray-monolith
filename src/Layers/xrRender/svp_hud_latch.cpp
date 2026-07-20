#include "stdafx.h"

#include "../../xrEngine/render.h"
#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/environment.h"
#include "../../xrEngine/CustomHUD.h"

#include "FBasicVisual.h"
#include "CHudInitializer.h"

#include "fhierrarhyvisual.h"
#include "SkeletonCustom.h"
#include "SkeletonX.h"
#include "../../xrEngine/fmesh.h"
#include "flod.h"

#include "../../xrEngine/xr_object.h"

using namespace R_dsgraph;

// pip object-space render (skinning) matrix of the lens bone, the SVP eyepiece uses it so a skinned
// scope lens follows its bone through ADS and sway instead of the kinematics root
bool CSkeletonX::SVP_LensBoneXform(Fmatrix& out)
{
	if (!Parent)
		return false;
	u16 bone;
	if (RenderMode == RM_SINGLE)
		bone = (u16)RMS_boneid;
	else if (BonesUsed.size())
		bone = BonesUsed[0];
	else
		return false;
	out = Parent->LL_GetBoneInstance(bone).mRenderTransform;
	return true;
}

// pip lens bone visibility, hidden markswitch lens meshes must not win the eyepiece pick
bool CSkeletonX::SVP_LensBoneVisible()
{
	if (!Parent)
		return true;
	u16 bone;
	if (RenderMode == RM_SINGLE)
		bone = (u16)RMS_boneid;
	else if (BonesUsed.size())
		bone = BonesUsed[0];
	else
		return true;
	return !!Parent->LL_GetBoneVisible(bone);
}

// pip forward the owning kinematics measured lens fit, lets the render side query detection
// from a lens leaf visual, false when unparented
bool CSkeletonX::SVP_GetLensDetection(SLensDetection& out)
{
	if (!Parent)
		return false;
	return Parent->GetLensDetection(out);
}

// pip snapshot the distinct hud matrices as the housing draws, logic keeps rewriting them mid
// render and the scope draws later in the frame must not pick up a newer pose
void CDSGraphManager::svp_latch_hud_poses()
{
	m_svp_pose.count = 0;
	m_svp_pose.frame = Device.dwFrame;
	auto grab = [this](Fmatrix* p)
	{
		if (!p || m_svp_pose.count >= 8)
			return;
		for (u32 i = 0; i < m_svp_pose.count; ++i)
			if (m_svp_pose.src[i] == p)
				return;
		m_svp_pose.src[m_svp_pose.count] = p;
		m_svp_pose.val[m_svp_pose.count] = *p;
		++m_svp_pose.count;
	};
	for (auto& n : RGraph.mapHUD)
		grab(n.pMatrix);
#if defined(USE_DX11)
	for (auto& n : RGraph.mapScopeHUDSorted)
		grab(n.pMatrix);
	for (auto& n : RGraph.mapScopeHUDObjective)
		grab(n.pMatrix);
	for (auto& n : RGraph.mapReflexHUDSorted)
		grab(n.pMatrix);

	// lens bones move on the logic thread too, sample them once beside the poses
	m_svp_bone.count = 0;
	m_svp_bone.frame = Device.dwFrame;
	auto grab_bone = [this](dxRender_Visual* v)
	{
		if (!v || m_svp_bone.count >= 4)
			return;
		for (u32 i = 0; i < m_svp_bone.count; ++i)
			if (m_svp_bone.vis[i] == v)
				return;
		CSkeletonX* sk = fast_dynamic_cast<CSkeletonX*>(v);
		Fmatrix b;
		if (sk && sk->SVP_LensBoneXform(b))
		{
			m_svp_bone.vis[m_svp_bone.count] = v;
			m_svp_bone.bone[m_svp_bone.count] = b;
			++m_svp_bone.count;
		}
	};
	for (auto& n : RGraph.mapScopeHUDSorted)
		grab_bone(n.pVisual);
	for (auto& n : RGraph.mapScopeHUDObjective)
		grab_bone(n.pVisual);
	for (auto& n : RGraph.mapReflexHUDSorted)
		grab_bone(n.pVisual);
#endif
}

// the latched pose while this frame's latch is live, else the caller's pointer unchanged
Fmatrix* CDSGraphManager::svp_pose_of(Fmatrix* p)
{
	if (m_svp_pose.frame == Device.dwFrame)
		for (u32 i = 0; i < m_svp_pose.count; ++i)
			if (m_svp_pose.src[i] == p)
				return &m_svp_pose.val[i];
	return p;
}

// the latched lens bone while this frame's latch is live, false falls back to the live read
bool CDSGraphManager::svp_lens_bone_of(dxRender_Visual* v, Fmatrix& out)
{
	if (m_svp_bone.frame == Device.dwFrame)
		for (u32 i = 0; i < m_svp_bone.count; ++i)
			if (m_svp_bone.vis[i] == v)
			{
				out = m_svp_bone.bone[i];
				return true;
			}
	return false;
}

// pip drain only the weapon list into the scope image, the caller owns the view/projection
int g_svp_hud_skip_scope = 0; // hud_full mode: 1 drops the scope body, 2 also drops pieces fully behind the front lens
float g_svp_hud_front_m = 0.f; // scope-body front extent along the axis from the eyepiece (m), clip-ons push it past the objective
void CDSGraphManager::r_dsgraph_render_hud_svp()
{
	PROF_EVENT("r_dsgraph_render_hud_svp");
	// mode 1 keeps the heuristic body skip, the mode 2 pupil camera near plane replaces it
	{
		extern int ps_r__svp_hud_full;
		extern int scope_svp_enabled;
		g_svp_hud_skip_scope = (scope_svp_enabled == 1) ? ps_r__svp_hud_full : 0;
	}
	// same camera and projection as the world, real depth so the scene occludes the weapon
	// sliver and vice versa (rmNear depth-fronting belonged to the retired separate camera)
	RImplementation.rmNormal();
	auto& graph = RGraph.mapHUD;
	if (!graph.empty())
	{
		std::sort(graph.begin(), graph.end());
		// the scope body hugs the eyepiece-objective axis, everything else on the weapon (barrel,
		// sights, hands) sits off-axis. the size cap keeps a merged whole-gun mesh from matching
		const auto& vp = Device.m_SecondViewport;
		const Fvector A = vp.eyepiece.m_W.c;
		Fvector ax; ax.sub(vp.objective.m_W.c, A);
		const float len2 = ax.square_magnitude();
		const float tube = _sqrt(len2);
		const float rcyl = std::max(vp.eyepiece.radius, vp.objective.radius) * 1.75f + 0.01f;
		const bool measure_ok = len2 > EPS && vp.eyepiece.radius > EPS;
		const bool skip_ok = g_svp_hud_skip_scope && measure_ok;
		float front = 0.f;
		float clipon_front = 0.f;
		// the clip-on window widens to the authored front plane when the per-scope data reaches
		// past the fixed bound, widening only so no rig loses a classification it had
		float clip_hi = 2.6f;
		{
			extern int ps_r__svp_drain_anchor;
			const float auth_z = vp.svp_opt_offset.z; // bus resolved, authored_optics folded in
			if (ps_r__svp_drain_anchor && auth_z > EPS && tube > EPS)
			{
				const float t_front = auth_z * vp.eyepiece.radius / tube;
				if (t_front > clip_hi)
					clip_hi = t_front;
			}
		}
		extern int ps_r__svp_cop_diag;
		static u32 s_hud_diag_ms = 0;
		const bool diag = ps_r__svp_cop_diag && (Device.dwTimeGlobal - s_hud_diag_ms > 3000);
		if (diag)
		{
			s_hud_diag_ms = Device.dwTimeGlobal;
			PipMsg("[SVP-HUD] tube=%.1fcm rcyl=%.1fcm cap=%.1fcm items=%u skip_ok=%d front=%.1fcm",
				tube * 100.f, rcyl * 100.f, tube * 1.75f * 100.f, (u32)graph.size(), (int)skip_ok,
				g_svp_hud_front_m * 100.f);
		}
		extern int ps_r__svp_stats; extern u32 svp_stats_hud_cull_reject; extern u32 svp_ledger_hud_cull_reject;
		for (auto& item : graph)
		{
			if (svp_cull_reject(item.pVisual, svp_pose_of(item.pMatrix))) { if (ps_r__svp_stats) ++svp_stats_hud_cull_reject; svp_ledger_hud_cull_reject = 1; continue; } // pip skip off-cone SVP geometry
			dxRender_Visual* V = item.pVisual;
			VERIFY(V && V->shader._get());
			bool drop = false;
			bool clip_obj = false;
			float t = 0.f, rad = -1.f;
			if (measure_ok && item.pMatrix)
			{
				// skinned parts (addon scopes) sit at their bone, the rest-pose sphere lies elsewhere
				Fmatrix W = *svp_pose_of(item.pMatrix);
				CSkeletonX* sk = fast_dynamic_cast<CSkeletonX*>(V);
				if (sk)
				{
					Fmatrix boneR;
					if (svp_lens_bone_of(V, boneR) || sk->SVP_LensBoneXform(boneR))
						W.mulB_43(boneR);
				}
				Fvector c; W.transform_tiny(c, V->vis.sphere.P);
				Fvector ac; ac.sub(c, A);
				t = ac.dotproduct(ax) / len2;
				// radial distance to the axis LINE, the ocular stack (eyecup, rear housing) sits
				// on-axis behind the eyepiece and must match too or it shows as rings in the image
				Fvector p; p.set(A); p.mad(ax, t);
				rad = p.distance_to(c);
				// mounts wrap the tube with the center pulled off-axis, the size cap keeps a
				// barrel sphere from impersonating a clamp
				const float R = V->vis.sphere.R;
				const bool wrap = (rad < R * 0.6f) && (R < tube * 0.3f);
				// clip-on optics sit coaxial PAST the objective, the tight radial keeps the
				// under-slung barrel out of this branch
				const bool clipon = (t >= 1.4f && t < clip_hi) && (rad < rcyl * 0.8f);
				drop = (R < tube * 1.75f) &&
					(clipon || ((t > -0.6f && t < 1.4f) && (rad < rcyl || wrap)));
				// only coaxial optic bodies define the front plane, a wrap mount's sphere is
				// length-dominated and says nothing about the front lens
				if (drop && (rad < rcyl || clipon))
					front = _max(front, t * tube + R);
				// a real clip-on front is a flat round disc, its mesh box has two long axes and a thin
				// one, a front-sight post is a spike and must not push the plane past the objective
				Fvector bs; V->vis.box.getsize(bs);
				const float bmax = std::max(bs.x, std::max(bs.y, bs.z));
				const float bmin = std::min(bs.x, std::min(bs.y, bs.z));
				const float bmid = bs.x + bs.y + bs.z - bmax - bmin;
				const bool disc_like = (bmax > EPS) && (bmid > 0.5f * bmax) && (bmin < 0.5f * bmax);
				if (clipon && disc_like)
					clipon_front = _max(clipon_front, t * tube + R);
				// mode 2 drops pieces wholly behind the front plane, spanning pieces render whole
				const float plane = (g_svp_hud_front_m > EPS) ? g_svp_hud_front_m : tube;
				if (!drop && g_svp_hud_skip_scope >= 2 && (t * tube + R) < plane)
					drop = true;
				// the objective clip drops the barrel wholly behind the authored front lens, w2
				// near-blur dissolves the surviving forward sliver edge
				extern int ps_r__svp_drain_clip;
				if (ps_r__svp_drain_clip && vp.svp_opt_offset.z > EPS && vp.eyepiece.radius > EPS
					&& (t * tube + R) < vp.svp_opt_offset.z * vp.eyepiece.radius)
					clip_obj = true;
			}
			if (diag)
			{
				auto tx = V->GetTexture();
				PipMsg("[SVP-HUD] %s t=%.2f rad=%.1fcm R=%.1fcm %s", (drop || clip_obj) ? "SKIP" : "keep",
					t, rad * 100.f, V->vis.sphere.R * 100.f, tx ? tx->cName.c_str() : "?");
			}
			if ((drop && skip_ok) || clip_obj) continue;
			RCache.set_Element(item.pSE);
			RCache.set_xform_world(*svp_pose_of(item.pMatrix));
			RImplementation.apply_object(item.pObject);
			RImplementation.apply_lmaterial();
			V->Render(svp_drain_lod(item.ssa, V->vis.sphere.R));
		}
		// consumed by next frame's near plane, the authored (or detected) objective plane wins,
		// a confirmed clip-on front sitting beyond it keeps the plane ahead of the ring
		const float auth_z = vp.svp_opt_offset.z; // bus resolved, authored_optics folded in
		g_svp_hud_front_m = _max(front, clipon_front);
		if (auth_z > EPS && vp.eyepiece.radius > EPS)
		{
			const float authored = auth_z * vp.eyepiece.radius;
			g_svp_hud_front_m = _max(authored, clipon_front);
		}
		graph.clear();
	}
	RCache.set_xform_world(Fidentity);
	RImplementation.rmNormal();
}
