#include "stdafx.h"

#include "blender_smaa.h"

CBlender_smaa::CBlender_smaa() { description.CLS = 0; }

CBlender_smaa::~CBlender_smaa()
{
}

void CBlender_smaa::Compile(CBlender_Compile& C)
{
	IBlender::Compile(C);
	switch (C.iElement)
	{
	case 0: //Edge detection
		C.r_Pass("pp_smaa_ed", "pp_smaa_ed", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_image", r2_RT_generic0);

		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;
	case 1:	//Weight 
		C.r_Pass("pp_smaa_bc", "pp_smaa_bc", FALSE, FALSE, FALSE);	
		C.r_dx10Texture("s_image", r2_RT_generic0);
		C.r_dx10Texture("s_edgetex", r2_RT_smaa_edgetex);

		C.r_dx10Texture("s_areatex", "shaders\\smaa\\area_tex_dx11");
		C.r_dx10Texture("s_searchtex", "shaders\\smaa\\search_tex");

		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;
	case 2:	//Blending
		C.r_Pass("pp_smaa_nb", "pp_smaa_nb", FALSE, FALSE, FALSE);	
		C.r_dx10Texture("s_image", r2_RT_generic0);
		C.r_dx10Texture("s_blendtex", r2_RT_smaa_blendtex);

		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_rtlinear");	
		C.r_End();
		break;	
	}
}
