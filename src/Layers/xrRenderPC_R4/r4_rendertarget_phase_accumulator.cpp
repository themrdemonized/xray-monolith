#include "stdafx.h"

void CRenderTarget::phase_accumulator()
{
	// Targets
	if (dwAccumulatorClearMark == Device.dwFrame)
	{
		// normal operation - setup
		if (!RImplementation.o.dx10_msaa)
		{
			if (RImplementation.o.fp16_blend) u_setrt(rt_Accumulator, NULL,NULL, HW.pBaseZB);
			else u_setrt(rt_Accumulator_temp, NULL,NULL, HW.pBaseZB);
		}
		else
		{
			if (RImplementation.o.fp16_blend) u_setrt(rt_Accumulator, NULL,NULL, rt_MSAADepth->pZRT);
			else u_setrt(rt_Accumulator_temp, NULL,NULL, rt_MSAADepth->pZRT);
		}
	}
	else
	{
		// initial setup
		dwAccumulatorClearMark = Device.dwFrame;

		// clear
		if (!RImplementation.o.dx10_msaa)
			u_setrt(rt_Accumulator, NULL,NULL, HW.pBaseZB);
		else
			u_setrt(rt_Accumulator, NULL,NULL, rt_MSAADepth->pZRT);
		//dwLightMarkerID						= 5;					// start from 5, increment in 2 units
		reset_light_marker();
		//	Igor: AMD bug workaround. Should be fixed in 8.7 catalyst
		//	Need for MSAA to work correctly.
		if (RImplementation.o.dx10_msaa)
		{
			HW.pContext->OMSetRenderTargets(1, &(rt_Accumulator->pRT), 0);
		}
		//		u32		clr4clear					= color_rgba(0,0,0,0);	// 0x00
		//CHK_DX	(HW.pDevice->Clear			( 0L, NULL, D3DCLEAR_TARGET, clr4clear, 1.0f, 0L));
		FLOAT ColorRGBA[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		HW.pContext->ClearRenderTargetView(rt_Accumulator->pRT, ColorRGBA);

		//	render this after sun to avoid troubles with sun
		/*
		// Render emissive geometry, stencil - write 0x0 at pixel pos
		RCache.set_xform_project					(Device.mProject);
		RCache.set_xform_view						(Device.mView);
		// Stencil - write 0x1 at pixel pos -
		RCache.set_Stencil							( TRUE,D3DCMP_ALWAYS,0x01,0xff,0xff,D3DSTENCILOP_KEEP,D3DSTENCILOP_REPLACE,D3DSTENCILOP_KEEP);
		//RCache.set_Stencil						(TRUE,D3DCMP_ALWAYS,0x00,0xff,0xff,D3DSTENCILOP_KEEP,D3DSTENCILOP_REPLACE,D3DSTENCILOP_KEEP);
		RCache.set_CullMode							(CULL_CCW);
		RCache.set_ColorWriteEnable					();
		RImplementation.r_dsgraph_render_emissive	();
		*/
		// Stencil	- draw only where stencil >= 0x1
		RCache.set_Stencil(TRUE, D3DCMP_LESSEQUAL, 0x01, 0xff, 0x00);
		RCache.set_CullMode(CULL_NONE);
		RCache.set_ColorWriteEnable();
	}

	//	Restore viewport after shadow map rendering
	RImplementation.rmNormal();
}

void CRenderTarget::phase_vol_accumulator()
{
	// TODO: there is a bug in this function when MSAA is enabled, if you run with the D3D debug layer enabled, you'll get this error:
	// ID3D11DeviceContext::OMSetRenderTargets: The RenderTargetView at slot 0 is not compatible with the DepthStencilView. DepthStencilViews may only be used with RenderTargetViews if the effective dimensions of the Views are equal, as well as the Resource types, multisample count, and multisample quality. The RenderTargetView at slot 0 has (w:2560,h:1440,as:1), while the Resource is a Texture2D with (mc:4,mq:4294967295). The DepthStencilView has (w:2560,h:1440,as:1), while the Resource is a Texture2D with (mc:1,mq:0).
	// this seems to be because the pBaseZB target is always (SampleCount, SampleQuality) = (1, 0) (see dx10HW.cpp) but the
	// `rt_Generic_2` render target is a multisampled rendertarget
	// Not sure how to fix it correctly, if it doesn't need a depth buffer then just pass NULL, if it does
	// we probably need to create a multisampled depth target (e.g. `rt_Generic_2_zb`)
	//
	// The best way I've found to see this happen is to launch the DX11 exe with `--dxgi-dbg` to enable the debug layer,
	// then launch the game in RenderDoc with `Enable API Validation` and `Collect Callstacks` enabled, take a capture
	// then go to `Tools > Load Symbols` to load the callstack symbols. In the bottom left of the main RenderDoc window
	// there's a little status message that you can click on to see all the debug layer errors/warnings that lets you jump
	// directly to the render event where the error occurred.
	//
	// This will let you go to the precise render event that caused the debug layer to log an error, and let you see the
	// callstack where the Render() call occurred

	if (!m_bHasActiveVolumetric)
	{
		m_bHasActiveVolumetric = true;
		if (!RImplementation.o.dx10_msaa)
			u_setrt(rt_Generic_2, NULL,NULL, RImplementation.o.ssfx_volumetric ? NULL : HW.pBaseZB);
		else
			u_setrt(rt_Generic_2, NULL,NULL, RImplementation.o.ssfx_volumetric ? NULL : HW.pBaseZB);
		//u32		clr4clearVol				= color_rgba(0,0,0,0);	// 0x00
		//CHK_DX	(HW.pDevice->Clear			( 0L, NULL, D3DCLEAR_TARGET, clr4clearVol, 1.0f, 0L));
		FLOAT ColorRGBA[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		HW.pContext->ClearRenderTargetView(rt_Generic_2->pRT, ColorRGBA);
	}
	else
	{
		if (!RImplementation.o.dx10_msaa)
			u_setrt(rt_Generic_2, NULL,NULL, RImplementation.o.ssfx_volumetric ? NULL : HW.pBaseZB);
		else
			u_setrt(rt_Generic_2, NULL,NULL, RImplementation.o.ssfx_volumetric ? NULL : HW.pBaseZB);
	}

	RCache.set_Stencil(FALSE);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_ColorWriteEnable();
}
