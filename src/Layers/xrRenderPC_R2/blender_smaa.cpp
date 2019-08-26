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
		C.r_Sampler("s_image", r2_RT_generic0, false, D3DTADDRESS_CLAMP, D3DTEXF_LINEAR, D3DTEXF_POINT, D3DTEXF_LINEAR);
		
		C.r_End();
		break;
	case 1:	//Weight 
		C.r_Pass("pp_smaa_bc", "pp_smaa_bc", FALSE, FALSE, FALSE);	
		C.r_Sampler_clf("s_image", r2_RT_generic0);	
		C.r_Sampler("s_edgetex", r2_RT_smaa_edgetex, false, D3DTADDRESS_CLAMP);

		C.r_Sampler("s_areatex", "shaders\\smaa\\area_tex_dx9", false, D3DTADDRESS_CLAMP);
		C.r_Sampler("s_searchtex", "shaders\\smaa\\search_tex", false, D3DTADDRESS_CLAMP, D3DTEXF_POINT, D3DTEXF_POINT, D3DTEXF_POINT);
	
		C.r_End();
		break;
	case 2:	//Blending
		C.r_Pass("pp_smaa_nb", "pp_smaa_nb", FALSE, FALSE, FALSE);	
		C.r_Sampler_clf("s_image", r2_RT_generic0);	
		C.r_Sampler("s_blendtex", r2_RT_smaa_blendtex, false, D3DTADDRESS_CLAMP);
		
		C.r_End();
		break;	
	}
}
