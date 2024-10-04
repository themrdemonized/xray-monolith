#include "stdafx.h"
#pragma hdrstop

#include "blender_hdr10_lens_flare.h"

CBlender_hdr10_lens_flare_downsample::CBlender_hdr10_lens_flare_downsample()	{ description.CLS = 0; }
CBlender_hdr10_lens_flare_downsample::~CBlender_hdr10_lens_flare_downsample()	{	}

void CBlender_hdr10_lens_flare_downsample::Compile(CBlender_Compile& C)
{
    IBlender::Compile(C);

	C.r_Pass("stub_screen_space", "hdr10_lens_flare_downsample", FALSE, FALSE, FALSE);

    switch (C.iElement)
    {
		case 0: {
			C.r_dx10Texture("s_hdr10_game", r2_RT_generic0);
		} break;
    }

	C.r_dx10Sampler("smp_rtlinear");
	C.r_End();
}

CBlender_hdr10_lens_flare_fgen::CBlender_hdr10_lens_flare_fgen()	{ description.CLS = 0; }
CBlender_hdr10_lens_flare_fgen::~CBlender_hdr10_lens_flare_fgen()	{	}

void CBlender_hdr10_lens_flare_fgen::Compile(CBlender_Compile& C)
{
    IBlender::Compile(C);

	C.r_Pass("stub_screen_space", "hdr10_lens_flare_fgen", FALSE, FALSE, FALSE);

    switch (C.iElement)
    {
		case 0: {
			C.r_dx10Texture("s_hdr10_halfres", r4_RT_HDR10_halfres0);
		} break;

		case 1: {
			C.r_dx10Texture("s_hdr10_halfres", r4_RT_HDR10_halfres1);
		} break;
    }

	C.r_dx10Sampler("smp_rtlinear");
	C.r_End();
}

CBlender_hdr10_lens_flare_blur::CBlender_hdr10_lens_flare_blur()	{ description.CLS = 0; }
CBlender_hdr10_lens_flare_blur::~CBlender_hdr10_lens_flare_blur()	{	}

void CBlender_hdr10_lens_flare_blur::Compile(CBlender_Compile& C)
{
    IBlender::Compile(C);

	C.r_Pass("stub_screen_space", "hdr10_lens_flare_blur", FALSE, FALSE, FALSE);

    switch (C.iElement)
    {
		case 0: {
			C.r_dx10Texture("s_hdr10_halfres", r4_RT_HDR10_halfres0);
		} break;

		case 1: {
			C.r_dx10Texture("s_hdr10_halfres", r4_RT_HDR10_halfres1);
		} break;
    }

	C.r_dx10Sampler("smp_rtlinear");
	C.r_End();
}

CBlender_hdr10_lens_flare_upsample::CBlender_hdr10_lens_flare_upsample()	{ description.CLS = 0; }
CBlender_hdr10_lens_flare_upsample::~CBlender_hdr10_lens_flare_upsample()	{	}

void CBlender_hdr10_lens_flare_upsample::Compile(CBlender_Compile& C)
{
    IBlender::Compile(C);

	C.r_Pass("stub_screen_space", "hdr10_lens_flare_upsample", FALSE, FALSE, FALSE);

    switch (C.iElement)
    {
		case 0: {
			C.r_dx10Texture("s_hdr10_halfres", r4_RT_HDR10_halfres0);
		} break;

		case 1: {
			C.r_dx10Texture("s_hdr10_halfres", r4_RT_HDR10_halfres1);
		} break;
    }

	C.r_dx10Texture("s_hdr10_game", r2_RT_generic0);
	C.r_dx10Sampler("smp_rtlinear");
	C.r_End();
}
