#include "stdafx.h"
#pragma hdrstop

#include "xrCDB.h"
#include "frustum.h"

using namespace CDB;
using namespace Opcode;

class _MM_ALIGN16 cform_frustum_collider
{
public:
	COLLIDER* dest;
	TRI* tris;
	Fvector* verts;

	const CFrustum* F;

	Fvector mM[2];

	sPoly Src, Dst;

	bool bClass3 = false;

	bool bFirst = false;

	cform_frustum_collider(bool bClass, bool bFrst)

		:bClass3(bClass), bFirst(bFrst) {}

	IC void _init(COLLIDER* CL, Fvector* V, TRI* T, const CFrustum* _F)
	{
		dest = CL;
		tris = T;
		verts = V;
		F = _F;
	}

	IC void SetByVerts(Fvector* Val, DWORD prim)
	{
		Val[0] = verts[tris[prim].verts[0]];
		Val[1] = verts[tris[prim].verts[1]];
		Val[2] = verts[tris[prim].verts[2]];
	}

	IC EFC_Visible Box(const Fvector& C, const Fvector& E, u32& mask)
	{
		mM[0].sub(C, E);
		mM[1].add(C, E);
		return F->testAABB(&mM[0].x, mask);
	}

	void Prim(DWORD InPrim)
	{
		if (bClass3)
		{
			Src.resize(3);
			SetByVerts(Src.begin(), InPrim);
			if (F->ClipPoly(Src, Dst))
			{
				RESULT& R = dest->r_add();
				R.id = InPrim;
				R.verts[0] = verts[tris[InPrim].verts[0]];
				R.verts[1] = verts[tris[InPrim].verts[1]];
				R.verts[2] = verts[tris[InPrim].verts[2]];
				R.dummy = tris[InPrim].dummy;
			}
		}
		else
		{
			RESULT& R = dest->r_add();
			R.id = InPrim;
			R.verts[0] = verts[tris[InPrim].verts[0]];
			R.verts[1] = verts[tris[InPrim].verts[1]];
			R.verts[2] = verts[tris[InPrim].verts[2]];
			R.dummy = tris[InPrim].dummy;
		}
	}

	void Stab(const AABBNoLeafNode* node, u32 mask)
	{
		// Actual frustum/aabb test
		EFC_Visible result = Box((Fvector&)node->mAABB.mCenter, (Fvector&)node->mAABB.mExtents, mask);
		if (fcvNone == result) return;

		// 1st chield
		if (node->HasLeaf()) Prim(node->GetPrimitive());
		else Stab(node->GetPos(), mask);

		// Early exit for "only first"
		if (bFirst && dest->r_count()) return;

		// 2nd chield
		if (node->HasLeaf2()) Prim(node->GetPrimitive2());
		else Stab(node->GetNeg(), mask);
	}
};

void COLLIDER::frustum_query(const MODEL* m_def, const CFrustum& F)
{
	PROF_EVENT("COLLIDER::frustum_query")
	if (!m_def)
		return;
	const_cast<MODEL*>(m_def)->syncronize();

	// Get nodes
	const AABBNoLeafNode* pNodes = ((AABBNoLeafTree*)m_def->tree->GetTree())->GetNodes();
	const DWORD mask = F.getMask();
	r_clear();

	cform_frustum_collider BC(!!(frustum_mode&OPT_FULL_TEST), !!(frustum_mode&OPT_ONLYFIRST));
	BC._init(this, m_def->verts, m_def->tris, &F);
	BC.Stab(pNodes, mask);
}
