#include "stdafx.h"
#pragma hdrstop

#include "blender_pp_bloom.h"

CBlender_pp_bloom::CBlender_pp_bloom()	{ description.CLS = 0; }
CBlender_pp_bloom::~CBlender_pp_bloom()	{	}

void	CBlender_pp_bloom::Compile(CBlender_Compile& C)
{
    IBlender::Compile(C);

    switch (C.iElement)
    {
    case 0:		
		C.r_Pass("stub_screen_space", "pp_bloom", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_image", r2_RT_generic0);

		C.r_dx10Texture("s_blur_2", r2_RT_blur_2);
		C.r_dx10Texture("s_blur_4", r2_RT_blur_4);
		C.r_dx10Texture("s_blur_8", r2_RT_blur_8);				
	
		C.r_dx10Sampler("smp_base");
		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_rtlinear");
        C.r_End();
        break;
    }
} 