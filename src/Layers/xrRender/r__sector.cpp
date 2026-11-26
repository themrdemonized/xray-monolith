// Portal.cpp: implementation of the CPortal class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "r__sector.h"
#include "../../xrEngine/xrLevel.h"
#include "../../xrEngine/xr_object.h"
#include "fbasicvisual.h"
#include "../../xrEngine/IGame_Persistent.h"
#include "dxRenderDeviceRender.h"

void CPortal::Setup(Fvector* V, int vcnt, CSector* face, CSector* back)
{
	// calc sphere
	BB.invalidate();
	for (int v = 0; v < vcnt; v++)
		BB.modify(V[v]);
	BB.getsphere(S.P, S.R);

	// 
	poly.assign(V, vcnt);
	pFace = face;
	pBack = back;

	Fvector N, T;
	N.set(0, 0, 0);

	FPU::m64r();
	u32 _cnt = 0;
	for (int i = 2; i < vcnt; i++)
	{
		T.mknormal_non_normalized(poly[0], poly[i - 1], poly[i]);
		float m = T.magnitude();
		if (m > EPS_S)
		{
			N.add(T.div(m));
			_cnt ++;
		}
	}
	R_ASSERT2(_cnt, "Invalid portal detected");
	N.div(float(_cnt));
	P.build(poly[0], N);
	FPU::m24r();

	/*
	if (_abs(1-P.n.magnitude())<EPS)
	Debug.fatal		(DEBUG_INFO,"Degenerated portal found at {%3.2f,%3.2f,%3.2f}.",VPUSH(poly[0]));
	*/
}

//
CSector::~CSector()
{
}

//
extern float r_ssaDISCARD;
extern float r_ssaLOD_A, r_ssaLOD_B;
IC CFrustum CreateFrustumFromPortal(sPoly* poly, Fvector& vBase, Fmatrix& mFullXFORM)
{
	CFrustum F;
	Fplane P;
	P.build_precise	((*poly)[0],(*poly)[1],(*poly)[2]);

	if (poly->size()>6) {
		F.SimplifyPoly_AABB(poly,P);
		P.build_precise	((*poly)[0],(*poly)[1],(*poly)[2]);
	}

	// Check plane orientation relative to viewer
	// and reverse if needed
	if (P.classify(vBase)<0)
	{
		std::reverse(poly->begin(),poly->end());
		P.build_precise	((*poly)[0],(*poly)[1],(*poly)[2]);
	}

	// Base creation
	F.CreateFromPoints(poly->begin(),poly->size(),vBase);

	// Near clipping plane
	F._add		(P);

	// Far clipping plane
	Fmatrix &M	= mFullXFORM;
	P.n.x		= -(M._14 - M._13);
	P.n.y		= -(M._24 - M._23);
	P.n.z		= -(M._34 - M._33);
	P.d			= -(M._44 - M._43);
	float denom = 1.0f / P.n.magnitude();
	P.n.x		*= denom;
	P.n.y		*= denom;
	P.n.z		*= denom;
	P.d			*= denom;
	F._add		(P);

	return F;
}

void CSector::traverse(CFrustum &&F, CDSGraphManager& DM)
{
	PROF_EVENT("CSector::traverse")
	const bool dbg_enabled = PortalTraverseDbg_Enabled();
	PortalTraverseDebugStats* dbg = dbg_enabled ? &PortalTraverseDbg_Get() : nullptr;
	const bool opt_bucket = PortalTraverseDbg_IsOptions(DM.i_options);
	if (dbg)
	{
		++dbg->traverse_calls;
		if (opt_bucket)
			++dbg->traverse_calls_with_options;
		else
			++dbg->traverse_calls_without_options;
	}

	// Register traversal process
	auto SNODE = DM.m_sector_frustums.insert(this);
    SNODE->val.first.push_back(F);
	if (dbg)
	{
		++dbg->frustums_pushed;
		if (opt_bucket)
			++dbg->frustums_pushed_opt;
		else
			++dbg->frustums_pushed_noopt;

		const u32 frustum_count = u32(SNODE->val.first.size());
		if (frustum_count > dbg->max_frustums_in_sector)
			dbg->max_frustums_in_sector = frustum_count;
		if (opt_bucket)
		{
			if (frustum_count > dbg->max_frustums_in_sector_opt)
				dbg->max_frustums_in_sector_opt = frustum_count;
		}
		else
		{
			if (frustum_count > dbg->max_frustums_in_sector_noopt)
				dbg->max_frustums_in_sector_noopt = frustum_count;
		}
	}

	// If the map reallocates, SNODE becomes invalid, but the Index stays correct.
	u32 snode_idx = DM.m_sector_frustums.get_index(SNODE);

	Fvector& cam_pos = Device.vCameraPosition_saved;
	float test_sphere_r = VIEWPORT_NEAR + EPS_L;
	// Search visible portals and go through them
	for (CPortal* PORTAL : m_portals)
	{
		if (dbg)
		{
			++dbg->portals_checked;
			if (opt_bucket)
				++dbg->portals_checked_opt;
			else
				++dbg->portals_checked_noopt;
		}

		// Refresh pointer before use (in case previous loop iteration realloc'd)
		SNODE = DM.m_sector_frustums.get_node(snode_idx);

		if (SNODE->val.second.find(PORTAL))
		{
			if (dbg)
				++dbg->portals_skipped_already_visited;
			continue;
		}
		// Early-out sphere
		if (!F.testSphere_dirty(PORTAL->S.P, PORTAL->S.R))
		{
			if (dbg)
				++dbg->portals_rejected_sphere;
			continue;
		}

		CSector* pSector = nullptr;

		// Select sector (allow intersecting portals to be finely classified)
		if (PORTAL->S.P.distance_to(cam_pos) < PORTAL->S.R && PORTAL->distance(cam_pos) <= test_sphere_r)//bDualRender
			pSector = PORTAL->getSector(this);
		else
			pSector = PORTAL->getSectorBack(DM.i_vBase);

		if (pSector == nullptr || pSector == this || pSector == DM.i_start)
		{
			if (dbg)
				++dbg->portals_rejected_sector;
			continue;
		}

		if (DM.i_options & CDSGraphManager::VQ_FADE | CDSGraphManager::VQ_SSA && psDeviceFlags.test(rsDrawPortals))
			DM.fade_portal(PORTAL, 1.f);
		else
		{
			// SSA   (if required)
			if (DM.i_options & CDSGraphManager::VQ_SSA)
			{
				Fvector dir2portal;
				dir2portal.sub(PORTAL->S.P, DM.i_vBase);
				float R = PORTAL->S.R;
				float distSQ = dir2portal.square_magnitude();
				float ssa = R * R / distSQ;
				dir2portal.div(_sqrt(distSQ));
				ssa *= _abs(PORTAL->P.n.dotproduct(dir2portal));
				if (ssa < r_ssaDISCARD)
				{
					if (dbg)
						++dbg->portals_rejected_ssa;
					continue;
				}

				if (DM.i_options & CDSGraphManager::VQ_FADE)
				{
					if (ssa < r_ssaLOD_A)
						DM.fade_portal(PORTAL, ssa);

					if (ssa < r_ssaLOD_B)
						continue;
				}
			}
		}

		// Clip by frustum
		svector<Fvector, 8>& POLY = PORTAL->getPoly();
		DM.S.clear();
		DM.S.assign(&*POLY.begin(), POLY.size());
		DM.D.clear();
		sPoly* P = F.ClipPoly(DM.S, DM.D);

		if (0 == P)
		{
			if (dbg)
				++dbg->portals_rejected_clip;
			continue;
		}

		// Cull by HOM (slower algo)
		if ((DM.i_options & CDSGraphManager::VQ_HOM) && !RImplementation.HOM.visible(*P))
		{
			if (dbg)
				++dbg->portals_rejected_hom;
			continue;
		}

		if (pSector)
		{
			// Create _new_ frustum and recurse
			SNODE->val.second.insert(PORTAL);
			if (dbg)
			{
				++dbg->portals_recursed;
				if (opt_bucket)
					++dbg->portals_recursed_opt;
				else
					++dbg->portals_recursed_noopt;
			}
			pSector->traverse(CreateFrustumFromPortal(P, DM.i_vBase, DM.i_mXFORM), DM);
		}
	}
}

void CSector::load(IReader& fs)
{
	// Assign portal polygons
	u32 size = fs.find_chunk(fsP_Portals);
	R_ASSERT(0 == (size & 1));
	u32 count = size / 2;
	m_portals.reserve(count);
	while (count)
	{
		u16 ID = fs.r_u16();
		CPortal* P = (CPortal*)RImplementation.getPortal(ID);
		m_portals.push_back(P);
		count--;
	}

	if (g_dedicated_server) m_root = 0;
	else
	{
		// Assign visual
		size = fs.find_chunk(fsP_Root);
		R_ASSERT(size == 4);
		m_root = (dxRender_Visual*)RImplementation.getVisual(fs.r_u32());
	}
}
