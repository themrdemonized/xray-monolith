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
		C.r_Pass("null", "pp_bloom", FALSE, FALSE, FALSE);
		C.r_Sampler_clf("s_image", r2_RT_generic0);

		C.r_Sampler_clf("s_blur_2", r2_RT_blur_2);
		C.r_Sampler_clf("s_blur_4", r2_RT_blur_4);
		C.r_Sampler_clf("s_blur_8", r2_RT_blur_8);		
	
        C.r_End();
        break;
    }
} 