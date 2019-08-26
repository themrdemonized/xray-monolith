#include "stdafx.h"

#include "blender_lut.h"

CBlender_lut::CBlender_lut() { description.CLS = 0; }

CBlender_lut::~CBlender_lut()
{
}

void CBlender_lut::Compile(CBlender_Compile& C)
{
	IBlender::Compile(C);

	C.r_Pass("stub_screen_space", "pp_lut", FALSE, FALSE, FALSE);
	C.r_dx10Texture("s_image", r2_RT_generic0);
	C.r_dx10Texture("s_lut_atlas", "shaders\\lut_atlas");

	C.r_dx10Sampler("smp_base");
	C.r_dx10Sampler("smp_nofilter");
	C.r_dx10Sampler("smp_rtlinear");
	C.r_dx10Sampler("smp_linear");	
	C.r_End();
}
