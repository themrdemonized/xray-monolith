#include "stdafx.h"

#include "../../xrEngine/render.h"
#include "FBasicVisual.h"

// pip SVP geometry cull, rejects items outside the SVP frustum before the draw, armed only
// between svp_cull_begin/end around the SVP gbuffer pass
static CFrustum s_svp_cull_frustum;
static bool s_svp_cull_on = false;    // frustum armed for this SVP gbuffer pass
static bool s_svp_cull_world = false; // also reject world geometry, grass culling can arm the frustum alone

// svp stats overlay counters, mirror the r__dsgraph_render extern style
extern int ps_r__svp_stats;
extern u32 svp_stats_cull_reject_ident;
extern u32 svp_ledger_cull_reject;
extern u32 svp_ledger_cull_reject_ident;

void CDSGraphManager::svp_cull_begin(Fmatrix& full_xform, bool cull_world)
{
	s_svp_cull_frustum.CreateFromMatrix(full_xform, FRUSTUM_P_LRTB + FRUSTUM_P_FAR);
	s_svp_cull_on = true;
	s_svp_cull_world = cull_world;
}

void CDSGraphManager::svp_cull_end()
{
	s_svp_cull_on = false;
	s_svp_cull_world = false;
}

bool CDSGraphManager::svp_cull_active()
{
	return s_svp_cull_on;
}

// grass cull, the detail manager replays the main frustum field on the SVP pass, reject instances off
// the scope cone, the radius covers a blade so the root can sit a little outside without popping
bool CDSGraphManager::svp_cull_reject_sphere(const Fvector& c, float r)
{
	if (!s_svp_cull_on)
		return false;
	Fvector wc = c;
	return !s_svp_cull_frustum.testSphere_dirty(wc, r);
}

bool CDSGraphManager::svp_cull_reject(dxRender_Visual* V, Fmatrix* M)
{
	if (!s_svp_cull_on || !s_svp_cull_world || !V)
		return false;
	// dynamic vis spheres are rest-pose baked and drop animated parts, only world statics cull,
	// the sorted lists tag world statics with &Fidentity so accept that alongside a null matrix
	if (M && M != &Fidentity)
		return false;
	// world static: the geometry (and so vis.sphere) is already in world space
	Fvector wc; wc.set(V->vis.sphere.P);
	float wr = V->vis.sphere.R;
	const bool reject = !s_svp_cull_frustum.testSphere_dirty(wc, wr);
	// count the identity-matrix rejects distinctly, the ledger proof the sorted cull now fires
	if (reject && M == &Fidentity && ps_r__svp_stats)
		++svp_stats_cull_reject_ident;
	if (reject)
	{
		if (!svp_ledger_cull_reject) svp_ledger_cull_reject = 1; // any world-static cone reject
		if (M == &Fidentity && !svp_ledger_cull_reject_ident) svp_ledger_cull_reject_ident = 1;
	}
	return reject;
}
