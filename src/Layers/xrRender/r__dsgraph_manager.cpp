#include "stdafx.h"
#include "../../xrEngine/IGame_Persistent.h"
#include "../../xrEngine/Environment.h"
#include "FVF.h"
#include "../../xrEngine/xr_object.h"
#include "../../xrEngine/PS_instance.h"
#include "LightTrack.h"
#include "xrRender_console.h"
#include "../xrServerEntities/smart_cast.h"

static PortalTraverseDebugStats g_portal_traverse_dbg_stats;
static u32 g_portal_traverse_dbg_last_frame = u32(-1);

bool PortalTraverseDbg_Enabled()
{
	return !!ps_r__portal_traverse_stats;
}

bool PortalTraverseDbg_IsOptions(u32 options)
{
	return !!(options & (CDSGraphManager::VQ_HOM | CDSGraphManager::VQ_SSA | CDSGraphManager::VQ_FADE));
}

PortalTraverseDebugStats& PortalTraverseDbg_Get()
{
	const u32 frame = Device.dwFrame;
	if (g_portal_traverse_dbg_last_frame != frame)
	{
		g_portal_traverse_dbg_last_frame = frame;
		g_portal_traverse_dbg_stats.reset(frame);
	}

	return g_portal_traverse_dbg_stats;
}

const PortalTraverseDebugStats& PortalTraverseDbg_Peek()
{
	return g_portal_traverse_dbg_stats;
}

void CDSGraphManager::traverse(CSector* start, CFrustum& F, Fvector& vBase, Fmatrix& mXFORM)
{
	if (!start) return;
	PROF_EVENT("CDSGraphManager::traverse")

	if (i_options & VQ_FADE)
	{
		xrCriticalSectionGuard guard(&P_CS);
		f_portals.clear();
	}

	i_vBase				= vBase;
	i_frustum			= F;
	i_mXFORM			= mXFORM;
	i_start				= start;

    RGraph.mapLOD.clear();

	xrSRWLockGuard guard(&S_LC,false);
	if(m_sector_frustums.size())
	{
		for (auto& pair : m_sector_frustums)
		{
			pair.val.first.clear();
			pair.val.second.clear();
		}
	}
	// Keep sector-frustum map frame-local: stale sector keys from previous
	// frames create empty-frustum entries and skew static capture decisions.
	m_sector_frustums.clear();
	m_static_seen.clear();
	i_start->traverse(std::move(F),*this);
}

void CDSGraphManager::set_Object(IRenderable* O)
{
	val_pObject = O;		// nullptr is OK, trust me :)
#if RENDER==R_R1
	if (val_pObject)
	{
		VERIFY(smart_cast<CObject*>(O) || smart_cast<CPS_Instance*>(O));
		if (O->renderable.pROS) { VERIFY(smart_cast<CROS_impl*>(O->renderable.pROS)); }
	}
	if (CRender::PHASE_NORMAL == RImplementation.phase)
	{
		if (RImplementation.L_Shadows)
			RImplementation.L_Shadows->set_object(O, *this);

		if (RImplementation.L_Projector)
			RImplementation.L_Projector->set_object(O, *this);
	}
	else
	{
		if (RImplementation.L_Shadows)
			RImplementation.L_Shadows->set_object(0, *this);

		if (RImplementation.L_Projector)
			RImplementation.L_Projector->set_object(0, *this);
	}
#endif // RENDER==R_R1
}

void CDSGraphManager::fade_portal	(CPortal* _p, float ssa)
{
	if(RImplementation.HOM.visible(_p->S))
	{
		xrCriticalSectionGuard guard(&P_CS);
		f_portals.insert(_p, ssa);
	}
}
void CDSGraphManager::initialize	()
{
	f_shader.create					("portal");
	f_geom.create					(FVF::F_L, RCache.Vertex.Buffer(), 0);
}
void CDSGraphManager::destroy		()
{
	f_geom.destroy					();
	f_shader.destroy				();
}
extern float r_ssaDISCARD			;
extern float r_ssaLOD_A, r_ssaLOD_B ;
void CDSGraphManager::fade_render	()
{
	xrCriticalSectionGuard guard(&P_CS);
	if (!f_portals.size()) return;

	// re-sort, back to front
	if(!psDeviceFlags.test(rsDrawPortals))
	{
		std::sort(f_portals.begin(), f_portals.end(),
		[&](const auto& _1, const auto& _2)
		{
			float		d1 = i_vBase.distance_to_sqr(_1.key->S.P);
			float		d2 = i_vBase.distance_to_sqr(_2.key->S.P);
			return		d2 > d1;	// descending, back to front
		});
	}
	
	// calc poly-count
	u32		_pcount					= 0;
	for		(auto& fp : f_portals)	_pcount	+= fp.key->getPoly().size()-2;

	// fill buffers
	u32			_offset				= 0;
	FVF::L*		_v					= (FVF::L*)RCache.Vertex.Lock(_pcount*3,f_geom.stride(),_offset);
	float		ssaRange			= r_ssaLOD_A - r_ssaLOD_B;
	Fvector		_ambient_f			= g_pGamePersistent->Environment().CurrentEnv->ambient;
	u32			_ambient			= psDeviceFlags.test(rsDrawPortals) ? u32(-1) : color_rgba_f(_ambient_f.x,_ambient_f.y,_ambient_f.z,0.f);
	for (auto& fp : f_portals)
	{
		CPortal*					_P		= fp.key;
		u32							_clr=u32(-1);
		if(psDeviceFlags.test(rsDrawPortals))
			_clr	= color_rgba(0,255,100,255);
		else
		{
			float						_ssa	= fp.val;
			float		ssaDiff					= _ssa-r_ssaLOD_B	;
			float		ssaScale				= ssaDiff/ssaRange	;
			int			iA						= iFloor((1-ssaScale)*255.5f);	clamp(iA,0,255);
			_clr	= subst_alpha(_ambient,u32(iA));	
		}

		// fill polys
		u32			_polys					= _P->getPoly().size()-2;
		for			(u32 _pit=0; _pit<_polys; _pit++)	{
			_v->set	(_P->getPoly()[0],		_clr);	_v++;
			_v->set (_P->getPoly()[_pit+1],_clr);	_v++;
			_v->set (_P->getPoly()[_pit+2],_clr);	_v++;
		}
	}
	RCache.Vertex.Unlock			(_pcount*3,f_geom.stride());

	// render
	RCache.set_xform_world			(Fidentity);
	RCache.set_Shader				(f_shader);
	RCache.set_Geometry				(f_geom);
	RCache.set_CullMode				(CULL_NONE);
	RCache.Render					(D3DPT_TRIANGLELIST,_offset,_pcount);
	RCache.set_CullMode				(CULL_CCW);

	// cleanup
	f_portals.clear					();
}
