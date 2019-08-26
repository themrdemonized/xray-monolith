#include "stdafx.h"

#include "blender_nightvision.h"

CBlender_nightvision::CBlender_nightvision() { description.CLS = 0; }

CBlender_nightvision::~CBlender_nightvision()
{
}

void CBlender_nightvision::Compile(CBlender_Compile& C)
{
	IBlender::Compile(C);
	switch (C.iElement)
	{
	case 0: //Dummy shader - because IDK what gonna happen when r2_nightvision will be 0
		C.r_Pass("null", "copy", FALSE, FALSE, FALSE);
		C.r_Sampler_clf("s_base", r2_RT_generic0);
		C.r_End();
		break;
	case 1:	
		C.r_Pass("null", "nightvision_gen_1", FALSE, FALSE, FALSE);	
		C.r_Sampler_rtf("s_position", r2_RT_P);
		C.r_Sampler_clf("s_image", r2_RT_generic0);	
		C.r_End();
		break;
	case 2:	
		C.r_Pass("null", "nightvision_gen_2", FALSE, FALSE, FALSE);	
		C.r_Sampler_rtf("s_position", r2_RT_P);
		C.r_Sampler_clf("s_image", r2_RT_generic0);	
		C.r_End();
		break;
	case 3:	
		C.r_Pass("null", "nightvision_gen_3", FALSE, FALSE, FALSE);	
		C.r_Sampler_rtf("s_position", r2_RT_P);
		C.r_Sampler_clf("s_image", r2_RT_generic0);	
		C.r_End();
		break;	
	}
}
