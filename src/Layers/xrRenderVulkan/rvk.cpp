// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#include "stdafx.h"
#include "rvk.h"
#include "vk_R_Backend.h"
#include "vk_sector.h"
#include "HW_Vulkan.h"
#include "vk_swapchain.h"
#include "vk_command_buffer.h"
#include "vk_sync.h"
#include "vk_rendertarget.h"
#include "vk_shaders.h"
#include "vk_descriptors.h"
#include "vk_pipeline.h"
#include "vk_buffer.h"
#include "vk_buffer_pool.h"
#include "vk_texture.h"
#include "vk_material.h"
#include "vk_shader.h"
#include "vk_buffer_pool.h"
#include "vk_ModelPool.h"
#include "vk_Visual.h"
#include "vk_ParticleCustom.h"
#include "vk_ParticleEffect.h"
#include "vk_ParticleGroup.h"
#include "vk_WallmarksEngine.h"
#include "../xrRender/dxWallMarkArray.h"
#include "../xrRender/dxUIShader.h"
#include "../xrRender/PSLibrary.h"
#include "../../Include/xrRender/Kinematics.h"
#include "../../xrCDB/ISpatial.h"

// Direct diagnostic write for crash debugging
static void VkDiagFrame(const char* /*msg*/) {
	// Disabled: file I/O per frame is too expensive
}

// Light system
#include "../xrRender/light.h"
#include "../xrRender/light_db.h"

// Engine includes for Device access
#include "../../xrEngine/device.h"
#include "../../xrEngine/GameFont.h"
#include "../../xrEngine/IGame_Persistent.h"

// VULKAN_DIAG: Static init diagnostics (using Win32 API to avoid CRT issues)
#include <windows.h>
static void VulkanDiagWriteRvk(const char* msg) {
	HANDLE h = CreateFileA("D:\\anomaly\\appdata\\logs\\vulkan_diag.txt",
		FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, NULL);
	if (h != INVALID_HANDLE_VALUE) {
		DWORD written;
		WriteFile(h, msg, (DWORD)strlen(msg), &written, NULL);
		WriteFile(h, "\r\n", 2, &written, NULL);
		FlushFileBuffers(h);
		CloseHandle(h);
	}
}
struct VulkanDiagRvk {
	VulkanDiagRvk() { VulkanDiagWriteRvk("[DIAG] Before CRender RImplementation construction"); }
} g_VulkanDiagRvk;

// Global render instance
CRender RImplementation;

struct VulkanDiagRvk2 {
	VulkanDiagRvk2() { VulkanDiagWriteRvk("[DIAG] After CRender RImplementation construction"); }
} g_VulkanDiagRvk2;

// SSA (Screen-Space Area) culling thresholds
// Used by sector/portal traversal and LOD system
float r_ssaDISCARD       = 4.f;    // Discard objects smaller than this SSA
float r_ssaDONTSORT      = 32.f;   // Don't sort objects larger than this SSA (always full detail)
float r_ssaLOD_A         = 64.f;   // Start LOD transition at this SSA
float r_ssaLOD_B         = 48.f;   // End LOD transition at this SSA

// External test render function (temporary)
extern void TestRenderFrame();

// ============================================================================
// CRender - Constructor/Destructor
// ============================================================================
CRender::CRender()
{
    VulkanDiagWriteRvk("[DIAG] CRender ctor: step 1 - basic fields");
    phase = PHASE_NORMAL;
    marker = 0;
    pmask[0] = pmask[1] = true;
    pmask_wmark = false;

    val_pObject = nullptr;
    val_pTransform = nullptr;
    val_bHUD = FALSE;
    val_bCamAttached = FALSE;
    val_bInvisible = FALSE;
    val_bRecordMP = FALSE;

    VulkanDiagWriteRvk("[DIAG] CRender ctor: step 2a - o_hemi");
    o_hemi = 0.f;
    VulkanDiagWriteRvk("[DIAG] CRender ctor: step 2b - o_sun");
    o_sun = 0.f;
    VulkanDiagWriteRvk("[DIAG] CRender ctor: step 2c - o_hemi_cube");
    // NOTE: Using memset instead of ZeroMemory because ZeroMemory is redefined
    // as Memory.mem_fill() which requires initialized Memory system.
    // During static construction, Memory is not yet initialized.
    memset(o_hemi_cube, 0, sizeof(o_hemi_cube));
    VulkanDiagWriteRvk("[DIAG] CRender ctor: step 2c2 - after o_hemi_cube");
    VulkanDiagWriteRvk("[DIAG] CRender ctor: step 2d - m_bFirstFrameAfterReset");
    m_bFirstFrameAfterReset = false;
    VulkanDiagWriteRvk("[DIAG] CRender ctor: step 2e - counters");
    counter_S = 0;
    counter_D = 0;
    b_loaded = FALSE;

    VulkanDiagWriteRvk("[DIAG] CRender ctor: step 3 - subsystems");
    Models = nullptr;
    HOM = nullptr;

    VulkanDiagWriteRvk("[DIAG] CRender ctor: step 4 - level data");
    pLastSector = nullptr;
    vLastCameraPos.set(0, 0, 0);
    rmPortals = nullptr;

    VulkanDiagWriteRvk("[DIAG] CRender ctor: step 5 - options");
    memset(&o, 0, sizeof(o));  // memset instead of ZeroMemory (static init safe)
    o.smapsize = 2048;
    o.mrt = 1;
    o.HW_smap = 1;
    o.distortion = 1;
    o.distortion_enabled = 1;
    o.advancedpp = 1;
    o.ssfx_water = 1;

    VulkanDiagWriteRvk("[DIAG] CRender ctor: step 6 - stats");
    memset(&stats, 0, sizeof(stats));  // memset instead of ZeroMemory (static init safe)

    VulkanDiagWriteRvk("[DIAG] CRender ctor: step 7 - scene graph");
    // Scene graph / visibility (Phase 1)
    View = nullptr;  // Will be set to &ViewBase in Calculate()
    VulkanDiagWriteRvk("[DIAG] CRender ctor: DONE");
}

CRender::~CRender()
{
}

// ============================================================================
// Lifecycle
// ============================================================================
void CRender::create()
{
    Msg("[Vulkan] CRender::create()");

    // Initialize backend
    RCache.OnDeviceCreate();

    // Create HOM (deferred from constructor due to xrCriticalSection needing Memory system)
    if (!HOM) {
        HOM = xr_new<vkCHOM>();
        Msg("[Vulkan] HOM created");
    }

    // Create model pool
    if (!Models) {
        Models = xr_new<vkModelPool>();
        Msg("[Vulkan] ModelPool created");
    }

    // Initialize particle system library
    PSLibrary.OnCreate();
    Msg("[Vulkan] PSLibrary initialized");

    // Create managers if not already created
    if (!g_ShaderManager) {
        g_ShaderManager = xr_new<VK::CVulkanSPIRVLoader>();
        Msg("[Vulkan] ShaderManager created");
    }

    if (!g_DescriptorManager) {
        g_DescriptorManager = xr_new<VK::CVulkanDescriptorManager>();
        g_DescriptorManager->Create();
        Msg("[Vulkan] DescriptorManager created");
    }

    if (!VK::g_PipelineManager) {
        VK::g_PipelineManager = xr_new<VK::CVulkanPipelineManager>();
        VK::g_PipelineManager->Create();
        Msg("[Vulkan] PipelineManager created");
    }

    // Create Material Manager (Phase 2.22)
    if (!g_MaterialManager) {
        g_MaterialManager = xr_new<VK::CMaterialManager>();
        g_MaterialManager->Create();
        Msg("[Vulkan] MaterialManager created");
    }

    // Create Vulkan Shader Manager (Phase 2.32)
    if (!g_VulkanShaderManager) {
        g_VulkanShaderManager = xr_new<VK::CVulkanShaderManager>();
        g_VulkanShaderManager->Create();
        Msg("[Vulkan] VulkanShaderManager created");
    }

    // Create Buffer Pool (Phase 2.23)
    if (!VK::g_BufferPool) {
        VK::g_BufferPool = xr_new<VK::CBufferPool>();
        VK::g_BufferPool->Create();
        Msg("[Vulkan] BufferPool created");
    }

    // Create G-Buffer render target
    if (!RTarget && Swapchain.m_Swapchain != VK_NULL_HANDLE) {
        RTarget = xr_new<VK::CRenderTarget>();
        RTarget->Create(Swapchain.m_Extent.width, Swapchain.m_Extent.height);
        Msg("[Vulkan] RenderTarget (G-Buffer) created: %dx%d",
            Swapchain.m_Extent.width, Swapchain.m_Extent.height);
    }

    // Initialize portal traverser for fade rendering
    vkPortalTraverser.initialize();
    Msg("[Vulkan] PortalTraverser initialized");

    // Initialize sun cascade shadow maps (Phase 2.15)
    init_sun_cascades();

    Msg("[Vulkan] CRender::create() complete");
}

// Forward declaration from vk_RenderFactory.cpp
extern void UITextureCache_DestroyAll();

void CRender::destroy()
{
    Msg("[Vulkan] CRender::destroy()");

    // Wait for GPU to finish
    if (VulkanHW.m_Device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(VulkanHW.m_Device);
    }

    // Destroy UI texture cache (must be done before descriptor pool is destroyed)
    UITextureCache_DestroyAll();

    // Destroy HOM
    if (HOM) {
        xr_delete(HOM);
        HOM = nullptr;
        Msg("[Vulkan] HOM destroyed");
    }

    // Clear level visuals
    Visuals.clear();

    // Destroy portal traverser
    vkPortalTraverser.destroy();
    Msg("[Vulkan] PortalTraverser destroyed");

    // Destroy particle system library
    PSLibrary.OnDestroy();
    Msg("[Vulkan] PSLibrary destroyed");

    // Destroy model pool
    if (Models) {
        xr_delete(Models);
        Models = nullptr;
        Msg("[Vulkan] ModelPool destroyed");
    }

    // Destroy render target
    if (RTarget) {
        xr_delete(RTarget);
        RTarget = nullptr;
        Msg("[Vulkan] RenderTarget destroyed");
    }

    // Destroy managers
    if (VK::g_BufferPool) {
        xr_delete(VK::g_BufferPool);
        VK::g_BufferPool = nullptr;
        Msg("[Vulkan] BufferPool destroyed");
    }

    if (g_MaterialManager) {
        xr_delete(g_MaterialManager);
        g_MaterialManager = nullptr;
        Msg("[Vulkan] MaterialManager destroyed");
    }

    if (g_VulkanShaderManager) {
        xr_delete(g_VulkanShaderManager);
        g_VulkanShaderManager = nullptr;
        Msg("[Vulkan] VulkanShaderManager destroyed");
    }

    if (VK::g_PipelineManager) {
        xr_delete(VK::g_PipelineManager);
        VK::g_PipelineManager = nullptr;
        Msg("[Vulkan] PipelineManager destroyed");
    }

    if (g_DescriptorManager) {
        xr_delete(g_DescriptorManager);
        g_DescriptorManager = nullptr;
        Msg("[Vulkan] DescriptorManager destroyed");
    }

    if (g_ShaderManager) {
        xr_delete(g_ShaderManager);
        g_ShaderManager = nullptr;
        Msg("[Vulkan] ShaderManager destroyed");
    }

    // Cleanup backend
    RCache.OnDeviceDestroy();

    Msg("[Vulkan] CRender::destroy() complete");
}

void CRender::reset_begin()
{
    Msg("[Vulkan] CRender::reset_begin()");

    // Called before device reset (window resize, etc.)
    // Wait for GPU to finish before releasing resources
    if (VulkanHW.m_Device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(VulkanHW.m_Device);
    }

    // Release G-Buffer (will be recreated with new size)
    if (RTarget) {
        RTarget->Destroy();
        Msg("[Vulkan] G-Buffer released for resize");
    }
}

void CRender::reset_end()
{
    Msg("[Vulkan] CRender::reset_end()");

    // Called after device reset - recreate resources with new dimensions
    if (RTarget && Swapchain.m_Swapchain != VK_NULL_HANDLE) {
        RTarget->Create(Swapchain.m_Extent.width, Swapchain.m_Extent.height);
        Msg("[Vulkan] G-Buffer recreated: %dx%d",
            Swapchain.m_Extent.width, Swapchain.m_Extent.height);
    }

    m_bFirstFrameAfterReset = true;
}

// ============================================================================
// Level management - see rvk_loader.cpp for implementation
// ============================================================================

// ============================================================================
// Information
// ============================================================================
void CRender::Statistics(CGameFont* F)
{
    // Display render statistics
    if (!F) return;

    F->OutNext("*** VULKAN RENDER ***");
    F->OutNext("Polys:     %d", RCache.stat.polys);
    F->OutNext("Verts:     %d", RCache.stat.verts);
    F->OutNext("DIP/DP:    %d", RCache.stat.calls);
    F->OutNext("Xforms:    %d", RCache.stat.xforms);

    // Swapchain info
    if (Swapchain.m_Swapchain != VK_NULL_HANDLE) {
        F->OutNext("Resolution: %dx%d", Swapchain.m_Extent.width, Swapchain.m_Extent.height);
    }

    // Vulkan HW info
    if (VulkanHW.Caps.deviceName[0]) {
        F->OutNext("GPU: %s", VulkanHW.Caps.deviceName);
    }
}

// ============================================================================
// Main rendering
// ============================================================================
void CRender::Calculate()
{
    // Skip if level not loaded
    if (!b_loaded) return;

    // ========================================================================
    // Phase 2: Full scene graph with Portal Visibility and HOM Occlusion
    // ========================================================================

    // Detect camera sector if not yet known (or sectors were loaded)
    if (!pLastSector && !Sectors.empty())
    {
        pLastSector = (vkCSector*)detectSector(Device.vCameraPosition);
        if (pLastSector)
            Msg("[Vulkan] Detected initial sector for camera");
    }

    // Update lights (sun direction/color from environment)
    Lights.Update();

    // Clear render queues
    lstNormal.clear();
    lstMatrix.clear();

    // Increment marker for visibility tracking
    marker++;

    // Build frustum from camera transform
    ViewBase.CreateFromMatrix(Device.mFullTransform, FRUSTUM_P_LRTB | FRUSTUM_P_FAR);
    View = &ViewBase;

    // ========================================================================
    // HOM (Hierarchical Occlusion Map) - Render occluders
    // ========================================================================
    // HOM renders occluder geometry to a low-res Z-buffer for fast culling
    if (HOM && pLastSector)
    {
        HOM->Enable();
        HOM->Render(ViewBase);
    }

    // ========================================================================
    // Portal Traversal - Determine visible sectors
    // ========================================================================
    if (pLastSector)
    {
        // Calculate view-projection matrix for portal traversal
        Fmatrix m_ViewProjection;
        m_ViewProjection.mul(Device.mProject, Device.mView);

        // Traverse sector/portal structure with HOM + SSA + FADE
        vkPortalTraverser.traverse(
            pLastSector,
            ViewBase,
            Device.vCameraPosition,
            m_ViewProjection,
            vkCPortalTraverser::VQ_HOM | vkCPortalTraverser::VQ_SSA | vkCPortalTraverser::VQ_FADE
        );

        // ========================================================================
        // Render static geometry from visible sectors
        // ========================================================================
        for (u32 s_it = 0; s_it < vkPortalTraverser.r_sectors.size(); s_it++)
        {
            vkCSector* sector = (vkCSector*)vkPortalTraverser.r_sectors[s_it];
            vkRender_Visual* root = sector->root();

            // Process each frustum for this sector
            for (u32 v_it = 0; v_it < sector->r_frustums.size(); v_it++)
            {
                CFrustum& frustum = sector->r_frustums[v_it];
                View = &frustum;

                // Add sector geometry to render queue
                if (root)
                    add_Static(root, SF_RENDERING);
            }
        }

        // Restore main frustum
        View = &ViewBase;
    }
    else
    {
        // ========================================================================
        // Fallback: No portal system - render all visuals
        // ========================================================================
        for (auto visual : Visuals)
        {
            if (!visual) continue;
            vkRender_Visual* pV = static_cast<vkRender_Visual*>(visual);
            add_Static(pV, SF_RENDERING);
        }
    }

    // ========================================================================
    // Dynamic objects from Spatial Database
    // ========================================================================
    // TODO: Implement dynamic object rendering with proper spatial API
    // Currently disabled due to undefined CSpatial_SyncedData type
    /*
    if (g_SpatialSpace && pLastSector)
    {
        // Query all spatials in frustum
        lstSpatial.clear();
        g_SpatialSpace->q_frustum(lstSpatial, ISpatial_DB::O_ORDERED, STYPE_RENDERABLE, ViewBase);

        // Process dynamic renderables with HOM culling
        for (u32 o_it = 0; o_it < lstSpatial.size(); o_it++)
        {
            ISpatial* spatial = lstSpatial[o_it];
            if (!spatial) continue;

            CSpatial_SyncedData* renderable = (CSpatial_SyncedData*)spatial->dcast_SpatialSyncedData();
            if (!renderable) continue;
            if (!renderable->renderable.visual) continue;

            // HOM visibility test for dynamic objects
            if (HOM && HOM->bEnabled)
            {
                vis_data& v_orig = ((vkRender_Visual*)renderable->renderable.visual)->vis;
                vis_data v_copy = v_orig;
                v_copy.box.xform(renderable->renderable.xform);

                BOOL bVisible = HOM->visible(v_copy);
                v_orig.marker = v_copy.marker;
                v_orig.accept_frame = v_copy.accept_frame;
                v_orig.hom_frame = v_copy.hom_frame;
                v_orig.hom_tested = v_copy.hom_tested;

                if (!bVisible) continue;
            }

            // Add to render queue
            r_dsgraph_insert_dynamic(
                (vkRender_Visual*)renderable->renderable.visual,
                renderable->spatial.sphere.P
            );
        }
    }
    */

    // ========================================================================
    // Lights visibility with HOM culling
    // ========================================================================
    if (g_SpatialSpace && pLastSector)
    {
        // Get lights from spatial database
        lstSpatial.clear();
        g_SpatialSpace->q_frustum(lstSpatial, ISpatial_DB::O_ORDERED, STYPE_LIGHTSOURCE, ViewBase);

        for (u32 o_it = 0; o_it < lstSpatial.size(); o_it++)
        {
            ISpatial* spatial = lstSpatial[o_it];
            if (!spatial) continue;

            light* L = (light*)spatial->dcast_Light();
            if (!L) continue;
            if (!L->flags.bActive) continue;

            // HOM visibility test for lights
            if (HOM && HOM->bEnabled)
            {
                float lod = L->get_LOD();
                if (lod > EPS_L)
                {
                    vis_data& vis = L->get_homdata();
                    if (HOM->visible(vis))
                        Lights.add_light(L);
                }
            }
            else
            {
                Lights.add_light(L);
            }
        }
    }

    // ========================================================================
    // Statistics (occasional logging)
    // ========================================================================
    static u32 lastLogFrame = 0;
    if (Device.dwFrame - lastLogFrame > 300)
    {
        Msg("[Vulkan] Calculate: sectors=%u, visuals=%u",
            pLastSector ? vkPortalTraverser.r_sectors.size() : 0,
            lstNormal.size());
        lastLogFrame = Device.dwFrame;
    }
}

// ============================================================================
// add_Static_Simple - Add static visual to render queue with frustum culling
// ============================================================================
// This is Phase 1 implementation - simplified for MVP
// Phase 2+ will add:
//   - SSA culling (r_ssaDISCARD threshold)
//   - HOM occlusion culling
//   - Portal/sector visibility
//   - Pipeline sorting for state change minimization
// ============================================================================
// ============================================================================
// Helper: Calculate SSA (Screen Space Area)
// ============================================================================
ICF float CalcSSA(float& distSQ, Fvector& C, vkRender_Visual* V)
{
    float R = V->vis.sphere.R;
    distSQ = Device.vCameraPosition.distance_to_sqr(C) + EPS;
    return R / distSQ;
}

ICF float CalcSSA(float& distSQ, Fvector& C, float R)
{
    distSQ = Device.vCameraPosition.distance_to_sqr(C) + EPS;
    return R / distSQ;
}

// ============================================================================
// add_Static - Add static visual with full frustum culling
// ============================================================================
void CRender::add_Static(vkRender_Visual* pVisual, u32 planes)
{
    if (!pVisual) return;

    // Skip if already processed this frame
    if (pVisual->vis.marker == marker) return;
    pVisual->vis.marker = marker;

    // ========================================================================
    // SSA (Screen Space Area) Culling - Skip tiny objects
    // ========================================================================
    // SSA = sphere_radius / distance_squared
    // Objects with SSA < r_ssaDISCARD are too small to be visible
    float distSQ;
    float SSA = CalcSSA(distSQ, pVisual->vis.sphere.P, pVisual);
    if (SSA <= r_ssaDISCARD) return;  // Skip tiny objects

    // ========================================================================
    // Frustum culling with plane mask
    // ========================================================================
    EFC_Visible VIS = View->testSphere(pVisual->vis.sphere.P, pVisual->vis.sphere.R, planes);
    if (VIS == fcvNone) return;  // Completely outside frustum

    // ========================================================================
    // HOM visibility test (optional - already done at sector level)
    // ========================================================================
    // Per-object HOM test disabled here to avoid double-testing
    // Sectors are already HOM-culled during portal traversal

    // ========================================================================
    // Handle by visual type
    // ========================================================================
    switch (pVisual->Type)
    {
    case MT_HIERRARHY:
        {
            // Hierarchical visual - recursively process children
            vkFHierrarhyVisual* pV = (vkFHierrarhyVisual*)pVisual;
            for (auto child : pV->children)
            {
                if (!child) continue;

                if (VIS == fcvPartial)
                    // Partially visible - need per-child culling
                    add_Static((vkRender_Visual*)child, planes);
                else
                    // Fully visible - skip culling for children
                    add_leafs_Static((vkRender_Visual*)child);
            }
        }
        break;

    case MT_LOD:
        {
            // LOD visual - add to LOD queue for later rendering
            r_dsgraph_insert_LOD(reinterpret_cast<dxRender_Visual*>(pVisual));
        }
        break;

    default:
        // Regular visual - add to render queue
        r_dsgraph_insert_static(reinterpret_cast<dxRender_Visual*>(pVisual));
        break;
    }
}

// ============================================================================
// add_leafs_Static - Add visual without additional culling
// ============================================================================
// Used for children of fully visible parent nodes
// Note: Still performs SSA culling to skip tiny objects
void CRender::add_leafs_Static(vkRender_Visual* pVisual)
{
    if (!pVisual) return;

    // Skip if already processed
    if (pVisual->vis.marker == marker) return;
    pVisual->vis.marker = marker;

    // ========================================================================
    // SSA Culling - Still needed even for "fully visible" children
    // ========================================================================
    // Parent might be visible, but child could still be too small to render
    float distSQ;
    float SSA = CalcSSA(distSQ, pVisual->vis.sphere.P, pVisual);
    if (SSA <= r_ssaDISCARD) return;  // Skip tiny objects

    switch (pVisual->Type)
    {
    case MT_HIERRARHY:
        {
            // Recursively add all children
            vkFHierrarhyVisual* pV = (vkFHierrarhyVisual*)pVisual;
            for (auto child : pV->children)
            {
                if (child)
                    add_leafs_Static((vkRender_Visual*)child);
            }
        }
        break;

    case MT_LOD:
        r_dsgraph_insert_LOD(reinterpret_cast<dxRender_Visual*>(pVisual));
        break;

    default:
        r_dsgraph_insert_static(reinterpret_cast<dxRender_Visual*>(pVisual));
        break;
    }
}

void CRender::add_Static_Simple(vkRender_Visual* pVisual)
{
    if (!pVisual) return;

    // Skip if already processed this frame (marker check)
    if (pVisual->vis.marker == marker) return;
    pVisual->vis.marker = marker;

    // ========================================================================
    // Frustum culling - skip objects outside camera view
    // ========================================================================
    if (View && !View->testSphere_dirty(pVisual->vis.sphere.P, pVisual->vis.sphere.R))
        return;

    // ========================================================================
    // Handle hierarchical visuals (recursively process children)
    // ========================================================================
    if (pVisual->Type == MT_HIERRARHY)
    {
        vkFHierrarhyVisual* pH = static_cast<vkFHierrarhyVisual*>(pVisual);
        for (auto child : pH->children)
        {
            if (child)
                add_Static_Simple(static_cast<vkRender_Visual*>(child));
        }
        return;  // Hierarchy node itself has no geometry
    }

    // ========================================================================
    // Calculate Screen-Space Area (SSA) for LOD and culling
    // ========================================================================
    // SSA = sphere_radius / distance_squared
    // Larger SSA = closer/bigger objects = higher priority
    float distSQ = Device.vCameraPosition.distance_to_sqr(pVisual->vis.sphere.P);
    if (distSQ < EPS) distSQ = EPS;  // Avoid division by zero
    float SSA = pVisual->vis.sphere.R / distSQ;

    // ========================================================================
    // SSA Culling - Skip tiny objects
    // ========================================================================
    if (SSA <= r_ssaDISCARD) return;  // Skip objects too small to be visible

    // ========================================================================
    // Add to render queue
    // ========================================================================
    R_dsgraph::_NormalItem item;
    item.ssa = SSA;
    item.pVisual = reinterpret_cast<dxRender_Visual*>(pVisual);  // Type alias for Vulkan
    lstNormal.push_back(item);
}

void CRender::Render()
{
    VkDiagFrame("[RENDER] Render() enter");

    // Skip rendering if no command buffer is active (swapchain not ready, window minimized, etc.)
    if (RCache.GetCommandBuffer() == VK_NULL_HANDLE) {
        VkDiagFrame("[RENDER] no cmd buffer, return");
        return;
    }

    // ========================================================================
    // Check if main menu needs PP-UI rendering (same as DX R4 render_menu)
    // When the main menu is active with post-process, CMainMenu::OnRender()
    // expects the renderer to call OnRenderPPUI_main/PP. Without this,
    // CMainMenu skips DoRenderDialogs() → 0 UI draw calls.
    // ========================================================================
    bool _menu_pp = g_pGamePersistent ? g_pGamePersistent->OnRenderPPUI_query() : false;
    if (_menu_pp)
    {
        VkDiagFrame("[RENDER] render_menu (PP-UI)");

        // Render main UI elements to swapchain
        g_pGamePersistent->OnRenderPPUI_main();

        // TODO Phase 2.20.2: Render PP-UI (magnifier) to rt_Distortion
        // For now, just call OnRenderPPUI_PP() which renders to swapchain
        // This means magnifier won't have distortion effect yet, but will display
        g_pGamePersistent->OnRenderPPUI_PP();

        return;
    }

    // Begin frame
    VkDiagFrame("[RENDER] OnFrameBegin");
    RCache.OnFrameBegin();

    // ========================================================================
    // Set camera matrices from Device (engine integration)
    // ========================================================================
    // Current frame matrices
    RCache.set_xform_view(Device.mView);
    RCache.set_xform_project(Device.mProject);

    // Previous frame matrices (for motion blur, TAA, etc.)
    RCache.set_xform_view_prev(Device.mView_prev);
    RCache.set_xform_project_prev(Device.mProject_prev);

    // World matrix starts as identity (will be set per-object)
    Fmatrix identity;
    identity.identity();
    RCache.set_xform_world(identity);

    // Update hemi/sun values from engine
    // TODO: Get actual values from lights DB
    RCache.hemi.set_material(o_hemi, o_sun, 0.0f, 0.0f);

    // ========================================================================
    // Render scene - Deferred Shading Pipeline
    // ========================================================================

    // ========================================================================
    // PASS 1: Shadow Map Pass (if level is loaded)
    // ========================================================================
    VkDiagFrame("[RENDER] PASS 1: shadows");
    if (b_loaded && !Visuals.empty()) {
        // Render cascade shadow maps for directional light (sun)
        render_sun_cascades();

        // Render shadow cubemaps for point lights (Phase 2.16.2)
        for (u32 i = 0; i < Lights.package.v_point.size(); i++) {
            light* L = Lights.package.v_point[i];
            if (L && L->flags.bActive && L->flags.bShadow) {
                RTarget->phase_smap_point(L);
            }
        }
    }

    // ========================================================================
    // PASS 2: G-Buffer Pass (geometry to MRT)
    // ========================================================================
    // Phase 2.21: Render opaque geometry to G-Buffer (position, normal, albedo, material)
    //
    // This fills the G-Buffer render targets for deferred shading:
    //   - rt_Position: Eye-space positions
    //   - rt_Normal: Eye-space normals
    //   - rt_Color: Albedo (diffuse color)
    //   - rt_Material: PBR properties (metallic/roughness/SSS/AO)
    //   - Depth buffer: For occlusion and forward pass
    //
    VkDiagFrame("[RENDER] PASS 2: gbuffer");
    if (b_loaded && RTarget) {
        RTarget->phase_gbuffer();
    }

    // TODO Phase 2.22+: Advanced features
    //   - Portal traversal: vkPortalTraverser.traverse(...)
    //   - Depth pre-pass (optional optimization)
    //   - HOM occlusion culling
    //
    // For testing: Keep test render frame for now (can be removed later)
    // TestRenderFrame();

    // ========================================================================
    // PASS 3: Lighting Pass (deferred lighting)
    // ========================================================================
    VkDiagFrame("[RENDER] PASS 3: lighting");
    if (b_loaded && RTarget) {
        // Clear accumulator
        RTarget->phase_accumulator();

        // Render directional light with cascade shadows (all 3 cascades)
        RTarget->accum_direct_cascades(SE_SUN_NEAR);
        RTarget->accum_direct_cascades(SE_SUN_MIDDLE);
        RTarget->accum_direct_cascades(SE_SUN_FAR);

        // Point lights (Phase 2.16.5)
        for (u32 i = 0; i < Lights.package.v_point.size(); i++) {
            light* L = Lights.package.v_point[i];
            if (L && L->flags.bActive) {
                RTarget->accum_point(L);
            }
        }

        // Spot lights (Phase 2.17.5)
        for (u32 i = 0; i < Lights.package.v_spot.size(); i++) {
            light* L = Lights.package.v_spot[i];
            if (L && L->flags.bActive) {
                // First render shadow map (if light casts shadows)
                if (L->flags.bShadow) {
                    RTarget->phase_smap_spot(L);
                }
                // Then accumulate lighting
                RTarget->accum_spot(L);
            }
        }
    }

    // ========================================================================
    // PASS 4: Distortion Pass (PP-UI elements like magnifier)
    // ========================================================================
    // Phase 2.20.1: Render distortion elements to rt_Distortion
    // This must happen BEFORE combine so the combine shader can sample it
    VkDiagFrame("[RENDER] PASS 4: distortion");
    if (RTarget) {
        RTarget->phase_distortion();
    }

    // ========================================================================
    // PASS 4.5: Water SSR + Final Water Rendering
    // ========================================================================
    VkDiagFrame("[RENDER] PASS 4.5: water");
    if (b_loaded && RTarget && o.ssfx_water && mapWater.size()) {
        RTarget->phase_water_ssr();
        RTarget->phase_water_blur();
        RTarget->phase_water_waves();
        RTarget->phase_water();
    }

    // ========================================================================
    // PASS 5: Combine Pass (accumulator → swapchain)
    // ========================================================================
    // Phase 2.18: Combine accumulated lighting with albedo and output to swapchain
    // Also applies distortion from rt_Distortion (magnifier glass effect)
    VkDiagFrame("[RENDER] PASS 5: combine");
    if (RTarget) {
        RTarget->phase_combine();
        VkDiagFrame("[RENDER] PASS 5: combine done");
    } else {
        // Fallback: no RTarget — clear swapchain to a solid color so it's not garbage
        VkCommandBuffer cmd = RCache.GetCommandBuffer();
        if (cmd != VK_NULL_HANDLE && Swapchain.m_Swapchain != VK_NULL_HANDLE) {
            VkImage swapImg = Swapchain.GetCurrentImage();
            if (swapImg != VK_NULL_HANDLE) {
                // Transition swapchain: UNDEFINED → TRANSFER_DST
                VkImageMemoryBarrier barrier = {};
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.srcAccessMask = 0;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = swapImg;
                barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                barrier.subresourceRange.baseMipLevel = 0;
                barrier.subresourceRange.levelCount = 1;
                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount = 1;

                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &barrier);

                // Clear to dark blue
                VkClearColorValue clearColor = {{0.0f, 0.05f, 0.1f, 1.0f}};
                VkImageSubresourceRange clearRange = {};
                clearRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                clearRange.baseMipLevel = 0;
                clearRange.levelCount = 1;
                clearRange.baseArrayLayer = 0;
                clearRange.layerCount = 1;
                vkCmdClearColorImage(cmd, swapImg,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &clearRange);

                // Transition swapchain: TRANSFER_DST → PRESENT_SRC
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = 0;
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &barrier);
            }
        }
    }

    // ========================================================================
    // PASS 6: Forward Pass (transparent objects, particles, etc.)
    // ========================================================================
    // Phase 2.19: Forward rendering для transparent objects
    VkDiagFrame("[RENDER] PASS 6: forward");
    if (RTarget) {
        RTarget->phase_forward();
    }

    // Render sorted (transparent) geometry back-to-front
    r_dsgraph_render_sorted();

    // TODO: Portal fade rendering: vkPortalTraverser.fade_render()

    // ========================================================================
    // PASS 6.5: Wallmarks (blood, bullet holes, decals)
    // ========================================================================
    VkDiagFrame("[RENDER] PASS 6.5: wallmarks");
    if (Wallmarks) {
        Wallmarks->Render();
    }

    // ========================================================================
    // PASS 7: HUD 3D Rendering (weapons, hands, HUD particles)
    // ========================================================================
    VkDiagFrame("[RENDER] PASS 7: HUD 3D");
    r_dsgraph_render_hud(false);

    // ========================================================================
    // PASS 7.5: HUD UI Overlay (active item UI, camera-attached UI)
    // ========================================================================
    // Phase 2.28: Render HUD UI overlays with proper projection switching
    VkDiagFrame("[RENDER] PASS 7.5: HUD UI");
    // TODO: Implement HUD UI rendering
    // Temporarily disabled due to undefined CHUDManager type
    /*
    extern CHUDManager* g_hud;
    if (g_hud)
    {
        // Active item UI (weapon sights, scopes, etc.)
        if (g_hud->RenderActiveItemUIQuery())
            r_dsgraph_render_hud_ui();

        // Camera-attached UI (special effects with custom FOV)
        if (g_hud->RenderCamAttachedUIQuery())
            r_dsgraph_render_cam_ui();
    }
    */

    // ========================================================================
    // PASS 8: Post-Process Pass
    // ========================================================================
    // TODO: Bloom, color grading, etc.

    // ========================================================================
    // PASS 9: UI Pass (in-game UI when level is loaded)
    // ========================================================================
    // In-game UI (HUD, inventory, etc.) is rendered via seqRender callbacks
    // from CMainMenu::OnRender() / IGame_Level::OnRender() after Render() returns.
    // Main menu PP-UI is handled by the render_menu check at the top of Render().

    VkDiagFrame("[RENDER] OnFrameEnd");
    // End frame - flush statistics
    RCache.OnFrameEnd();

    // Update stats
    stats.l_total = 0;
    stats.l_visible = 0;
}

void CRender::OnFrame()
{
    // Per-frame update - called before Render()

    // Save previous frame matrices for motion vectors
    RCache.xforms.set_W_prev(RCache.xforms.get_W());
    RCache.xforms.set_V_prev(RCache.xforms.get_V());
    RCache.xforms.set_P_prev(RCache.xforms.get_P());

    // Reset markers for new frame
    marker++;

    // TODO: Update animations, particles, weather effects, etc.
}

// ============================================================================
// Object management
// ============================================================================
void CRender::set_Object(IRenderable* O)
{
    val_pObject = O;

    if (O)
    {
        // TODO: Update object-specific rendering state
        // Similar to apply_object in R4
    }
}

void CRender::add_Visual(IRenderVisual* V)
{
    if (!V) return;

    vkRender_Visual* pVisual = static_cast<vkRender_Visual*>(V);

    // Route HUD visuals to mapHUD (rendered by r_dsgraph_render_hud)
    if (val_bHUD)
    {
        R_dsgraph::_MatrixItemS item;
        item.ssa = 1.0f;
        item.pObject = val_pObject;
        item.pVisual = reinterpret_cast<dxRender_Visual*>(pVisual);
        item.Matrix = (val_pTransform) ? *val_pTransform : Fidentity;
        item.PrevMatrix = item.Matrix;
        item.se = nullptr;

        mapHUD.insertInAnyWay(0.f, item);
        return;
    }

    // Expand composite visuals into leaf visuals
    add_leafs_Dynamic_VK(pVisual);
}

// ============================================================================
// add_leafs_Dynamic_VK - Recursively expand dynamic visuals into render queue
// ============================================================================
void CRender::add_leafs_Dynamic_VK(vkRender_Visual* pVisual)
{
    if (!pVisual) return;

    switch (pVisual->Type)
    {
    case MT_PARTICLE_GROUP:
    {
        // Expand particle group: iterate all child items
        vkCParticleGroup* pG = static_cast<vkCParticleGroup*>(pVisual);
        for (auto& item : pG->items)
        {
            if (item.pVisual)
                add_leafs_Dynamic_VK(static_cast<vkRender_Visual*>(item.pVisual));
        }
        return;
    }

    case MT_HIERRARHY:
    {
        // Expand hierarchy via IRenderVisual::get_children()
        xr_vector<IRenderVisual*>* children = pVisual->get_children();
        if (children) {
            for (auto child : *children) {
                if (child)
                    add_leafs_Dynamic_VK(static_cast<vkRender_Visual*>(child));
            }
        }
        return;
    }

    case MT_SKELETON_ANIM:
    case MT_SKELETON_RIGID:
    {
        // Skeleton: calculate bones, then expand children
        IKinematics* pK = pVisual->dcast_PKinematics();
        if (pK) {
            pK->CalculateBones(TRUE);
        }

        // Expand skeleton children via IRenderVisual interface
        xr_vector<IRenderVisual*>* children = pVisual->get_children();
        if (children) {
            for (auto child : *children) {
                if (child)
                    add_leafs_Dynamic_VK(static_cast<vkRender_Visual*>(child));
            }
        }
        return;
    }

    default:
    {
        // Leaf visual (geometry, particle effect, etc.) - add to render queue
        R_dsgraph::_NormalItem item;
        item.ssa = 1.0f;  // Dynamic objects: max priority
        item.pVisual = reinterpret_cast<dxRender_Visual*>(pVisual);
        lstNormal.push_back(item);
        return;
    }
    }
}

void CRender::add_Geometry(IRenderVisual* V)
{
    // Same as add_Visual for Vulkan renderer
    add_Visual(V);
}

void CRender::add_Occluder(Fbox2& bb_screenspace)
{
    // TODO: Implement HOM integration
}

void CRender::flush()
{
    // TODO: Flush queued render objects
}

// ============================================================================
// Model management
// ============================================================================
IRenderVisual* CRender::model_Create(LPCSTR name, IReader* data)
{
    Msg("[Vulkan] model_Create ENTER: '%s'", name ? name : "NULL");

    if (!Models) {
        Msg("[Vulkan] model_Create: Models is NULL!");
        return nullptr;
    }

    Msg("[Vulkan] model_Create: calling Models->Create...");
    IRenderVisual* result = Models->Create(name, data, true);
    Msg("[Vulkan] model_Create: Models->Create returned %p", result);

    // Diagnostic for skeleton models
    if (result)
    {
        Msg("[Vulkan] model_Create: calling dcast methods...");
        IKinematics* K = result->dcast_PKinematics();
        IKinematicsAnimated* KA = result->dcast_PKinematicsAnimated();
        Msg("[Vulkan] model_Create('%s'): result=%p, dcast_PKinematics=%p, dcast_PKinematicsAnimated=%p",
            name, result, K, KA);
    }
    else
    {
        Msg("[Vulkan] model_Create('%s'): FAILED (nullptr)", name);
    }

    return result;
}

IRenderVisual* CRender::model_CreateChild(LPCSTR name, IReader* data)
{
    if (!Models) return nullptr;
    return Models->CreateChild(name, data);
}

IRenderVisual* CRender::model_CreateParticles(LPCSTR name)
{
    if (!Models) {
        Msg("![Vulkan] ModelPool not initialized");
        return nullptr;
    }

    // Try to find particle effect definition
    PS::CPEDef* pe_def = PSLibrary.FindPED(name);
    if (pe_def) {
        return Models->CreatePE(pe_def);
    }

    // Try to find particle group definition
    PS::CPGDef* pg_def = PSLibrary.FindPGD(name);
    if (pg_def) {
        return Models->CreatePG(pg_def);
    }

    Msg("![Vulkan] Particle not found: %s", name);
    return nullptr;
}

IRenderVisual* CRender::model_Duplicate(IRenderVisual* V)
{
    if (!Models || !V) return nullptr;
    vkRender_Visual* vkV = static_cast<vkRender_Visual*>(V);
    return Models->Instance_Duplicate(vkV);
}

void CRender::model_Delete(IRenderVisual*& V, BOOL bDiscard)
{
    if (!Models || !V) return;
    vkRender_Visual* vkV = static_cast<vkRender_Visual*>(V);
    Models->Delete(vkV, bDiscard);
    V = nullptr;
}

void CRender::model_Logging(BOOL bEnable)
{
    if (Models)
        Models->Logging(bEnable);
}

void CRender::models_Prefetch()
{
    if (Models)
        Models->Prefetch();
}

void CRender::models_PrefetchOne(LPCSTR name, bool assert_on_fail)
{
    if (Models)
        Models->Prefetch_One(name, assert_on_fail);
}

void CRender::models_Clear(BOOL b_complete)
{
    if (Models)
        Models->ClearPool(b_complete);
}

bool CRender::models_Exists(LPCSTR name)
{
    if (!Models) return false;
    return Models->Exists(name);
}

// ============================================================================
// Light management
// ============================================================================
IRender_Light* CRender::light_create()
{
    return Lights.Create();
}

// ============================================================================
// Glow - minimal implementation (stub rendering, proper interface)
// ============================================================================
class CGlow : public IRender_Glow
{
public:
    bool     bActive;
    Fvector  position;
    Fvector  direction;
    float    radius;
    Fcolor   color;

    CGlow() : bActive(false), radius(0.5f)
    {
        position.set(0, 0, 0);
        direction.set(0, 0, 1);
        color.set(1, 1, 1, 1);
    }

    virtual void set_active(bool b) override    { bActive = b; }
    virtual bool get_active() override          { return bActive; }
    virtual void set_position(const Fvector& P) override { position.set(P); }
    virtual void set_direction(const Fvector& D) override { direction.set(D); }
    virtual void set_radius(float R) override   { radius = R; }
    virtual void set_texture(LPCSTR name) override { /* Vulkan: glow textures not yet implemented */ }
    virtual void set_color(const Fcolor& C) override { color.set(C); }
    virtual void set_color(float r, float g, float b) override { color.set(r, g, b, 1); }
};

IRender_Glow* CRender::glow_create()
{
    return xr_new<CGlow>();
}

// ============================================================================
// Wallmarks
// ============================================================================

// Helper: ref_shader overload with random rotation
void CRender::add_StaticWallmark(ref_shader& S, const Fvector& P, float s, CDB::TRI* T, Fvector* V, float ttl, bool ignore_opt, bool random_rotation)
{
    add_StaticWallmark(S, P, s, T, V, ttl, ignore_opt, random_rotation ? ::Random.randF(-20.f, 20.f) : 0.f);
}

// Helper: ref_shader overload with explicit rotation
void CRender::add_StaticWallmark(ref_shader& S, const Fvector& P, float s, CDB::TRI* T, Fvector* V, float ttl, bool ignore_opt, float rotation)
{
    if (T->suppress_wm) return;
    VERIFY2(_valid(P) && _valid(s) && T && V && (s > EPS_L), "Invalid static wallmark params");
    if (Wallmarks)
        Wallmarks->AddStaticWallmark(T, V, P, S, s, ttl, ignore_opt, rotation);
}

// IWallMarkArray overload with random rotation (called by game code)
void CRender::add_StaticWallmark(IWallMarkArray* pArray, const Fvector& P, float s, CDB::TRI* T, Fvector* V, float ttl, bool ignore_opt, bool random_rotation)
{
    add_StaticWallmark(pArray, P, s, T, V, ttl, ignore_opt, random_rotation ? ::Random.randF(-20.f, 20.f) : 0.f);
}

// IWallMarkArray overload with explicit rotation (called by game code)
void CRender::add_StaticWallmark(IWallMarkArray* pArray, const Fvector& P, float s, CDB::TRI* T, Fvector* V, float ttl, bool ignore_opt, float rotation)
{
    dxWallMarkArray* pWMA = (dxWallMarkArray*)pArray;
    ref_shader* pShader = pWMA->dxGenerateWallmark();
    if (pShader) add_StaticWallmark(*pShader, P, s, T, V, ttl, ignore_opt, rotation);
}

// wm_shader overload (used by older/UI code paths)
void CRender::add_StaticWallmark(const wm_shader& S, const Fvector& P, float s, CDB::TRI* T, Fvector* V)
{
    dxUIShader* pShader = (dxUIShader*)&*S;
    add_StaticWallmark(pShader->hShader, P, s, T, V, 0.0f, false, true);
}

void CRender::clear_static_wallmarks()
{
    if (Wallmarks)
        Wallmarks->clear();
}

// IKinematics + IWallMarkArray overload (called by game code for blood on animated models)
void CRender::add_SkeletonWallmark(const Fmatrix* xf, IKinematics* obj, IWallMarkArray* pArray, const Fvector& start,
                                   const Fvector& dir, float size, float ttl, bool ignore_opt)
{
    dxWallMarkArray* pWMA = (dxWallMarkArray*)pArray;
    ref_shader* pShader = pWMA->dxGenerateWallmark();
    if (pShader) add_SkeletonWallmark(xf, (CKinematics*)obj, *pShader, start, dir, size, ttl, ignore_opt);
}

// Include CSkeletonWallmark for intrusive_ptr wrapper
// Prevent FBasicVisual.h from being re-included (same pattern as wrapper files)
#define FBasicVisualH
#define dxRender_Visual vkRender_Visual
#include "../xrRender/SkeletonCustom.h"
#undef dxRender_Visual
#undef FBasicVisualH

void CRender::add_SkeletonWallmark_impl(const CSkeletonWallmark* wm)
{
    // Not used directly - see intrusive_ptr overload below
}

// intrusive_ptr overload (called by CKinematics when wallmark is ready)
void CRender::add_SkeletonWallmark(intrusive_ptr<CSkeletonWallmark> wm)
{
    if (Wallmarks)
        Wallmarks->AddSkeletonWallmark(wm);
}

// CKinematics + ref_shader overload (direct skeleton wallmark creation)
void CRender::add_SkeletonWallmark(const Fmatrix* xf, CKinematics* obj, ref_shader& sh, const Fvector& start,
                                   const Fvector& dir, float size, float ttl, bool ignore_opt)
{
    if (Wallmarks)
        Wallmarks->AddSkeletonWallmark(xf, obj, sh, start, dir, size, ttl, ignore_opt);
}

// ============================================================================
// ROS (Render Object Specific) - Stub for Vulkan
// ============================================================================
// Minimal implementation returning safe dummy luminosity values.
// Full light tracking (CROS_impl) not yet ported to Vulkan.
// ============================================================================
class vkROS : public IRender_ObjectSpecific
{
    u32   m_mode;
    float m_hemi_cube[6];
public:
    vkROS() : m_mode(TRACE_ALL)
    {
        for (int i = 0; i < 6; i++)
            m_hemi_cube[i] = 0.5f;   // neutral hemisphere
    }
    virtual void   force_mode(u32 mode)            { m_mode = mode; }
    virtual float  get_luminocity()                 { return 0.5f; }
    virtual float  get_luminocity_hemi()            { return 0.5f; }
    virtual float* get_luminocity_hemi_cube()       { return m_hemi_cube; }
    virtual ~vkROS() {}
};

IRender_ObjectSpecific* CRender::ros_create(IRenderable* parent)
{
    return xr_new<vkROS>();
}

void CRender::ros_destroy(IRender_ObjectSpecific*& ROS)
{
    xr_delete(ROS);
    ROS = nullptr;
}

// ============================================================================
// Sector/Portal (stubs for now)
// ============================================================================
IRender_Sector* CRender::getSector(int id)
{
    if (id >= 0 && id < (int)Sectors.size())
        return Sectors[id];
    return nullptr;
}

IRenderVisual* CRender::getVisual(int id)
{
    if (id >= 0 && id < (int)Visuals.size())
        return Visuals[id];
    return nullptr;
}

IRender_Sector* CRender::detectSector(const Fvector& P)
{
    // Simplified sector detection for Vulkan MVP.
    // Full implementation would ray-cast against sector geometry.
    // For now, return first available sector so portal traversal
    // and spatial system have a valid sector to work with.
    if (!Sectors.empty())
        return Sectors[0];
    return nullptr;
}

IRender_Target* CRender::getTarget()
{
    return RTarget;
}

// ============================================================================
// Occlusion (stubs for now)
// ============================================================================
BOOL CRender::occ_visible(vis_data& V)
{
    // TODO: Implement occlusion query
    return TRUE;
}

BOOL CRender::occ_visible(Fbox& B)
{
    // TODO: Implement
    return TRUE;
}

BOOL CRender::occ_visible(sPoly& P)
{
    // TODO: Implement
    return TRUE;
}

// ============================================================================
// Screenshots (stubs for now)
// ============================================================================
void CRender::Screenshot(ScreenshotMode mode, LPCSTR name)
{
    // TODO: Implement Vulkan screenshot
    Msg("[Vulkan] Screenshot requested: %s", name ? name : "unnamed");
}

void CRender::Screenshot(ScreenshotMode mode, CMemoryWriter& memory_writer)
{
    // TODO: Implement
}

void CRender::ScreenshotAsyncBegin()
{
    // TODO: Implement async screenshot
}

void CRender::ScreenshotAsyncEnd(CMemoryWriter& memory_writer)
{
    // TODO: Implement
}

// ============================================================================
// Render mode
// ============================================================================
void CRender::rmNear()
{
    // Set viewport depth range for HUD weapon rendering (front of depth buffer)
    IRender_Target* T = getTarget();
    if (!T) return;
    RCache.m_Viewport.x = 0;
    RCache.m_Viewport.y = 0;
    RCache.m_Viewport.width  = (float)T->get_width();
    RCache.m_Viewport.height = (float)T->get_height();
    RCache.m_Viewport.minDepth = 0.f;
    RCache.m_Viewport.maxDepth = 0.02f;
    RCache.ApplyViewportScissor();
}

void CRender::rmFar()
{
    // Set viewport depth range for sky rendering (back of depth buffer)
    IRender_Target* T = getTarget();
    if (!T) return;
    RCache.m_Viewport.x = 0;
    RCache.m_Viewport.y = 0;
    RCache.m_Viewport.width  = (float)T->get_width();
    RCache.m_Viewport.height = (float)T->get_height();
    RCache.m_Viewport.minDepth = 0.99999f;
    RCache.m_Viewport.maxDepth = 1.f;
    RCache.ApplyViewportScissor();
}

void CRender::rmNormal()
{
    // Reset viewport depth range to full (normal rendering)
    IRender_Target* T = getTarget();
    if (!T) return;
    RCache.m_Viewport.x = 0;
    RCache.m_Viewport.y = 0;
    RCache.m_Viewport.width  = (float)T->get_width();
    RCache.m_Viewport.height = (float)T->get_height();
    RCache.m_Viewport.minDepth = 0.f;
    RCache.m_Viewport.maxDepth = 1.f;
    RCache.ApplyViewportScissor();
}

// ============================================================================
// Shader compilation
// ============================================================================
HRESULT CRender::shader_compile(
    LPCSTR name,
    DWORD const* pSrcData,
    UINT SrcDataLen,
    LPCSTR pFunctionName,
    LPCSTR pTarget,
    DWORD Flags,
    void*& result)
{
    // TODO: Implement Vulkan shader compilation
    // This will need to use glslang or similar to compile HLSL/GLSL to SPIR-V
    result = nullptr;
    return E_NOTIMPL;
}

// ============================================================================
// Particles
// ============================================================================
void CRender::ExportParticles()
{
    // TODO: Implement particle export
}

void CRender::ImportParticles()
{
    // TODO: Implement particle import
}

// ============================================================================
// RenderToTarget
// ============================================================================
void CRender::RenderToTarget(RRT target)
{
    // TODO: Implement render to target
}

// ============================================================================
// Sun values (anglobes)
// ============================================================================
Fvector CRender::GetSunPosition()
{
    static Fvector default_pos = {0, 0, 0};
    // TODO: Return actual sun position from lights DB
    return default_pos;
}

Fcolor CRender::GetSunColor()
{
    static Fcolor default_color = {0.0f, 0.0f, 0.0f, 0.0f};
    // TODO: Return actual sun color from lights DB
    return default_color;
}

float CRender::GetSunIntensity()
{
    // TODO: Return actual sun intensity
    return 0.0f;
}

bool CRender::IsSun()
{
    // TODO: Check if sun light is active
    return false;
}

// ============================================================================
// Selective Screenshot (antglobes)
// ============================================================================
void CRender::TakeScreenshot(LPCSTR path, Fvector2 dimensions, DxEncoding encoding)
{
    // TODO: Implement selective screenshot
    Msg("[Vulkan] TakeScreenshot: %s (%.0fx%.0f)", path, dimensions.x, dimensions.y);
}

// ============================================================================
// Screenshot implementation
// ============================================================================
void CRender::ScreenshotImpl(ScreenshotMode mode, LPCSTR name, CMemoryWriter* memory_writer)
{
    // TODO: Implement actual Vulkan screenshot capture
    Msg("[Vulkan] ScreenshotImpl mode:%d name:%s", mode, name ? name : "null");
}

// ============================================================================
// HUD Particle Rendering
// ============================================================================
void CRender::RenderHUDParticles()
{
    // TODO: Render particles that have flRT_HUDmode flag set
    // These are typically muzzle flashes, impact effects, etc.
    // that should render in screen space on top of HUD objects
}
