#include "stdafx.h"



void CRenderTarget::phase_blur()
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
////Horizontal blur
///////////////////////////////////////////////////////////////////////////////////
	w = float(Device.dwWidth) * 0.5f;
	h = float(Device.dwHeight) * 0.5f;

	u_setrt(rt_blur_h_2, 0, 0, HW.pBaseZB);
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
	RCache.set_Element(s_blur->E[0]);
	RCache.set_c("blur_params", 1.0, 0.0, w, h);	
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
///////////////////////////////////////////////////////////////////////////////////
////Final blur
///////////////////////////////////////////////////////////////////////////////////
	u_setrt(rt_blur_2, 0, 0, HW.pBaseZB);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	// Fill vertex buffer
	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, float(h), d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(float(w), float(h), d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(float(w), 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	// Draw COLOR
	RCache.set_Element(s_blur->E[1]);
	RCache.set_c("blur_params", 0.0, 1.0, w, h);	
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
///////////////////////////////////////////////////////////////////////////////////
////Horizontal blur / Half res
///////////////////////////////////////////////////////////////////////////////////
	w = float(Device.dwWidth) * 0.25f;
	h = float(Device.dwHeight) * 0.25f;

	u_setrt(rt_blur_h_4, 0, 0, HW.pBaseZB);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	// Fill vertex buffer
	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, float(h), d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(float(w), float(h), d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(float(w), 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	// Draw COLOR
	RCache.set_Element(s_blur->E[2]);
	RCache.set_c("blur_params", 1.0, 0.0, w, h);	
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
///////////////////////////////////////////////////////////////////////////////////
////Final blur
///////////////////////////////////////////////////////////////////////////////////
	u_setrt(rt_blur_4, 0, 0, HW.pBaseZB);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	// Fill vertex buffer
	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, float(h), d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(float(w), float(h), d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(float(w), 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	// Draw COLOR
	RCache.set_Element(s_blur->E[3]);
	RCache.set_c("blur_params", 0.0, 1.0, w, h);	
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
///////////////////////////////////////////////////////////////////////////////////
////Horizontal blur
///////////////////////////////////////////////////////////////////////////////////
	w = float(Device.dwWidth) * 0.125f;
	h = float(Device.dwHeight) * 0.125f;
	
	u_setrt(rt_blur_h_8, 0, 0, HW.pBaseZB);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	// Fill vertex buffer
	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, float(h), d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(float(w), float(h), d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(float(w), 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	// Draw COLOR
	RCache.set_Element(s_blur->E[4]);
	RCache.set_c("blur_params", 1.0, 0.0, w, h);	
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
///////////////////////////////////////////////////////////////////////////////////
////Final blur
///////////////////////////////////////////////////////////////////////////////////
	u_setrt(rt_blur_8, 0, 0, HW.pBaseZB);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	// Fill vertex buffer
	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, float(h), d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(float(w), float(h), d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(float(w), 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	// Draw COLOR
	RCache.set_Element(s_blur->E[5]);
	RCache.set_c("blur_params", 0.0, 1.0, w, h);		
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
///////////////////////////////////////////////////////////////////////////////////
};

#if defined(USE_DX11)

void CRenderTarget::phase_ssfx_ssr()
{
	//Constants
	u32 Offset = 0;
	u32 C = color_rgba(0, 0, 0, 255);

	float d_Z = EPS_S;
	float d_W = 1.0f;
	float w = float(Device.dwWidth);
	float h = float(Device.dwHeight);

	float ScaleFactor = std::min(std::max(ps_ssfx_ssr.x, 1.0f), 2.0f);

	Fvector2 p0, p1;
	p0.set(0.0f, 0.0f);
	p1.set(1.0f / ScaleFactor, 1.0f / ScaleFactor);

	// Fill VB
	float scale_X = w / ScaleFactor;
	float scale_Y = h / ScaleFactor;

	D3D_VIEWPORT VP_FullRes = { 0, 0, w, h, 0, 1 };
	D3D_VIEWPORT VP_Scaled = { 0, 0, scale_X, scale_Y, 0, 1 };

	Fmatrix m_previous, m_current;
	{
		static Fmatrix m_saved_viewproj;

		Fmatrix m_invview;
		m_invview.invert(Device.mView);
		m_previous.mul(m_saved_viewproj, m_invview);
		m_current.set(Device.mProject);
		m_saved_viewproj.set(Device.mFullTransform);
	}

	// SSR ///////////////////////////////////////////////////////////
	u_setrt(rt_ssfx_temp2, 0, 0, HW.pBaseZB);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	if (ScaleFactor > 1.0f)
		HW.pContext->RSSetViewports(1, &VP_Scaled);

	//Fill vertex buffer
	FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	//Set pass
	RCache.set_Element(s_ssfx_ssr->E[0]);
	RCache.set_c("m_current", m_current);
	RCache.set_c("m_previous", m_previous);
	RCache.set_c("ssr_setup", ps_ssfx_ssr);
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

	// COPY SSR RESULT ( ACC ) ////////////////////////////////////////////
	HW.pContext->CopyResource(rt_ssfx->pTexture->surface_get(), rt_ssfx_temp2->pTexture->surface_get());

	// Disable/Enable Blur if the value is <= 0
	if (ps_ssfx_ssr.y > 0)
	{
		// BLUR PHASE 1 //////////////////////////////////////////////////////////
		u_setrt(rt_ssfx_temp, 0, 0, HW.pBaseZB);
		RCache.set_CullMode(CULL_NONE);
		RCache.set_Stencil(FALSE);

		// Fill vertex buffer
		pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
		pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
		pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
		pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
		pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
		RCache.Vertex.Unlock(4, g_combine->vb_stride);

		// Draw COLOR
		RCache.set_Element(s_ssfx_ssr->E[1]);
		RCache.set_c("blur_params", 1.0, 0.0, scale_X, scale_Y);
		RCache.set_c("ssr_setup", ps_ssfx_ssr);
		RCache.set_Geometry(g_combine);
		RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);


		// BLUR PHASE 2 //////////////////////////////////////////////////////////
		u_setrt(rt_ssfx_temp2, 0, 0, HW.pBaseZB);
		RCache.set_CullMode(CULL_NONE);
		RCache.set_Stencil(FALSE);

		// Fill vertex buffer
		pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
		pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
		pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
		pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
		pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
		RCache.Vertex.Unlock(4, g_combine->vb_stride);

		// Draw COLOR
		RCache.set_Element(s_ssfx_ssr->E[2]);
		RCache.set_c("blur_params", 0.0, 1.0, w, h);
		RCache.set_c("ssr_setup", ps_ssfx_ssr);
		RCache.set_Geometry(g_combine);
		RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
	}
	else
	{
		// NO BLUR //////////////////////////////////////////////////
		u_setrt(rt_ssfx_temp2, 0, 0, HW.pBaseZB);
		RCache.set_CullMode(CULL_NONE);
		RCache.set_Stencil(FALSE);

		// Fill vertex buffer
		pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
		pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
		pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
		pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
		pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
		RCache.Vertex.Unlock(4, g_combine->vb_stride);

		// Draw COLOR
		RCache.set_Element(s_ssfx_ssr->E[4]);
		RCache.set_Geometry(g_combine);
		RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
	}

	// COMBINE //////////////////////////////////////////////////////////
	
	 // Reset Viewport
	if (ScaleFactor > 1.0f)
		HW.pContext->RSSetViewports(1, &VP_FullRes);

	p1.set(1.0f, 1.0f);

	if (!RImplementation.o.dx10_msaa)
		u_setrt(rt_Generic_0, nullptr, nullptr, nullptr);
	else
		u_setrt(rt_Generic_0_r, nullptr, nullptr, nullptr);

	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	// Fill vertex buffer
	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	// Draw COLOR
	RCache.set_Element(s_ssfx_ssr->E[3]);
	RCache.set_c("ssr_setup", ps_ssfx_ssr);
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
};

void CRenderTarget::phase_ssfx_volumetric_blur()
{
	//Constants
	u32 Offset = 0;
	u32 C = color_rgba(0, 0, 0, 255);

	float d_Z = EPS_S;
	float d_W = 1.0f;
	float w = float(Device.dwWidth);
	float h = float(Device.dwHeight);

	float ScaleFactor = ps_ssfx_volumetric.w;

	Fvector2 p0, p1;
	p0.set(0.0f, 0.0f);
	p1.set(1.0f / ScaleFactor, 1.0f / ScaleFactor);

	// Scale Viewport
	if (ScaleFactor > 1.0)
		set_viewport_size(HW.pContext, w / ScaleFactor, h / ScaleFactor);

	FLOAT ColorRGBA[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	HW.pContext->ClearRenderTargetView(rt_ssfx_accum->pRT, ColorRGBA);

	// BLUR PHASE 1 //////////////////////////////////////////////////////////
	u_setrt(rt_ssfx_accum, 0, 0, NULL); //!RImplementation.o.dx10_msaa ? HW.pBaseZB : rt_MSAADepth->pZRT
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	// Fill vertex buffer
	FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	// Draw COLOR
	RCache.set_Element(s_ssfx_volumetric_blur->E[0]);
	RCache.set_c("blur_setup", w, h, 0, 2);
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, w, h, 0, 2);


	// BLUR PHASE 2 //////////////////////////////////////////////////////////
	u_setrt(rt_Generic_2, 0, 0, NULL);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	// Fill vertex buffer
	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	// Draw COLOR
	RCache.set_Element(s_ssfx_volumetric_blur->E[1]);
	RCache.set_c("blur_setup", w, h, 1, 0.5);
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, w, h, 0, 2);


	// BLUR PHASE 3 //////////////////////////////////////////////////////////
	u_setrt(rt_ssfx_accum, 0, 0, NULL);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	// Fill vertex buffer
	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	// Draw COLOR
	RCache.set_Element(s_ssfx_volumetric_blur->E[0]);
	RCache.set_c("blur_setup", w, h, 1, 2.0f);
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);


	// BLUR PHASE 4 //////////////////////////////////////////////////////////
	u_setrt(rt_Generic_2, 0, 0, NULL);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	// Fill vertex buffer
	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	// Draw COLOR
	RCache.set_Element(s_ssfx_volumetric_blur->E[1]);
	RCache.set_c("blur_setup", w, h, 2, 0.5f);
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

	// Restore Viewport
	if (ScaleFactor > 1.0f)
		set_viewport_size(HW.pContext, w, h);

};
#endif
