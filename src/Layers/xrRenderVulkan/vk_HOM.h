// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#pragma once
// ============================================================================
// Vulkan HOM (Hierarchical Occlusion Map)
// Software-based occlusion culling system
// ============================================================================

#include "../../xrCDB/frustum.h"
#include "../../xrEngine/vis_common.h"
#include "../../xrEngine/IGame_Persistent.h"
#include "vk_occRasterizer.h"

class occTri;

class vkCHOM
{
public:
    BOOL bEnabled;  // Moved to public for access from rvk.cpp

private:
    xrXRC xrc;
    CDB::MODEL* m_pModel;
    occTri* m_pTris;
    Fmatrix m_xform;
    Fmatrix m_xform_01;

    occRasterizer Raster;  // Software rasterizer instance

    xrCriticalSection MT;
    volatile u32 MT_frame_rendered;

    void Render_DB(CFrustum& base);

public:
    void Load();
    void Unload();
    void Render(CFrustum& base);
    void Render_ZB() {}

    void occlude(Fbox2& space);

    void Disable();
    void Enable();

    void __stdcall MT_RENDER();
    ICF void MT_SYNC()
    {
        if (g_pGamePersistent->m_pMainMenu && g_pGamePersistent->m_pMainMenu->IsActive())
            return;

        MT_RENDER();
    }

    BOOL visible(vis_data& vis);
    BOOL visible(Fbox3& B);
    BOOL visible(sPoly& P);
    BOOL visible(Fbox2& B, float depth); // viewport-space (0..1)

    vkCHOM();
    ~vkCHOM();

#ifdef DEBUG
    void OnRender();
    void stats();
#endif
};
