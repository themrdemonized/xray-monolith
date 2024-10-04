#include "stdafx.h"

static void PushFSQ(FVF::TL* pv, float w, float h)
{
    u32   C   = color_rgba(0, 0, 0, 255);
    float d_Z = EPS_S;
    float d_W = 1.0f;

    pv->set(0, h, d_Z, d_W, C, 0.0f, 1.0f); pv++;
    pv->set(0, 0, d_Z, d_W, C, 0.0f, 0.0f); pv++;
    pv->set(w, h, d_Z, d_W, C, 1.0f, 1.0f); pv++;
    pv->set(w, 0, d_Z, d_W, C, 1.0f, 0.0f); pv++;
}

template<class T>
static void Swap(T& a, T& b)
{
    T tmp = a;
    a = b;
    b = tmp;
}

void CRenderTarget::phase_hdr10_lens_flare()
{
    RCache.set_Z(FALSE);

    float orig_w = float(Device.dwWidth);
    float orig_h = float(Device.dwHeight);

    float flare_w = floor(float(Device.dwWidth)  / 2.0f);
    float flare_h = floor(float(Device.dwHeight) / 2.0f);

    u32 Offset = 0;

    FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
    PushFSQ(pv, orig_w, orig_h);
    RCache.Vertex.Unlock(4, g_combine->vb_stride);

    int src = 0;
    int dst = 1;

    // downsample
    {
        RCache.set_Element(s_hdr10_lens_flare_downsample->E[0]);
        RCache.set_Geometry(g_combine);

        float dx = 1.0f / orig_w;
        float dy = 1.0f / orig_h;
        RCache.set_c("hdr10_flare_sparams", dx, dy, 0, 0);

        set_viewport_size(HW.pContext, flare_w, flare_h);
        u_setrt(rt_HDR10_HalfRes[dst], NULL, NULL, NULL);
        RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
    }
    
    Swap(src, dst);
    
    // feature generation
    // viewport size already set
    {
        RCache.set_Element(s_hdr10_lens_flare_fgen->E[src]);
        RCache.set_Geometry(g_combine);

        float dx = 1.0f / orig_w;
        float dy = 1.0f / orig_h;
        RCache.set_c("hdr10_flare_sparams", dx, dy, 0, 0);

        u_setrt(rt_HDR10_HalfRes[dst], NULL, NULL, NULL);
        RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
    }

    // blur
    {
        // ping pong between buffers, sampling the src target, applying blur at different scales (Kawase)
        // viewport already set to correct resolution
        for (int pass = 0; pass < ps_r4_hdr10_flare_blur_passes; pass++) {
            // alternating horizontal and vertical blurs per pass
            for (int dir = 0; dir < 2; dir++) {
                Swap(src, dst);

                RCache.set_Element(s_hdr10_lens_flare_blur->E[src]);
                RCache.set_Geometry(g_combine);

                float dx = ps_r4_hdr10_flare_blur_scale * float(pass + 1) * 1.0f / flare_w;
                float dy = ps_r4_hdr10_flare_blur_scale * float(pass + 1) * 1.0f / flare_h;
                RCache.set_c("hdr10_flare_sparams", dx, dy, float(dir % 2), 0);

                u_setrt(rt_HDR10_HalfRes[dst], NULL, NULL, NULL);
                RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
            }
        }
    }

    // upsample and add
    {
        RCache.set_Element(s_hdr10_lens_flare_upsample->E[dst]);
        RCache.set_Geometry(g_combine);

        set_viewport_size(HW.pContext, orig_w, orig_h);
        u_setrt(rt_Color, NULL, NULL, NULL);
        RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
    }

    RCache.set_Z(TRUE);
}
