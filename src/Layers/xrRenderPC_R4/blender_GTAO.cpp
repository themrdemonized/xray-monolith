//GTAO by doenitz

#include "stdafx.h"

#include "blender_GTAO.h"

CBlender_GTAO::CBlender_GTAO() { description.CLS = 0; }

CBlender_GTAO::~CBlender_GTAO()
{
}

void CBlender_GTAO::Compile(CBlender_Compile& C)
{
	IBlender::Compile(C);

	switch (C.iElement)
	{
	case 0:	//generate ambient occlusion
		C.r_Pass("stub_screen_space", "GTAO", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_position", r2_RT_P);
		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;
	case 1:	//apply blur / denoise
		C.r_Pass("stub_screen_space", "GTAO_blur", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_image", r2_RT_GTAO_1);
		C.r_dx10Texture("s_position", r2_RT_P);
		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;
	}
}