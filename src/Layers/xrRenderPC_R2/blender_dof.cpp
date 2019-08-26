#include "stdafx.h"

#include "blender_dof.h"

CBlender_dof::CBlender_dof() { description.CLS = 0; }

CBlender_dof::~CBlender_dof()
{
}

void CBlender_dof::Compile(CBlender_Compile& C)
{
	IBlender::Compile(C);

	switch (C.iElement)
	{
	case 0:
		C.r_Pass("null", "depth_of_field", FALSE, FALSE, FALSE);
		C.r_Sampler_rtf("s_position", r2_RT_P);
		C.r_Sampler_clf("s_image", r2_RT_generic0);
		C.r_Sampler_clf("s_blur_2", r2_RT_blur_2);
		C.r_End();
		break;
	case 1:
		C.r_Pass("null", "post_processing", FALSE, FALSE, FALSE);
		C.r_Sampler_clf("samplero_pepero", r2_RT_dof);
		C.r_End();
		break;
	}
}
