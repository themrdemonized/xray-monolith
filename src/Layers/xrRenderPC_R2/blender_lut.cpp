#include "stdafx.h"

#include "blender_lut.h"

CBlender_lut::CBlender_lut() { description.CLS = 0; }

CBlender_lut::~CBlender_lut()
{
}

void CBlender_lut::Compile(CBlender_Compile& C)
{
	IBlender::Compile(C);

	C.r_Pass("null", "pp_lut", FALSE, FALSE, FALSE);
	C.r_Sampler_clf("s_image", r2_RT_generic0);
	C.r_Sampler_clf("s_lut_atlas", "shaders\\lut_atlas");
	C.r_End();
}
