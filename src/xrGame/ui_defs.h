#pragma once

#include "../Include/xrRender/FactoryPtr.h"
#include "../Include/xrRender/UIRender.h"
#include "../Include/xrRender/UIShader.h"
typedef FactoryPtr<IUIShader> ui_shader;

#define UI_BASE_WIDTH	1024.0f
#define UI_BASE_HEIGHT	768.0f

enum EUIItemAlign
{
	alNone = 0x0000,
	alLeft = 0x0001,
	alRight = 0x0002,
	alTop = 0x0004,
	alBottom= 0x0008,
	alCenter= 0x0010
};

struct S2DVert
{
	Fvector2 pt;
	Fvector2 uv;

	S2DVert()
	{
	}

	S2DVert(float pX, float pY, float tU, float tV)
	{
		pt.set(pX, pY);
		uv.set(tU, tV);
	}

	void set(float pt_x, float pt_y, float uv_x, float uv_y)
	{
		pt.set(pt_x, pt_y);
		uv.set(uv_x, uv_y);
	}

	void set(const Fvector2& _pt, const Fvector2& _uv)
	{
		pt.set(_pt);
		uv.set(_uv);
	}

	void rotate_pt(const Fvector2& pivot, const float cosA, const float sinA, const float kx);
};

#define UI_FRUSTUM_MAXPLANES	12
#define UI_FRUSTUM_SAFE			(UI_FRUSTUM_MAXPLANES*4)
typedef svector<S2DVert,UI_FRUSTUM_SAFE> sPoly2D;

class C2DFrustum
{
	svector<Fplane2,FRUSTUM_MAXPLANES> planes;
	Frect m_rect;
	bool m_force_clip; // when true, ClipPoly always runs even if the poly is fully inside m_rect
public:
	C2DFrustum() : m_force_clip(false) {}
	void CreateFromRect(const Frect& rect);
	sPoly2D* ClipPoly(sPoly2D& S, sPoly2D& D) const;
	void AddClipPlane(const Fvector2& pt, const Fvector2& n)
	{
		Fplane2 p;
		p.build(pt, n);
		planes.push_back(p);
		m_force_clip = true;
	}
	// Half-plane clip along directed edge a->b: keeps the LEFT side -- points p with (b-a) x (p-a) > 0.
	// n is the outward normal (toward the discarded side);
	void AddEdgePlane(const Fvector2& a, const Fvector2& b)
	{
		Fvector2 dir;
		dir.sub(b, a);
		AddClipPlane(a, Fvector2().set(dir.y, -dir.x));
	}
	void Clear()
	{
		if (planes.size())
			planes.clear();
		m_force_clip = false;
	}
	u32 ClipBudget(u32 src_verts) const { return 3 * (src_verts + (u32)planes.size() - 2); }
};

extern ENGINE_API BOOL g_bRendering;
