#include "stdafx.h"

#include "fhierrarhyvisual.h"
#include "SkeletonCustom.h"
#include "../../xrEngine/fmesh.h"
#include "../../xrEngine/irenderable.h"

#include "flod.h"
#include "particlegroup.h"
#include "FTreeVisual.h"

using namespace R_dsgraph;

////////////////////////////////////////////////////////////////////////////////////////////////////
// Scene graph actual insertion and sorting ////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
float r_ssaDISCARD;
float r_ssaDONTSORT;
float r_ssaLOD_A, r_ssaLOD_B;
float r_ssaGLOD_start, r_ssaGLOD_end;
float r_ssaHZBvsTEX;

ICF float CalcSSA(float& distSQ, Fvector& C, float R)
{
	distSQ = Device.vCameraPosition.distance_to_sqr(C) + EPS;
    return (R * R / distSQ);
}

ICF float CalcSSA(float& distSQ, Fvector& C, dxRender_Visual* V)
{
    return CalcSSA(distSQ, C, V->vis.sphere.R);
}

void CDSGraphManager::r_dsgraph_insert_dynamic(dxRender_Visual *pVisual, Fmatrix* xform)
{
	Fvector Center;
	xform->transform_tiny(Center, pVisual->vis.sphere.P);

	float distSQ;
	float SSA = CalcSSA(distSQ, Center, pVisual);
    Flags16& flags = pVisual->flags;
    ShaderElement* sh_d = &*pVisual->shader->E[4];
    if (!(flags.test(IRenderVisualFlags::eIgnoreOptimization) || (sh_d && sh_d->flags.bEmissive)))
    {
		// pip the ssa is main-view, the magnified scope replays this shared graph and resolves
		// mag-times smaller parts (distant NPC heads), scale the discard by the scope coverage
		float ssa_discard = r_ssaDISCARD;
		{
			extern float g_pip_scope_ratio;
			extern float g_pip_scope_magnification;
			if (ps_r__svp_npc_detail && Device.true_pip_on && Device.m_SecondViewport.IsSVPActive()
				&& g_pip_scope_magnification > 1.f)
			{
				const float m = g_pip_scope_ratio * g_pip_scope_magnification;
				ssa_discard /= m * m;
			}
		}
        if (SSA < ssa_discard)
        {
            //Msg("SSA %.2f discarded", SSA);
            return;
        }
    }
    

	// HOM occlusion culling for dynamic objects
    if (ps_r__common_flags.test(RFLAG_HOM_DYNAMIC))
    {
#if RENDER!=R_R1
        if (i_mask[CDSGraphManager::fl_normal])
#endif
        {
            Fbox world_bb;
            world_bb.xform(pVisual->vis.box, *xform);
            if (!RImplementation.HOM.visible(world_bb))
                return;
        }
    }

	// Distortive geometry should be marked and R2 special-cases it
	// a) Allow to optimize RT order
	// b) Should be rendered to special distort buffer in another pass
	VERIFY(pVisual->shader._get());

	if (sh_d && sh_d->flags.bDistort && i_mask[sh_d->flags.iPriority/2])
	{
		// pip the weapon's own hud-mode heat haze paints its warp across the composited lens while
		// scoped, drop just that, psi and controller filter overlays keep rendering
		bool drop = false;
		if (ps_r__svp_skip_hud_distort && Device.true_pip_on && Device.m_SecondViewport.IsSVPActive())
		{
			IParticleCustom* pc = pVisual->dcast_ParticleCustom();
			drop = pc && pc->GetHudMode() && pc->GetWeaponFX();
		}
		if (!drop)
		{
			if (i_mask[CDSGraphManager::fl_hud])
				RGraph.mapHUDSorted.Distort.emplace_back(distSQ, SSA, val_pObject, pVisual, xform, sh_d, i_mask[CDSGraphManager::fl_hud]);
			else
				RGraph.mapDynamicSorted.Distort.emplace_back(distSQ, SSA, val_pObject, pVisual, xform, sh_d, i_mask[CDSGraphManager::fl_hud]);
		}
	}

	// Select shader
	ShaderElement* sh = RImplementation.rimp_select_sh_dynamic(pVisual, distSQ);

	if (0==sh)
		return;
	u32 shader_priority = sh->flags.iPriority/2;
	if (!i_mask[shader_priority])
		return;

	// Create common node
	// NOTE: Invisible elements exist only in R1

#if defined(USE_DX11) //  Redotix99: for 3D Shader Based Scopes
	// pip true-PiP scope capture, keep the eye-nearest eyepiece lens (==3) for the SVP composite, and
	// once the SVP is active drop the back-glass (==1) / zwrite (==2) since the SVP draws the whole lens
	if (Device.true_pip_on && sh->flags.iScopeLense > 0)
	{
		if (sh->flags.iScopeLense == 3)
		{
			// a scope can flag several lens surfaces (objective + ocular), keep the one in front of the eye
			// and nearest it (the ocular the player looks through) for the SVP composite, and separately
			// keep the FARTHEST in-front surface (the objective) as real geometry for the svpscope-2 camera
			Fvector lp, to;
			xform->transform_tiny(lp, pVisual->getVisData().sphere.P);
			to.sub(lp, Device.vCameraPosition);
			const bool ahead = to.dotproduct(Device.vCameraDirection) > 0.f;
			const float score = ahead ? to.square_magnitude() : (to.square_magnitude() + 1.0e6f);

			// ocular = nearest in-front lens (the SVP composite surface, also the drawn lens)
			auto& M = RGraph.mapScopeHUDSorted;
			bool keep_oc = M.empty();
			if (!keep_oc)
			{
				auto& f = M.front();
				Fvector ep, te;
				f.pMatrix->transform_tiny(ep, f.pVisual->getVisData().sphere.P);
				te.sub(ep, Device.vCameraPosition);
				const float fscore = (te.dotproduct(Device.vCameraDirection) > 0.f) ? te.square_magnitude() : (te.square_magnitude() + 1.0e6f);
				keep_oc = score < fscore;
			}
			if (keep_oc)
			{
				M.clear();
				M.emplace_back(distSQ, SSA, val_pObject, pVisual, xform, sh, i_mask[CDSGraphManager::fl_hud]);
			}

			// objective = farthest in-front lens (front of the scope), geometry only for the camera, never drawn
			if (ahead)
			{
				auto& O = RGraph.mapScopeHUDObjective;
				bool keep_obj = O.empty();
				if (!keep_obj)
				{
					auto& f = O.front();
					Fvector op, te;
					f.pMatrix->transform_tiny(op, f.pVisual->getVisData().sphere.P);
					te.sub(op, Device.vCameraPosition);
					keep_obj = (te.dotproduct(Device.vCameraDirection) > 0.f) && (score > te.square_magnitude());
				}
				if (keep_obj)
				{
					O.clear();
					O.emplace_back(distSQ, SSA, val_pObject, pVisual, xform, sh, i_mask[CDSGraphManager::fl_hud]);
				}
			}
			return;
		}
		if (sh->flags.iScopeLense == 10)
		{
			// dedup per visual, the HUD capture inserts the same reflex mesh many times per frame and
			// draw_reflex would otherwise draw it hundreds of times
			for (const auto& n : RGraph.mapReflexHUDSorted)
				if (n.pVisual == pVisual)
					return;
			RGraph.mapReflexHUDSorted.emplace_back(distSQ, SSA, val_pObject, pVisual, xform, sh, i_mask[CDSGraphManager::fl_hud]);
			return;
		}
		if (Device.m_SecondViewport.IsSVPActive())
			return; // the SVP composite draws the whole lens, skip the back-glass / zwrite
		// a fake / not-yet-active optic, fall through to the legacy ==1/==2 handling below
	}
	switch (sh->flags.iScopeLense) {
		case 0:
			break;

		case 1: {
			RGraph.mapHUD.emplace_back(EPS, SSA, val_pObject, pVisual, xform, sh, i_mask[CDSGraphManager::fl_hud]);

			// SSS: Deprecated
			/*if (!sh->passes[0]->ps->hud_disabled)
			{
				HUDMask_Node* N2 = HUDMask.insertInAnyWay(EPS);
				N2->val.ssa = SSA;
				N2->val.pObject = RI.val_pObject;
				N2->val.pVisual = pVisual;
				N2->val.Matrix = *RI.val_pTransform;
				N2->val.se = sh;
			}*/
			return;
		}

		case 2: {
			RGraph.mapScopeHUD.emplace_back(distSQ, SSA, val_pObject, pVisual, xform, sh, i_mask[CDSGraphManager::fl_hud]);
			return;
		}

		case 3: {
			RGraph.mapScopeHUDSorted.emplace_back(distSQ, SSA, val_pObject, pVisual, xform, sh, i_mask[CDSGraphManager::fl_hud]);
			return;
		}
	}
#endif
	// HUD rendering
	if (i_mask[CDSGraphManager::fl_hud])
	{
		if (sh->flags.bStrictB2F)
		{
#if RENDER!=R_R1
			if (sh->flags.bEmissive)
			{
				if (i_mask[CDSGraphManager::fl_cam])
					RGraph.mapCamAttachedSorted.Emissive.emplace_back(distSQ, SSA, val_pObject, pVisual, xform, sh_d, i_mask[CDSGraphManager::fl_cam]);
				else
					RGraph.mapHUDSorted.Emissive.emplace_back(distSQ, SSA, val_pObject, pVisual, xform, sh_d, i_mask[CDSGraphManager::fl_hud]);
			}
#endif // RENDER!=R_R1
			if (i_mask[CDSGraphManager::fl_cam])
				RGraph.mapCamAttachedSorted.Sorted.emplace_back(distSQ, SSA, val_pObject, pVisual, xform, sh, i_mask[CDSGraphManager::fl_cam]);
			else
				RGraph.mapHUDSorted.Sorted.emplace_back(distSQ, SSA, val_pObject, pVisual, xform, sh, i_mask[CDSGraphManager::fl_hud]);
			return;
		}
		else
		{
			if (i_mask[CDSGraphManager::fl_cam])
				RGraph.mapCamAttached.emplace_back(distSQ, SSA, val_pObject, pVisual, xform, sh, i_mask[CDSGraphManager::fl_cam]);
			else
				RGraph.mapHUD.emplace_back(distSQ, SSA, val_pObject, pVisual, xform, sh, i_mask[CDSGraphManager::fl_hud]);

			/*
#if RENDER==R_R4
			if (RImplementation.o.ssfx_core && !sh->passes[0]->ps->hud_disabled)
			{
				HUDMask_Node* N2 = val_bCamAttached ? HUDMaskCamAttached.insertInAnyWay(distSQ) : HUDMask.insertInAnyWay(distSQ);
				N2->val.ssa = SSA;
				N2->val.pObject = RI.val_pObject;
				N2->val.pVisual = pVisual;
				N2->val.Matrix = *RI.val_pTransform;
				N2->val.se = sh;
			}
#endif*/

#if RENDER!=R_R1
			if (sh->flags.bEmissive)
			{
				if (i_mask[CDSGraphManager::fl_cam])
					RGraph.mapCamAttachedSorted.Emissive.emplace_back(distSQ, SSA, val_pObject, pVisual, xform, sh_d, i_mask[CDSGraphManager::fl_cam]);
				else
					RGraph.mapHUDSorted.Emissive.emplace_back(distSQ, SSA, val_pObject, pVisual, xform, sh_d, i_mask[CDSGraphManager::fl_hud]);
			}
				
#endif	//	RENDER!=R_R1

			return;
		}
	}

	// Shadows registering
#if RENDER==R_R1
	DSGraphItem<u32, false> item = { 0, SSA, val_pObject, pVisual, xform, nullptr, i_mask[CDSGraphManager::fl_hud] };
	R_dsgraph::mapDSGraphItemsMap<u32, false>::TNode N = { 0, item };
	RImplementation.L_Shadows->add_element(N);
#endif
	if (i_mask[CDSGraphManager::fl_invisible])
		return;

	// strict-sorting selection
	if (sh->flags.bStrictB2F && !pVisual->dcast_ParticleCustom())
	{
		RGraph.mapDynamicSorted.Sorted.emplace_back(distSQ, SSA, val_pObject, pVisual, xform, sh, i_mask[CDSGraphManager::fl_hud]);
		return;
	}

#if RENDER!=R_R1
	// Emissive geometry should be marked and R2 special-cases it
	// a) Allow to skeep already lit pixels
	// b) Allow to make them 100% lit and really bright
	// c) Should not cast shadows
	// d) Should be rendered to accumulation buffer in the second pass
	if (sh->flags.bEmissive)
		RGraph.mapDynamicSorted.Emissive.emplace_back(distSQ, SSA, val_pObject, pVisual, xform, sh_d, i_mask[CDSGraphManager::fl_hud]);

	if (sh->flags.bWmark && i_mask[CDSGraphManager::fl_wmarks])
	{
		if (i_mask[CDSGraphManager::fl_hud])
			RGraph.mapHUDSorted.Wmark.emplace_back(distSQ, SSA, val_pObject, pVisual, xform, sh, i_mask[CDSGraphManager::fl_hud]);
		else
			RGraph.mapDynamicSorted.Wmark.emplace_back(distSQ, SSA, val_pObject, pVisual, xform, sh, i_mask[CDSGraphManager::fl_hud]);
		return;
	}
#endif

	for (u32 iPass = 0; iPass < sh->passes.size(); ++iPass)
	{
		// the most common node
		
		if (sh->passes[iPass] == nullptr)
		{
			continue;
		}

		SPass& pass = *sh->passes[iPass];

#if RENDER==R_R1
		AddToRenderQueue(RGraph.mapDynamicPasses[shader_priority][iPass], item, pass);
#else
		AddToRenderQueue(RGraph.mapDynamicPasses[shader_priority][iPass], { 0, SSA, val_pObject, pVisual, xform, nullptr, i_mask[CDSGraphManager::fl_hud] }, pass);
#endif
	}
}

extern float ps_r__ssaDISCARD_exp;
extern float ps_r__ssaDISCARD_fade_k;
void CDSGraphManager::r_dsgraph_insert_static(dxRender_Visual *pVisual)
{
	if (m_static_seen.find(pVisual) != m_static_seen.end())
	{
		if (PortalTraverseDbg_Enabled())
		{
			PortalTraverseDebugStats& dbg = PortalTraverseDbg_Get();
			const bool opt_bucket = PortalTraverseDbg_IsOptions(i_options);
			++dbg.static_dedup_skipped;
			if (opt_bucket)
				++dbg.static_dedup_skipped_opt;
			else
				++dbg.static_dedup_skipped_noopt;
		}
		return;
	}
	m_static_seen.insert(pVisual);
	if (PortalTraverseDbg_Enabled())
	{
		PortalTraverseDebugStats& dbg = PortalTraverseDbg_Get();
		const bool opt_bucket = PortalTraverseDbg_IsOptions(i_options);
		++dbg.static_dedup_seen;
		if (opt_bucket)
			++dbg.static_dedup_seen_opt;
		else
			++dbg.static_dedup_seen_noopt;
	}

	float distSQ;
	float SSA = CalcSSA(distSQ, pVisual->vis.sphere.P, pVisual);
    Flags16& flags = pVisual->flags;
    ShaderElement* sh_d = &*pVisual->shader->E[4];
    if (!(flags.test(IRenderVisualFlags::eIgnoreOptimization) || (sh_d && sh_d->flags.bEmissive)))
    {
        if (SSA < r_ssaDISCARD)
            return;

        // demonized: Replace hard cutoff with gradient cutoff
        // Smaller objects that fail the SSA test will still render depending on how much smaller they are than the discard limit.
        // Reduces the "rendering radius" effect and makes pop-in less noticeable
        // Allows to increase the discard limit for better performance without making pop-in much worse
        // Define where the "thinning" begins. 
        // E.g., objects 4x the size of the discard limit start fading.
        float fade_start = r_ssaDISCARD * ps_r__ssaDISCARD_fade_k;

        // The Gradient Zone
        if (SSA < fade_start)
        {
            // Calculate a linear survival probability between 0.0 and 1.0
            float survival_chance = (SSA - r_ssaDISCARD) / (fade_start - r_ssaDISCARD);

            // Convert the 32-bit hash to a float between 0.0 and 1.0
            // Multiplying by 1.0 / 2^32 is faster than float division
            u32 hash = GetFvectorHash(pVisual->vis.sphere.P);
            constexpr float hash_to_float = 1.0f / 4294967296.0f;
            float val = hash * hash_to_float;

            // If the object's hash value is higher than its survival chance, cull it
            if (val > _powf(survival_chance, ps_r__ssaDISCARD_exp))
                return;
        }
    }

	// Distortive geometry should be marked and R2 special-cases it
	// a) Allow to optimize RT order
	// b) Should be rendered to special distort buffer in another pass
	VERIFY(pVisual->shader._get());
	if (sh_d && sh_d->flags.bDistort && i_mask[sh_d->flags.iPriority/2])
		RGraph.mapStaticSorted.Distort.emplace_back(distSQ, SSA, nullptr, pVisual, &Fidentity, sh_d, false);

	// Select shader
	ShaderElement* sh = RImplementation.rimp_select_sh_static(pVisual, distSQ);

	if (0 == sh)
		return;
	u32 shader_priority = sh->flags.iPriority / 2;
	if (!i_mask[shader_priority])
		return;

	// Water rendering
#if RENDER==R_R4
	if (sh->flags.isWater && RImplementation.o.ssfx_water)
	{
		RGraph.mapWater.emplace_back(distSQ, SSA, nullptr, pVisual, &Fidentity, sh, false);
		return;
	}
#endif

	// strict-sorting selection
	if (sh->flags.bStrictB2F)
	{
		RGraph.mapStaticSorted.Sorted.emplace_back(distSQ, SSA, nullptr, pVisual, &Fidentity, sh, false);
		return;
	}

#if RENDER!=R_R1
	// Emissive geometry should be marked and R2 special-cases it
	// a) Allow to skeep already lit pixels
	// b) Allow to make them 100% lit and really bright
	// c) Should not cast shadows
	// d) Should be rendered to accumulation buffer in the second pass
	if (sh->flags.bEmissive)
		RGraph.mapStaticSorted.Emissive.emplace_back(distSQ, SSA, nullptr, pVisual, &Fidentity, sh_d, false );

	if (sh->flags.bWmark && i_mask[CDSGraphManager::fl_wmarks])
	{
		RGraph.mapStaticSorted.Wmark.emplace_back(distSQ, SSA, nullptr, pVisual, &Fidentity, sh, false);
		return;
	}
#endif

	for (u32 iPass = 0; iPass < sh->passes.size(); ++iPass)
	{
		// the most common node
		if (sh->passes[iPass] == nullptr)
			continue;

		SPass& pass	= *sh->passes[iPass];

		AddToRenderQueue(RGraph.mapStaticPasses[shader_priority][iPass], { 0, SSA, nullptr, pVisual, nullptr, nullptr, false }, pass);
	}
}

void CDSGraphManager::AddToRenderQueue(R_dsgraph::RenderQueue& queue, const R_dsgraph::DSGraphItem<u32, false>& item, const SPass& pass)
{
	if (PortalTraverseDbg_Enabled())
	{
		PortalTraverseDebugStats& dbg = PortalTraverseDbg_Get();
		const bool opt_bucket = PortalTraverseDbg_IsOptions(i_options);
		// Static items use nullptr matrix/object in current pipeline.
		if (item.pMatrix == nullptr && item.pObject == nullptr)
		{
			++dbg.queue_static_packets;
			if (opt_bucket)
				++dbg.queue_static_packets_opt;
			else
				++dbg.queue_static_packets_noopt;
		}
		else
		{
			++dbg.queue_dynamic_packets;
			if (opt_bucket)
				++dbg.queue_dynamic_packets_opt;
			else
				++dbg.queue_dynamic_packets_noopt;
		}
	}

	queue.emplace_back(item, pass);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
void CDSGraphManager::add_Dynamic(IRenderVisual* piVisual, Fmatrix* xform)
{
	
	dxRender_Visual* pVisual = (dxRender_Visual*)piVisual;
	if (!pVisual) return;

	Flags16& flags = piVisual->flags;

	if (!i_mask[CDSGraphManager::fl_normal] && !!flags.test(IRenderVisualFlags::eNoShadow))
		return;

	// Visual is 100% visible - simply add it
	switch (pVisual->Type)
	{
	case MT_PARTICLE_GROUP:
	{
		// Add all children, doesn't perform any tests
		PS::CParticleGroup* pG = (PS::CParticleGroup*)pVisual->dcast_ParticleCustom();
		xrCriticalSectionGuard guard(&pG->onframe_lock);
		for (PS::CParticleGroup::SItem& I_ : pG->items)
		{
			add_Dynamic(I_._effect, xform);
			add_leafs_Dynamic(I_._children_related, xform);
			add_leafs_Dynamic(I_._children_free, xform);
		}
	}return;

	case MT_HIERRARHY:
	{
		// Add all children, doesn't perform any tests
		FHierrarhyVisual* pV = (FHierrarhyVisual*)pVisual;
		add_leafs_Dynamic(pV->children, xform);
	}return;

	case MT_SKELETON_ANIM:
	case MT_SKELETON_RIGID:
	{
		// Add all children, doesn't perform any tests
		CKinematics* pV = (CKinematics*)pVisual;
		BOOL _use_lod = FALSE;
		if (pV->m_lod)
		{
			Fvector Tpos;
			float D;
			xform->transform_tiny(Tpos, pV->vis.sphere.P);
			float ssa = CalcSSA(D, Tpos, pV->vis.sphere.R * 0.5f);	// assume dynamics never consume full sphere
			if (ssa < r_ssaLOD_A)
				_use_lod = TRUE;
		}
		if (_use_lod)
			add_Dynamic(pV->m_lod, xform);
		else
		{
			//pV->CalculateBones(TRUE);
			if (i_mask[CDSGraphManager::fl_normal])
				pV->CalculateWallmarks();
			add_leafs_Dynamic(pV->children, xform);
		}
	}return;

	default:
	{
		// General type of visual
		// Calculate distance to it's center
		r_dsgraph_insert_dynamic(pVisual, xform);
	}return;
	}
}

void CDSGraphManager::add_Dynamic(dxRender_Visual* pVisual, Fmatrix* xform)
{
	if (!pVisual) return;

	Flags16& flags = pVisual->dcast_RenderVisual()->flags;

	if (!i_mask[CDSGraphManager::fl_normal] && !!flags.test(IRenderVisualFlags::eNoShadow))
		return;

	// Visual is 100% visible - simply add it
	switch (pVisual->Type)
	{
	case MT_PARTICLE_GROUP:
	{
		// Add all children, doesn't perform any tests
		PS::CParticleGroup* pG = (PS::CParticleGroup*)pVisual->dcast_ParticleCustom();
		xrCriticalSectionGuard guard(&pG->onframe_lock);
		for (PS::CParticleGroup::SItem& I_ : pG->items)
		{
			add_Dynamic(I_._effect, xform);
			add_leafs_Dynamic(I_._children_related, xform);
			add_leafs_Dynamic(I_._children_free, xform);
		}
	}break;

	case MT_HIERRARHY:
	{
		// Add all children, doesn't perform any tests
		FHierrarhyVisual* pV = (FHierrarhyVisual*)pVisual;
		add_leafs_Dynamic(pV->children, xform);
	}break;

	case MT_SKELETON_ANIM:
	case MT_SKELETON_RIGID:
	{
		// Add all children, doesn't perform any tests
		CKinematics* pV = (CKinematics*)pVisual;
		BOOL _use_lod = FALSE;
		if (pV->m_lod)
		{
			Fvector Tpos;
			float D;
			xform->transform_tiny(Tpos, pV->vis.sphere.P);
			float ssa = CalcSSA(D, Tpos, pV->vis.sphere.R * 0.5f);	// assume dynamics never consume full sphere
			if (ssa < r_ssaLOD_A)
				_use_lod = TRUE;
		}
		if (_use_lod)
			add_Dynamic(pV->m_lod, xform);
		else
		{
			//pV->CalculateBones(TRUE);
			if (i_mask[CDSGraphManager::fl_normal])
				pV->CalculateWallmarks();
			add_leafs_Dynamic(pV->children, xform);
		}
	}break;

	default:
	{
		// General type of visual
		// Calculate distance to it's center
		r_dsgraph_insert_dynamic(pVisual, xform);
	}break;
	}
}

void CDSGraphManager::add_leafs_Dynamic(xr_vector<dxRender_Visual*>& children, Fmatrix* xform)
{
	for (dxRender_Visual* pVisual : children)
	{
		add_Dynamic(pVisual, xform);
	}
}

void CDSGraphManager::add_leafs_Dynamic(xr_vector<IRenderVisual*>& children, Fmatrix* xform)
{
	for (IRenderVisual* pVisual : children)
	{
		add_Dynamic(pVisual, xform);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
void CDSGraphManager::add_Static(IRenderVisual* piVisual, CFrustum& frustum, u32 planes)
{
	dxRender_Visual* pVisual = (dxRender_Visual*)piVisual;
	if (!pVisual) return;

	Flags16& flags = piVisual->flags;
	if (!i_mask[CDSGraphManager::fl_normal] && !!flags.test(IRenderVisualFlags::eNoShadow))
		return;

	// Check frustum visibility and calculate distance to visual's center
	vis_data& vis = pVisual->vis;
	EFC_Visible VIS = frustum.testSAABB(vis.sphere.P, vis.sphere.R, vis.box.data(), planes);

	if (fcvNone == VIS)
		return;

#if RENDER!=R_R1
	if (i_mask[CDSGraphManager::fl_normal])//phase normal
#endif
		if (!RImplementation.HOM.visible(vis))
			return;

	// If we get here visual is visible or partially visible
	switch (pVisual->Type)
	{
	case MT_HIERRARHY:
	{
		// Add all children
		FHierrarhyVisual* pV = (FHierrarhyVisual*)pVisual;
		if (fcvPartial == VIS)
		{
			for (auto V : pV->children)
				add_Static(V, frustum, planes);
		}
		else
			add_leafs_Static(pV->children);
	}break;

	case MT_LOD:
	{
		FLOD* pV = (FLOD*)pVisual;
		float D;
		float ssa = CalcSSA(D, pV->vis.sphere.P, pV) * pV->lod_factor;

		if (ssa < r_ssaLOD_A)
		{
			if (ssa < r_ssaDISCARD)
				return;

			RGraph.mapLOD.emplace_back(D, ssa, nullptr, pVisual, nullptr, nullptr, false);
		}

#if RENDER!=R_R1
		if (ssa > r_ssaLOD_B || i_mask[CDSGraphManager::fl_shmap])//phase shmap
#else
		if (ssa > r_ssaLOD_B)
#endif
		{
			// Add all children, perform tests
			add_leafs_Static(pV->children);
		}
	}break;

	case MT_TREE_ST:
	case MT_TREE_PM:
	default:
	{
		// General type of visual
		r_dsgraph_insert_static(pVisual);
	}return;
	}
}

void CDSGraphManager::add_Static_MultiFrustum(IRenderVisual* piVisual, const xr_vector<CFrustum>& frustums, const xr_vector<u32>& masks)
{
	constexpr u32 FULLY_VISIBLE_MASK = u32(-1);

	dxRender_Visual* pVisual = (dxRender_Visual*)piVisual;
	if (!pVisual) return;

	Flags16& flags = piVisual->flags;
	if (!i_mask[CDSGraphManager::fl_normal] && !!flags.test(IRenderVisualFlags::eNoShadow))
		return;

	// Check visibility against all active frustums and propagate per-frustum masks.
	vis_data& vis = pVisual->vis;
	bool anyVisible = false;
	bool hasFullyVisibleFrustum = false;
	xr_vector<u32> childMasks;
	childMasks.resize(masks.size(), 0);

	for (u32 i = 0; i < masks.size(); ++i)
	{
		u32 planeMask = masks[i];
		if (!planeMask)
			continue;

		// Propagated from parent: this visual is fully visible in this frustum.
		if (planeMask == FULLY_VISIBLE_MASK)
		{
			anyVisible = true;
			hasFullyVisibleFrustum = true;
			childMasks[i] = FULLY_VISIBLE_MASK;
			continue;
		}

		EFC_Visible VIS = frustums[i].testSAABB(vis.sphere.P, vis.sphere.R, vis.box.data(), planeMask);
		if (VIS == fcvNone)
		{
			continue;
		}

		anyVisible = true;
		if (VIS == fcvFully)
		{
			// In original per-frustum traversal this path immediately adds leafs.
			// Preserve union semantics by marking this frustum as fully visible.
			hasFullyVisibleFrustum = true;
			childMasks[i] = FULLY_VISIBLE_MASK;
		}
		else
		{
			childMasks[i] = planeMask;
		}
	}

	if (!anyVisible)
		return;

#if RENDER!=R_R1
	if (i_mask[CDSGraphManager::fl_normal])//phase normal
#endif
		if (!RImplementation.HOM.visible(vis))
			return;

	// If we get here visual is visible in at least one frustum
	switch (pVisual->Type)
	{
	case MT_HIERRARHY:
	{
		FHierrarhyVisual* pV = (FHierrarhyVisual*)pVisual;
		if (hasFullyVisibleFrustum)
			add_leafs_Static(pV->children);
		else
		{
			for (auto V : pV->children)
				add_Static_MultiFrustum(V, frustums, childMasks);
		}
	}break;

	case MT_LOD:
	{
		FLOD* pV = (FLOD*)pVisual;
		float D;
		float ssa = CalcSSA(D, pV->vis.sphere.P, pV) * pV->lod_factor;

		if (ssa < r_ssaLOD_A)
		{
			if (ssa < r_ssaDISCARD)
				return;

			RGraph.mapLOD.emplace_back(D, ssa, nullptr, pVisual, nullptr, nullptr, false);
		}

#if RENDER!=R_R1
		if (ssa > r_ssaLOD_B || i_mask[CDSGraphManager::fl_shmap])//phase shmap
#else
		if (ssa > r_ssaLOD_B)
#endif
		{
			add_leafs_Static(pV->children);
		}
	}break;

	case MT_TREE_ST:
	case MT_TREE_PM:
	default:
	{
		r_dsgraph_insert_static(pVisual);
	}return;
	}
}

void CDSGraphManager::add_leaf_Static(dxRender_Visual* pVisual)
{
#if RENDER!=R_R1
	if (i_mask[CDSGraphManager::fl_normal])//phase normal
#endif
		if (!RImplementation.HOM.visible(pVisual->vis))
			return;

	// Visual is 100% visible - simply add it
	switch (pVisual->Type)
	{
	case MT_HIERRARHY:
	{
		FHierrarhyVisual* pV = (FHierrarhyVisual*)pVisual;
		add_leafs_Static(pV->children);
	}break;

	case MT_LOD:
	{
		FLOD* pV = (FLOD*)pVisual;
		float D;
		float ssa = CalcSSA(D, pV->vis.sphere.P, pV) * pV->lod_factor;

		if (ssa < r_ssaLOD_A)
		{
			if (ssa < r_ssaDISCARD)
				break;

			RGraph.mapLOD.emplace_back(D, ssa, nullptr, pVisual, nullptr, nullptr, false);
		}

#if RENDER!=R_R1
		if (ssa > r_ssaLOD_B || i_mask[CDSGraphManager::fl_shmap])//phase shmap
#else
		if (ssa > r_ssaLOD_B)
#endif
		{
			// Add all children, doesn't perform any tests
			add_leafs_Static(pV->children);
		}
	}break;

	case MT_TREE_PM:
	case MT_TREE_ST:
	default:
	{
		// General type of visual
		r_dsgraph_insert_static(pVisual);
	}break;
	}
}

void CDSGraphManager::add_leafs_Static(xr_vector<dxRender_Visual*>& children)
{
	for (dxRender_Visual* pVisual : children)
	{
		add_leaf_Static(pVisual);
	}
}

void CDSGraphManager::add_leafs_Static(xr_vector<IRenderVisual*>& children)
{
	for (IRenderVisual* pVisual : children)
	{
		dxRender_Visual* pV = (dxRender_Visual*)pVisual;
		if (!pV) continue;
		add_leaf_Static(pV);
	}
}
