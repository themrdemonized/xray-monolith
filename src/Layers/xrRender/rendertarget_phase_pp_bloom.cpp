#include "stdafx.h"

extern Fvector4 ps_pp_bloom_thresh;
extern Fvector4 ps_pp_bloom_weight;

void CRenderTarget::phase_pp_bloom()
{
	//Get common data
	u32 Offset = 0;
	float d_Z = EPS_S;
	float d_W = 1.0f;
	u32 C = color_rgba(0, 0, 0, 255);

	//Full resolution
	float w = float(Device.dwWidth);
	float h = float(Device.dwHeight);	

	Fvector2 p0, p1;
#if defined(USE_DX10) || defined(USE_DX11)	
	p0.set(0.0f, 0.0f);
	p1.set(1.0f, 1.0f);
#else
	p0.set(0.5f / w, 0.5f / h);
	p1.set((w + 0.5f) / w, (h + 0.5f) / h);
#endif
	
	
///////////////////////////////////////////////////////////////////////////////////
////Bloom pass
///////////////////////////////////////////////////////////////////////////////////
	u_setrt(rt_pp_bloom, 0, 0, HW.pBaseZB);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	// Fill vertex buffer
	FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, float(h), d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(float(w), float(h), d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(float(w), 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	// Draw COLOR
	RCache.set_Element(s_pp_bloom->E[0]);
	RCache.set_c("pp_bloom_thresh", ps_pp_bloom_thresh.x, ps_pp_bloom_thresh.y, ps_pp_bloom_thresh.z, ps_pp_bloom_thresh.w ); //Change to commands
	RCache.set_c("pp_bloom_weight", ps_pp_bloom_weight.x, ps_pp_bloom_weight.y, ps_pp_bloom_weight.z, ps_pp_bloom_weight.w); //Change to commands blyad
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
};