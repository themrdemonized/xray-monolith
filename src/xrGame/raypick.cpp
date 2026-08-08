#include "stdafx.h"
#include "raypick.h"
#include "level.h"

CRayPick::CRayPick()
{
	start_position.set(0, 0, 0);
	direction.set(0, 0, 0);
	range = 0;
	flags = collide::rq_target::rqtNone;
	ignore.clear();
};

CRayPick::CRayPick(const Fvector& P, const Fvector& D, float R, collide::rq_target F, CScriptGameObject* I)
{
	start_position.set(P);
	direction.set(D);
	range = R;
	flags = F;
	ignore.clear();
	if (I) {
		CObject* obj = smart_cast<CObject*>(&(I->object()));
		if (obj)
		{
			ignore.push_back(obj);
		}
	};
};

bool CRayPick::query()
{
	result.reset();
	collide::rq_result R;
	if (Level().ObjectSpace.RayPick(start_position, direction, range, flags, R, ignore))
	{
		result.set(R);
		return true;
	}
	else
		return false;
}

Fvector CRayPick::get_normal()
{
    if (result.O == nullptr && result.element != 0)
    {
        Fvector* V = Level().ObjectSpace.GetStaticVerts();
        CDB::TRI* T = Level().ObjectSpace.GetStaticTris() + result.element;
        return Fvector().mknormal(V[T->verts[0]], V[T->verts[1]], V[T->verts[2]]);
    }
    return Fvector().set(0, 1, 0);
}
