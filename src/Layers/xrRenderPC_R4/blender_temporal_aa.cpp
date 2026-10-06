#include "stdafx.h"
#include "blender_temporal_aa.h"

CBlender_temporal_prepare::CBlender_temporal_prepare()
{
    description.CLS = 0;
}

void CBlender_temporal_prepare::Compile(CBlender_Compile& C)
{
    IBlender::Compile(C);

    switch (C.iElement)
    {
    case 0:
        C.r_Pass("stub_screen_space", "temporal_prepare", FALSE, FALSE, FALSE);
        C.r_dx10Texture("s_position", r2_RT_P);
        C.r_dx10Sampler("smp_nofilter");
        C.r_End();
        break;
    case 1:
        C.r_Pass("stub_screen_space", "temporal_resolve", FALSE, FALSE, FALSE);
        C.r_dx10Texture("s_current", r2_RT_generic0);
        C.r_dx10Texture("s_history", r4_RT_temporal_history);
        C.r_dx10Texture("s_motion", r4_RT_temporal_velocity);
        C.r_dx10Texture("s_depth", r4_RT_temporal_depth);
        C.r_dx10Texture("s_reactive", r4_RT_temporal_reactive);
        C.r_dx10Texture("s_smaa", r2_RT_albedo);
        C.r_dx10Sampler("smp_nofilter");
        C.r_dx10Sampler("smp_rtlinear");
        C.r_End();
        break;
    }
}
