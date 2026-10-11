#pragma once
namespace r1st {
inline void correct_decompression(float* p,float p11,float p22,float p31,float p32){
    // gbuffer: view.xy = depth * (pixel.xy * p.zw - p.xy).
    // Off-centre projection adds p31/p32 in NDC; undo it in the origin only.
    p[0]+=p31/p11;
    p[1]+=p32/p22;
}
}
