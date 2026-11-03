#include "stdafx.h"
#include "r4_rendertarget.h"

#include "../xrRender/xrRender_console.h"

void CRenderTarget::begin_temporal_frame(const Fvector2& jitter)
{
    const bool continuous = m_temporalFrame + 1 == Device.dwFrame;
    const bool sameMode = m_temporalMode == ps_r4_temporal_aa;
    const bool nearby = m_temporalCameraPosition.distance_to_sqr(Device.vCameraPosition) < 25.f;

    m_temporalReset = !m_temporalHistoryValid || !continuous || !sameMode || !nearby;
    m_temporalJitter.set(jitter);
    m_temporalCurrent.set(Device.mProject);

    if (m_temporalReset)
        m_temporalPrevious.mul(Device.mFullTransform, Device.mInvView);
    else
        m_temporalPrevious.mul(m_temporalPreviousFull, Device.mInvView);

    m_temporalPreviousFull.set(Device.mFullTransform);
    m_temporalCameraPosition.set(Device.vCameraPosition);
    m_temporalFrame = Device.dwFrame;
    m_temporalMode = ps_r4_temporal_aa;
    m_temporalHistoryValid = true;
}

void CRenderTarget::invalidate_temporal_history()
{
    m_temporalHistoryValid = false;
}

void CRenderTarget::phase_temporal_prepare()
{
    u_setrt(rt_temporal_velocity, rt_temporal_depth, rt_temporal_reactive, nullptr);
    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);

    u32 offset = 0;
    const u32 color = color_rgba(255, 255, 255, 255);
    const float width = float(Device.dwWidth);
    const float height = float(Device.dwHeight);

    FVF::TL* vertices = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, offset);
    vertices->set(0.f, height, EPS_S, 1.f, color, 0.f, 1.f); ++vertices;
    vertices->set(0.f, 0.f, EPS_S, 1.f, color, 0.f, 0.f); ++vertices;
    vertices->set(width, height, EPS_S, 1.f, color, 1.f, 1.f); ++vertices;
    vertices->set(width, 0.f, EPS_S, 1.f, color, 1.f, 0.f); ++vertices;
    RCache.Vertex.Unlock(4, g_combine->vb_stride);

    RCache.set_Element(s_temporal_prepare->E[0]);
    RCache.set_c("m_temporal_current", m_temporalCurrent);
    RCache.set_c("m_temporal_previous", m_temporalPrevious);
    RCache.set_c("temporal_params", VIEWPORT_NEAR,
        g_pGamePersistent->Environment().CurrentEnv->far_plane,
        m_temporalJitter.x, m_temporalJitter.y);
    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST, offset, 0, 4, 0, 2);
}

void CRenderTarget::phase_temporal_resolve()
{
    if (m_temporalReset)
        HW.pContext->CopyResource(rt_temporal_history->pSurface, rt_Generic_0->pSurface);

    u_setrt(rt_temporal_output, nullptr, nullptr, nullptr);
    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);

    u32 offset = 0;
    const u32 color = color_rgba(255, 255, 255, 255);
    const float width = float(Device.dwWidth);
    const float height = float(Device.dwHeight);

    FVF::TL* vertices = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, offset);
    vertices->set(0.f, height, EPS_S, 1.f, color, 0.f, 1.f); ++vertices;
    vertices->set(0.f, 0.f, EPS_S, 1.f, color, 0.f, 0.f); ++vertices;
    vertices->set(width, height, EPS_S, 1.f, color, 1.f, 1.f); ++vertices;
    vertices->set(width, 0.f, EPS_S, 1.f, color, 1.f, 0.f); ++vertices;
    RCache.Vertex.Unlock(4, g_combine->vb_stride);

    RCache.set_Element(s_temporal_prepare->E[1]);
    RCache.set_c("temporal_resolve_params", width, height, 1.f / width, 1.f / height);
    RCache.set_c("temporal_resolve_control", m_temporalReset ? 0.f : 1.f, 0.f, 0.f, 0.f);
    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST, offset, 0, 4, 0, 2);

    HW.pContext->CopyResource(rt_Generic_0->pSurface, rt_temporal_output->pSurface);
    HW.pContext->CopyResource(rt_temporal_history->pSurface, rt_temporal_output->pSurface);
}

bool CRenderTarget::phase_temporal_aa()
{
    if (ps_r4_temporal_aa != 1)
        return false;

    if (RImplementation.o.dx10_msaa)
    {
        static bool reported = false;
        if (!reported)
        {
            Msg("! TAA: disable MSAA before enabling temporal antialiasing");
            reported = true;
        }
        invalidate_temporal_history();
        return false;
    }

    // Keep the unfiltered frame for world TAA. phase_smaa leaves its result in
    // both rt_Color and rt_Generic_0, so restore the former input afterwards;
    // the resolve shader samples rt_Color only for the first-person mask.
    HW.pContext->CopyResource(rt_temporal_output->pSurface, rt_Generic_0->pSurface);
    phase_smaa();
    HW.pContext->CopyResource(rt_Generic_0->pSurface, rt_temporal_output->pSurface);
    phase_temporal_prepare();
    phase_temporal_resolve();
    return true;
}
