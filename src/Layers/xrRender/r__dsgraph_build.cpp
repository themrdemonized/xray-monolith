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

ICF float CalcSSA(float& distSQ, Fvector& C, dxRender_Visual* V)
{
	float R = V->vis.sphere.R + 0;
	distSQ = Device.vCameraPosition.distance_to_sqr(C) + EPS;
	return R / distSQ;
}

ICF float CalcSSA(float& distSQ, Fvector& C, float R)
{
	distSQ = Device.vCameraPosition.distance_to_sqr(C) + EPS;
	return R / distSQ;
}

void CDSGraphManager::r_dsgraph_insert_dynamic(dxRender_Visual *pVisual, Fmatrix* xform)
{
	Fvector Center;
	xform->transform_tiny(Center, pVisual->vis.sphere.P);

	float distSQ;
	float SSA = CalcSSA(distSQ, Center, pVisual);
	if (SSA <= r_ssaDISCARD) return;

	// Distortive geometry should be marked and R2 special-cases it
	// a) Allow to optimize RT order
	// b) Should be rendered to special distort buffer in another pass
	VERIFY(pVisual->shader._get());

	ShaderElement* sh_d = &*pVisual->shader->E[4];
	if (sh_d && sh_d->flags.bDistort && i_mask[sh_d->flags.iPriority/2])
	{
		if (i_mask[CDSGraphManager::fl_hud])
			RGraph.mapHUDSorted.Distort.push_back(DSGraphItem<float>{ distSQ, SSA, val_pObject, pVisual, xform, sh_d, i_mask[CDSGraphManager::fl_hud] });
		else
			RGraph.mapDynamicSorted.Distort.push_back(DSGraphItem<float>{ distSQ, SSA, val_pObject, pVisual, xform, sh_d, i_mask[CDSGraphManager::fl_hud] });
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
	switch (sh->flags.iScopeLense) {	
		case 0:
			break;

		case 1: {
			RGraph.mapHUD.push_back(DSGraphItem<float>{ EPS, SSA, val_pObject, pVisual, xform, sh, i_mask[CDSGraphManager::fl_hud] });

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
			RGraph.mapScopeHUD.push_back(DSGraphItem<float>{ distSQ, SSA, val_pObject, pVisual, xform, sh, i_mask[CDSGraphManager::fl_hud] });
			return;
		}

		case 3: {
			RGraph.mapScopeHUDSorted.push_back(DSGraphItem<float>{ distSQ, SSA, val_pObject, pVisual, xform, sh, i_mask[CDSGraphManager::fl_hud] });
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
					RGraph.mapCamAttachedSorted.Emissive.push_back(DSGraphItem<float>{ distSQ, SSA, val_pObject, pVisual, xform, sh_d, i_mask[CDSGraphManager::fl_cam] });
				else
					RGraph.mapHUDSorted.Emissive.push_back(DSGraphItem<float>{ distSQ, SSA, val_pObject, pVisual, xform, sh_d, i_mask[CDSGraphManager::fl_hud] });
			}
#endif // RENDER!=R_R1
			if (i_mask[CDSGraphManager::fl_cam])
				RGraph.mapCamAttachedSorted.Sorted.push_back(DSGraphItem<float>{ distSQ, SSA, val_pObject, pVisual, xform, sh, i_mask[CDSGraphManager::fl_cam] });
			else
				RGraph.mapHUDSorted.Sorted.push_back(DSGraphItem<float>{ distSQ, SSA, val_pObject, pVisual, xform, sh, i_mask[CDSGraphManager::fl_hud] });
			return;
		}
		else
		{
			if (i_mask[CDSGraphManager::fl_cam])
				RGraph.mapCamAttached.push_back(DSGraphItem<float>{ distSQ, SSA, val_pObject, pVisual, xform, sh, i_mask[CDSGraphManager::fl_cam] });
			else
				RGraph.mapHUD.push_back(DSGraphItem<float>{ distSQ, SSA, val_pObject, pVisual, xform, sh, i_mask[CDSGraphManager::fl_hud] });

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
					RGraph.mapCamAttachedSorted.Emissive.push_back(DSGraphItem<float>{ distSQ, SSA, val_pObject, pVisual, xform, sh_d, i_mask[CDSGraphManager::fl_cam] });
				else
					RGraph.mapHUDSorted.Emissive.push_back(DSGraphItem<float>{ distSQ, SSA, val_pObject, pVisual, xform, sh_d, i_mask[CDSGraphManager::fl_hud] });
			}
				
#endif	//	RENDER!=R_R1

			return;
		}
	}

	// Shadows registering
#if RENDER==R_R1
	DSGraphItem<dxRender_Visual*> item = { pVisual, SSA, val_pObject, pVisual, xform, nullptr, i_mask[CDSGraphManager::fl_hud] };
	R_dsgraph::mapDSGraphItemsMap<dxRender_Visual*>::TNode N = { pVisual, item };
	RImplementation.L_Shadows->add_element(N);
#endif
	if (i_mask[CDSGraphManager::fl_invisible])
		return;

	// strict-sorting selection
	if (sh->flags.bStrictB2F)
	{
		RGraph.mapDynamicSorted.Sorted.push_back(DSGraphItem<float>{ distSQ, SSA, val_pObject, pVisual, xform, sh, i_mask[CDSGraphManager::fl_hud] });
		return;
	}

#if RENDER!=R_R1
	// Emissive geometry should be marked and R2 special-cases it
	// a) Allow to skeep already lit pixels
	// b) Allow to make them 100% lit and really bright
	// c) Should not cast shadows
	// d) Should be rendered to accumulation buffer in the second pass
	if (sh->flags.bEmissive)
		RGraph.mapDynamicSorted.Emissive.push_back(DSGraphItem<float>{ distSQ, SSA, val_pObject, pVisual, xform, sh_d, i_mask[CDSGraphManager::fl_hud] });

	if (sh->flags.bWmark && i_mask[CDSGraphManager::fl_wmarks])
	{
		if (i_mask[CDSGraphManager::fl_hud])
			RGraph.mapHUDSorted.Wmark.push_back(DSGraphItem<float>{ distSQ, SSA, val_pObject, pVisual, xform, sh, i_mask[CDSGraphManager::fl_hud] });
		else
			RGraph.mapDynamicSorted.Wmark.push_back(DSGraphItem<float>{ distSQ, SSA, val_pObject, pVisual, xform, sh, i_mask[CDSGraphManager::fl_hud] });
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

		// Step 1: Create render packet
		RenderPacket packet;

#if RENDER==R_R1
		packet.item = item;
#else
		packet.item = { pVisual, SSA, val_pObject, pVisual, xform, nullptr, i_mask[CDSGraphManager::fl_hud] };
#endif

		AddToRenderQueue(RGraph.mapDynamicPasses[shader_priority][iPass], packet, pass);
	}
}

void CDSGraphManager::r_dsgraph_insert_static(dxRender_Visual *pVisual)
{
	float distSQ;
	float SSA = CalcSSA(distSQ, pVisual->vis.sphere.P, pVisual);
	if (SSA <= r_ssaDISCARD) return;

	// Distortive geometry should be marked and R2 special-cases it
	// a) Allow to optimize RT order
	// b) Should be rendered to special distort buffer in another pass
	VERIFY(pVisual->shader._get());
	ShaderElement* sh_d = &*pVisual->shader->E[4];
	if (sh_d && sh_d->flags.bDistort && i_mask[sh_d->flags.iPriority/2])
		RGraph.mapStaticSorted.Distort.push_back(DSGraphItem<float>{ distSQ, SSA, nullptr, pVisual, &Fidentity, sh_d });

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
		RGraph.mapWater.push_back(DSGraphItem<float>{ distSQ, SSA, nullptr, pVisual, &Fidentity, sh });
		return;
	}
#endif

	// strict-sorting selection
	if (sh->flags.bStrictB2F)
	{
		RGraph.mapStaticSorted.Sorted.push_back(DSGraphItem<float>{ distSQ, SSA, nullptr, pVisual, &Fidentity, sh });
		return;
	}

#if RENDER!=R_R1
	// Emissive geometry should be marked and R2 special-cases it
	// a) Allow to skeep already lit pixels
	// b) Allow to make them 100% lit and really bright
	// c) Should not cast shadows
	// d) Should be rendered to accumulation buffer in the second pass
	if (sh->flags.bEmissive)
		RGraph.mapStaticSorted.Emissive.push_back(DSGraphItem<float>{ distSQ, SSA, nullptr, pVisual, &Fidentity, sh_d  });

	if (sh->flags.bWmark && i_mask[CDSGraphManager::fl_wmarks])
	{
		RGraph.mapStaticSorted.Wmark.push_back(DSGraphItem<float>{ distSQ, SSA, nullptr, pVisual, &Fidentity, sh });
		return;
	}
#endif

	for (u32 iPass = 0; iPass < sh->passes.size(); ++iPass)
	{
		// the most common node
		if (sh->passes[iPass] == nullptr)
			continue;

		SPass& pass	= *sh->passes[iPass];

		// Step 1: Create render packet
		RenderPacket packet;
		packet.item = { pVisual, SSA, nullptr, pVisual };
		AddToRenderQueue(RGraph.mapStaticPasses[shader_priority][iPass], packet, pass);
	}
}

void CDSGraphManager::AddToRenderQueue(R_dsgraph::RenderQueue& queue, R_dsgraph::RenderPacket& packet, SPass& pass)
{
	// Step 2: extract pointers (Previously map keys)
#if defined(USE_DX10) || defined(USE_DX11)
	packet.pVS = &*pass.vs;
	packet.pGS = pass.gs->gs;
#else
	packet.pVS = pass.vs->vs;
#endif

	packet.pPS = pass.ps->ps;

#ifdef USE_DX11
	packet.pHS = pass.hs->sh;
	packet.pDS = pass.ds->sh;
#endif

	packet.pCS = pass.constants._get();
	packet.pState = pass.state->state;
	packet.pTextures = pass.T._get();

	// Step 3: Make sort key with bit packing
	u64 keyHigh = 0;
	u64 keyLow = 0;

	keyHigh |= ((u64)packet.pVS >> 4 & 0xFFFF) << 48;

#if defined(USE_DX10) || defined(USE_DX11)
	keyHigh |= ((u64)packet.pGS >> 4 & 0xFFFF) << 32;
#endif

	keyHigh |= ((u64)packet.pPS >> 4 & 0xFFFF) << 16;

#ifdef USE_DX11
	keyHigh |= ((u64)packet.pHS >> 4 & 0xFFFF);
	keyLow |= ((u64)packet.pDS >> 4 & 0xFFFF) << 48;
#endif

	keyLow |= ((u64)packet.pCS >> 4 & 0xFFFF) << 32;
	keyLow |= ((u64)packet.pState >> 4 & 0xFFFF) << 16;
	keyLow |= ((u64)packet.pTextures >> 4 & 0xFFFF);

	packet.sortKey = { keyHigh, keyLow };
	queue.push_back(packet);
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

			RGraph.mapLOD.push_back(DSGraphItem<float>{ D, ssa, nullptr, pVisual });
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

			RGraph.mapLOD.push_back(DSGraphItem<float>{ D, ssa, nullptr, pVisual });
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
