// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#include "stdafx.h"
#include "rvk.h"

// Definition of g_bDeviceLost (declared extern in vk_core.h)
bool g_bDeviceLost = false;

// Reset UI state on crash (defined in xrRender_Vulkan.cpp)
extern "C" void VulkanUI_ResetState();
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
#include "vk_DetailManager.h"
#include "../xrRender/dxWallMarkArray.h"
#include "../xrRender/dxUIShader.h"
#include "3DFluid/vk3DFluidManager.h"  // Phase 0: 3D Fluid system
#include "../xrRender/FBasicVisual.h"  // dxRender_Visual full definition
#include "../xrRender/PSLibrary.h"
#include "../../Include/xrRender/Kinematics.h"
#include "../../xrCDB/ISpatial.h"
#include "../../xrCDB/xrXRC.h"  // CDB::Collider for detectSector
#include "../../xrEngine/IGame_Level.h"  // g_pGameLevel for detectSector geometry query
#include "../../xrEngine/customhud.h"   // g_hud for HUD rendering (Render_Last)

// Direct diagnostic write for crash debugging (Win32 file I/O, bypasses Msg buffer)
// After g_bDeviceLost, stops overwriting so the last phase before crash is preserved.
static void VkDiagFrame(const char* msg) {
	static HANDLE hFile = INVALID_HANDLE_VALUE;
	static bool s_frozen = false;  // Stop writing after device lost

	if (s_frozen) return;
	if (g_bDeviceLost) { s_frozen = true; return; }

	if (hFile == INVALID_HANDLE_VALUE) {
		const char* paths[] = {
			"vk_lastframe.txt",
			"D:\\anomaly\\vk_lastframe.txt",
			"D:\\anomaly\\bin\\vk_lastframe.txt"
		};
		for (int i = 0; i < 3 && hFile == INVALID_HANDLE_VALUE; i++)
			hFile = CreateFileA(paths[i], GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	}
	if (hFile != INVALID_HANDLE_VALUE) {
		SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
		SetEndOfFile(hFile);
		DWORD written;
		char buf[256];
		int len = wsprintfA(buf, "frame=%u %s\r\n", Device.dwFrame, msg);
		WriteFile(hFile, buf, len, &written, NULL);
		FlushFileBuffers(hFile);
	}
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
// Recomputed each frame in Calculate() from screen resolution (matching DX11)
float r_ssaDISCARD       = 4.f;    // Will be overwritten in Calculate()
float r_ssaDONTSORT      = 32.f;
float r_ssaLOD_A         = 64.f;
float r_ssaLOD_B         = 48.f;
extern float r_dtex_range;         // defined in vk_console.cpp

// Console variables (defined in vk_console.cpp)
extern float ps_r__LOD;
extern float ps_r__ssaDISCARD;
extern float ps_r__ssaDONTSORT;
extern float ps_r2_ssaLOD_A;
extern float ps_r2_ssaLOD_B;
extern float ps_r2_df_parallax_range;

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
    Details = nullptr;
    Wallmarks = nullptr;

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

    // Create detail manager (grass/debris)
    if (!Details) {
        Details = xr_new<VK::CDetailManager>();
        Msg("[Vulkan] DetailManager created");
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
        if (g_VulkanShaderManager) {
            g_VulkanShaderManager->Create();
            Msg("[Vulkan] VulkanShaderManager created");
        } else {
            Msg("![Vulkan] CRITICAL: Failed to allocate VulkanShaderManager!");
            // This is a fatal error - without shader manager, rendering is impossible
            // But we continue to avoid crashing during initialization
        }
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

    // Initialize viewport to normal full-screen rendering
    rmNormal();
    Msg("[Vulkan] Viewport initialized (rmNormal)");

    // Initialize portal traverser for fade rendering
    vkPortalTraverser.initialize();
    Msg("[Vulkan] PortalTraverser initialized");

    // Initialize sun cascade shadow maps (Phase 2.15)
    init_sun_cascades();

    // ========================================================================
    // Register frame callback (CRITICAL - without this OnFrame won't work)
    // ========================================================================
    static_cast<IRenderDevice&>(Device).AddSeqFrame(this, false);  // Non-multithreaded
    Msg("[Vulkan] Frame callback registered");

    Msg("[Vulkan] CRender::create() complete");
}

// Forward declaration from vk_RenderFactory.cpp
extern void UITextureCache_DestroyAll();

void CRender::destroy()
{
    Msg("[Vulkan] CRender::destroy()");

    // ========================================================================
    // Unregister frame callback (prevent crashes after destroy)
    // ========================================================================
    static_cast<IRenderDevice&>(Device).RemoveSeqFrame(this);
    Msg("[Vulkan] Frame callback unregistered");

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

    // Destroy detail manager
    if (Details) {
        xr_delete(Details);
        Details = nullptr;
        Msg("[Vulkan] DetailManager destroyed");
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

    // Clear per-frame maps at the start of Calculate() (before new items are added).
    // This prevents mapHUD growing unbounded if Render() early-returns (menu, device lost).
    mapHUD.clear();
    mapCamAttached.clear();
    mapHUDSorted.clear();
    mapCamAttachedSorted.clear();
    mapSorted.clear();

    // ========================================================================
    // Compute SSA thresholds from screen resolution (same as DX11 R4)
    // ========================================================================
    {
        IRender_Target* T = getTarget();
        float fov_factor = _sqr(90.f / Device.fFOV);
        float g_fSCREEN  = float(T->get_width() * T->get_height()) * fov_factor * (EPS_S + ps_r__LOD);
        r_ssaDISCARD     = _sqr(ps_r__ssaDISCARD)      / g_fSCREEN;
        r_ssaDONTSORT    = _sqr(ps_r__ssaDONTSORT / 3)  / g_fSCREEN;
        r_ssaLOD_A       = _sqr(ps_r2_ssaLOD_A   / 3)  / g_fSCREEN;
        r_ssaLOD_B       = _sqr(ps_r2_ssaLOD_B   / 3)  / g_fSCREEN;
        r_dtex_range     = ps_r2_df_parallax_range * g_fSCREEN / (1024.f * 768.f);
    }

    // ========================================================================
    // Phase 2: Full scene graph with Portal Visibility and HOM Occlusion
    // ========================================================================

    // ========================================================================
    // Detect camera sector every frame when camera moves (ported from DX11 R2)
    // ========================================================================
    if (!Sectors.empty())
    {
        if (!vLastCameraPos.similar(Device.vCameraPosition, EPS_S))
        {
            vkCSector* pSector = (vkCSector*)detectSector(Device.vCameraPosition);
            if (pSector && (pSector != pLastSector))
            {
                // Notify game about sector change
                int sectorIdx = -1;
                for (u32 i = 0; i < Sectors.size(); ++i)
                {
                    if (Sectors[i] == pSector) { sectorIdx = (int)i; break; }
                }
                if (sectorIdx >= 0)
                    g_pGamePersistent->OnSectorChanged(sectorIdx);
            }
            if (nullptr == pSector) pSector = pLastSector;
            pLastSector = pSector;
            vLastCameraPos.set(Device.vCameraPosition);
        }

        // If still no sector (first frame), detect it
        if (!pLastSector)
        {
            pLastSector = (vkCSector*)detectSector(Device.vCameraPosition);
            if (pLastSector)
                Msg("[Vulkan] Detected initial sector for camera");
            vLastCameraPos.set(Device.vCameraPosition);
        }
    }

    // ========================================================================
    // Check if camera is too near to some portal - force DualRender
    // (ported from DX11 R2 - prevents flickering at portal boundaries)
    // ========================================================================
    if (rmPortals)
    {
        float eps = VIEWPORT_NEAR + EPS_L;
        Fvector box_radius;
        box_radius.set(eps, eps, eps);
        Sectors_xrc.box_options(CDB::OPT_FULL_TEST);
        Sectors_xrc.box_query(rmPortals, Device.vCameraPosition, box_radius);
        for (int K = 0; K < Sectors_xrc.r_count(); K++)
        {
            CDB::TRI* pTri = rmPortals->get_tris() + Sectors_xrc.r_begin()[K].id;
            if (pTri->dummy < Portals.size())
            {
                vkCPortal* pPortal = (vkCPortal*)Portals[pTri->dummy];
                if (pPortal)
                    pPortal->bDualRender = TRUE;
            }
        }
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

            // Diagnostic: log root visual info once
            static u32 diag_frame = 0;
            if (Device.dwFrame - diag_frame > 600)
            {
                diag_frame = Device.dwFrame;
                if (root)
                {
                    Msg("[VK-DIAG] Sector %u: root=%p type=%u frustums=%u",
                        s_it, root, root->Type, sector->r_frustums.size());
                    if (root->Type == MT_HIERRARHY)
                    {
                        vkFHierrarhyVisual* pH = (vkFHierrarhyVisual*)root;
                        Msg("[VK-DIAG]   hierarchy children: %u", pH->children.size());
                    }
                }
                else
                {
                    Msg("[VK-DIAG] Sector %u: root=NULL", s_it);
                }
            }

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
    // Dynamic objects from Spatial Database (ported from R4 render_main)
    // ========================================================================
    if (g_SpatialSpace && pLastSector)
    {
        set_Object(0);

        // Query all renderables in frustum
        lstSpatial.clear();
        g_SpatialSpace->q_frustum(lstSpatial, ISpatial_DB::O_ORDERED, STYPE_RENDERABLE, ViewBase);

        static u32 s_dynLog = 0;
        if (s_dynLog < 5) {
            Msg("[DYN-DIAG] Calculate: lstSpatial(RENDERABLE)=%u, pLastSector=%p, marker=%u",
                lstSpatial.size(), pLastSector, vkPortalTraverser.i_marker);
            s_dynLog++;
        }

        u32 dbg_skipped_sector = 0, dbg_skipped_marker = 0, dbg_skipped_frustum = 0, dbg_rendered = 0;
        for (u32 o_it = 0; o_it < lstSpatial.size(); o_it++)
        {
            ISpatial* spatial = lstSpatial[o_it];
            if (!spatial) continue;

            spatial->spatial_updatesector();
            vkCSector* sector = (vkCSector*)spatial->spatial.sector;
            if (0 == sector) { dbg_skipped_sector++; continue; }

            // Skip objects in sectors not touched by portal traversal
            if (vkPortalTraverser.i_marker != sector->r_marker) { dbg_skipped_marker++; continue; }

            for (u32 v_it = 0; v_it < sector->r_frustums.size(); v_it++)
            {
                CFrustum& view = sector->r_frustums[v_it];
                if (!view.testSphere_dirty(spatial->spatial.sphere.P, spatial->spatial.sphere.R)) { dbg_skipped_frustum++; continue; }

                if (spatial->spatial.type & STYPE_RENDERABLE)
                {
                    IRenderable* renderable = spatial->dcast_Renderable();
                    if (0 == renderable) break;

                    // HOM occlusion test
                    if (renderable->renderable.visual)
                    {
                        vis_data& v_orig = ((vkRender_Visual*)renderable->renderable.visual)->vis;
                        vis_data v_copy = v_orig;
                        v_copy.box.xform(renderable->renderable.xform);
                        BOOL bVisible = (HOM && HOM->bEnabled) ? HOM->visible(v_copy) : TRUE;
                        v_orig.marker = v_copy.marker;
                        v_orig.accept_frame = v_copy.accept_frame;
                        v_orig.hom_frame = v_copy.hom_frame;
                        v_orig.hom_tested = v_copy.hom_tested;
                        if (!bVisible) break;
                    }

                    // Render the object (populates lstMatrix, mapHUD, etc.)
                    set_Object(renderable);
                    renderable->renderable_Render();
                    set_Object(0);
                    dbg_rendered++;
                }
                break; // exit loop on frustums (same as R4)
            }
        }

        if (s_dynLog <= 5) {
            Msg("[DYN-DIAG] Results: rendered=%u skipped_nosector=%u skipped_marker=%u skipped_frustum=%u mapHUD=%u lstMatrix=%u",
                dbg_rendered, dbg_skipped_sector, dbg_skipped_marker, dbg_skipped_frustum, mapHUD.size(), lstMatrix.size());
        }

        // HUD rendering - collect HUD visuals from game
        if (g_pGameLevel && (phase == PHASE_NORMAL))
        {
            extern ENGINE_API CCustomHUD* g_hud;
            u32 hudBefore = mapHUD.size();
            if (g_hud)
                g_hud->Render_Last();
            u32 hudAfter = mapHUD.size();

            static u32 s_hudFrameLog = 0;
            if (hudAfter > 0 || (Device.dwFrame - s_hudFrameLog > 300)) {
                Msg("[DYN-DIAG] Render_Last: g_hud=%p mapHUD before=%u after=%u frame=%u",
                    g_hud, hudBefore, hudAfter, Device.dwFrame);
                s_hudFrameLog = Device.dwFrame;
            }
        }
    }

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
// rimp_select_sh_static - Select shader element for static geometry
// ============================================================================
// Chooses appropriate shader element from visual's shader array based on:
// - Rendering phase (normal, shadow map, etc.)
// - Distance to camera (HQ vs LQ)
// Returns shader element with flags (bDistort, bEmissive, bStrictB2F, etc.)
ShaderElement* CRender::rimp_select_sh_static(dxRender_Visual* pVisual, float cdist_sq)
{
    // Vulkan: visuals use vkRender_Visual (not dxRender_Visual), so they don't
    // have the DX11 ref_shader member. We return a default ShaderElement to let
    // visuals pass through the dsgraph pipeline. Actual Vulkan shader binding
    // happens in vkFVisual::Render() using shader_id -> Shaders[] lookup.
    static ShaderElement* s_default = nullptr;
    if (!s_default) {
        s_default = xr_new<ShaderElement>();
        s_default->flags.iPriority   = 1;
        s_default->flags.bStrictB2F  = 0;
        s_default->flags.bEmissive   = 0;
        s_default->flags.bDistort    = 0;
        s_default->flags.bWmark      = 0;
        s_default->flags.bLandscape  = 0;
    }
    return s_default;
}

// ============================================================================
// rimp_select_sh_dynamic - Select shader element for dynamic geometry
// ============================================================================
// Similar to rimp_select_sh_static but for dynamic objects (characters, items)
ShaderElement* CRender::rimp_select_sh_dynamic(dxRender_Visual* pVisual, float cdist_sq)
{
    // Vulkan: same as rimp_select_sh_static - return default ShaderElement.
    // Actual shader binding happens in vkFVisual::Render() via shader_id.
    return rimp_select_sh_static(pVisual, cdist_sq);
}

// ============================================================================
// add_Static - Add static visual with full frustum culling
// ============================================================================
void CRender::add_Static(vkRender_Visual* pVisual, u32 planes)
{
    if (!pVisual) return;

    // Validate pointer is readable (guard against corrupted child pointers)
    __try {
        volatile u32 test = pVisual->Type;
        (void)test;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Msg("! add_Static: corrupted visual pointer %p at frame %u", pVisual, Device.dwFrame);
        return;
    }

    // Skip if already processed this frame
    if (pVisual->vis.marker == marker) return;
    pVisual->vis.marker = marker;

    // Diagnostic: count visuals processed per frame
    static u32 diag_add_frame2 = 0;
    static u32 diag_total = 0;
    static u32 diag_ssa_culled = 0;
    static u32 diag_frustum_culled = 0;
    static u32 diag_leaf_added = 0;
    static u32 diag_hierarchy = 0;
    if (Device.dwFrame != diag_add_frame2) {
        if (diag_add_frame2 != 0 && (diag_add_frame2 % 300 == 0))
            Msg("[VK-DIAG] Frame %u stats: total=%u ssa_culled=%u frustum_culled=%u hierarchy=%u leaf_added=%u lstNormal=%u",
                diag_add_frame2, diag_total, diag_ssa_culled, diag_frustum_culled, diag_hierarchy, diag_leaf_added, lstNormal.size());
        diag_add_frame2 = Device.dwFrame;
        diag_total = diag_ssa_culled = diag_frustum_culled = diag_leaf_added = diag_hierarchy = 0;
    }
    diag_total++;

    // ========================================================================
    // SSA (Screen Space Area) Culling - Skip tiny objects
    // ========================================================================
    // SSA = sphere_radius / distance_squared
    // Objects with SSA < r_ssaDISCARD are too small to be visible
    float distSQ;
    float SSA = CalcSSA(distSQ, pVisual->vis.sphere.P, pVisual);
    if (SSA <= r_ssaDISCARD) { diag_ssa_culled++; return; }  // Skip tiny objects

    // ========================================================================
    // Frustum culling with plane mask
    // ========================================================================
    EFC_Visible VIS = View->testSphere(pVisual->vis.sphere.P, pVisual->vis.sphere.R, planes);
    if (VIS == fcvNone) { diag_frustum_culled++; return; }  // Completely outside frustum

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
            diag_hierarchy++;
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
            // LOD visual - render children directly (skip LOD imposter for now)
            vkFLOD* pLOD = (vkFLOD*)pVisual;
            for (auto child : pLOD->children)
            {
                if (child)
                    add_leafs_Static((vkRender_Visual*)child);
            }
        }
        break;

    default:
        {
            diag_leaf_added++;

            // Debug: log tree types
            static u32 s_treeAddLog = 0;
            if ((pVisual->Type == 7 || pVisual->Type == 11) && s_treeAddLog < 20)
            {
                Msg("[TREE-ADD] add_Static default: type=%u name='%s' SSA=%.4f pos=(%.1f,%.1f,%.1f)",
                    pVisual->Type, pVisual->dbg_name.c_str(), SSA,
                    pVisual->vis.sphere.P.x, pVisual->vis.sphere.P.y, pVisual->vis.sphere.P.z);
                s_treeAddLog++;
            }

            // Leaf visual - add directly to lstNormal render queue.
            // We bypass r_dsgraph_insert_static() because it expects dxRender_Visual*
            // layout which is incompatible with vkRender_Visual* memory layout.
            R_dsgraph::_NormalItem item;
            item.ssa = SSA;
            item.pVisual = reinterpret_cast<dxRender_Visual*>(pVisual);
            lstNormal.push_back(item);
        }
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

    // Validate pointer is readable
    __try {
        volatile u32 test = pVisual->Type;
        (void)test;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Msg("! add_leafs_Static: corrupted visual pointer %p at frame %u", pVisual, Device.dwFrame);
        return;
    }

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
        {
            // LOD visual - render children directly
            vkFLOD* pLOD = (vkFLOD*)pVisual;
            for (auto child : pLOD->children)
            {
                if (child)
                    add_leafs_Static((vkRender_Visual*)child);
            }
        }
        break;

    default:
        {
            // Debug: log tree types
            static u32 s_treeLeafLog = 0;
            if ((pVisual->Type == 7 || pVisual->Type == 11) && s_treeLeafLog < 20)
            {
                Msg("[TREE-LEAF] add_leafs_Static default: type=%u name='%s' SSA=%.4f",
                    pVisual->Type, pVisual->dbg_name.c_str(), SSA);
                s_treeLeafLog++;
            }

            // Leaf visual - add directly to lstNormal
            R_dsgraph::_NormalItem item;
            item.ssa = SSA;
            item.pVisual = reinterpret_cast<dxRender_Visual*>(pVisual);
            lstNormal.push_back(item);
        }
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

// Crash recovery tracking — if we crash too many times in a short window,
// stop trying and leave g_bDeviceLost permanently set.
static u32 s_crashCount = 0;
static u32 s_lastCrashFrame = 0;
static constexpr u32 MAX_CRASHES_PER_WINDOW = 5;   // max crashes allowed
static constexpr u32 CRASH_WINDOW_FRAMES   = 300;  // within this many frames

void CRender::Render()
{
    // Skip all rendering if device is permanently lost
    if (g_bDeviceLost) return;

    // Check crash loop: if too many crashes in a short window, give up permanently
    if (s_crashCount >= MAX_CRASHES_PER_WINDOW &&
        (Device.dwFrame - s_lastCrashFrame) <= CRASH_WINDOW_FRAMES)
    {
        g_bDeviceLost = true;
        return;
    }

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
    // Heap corruption check at START of render pipeline (detect game logic corruption)
    {
        static u32 s_heapCheck0Frame = 0;
        if (Device.dwFrame - s_heapCheck0Frame > 100) {
            HANDLE heap = GetProcessHeap();
            if (!HeapValidate(heap, 0, NULL)) {
                Msg("!!! HEAP CORRUPT detected at START of Render() at frame %u", Device.dwFrame);
                FlushLog();
            }
            s_heapCheck0Frame = Device.dwFrame;
        }
    }
    bool bPassCrashed = false;  // Set by __except handlers to trigger recovery

    // Wait for ALL GPU work before recording render passes.
    // G-Buffer RTs are shared across frames — NVIDIA driver internal state
    // gets corrupted if we record barriers while previous present is still in flight.
    // Must be here (not in Begin_Inner) because corruption occurs after Begin.
    vkDeviceWaitIdle(VulkanHW.m_Device);

    // Reset descriptor pool for this frame (GPU is idle, safe to recycle all sets)
    if (g_DescriptorManager)
        g_DescriptorManager->ResetPool();

    // Invalidate cached descriptor set handles — they were freed by ResetPool()
    if (RTarget)
        RTarget->InvalidateDescriptorSets();

    // ========================================================================
    // PASS 1: Shadow Map Pass (if level is loaded)
    // ========================================================================
    VkDiagFrame("[RENDER] PASS 1: shadows (DISABLED for stability test)");
    if (g_bDeviceLost) return;
    // TEMPORARILY DISABLED: Shadow pass crashes at ~frame 1200-3500 in render_sun_cascades()
    // Root cause: access violation in r_dsgraph_render_graph() during shadow geometry rendering
    // TODO: Fix shadow caster geometry iteration (possible stale visual pointers)
    if (false && b_loaded && !Visuals.empty()) {
        // Render cascade shadow maps for directional light (sun)
        __try {
            VkDiagFrame("[RENDER] PASS 1a: sun cascades");
            render_sun_cascades();
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Msg("! CRASH in render_sun_cascades() at frame %u, exception 0x%08X",
                Device.dwFrame, GetExceptionCode());
            FlushLog();
            bPassCrashed = true;
        }

        // Render shadow cubemaps for point lights (Phase 2.16.2)
        if (!bPassCrashed) {
            __try {
                VkDiagFrame("[RENDER] PASS 1b: point shadows");
                for (u32 i = 0; i < Lights.package.v_point.size(); i++) {
                    light* L = Lights.package.v_point[i];
                    if (L && L->flags.bActive && L->flags.bShadow) {
                        RTarget->phase_smap_point(L);
                    }
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                Msg("! CRASH in point light shadows at frame %u, exception 0x%08X",
                    Device.dwFrame, GetExceptionCode());
                FlushLog();
                bPassCrashed = true;
            }
        }
    }

    // ========================================================================
    // PASS 2: G-Buffer Pass (geometry to MRT)
    // ========================================================================
    VkDiagFrame("[RENDER] PASS 2: gbuffer");
    if (g_bDeviceLost || bPassCrashed) { if (bPassCrashed) goto render_crash_recovery; return; }
    if (b_loaded && RTarget) {
        // Heap corruption check BEFORE gbuffer
        {
            static u32 s_heapCheckFrame = 0;
            if (Device.dwFrame - s_heapCheckFrame > 100) {
                HANDLE heap = GetProcessHeap();
                if (!HeapValidate(heap, 0, NULL)) {
                    Msg("!!! HEAP CORRUPT detected BEFORE phase_gbuffer at frame %u", Device.dwFrame);
                    FlushLog();
                }
                s_heapCheckFrame = Device.dwFrame;
            }
        }
        __try {
            RTarget->phase_gbuffer();
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Msg("! CRASH in phase_gbuffer() at frame %u, exception 0x%08X",
                Device.dwFrame, GetExceptionCode());
            FlushLog();
            bPassCrashed = true;
        }
    }

    // ========================================================================
    // PASS 3: Lighting Pass (deferred lighting)
    // ========================================================================
    VkDiagFrame("[RENDER] PASS 3: lighting");
    if (g_bDeviceLost || bPassCrashed) { if (bPassCrashed) goto render_crash_recovery; return; }
    // Heap corruption check AFTER gbuffer, BEFORE lighting
    {
        static u32 s_heapCheck2Frame = 0;
        if (Device.dwFrame - s_heapCheck2Frame > 100) {
            HANDLE heap = GetProcessHeap();
            if (!HeapValidate(heap, 0, NULL)) {
                Msg("!!! HEAP CORRUPT detected AFTER gbuffer / BEFORE lighting at frame %u", Device.dwFrame);
                FlushLog();
            }
            s_heapCheck2Frame = Device.dwFrame;
        }
    }
    if (b_loaded && RTarget) {
        __try {
            // Clear accumulator
            RTarget->phase_accumulator();

            // Sun lighting is now handled in the combine shader via push constants.
            // Cascade shadow accumulation disabled until shadow map infrastructure is ready.
            // RTarget->accum_direct_cascades(SE_SUN_NEAR);
            // RTarget->accum_direct_cascades(SE_SUN_MIDDLE);
            // RTarget->accum_direct_cascades(SE_SUN_FAR);

            // Point lights (Phase 2.16.5)
            VkDiagFrame("[RENDER] PASS 3b: point lights");
            if (Lights.package.v_point.size() > 4096) {
                Msg("! Suspicious point light count: %u — skipping", (u32)Lights.package.v_point.size());
            } else
            for (u32 i = 0; i < Lights.package.v_point.size(); i++) {
                light* L = Lights.package.v_point[i];
                if (L && L->flags.bActive) {
                    __try {
                        RTarget->accum_point(L);
                    } __except(EXCEPTION_EXECUTE_HANDLER) {
                        Msg("! CRASH in accum_point[%u] at frame %u, light=%p, exception 0x%08X",
                            i, Device.dwFrame, L, GetExceptionCode());
                    }
                }
            }

            // Spot lights (Phase 2.17.5)
            VkDiagFrame("[RENDER] PASS 3c: spot lights");
            if (Lights.package.v_spot.size() > 4096) {
                Msg("! Suspicious spot light count: %u — skipping", (u32)Lights.package.v_spot.size());
            } else
            for (u32 i = 0; i < Lights.package.v_spot.size(); i++) {
                light* L = Lights.package.v_spot[i];
                if (L && L->flags.bActive) {
                    __try {
                        // First render shadow map (if light casts shadows)
                        if (L->flags.bShadow) {
                            RTarget->phase_smap_spot(L);
                        }
                        // Then accumulate lighting
                        RTarget->accum_spot(L);
                    } __except(EXCEPTION_EXECUTE_HANDLER) {
                        // Don't access L->fields here — L might be the dangling pointer
                        Msg("! CRASH in accum_spot[%u] at frame %u, light=%p, exception 0x%08X",
                            i, Device.dwFrame, L, GetExceptionCode());
                    }
                }
            }

            // Transition rt_Accumulator: COLOR_ATTACHMENT → SHADER_READ_ONLY
            // (combine pass reads it as a sampled texture)
            {
                VkCommandBuffer cmdAccum = RCache.GetCommandBuffer();
                if (cmdAccum != VK_NULL_HANDLE) {
                    VkImageMemoryBarrier accumToRead = {};
                    accumToRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    accumToRead.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                    accumToRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    accumToRead.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    accumToRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    accumToRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    accumToRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    accumToRead.image = RTarget->rt_Accumulator.m_Image;
                    accumToRead.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    accumToRead.subresourceRange.baseMipLevel = 0;
                    accumToRead.subresourceRange.levelCount = 1;
                    accumToRead.subresourceRange.baseArrayLayer = 0;
                    accumToRead.subresourceRange.layerCount = 1;

                    vkCmdPipelineBarrier(cmdAccum,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        0, 0, nullptr, 0, nullptr, 1, &accumToRead);
                }
            }

        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Msg("! CRASH in lighting pass (outer) at frame %u, exception 0x%08X",
                Device.dwFrame, GetExceptionCode());
            FlushLog();
            bPassCrashed = true;
        }
    }

    // If any pass crashed, skip remaining passes and go to recovery
    if (bPassCrashed) goto render_crash_recovery;

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
    if (g_bDeviceLost || bPassCrashed) { if (bPassCrashed) goto render_crash_recovery; return; }
    if (RTarget) {
        RTarget->phase_combine();
        VkDiagFrame("[RENDER] PASS 5: combine done");
        RTarget->phase_sky();
        VkDiagFrame("[RENDER] PASS 5.5: sky done");
        RTarget->phase_clouds();
        VkDiagFrame("[RENDER] PASS 5.6: clouds done");
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
    // PASS 5.7: Detail rendering (grass, bushes, debris)
    // ========================================================================
    VkDiagFrame("[RENDER] PASS 5.7: details");
    if (Details && b_loaded) {
        Details->Render();
    }

    // ========================================================================
    // PASS 6: Forward Pass (transparent objects, particles, etc.)
    // ========================================================================
    // Phase 2.19: Forward rendering для transparent objects
    if (g_bDeviceLost || bPassCrashed) { if (bPassCrashed) goto render_crash_recovery; return; }
    VkDiagFrame("[RENDER] PASS 6: forward");
    if (RTarget) {
        RTarget->phase_forward();
    }

    // Render sorted (transparent) geometry back-to-front
    r_dsgraph_render_sorted();

    // Portal fade rendering (LOD transitions for distant portals)
    vkPortalTraverser.fade_render();

    // ========================================================================
    // PASS 6.5: Wallmarks (blood, bullet holes, decals)
    // ========================================================================
    VkDiagFrame("[RENDER] PASS 6.5: wallmarks");
    if (Wallmarks) {
        Wallmarks->Render();
    }

    // ========================================================================
    // PASS 6.6: 3D Fluid Volumes (volumetric smoke, fog, fire)
    // ========================================================================
    VkDiagFrame("[RENDER] PASS 6.6: 3D fluid");
    if (b_loaded && o.volumetricfog) {
        VkCommandBuffer cmd = RCache.GetCommandBuffer();
        if (cmd != VK_NULL_HANDLE) {
            VK::g_FluidManager.RenderFluid(cmd);
        }
    }

    // ========================================================================
    // PASS 7: HUD 3D Rendering (weapons, hands, HUD particles)
    // ========================================================================
    // NOTE: HUD is now rendered inside phase_gbuffer (Step 9c) so it goes
    // through the deferred pipeline. This pass just clears any leftover mapHUD.
    VkDiagFrame("[RENDER] PASS 7: HUD 3D");
    if (mapHUD.size() > 0) {
        Msg("! [HUD] WARNING: mapHUD=%u still has items at PASS 7 (should be 0 after gbuffer)", mapHUD.size());
        mapHUD.clear();
    }
    if (mapCamAttached.size() > 0) {
        Msg("! [HUD] WARNING: mapCamAttached=%u still has items at PASS 7", mapCamAttached.size());
        mapCamAttached.clear();
    }

    // ========================================================================
    // PASS 7.5: HUD UI Overlay (active item UI, camera-attached UI)
    // ========================================================================
    // Phase 2.28: Render HUD UI overlays with proper projection switching
    VkDiagFrame("[RENDER] PASS 7.5: HUD UI");
    {
        // g_hud is CCustomHUD* (declared in customhud.h, included at top)
        extern ENGINE_API CCustomHUD* g_hud;
        if (g_hud)
        {
            // Active item UI (weapon sights, scopes, etc.)
            if (g_hud->RenderActiveItemUIQuery())
                r_dsgraph_render_hud_ui();

            // Camera-attached UI (special effects with custom FOV)
            if (g_hud->RenderCamAttachedUIQuery())
                r_dsgraph_render_cam_ui();
        }
    }

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
    return;

    // ========================================================================
    // Crash recovery: a pass crashed with access violation.
    // Set g_bDeviceLost so End() knows to do recovery (reset cmd buffer,
    // submit minimal frame, present).  End() will clear g_bDeviceLost
    // after recovery, so the next frame can try rendering again.
    //
    // If we crash too often (MAX_CRASHES_PER_WINDOW within CRASH_WINDOW_FRAMES),
    // leave g_bDeviceLost permanently set to avoid an infinite crash loop.
    // ========================================================================
render_crash_recovery:
    {
        u32 currentFrame = Device.dwFrame;
        // Reset crash counter if we haven't crashed recently
        if (currentFrame - s_lastCrashFrame > CRASH_WINDOW_FRAMES) {
            s_crashCount = 0;
        }
        s_crashCount++;
        s_lastCrashFrame = currentFrame;

        Msg("! Render crash recovery: crash %u/%u at frame %u, signaling End() for cmd buffer recovery",
            s_crashCount, MAX_CRASHES_PER_WINDOW, currentFrame);

        // Signal End() to do recovery (reset cmd pool, submit minimal frame, present).
        // End() will clear g_bDeviceLost after recovery so the next frame can try again.
        // If s_crashCount reaches MAX_CRASHES_PER_WINDOW, the check at the top of
        // Render() will permanently set g_bDeviceLost and stop trying.
        g_bDeviceLost = true;
        VulkanUI_ResetState();  // prevent EndUIPass from crashing on stale state
        FlushLog();

        // End frame stats
        RCache.OnFrameEnd();
        stats.l_total = 0;
        stats.l_visible = 0;
    }
}

void CRender::OnFrame()
{
    // Per-frame update - called before Render()
    // This is called by Device.seqFrame.Process(rp_Frame) every frame

    // ========================================================================
    // Save previous frame matrices for temporal effects
    // ========================================================================
    // These are used for:
    // - Motion blur (velocity vectors)
    // - Temporal Anti-Aliasing (TAA)
    // - Motion vectors for reflections
    // - Reprojection for post-processing
    RCache.xforms.set_W_prev(RCache.xforms.get_W());
    RCache.xforms.set_V_prev(RCache.xforms.get_V());
    RCache.xforms.set_P_prev(RCache.xforms.get_P());

    // ========================================================================
    // Reset visibility markers for new frame
    // ========================================================================
    // Each visual has vis.marker field compared against RImplementation.marker
    // to prevent re-processing the same object multiple times per frame
    marker++;

    // ========================================================================
    // Update subsystems (if needed)
    // ========================================================================

    // Update model pool (LOD, animation caching)
    // Models->OnFrame() might update LOD distances, animation states, etc.
    // Currently Models is vkModelPool which doesn't have OnFrame() - may add later

    // Update particle systems
    // PSLibrary should update emitters, lifetime, spawning
    // Currently PSLibrary doesn't have OnFrame() - particles update in Render()

    // Update environment/weather
    // g_pGamePersistent->Environment() updates time-of-day, fog, rain, etc.
    // This is usually called by game logic, not renderer

    // ========================================================================
    // Statistics reset (optional)
    // ========================================================================
    // Some renderers reset per-frame stats here
    // We do it in OnFrameBegin()/OnFrameEnd() instead

    // TODO: If adding temporal effects (motion blur, TAA), implement here:
    // - Update velocity buffer history
    // - Update jitter pattern for TAA
    // - Update temporal accumulation buffers
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

// ============================================================================
// add_leafs_to_lstMatrix - Decompose hierarchy/skeleton into leaf visuals
// for lstMatrix. Each leaf gets its own _MatrixItem with the parent's world
// matrix. This mirrors what add_leafs_HUD_VK does for mapHUD.
// ============================================================================
static const u32 MAX_HIERARCHY_DEPTH = 32;

void CRender::add_leafs_to_lstMatrix(vkRender_Visual* pVisual, const Fmatrix& worldMatrix, u32 depth)
{
    if (!pVisual) return;
    if (depth > MAX_HIERARCHY_DEPTH) {
        static u32 s_depthWarn = 0;
        if (s_depthWarn < 5) {
            Msg("! [HIER] add_leafs_to_lstMatrix: depth %u exceeded limit, visual=%p type=%u",
                depth, pVisual, pVisual->Type);
            s_depthWarn++;
        }
        return;
    }

    switch (pVisual->Type)
    {
    case MT_SKELETON_ANIM:
    case MT_SKELETON_RIGID:
    case MT_HIERRARHY:
    {
        xr_vector<IRenderVisual*>* children = pVisual->get_children();
        if (children) {
            for (auto child : *children) {
                if (child)
                    add_leafs_to_lstMatrix(static_cast<vkRender_Visual*>(child), worldMatrix, depth + 1);
            }
        }
        return;
    }

    default:
    {
        // One-shot diagnostic: log skeleton children being added to lstMatrix
        {
            static u32 s_leafAddCount = 0;
            if (s_leafAddCount < 20) {
                s_leafAddCount++;
                const char* nm = (pVisual->dbg_name.size() > 0) ? pVisual->dbg_name.c_str() : "<empty>";
                Msg("[MATRIX-ADD] leaf Type=%u name='%s' depth=%u pos=(%.1f,%.1f,%.1f) ptr=%p",
                    pVisual->Type, nm, depth,
                    worldMatrix._41, worldMatrix._42, worldMatrix._43, pVisual);
            }
        }
        R_dsgraph::_MatrixItem item;
        item.ssa = 1.0f;
        item.pObject = val_pObject;
        item.pVisual = reinterpret_cast<dxRender_Visual*>(pVisual);
        item.Matrix = worldMatrix;
        item.PrevMatrix = worldMatrix;
        lstMatrix.push_back(item);
        return;
    }
    }
}

void CRender::add_Visual(IRenderVisual* V)
{
    if (!V) return;

    vkRender_Visual* pVisual = static_cast<vkRender_Visual*>(V);

    // Route HUD visuals to mapHUD — decompose hierarchy into leaf visuals
    if (val_bHUD)
    {
        static u32 s_hudAddFrame = 0;
        if (Device.dwFrame != s_hudAddFrame) {
            Msg("[HUD-DIAG] add_Visual: val_bHUD=TRUE, visual=%p type=%u frame=%u mapHUD_before=%u",
                pVisual, pVisual->Type, Device.dwFrame, mapHUD.size());
            s_hudAddFrame = Device.dwFrame;
        }
        add_leafs_HUD_VK(pVisual);
        return;
    }

    // For skeleton and hierarchy types: decompose into leaf visuals.
    // Bones are calculated at add-time, leaf visuals go into lstMatrix.
    if (pVisual->Type == MT_SKELETON_ANIM || pVisual->Type == MT_SKELETON_RIGID ||
        pVisual->Type == MT_HIERRARHY)
    {
        Fmatrix worldMatrix = (val_pTransform) ? *val_pTransform : Fidentity;

        // Calculate bones for skeleton types
        if (pVisual->Type == MT_SKELETON_ANIM || pVisual->Type == MT_SKELETON_RIGID)
        {
            IKinematics* pK = pVisual->dcast_PKinematics();
            if (pK) pK->CalculateBones(TRUE);
        }

        add_leafs_to_lstMatrix(pVisual, worldMatrix);
        return;
    }

    // Leaf visual — add directly to lstMatrix
    R_dsgraph::_MatrixItem item;
    item.ssa = 1.0f;
    item.pObject = val_pObject;
    item.pVisual = reinterpret_cast<dxRender_Visual*>(pVisual);
    item.Matrix = (val_pTransform) ? *val_pTransform : Fidentity;
    item.PrevMatrix = item.Matrix;
    lstMatrix.push_back(item);
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
        // Diagnostic: detect skeleton children entering lstNormal (no world matrix!)
        if (pVisual->Type == 5) { // MT_SKELETON_GEOMDEF_ST
            static u32 s_sklNormalWarn = 0;
            if (s_sklNormalWarn < 10) {
                s_sklNormalWarn++;
                const char* nm = (pVisual->dbg_name.size() > 0) ? pVisual->dbg_name.c_str() : "<empty>";
                Msg("[SKL-IN-NORMAL!] Skeleton child added to lstNormal (no matrix)! Type=%u name='%s' ptr=%p",
                    pVisual->Type, nm, pVisual);
            }
        }
        // Leaf visual (geometry, particle effect, etc.) - add to render queue
        R_dsgraph::_NormalItem item;
        item.ssa = 1.0f;  // Dynamic objects: max priority
        item.pVisual = reinterpret_cast<dxRender_Visual*>(pVisual);
        lstNormal.push_back(item);
        return;
    }
    }
}

// ============================================================================
// add_leafs_HUD_VK - Recursively expand HUD visuals into mapHUD/mapCamAttached
// ============================================================================
// Same as add_leafs_Dynamic_VK but routes leaf visuals to HUD render queues
// instead of lstNormal. This ensures skeleton hierarchies are properly decomposed
// so that each leaf (vkSkeletonX_PM, vkSkeletonX_ST, vkFVisual) can be rendered
// directly with its own pipeline/material binding.
// ============================================================================
void CRender::add_leafs_HUD_VK(vkRender_Visual* pVisual)
{
    if (!pVisual) return;

    // Frame-limited diagnostics (first 5 frames)
    static u32 s_diagHudFrame = 0;
    static u32 s_diagHudCount = 0;
    bool bDiag = (Device.dwFrame != s_diagHudFrame) && (s_diagHudCount < 5);
    if (bDiag) {
        s_diagHudFrame = Device.dwFrame;
        s_diagHudCount++;
    }

    switch (pVisual->Type)
    {
    case MT_PARTICLE_GROUP:
    {
        if (bDiag) Msg("[HUD-LEAF] ParticleGroup visual=%p", pVisual);
        vkCParticleGroup* pG = static_cast<vkCParticleGroup*>(pVisual);
        for (auto& item : pG->items)
        {
            if (item.pVisual)
                add_leafs_HUD_VK(static_cast<vkRender_Visual*>(item.pVisual));
        }
        return;
    }

    case MT_HIERRARHY:
    {
        xr_vector<IRenderVisual*>* children = pVisual->get_children();
        if (bDiag) Msg("[HUD-LEAF] Hierarchy visual=%p children=%u", pVisual, children ? (u32)children->size() : 0);
        if (children) {
            for (auto child : *children) {
                if (child)
                    add_leafs_HUD_VK(static_cast<vkRender_Visual*>(child));
            }
        }
        return;
    }

    case MT_SKELETON_ANIM:
    case MT_SKELETON_RIGID:
    {
        // Calculate bones first, then expand children
        IKinematics* pK = pVisual->dcast_PKinematics();

        if (bDiag) {
            Msg("[HUD-LEAF] Skeleton visual=%p type=%u dcast_PKinematics=%p", pVisual, pVisual->Type, pK);
            if (pK) {
                Msg("[HUD-LEAF]   IKinematics=%p LL_BoneCount=%u", pK, pK->LL_BoneCount());
            }
        }

        if (pK) {
            pK->CalculateBones(TRUE);
        } else {
            Msg("! [HUD-LEAF] WARNING: dcast_PKinematics returned NULL for skeleton visual=%p type=%u!", pVisual, pVisual->Type);
        }

        xr_vector<IRenderVisual*>* children = pVisual->get_children();
        if (bDiag && children) {
            Msg("[HUD-LEAF]   Skeleton children count=%u", (u32)children->size());
        }
        if (children) {
            for (u32 ci = 0; ci < children->size(); ci++) {
                IRenderVisual* child = (*children)[ci];
                if (!child) continue;
                if (bDiag) {
                    Msg("[HUD-LEAF]   child[%u]=%p type=%u", ci, child, child->getType());
                }
                add_leafs_HUD_VK(static_cast<vkRender_Visual*>(child));
            }
        }
        return;
    }

    default:
    {
        if (bDiag) {
            Msg("[HUD-LEAF] Leaf visual=%p type=%u -> mapHUD (val_bCamAttached=%d)", pVisual, pVisual->Type, val_bCamAttached?1:0);
        }
        // Leaf visual — insert into HUD render queue based on shader flags
        // (mirrors DX11 r_dsgraph_insert_dynamic HUD routing logic)
        R_dsgraph::_MatrixItemS item;
        item.ssa = 1.0f;
        item.pObject = val_pObject;
        item.pVisual = reinterpret_cast<dxRender_Visual*>(pVisual);
        item.Matrix = (val_pTransform) ? *val_pTransform : Fidentity;
        item.PrevMatrix = item.Matrix;
        item.se = nullptr;

        // Lookup Vulkan shader flags for this visual
        VK::CVulkanShader* pVKShader = nullptr;
        if (pVisual->shader_id < (u16)Shaders.size())
            pVKShader = Shaders[pVisual->shader_id];

        bool bDistort  = pVKShader && pVKShader->m_bDistort;
        bool bEmissive = pVKShader && pVKShader->m_bEmissive;
        bool bSorted   = pVKShader && pVKShader->m_PipelineConfig.blendEnable;

        // 1) Distortion pass (scope heat, barrel shimmer)
        if (bDistort)
            mapHUDDistort.insertInAnyWay(0.f, item);

        // 2) Sorted transparent (scope glass, transparent parts) — back-to-front
        if (bSorted)
        {
            // Emissive transparent also goes to emissive pass (collimator dots, LEDs)
            if (bEmissive)
            {
                if (val_bCamAttached)
                    mapCamAttachedEmissive.insertInAnyWay(0.f, item);
                else
                    mapHUDEmissive.insertInAnyWay(0.f, item);
            }

            if (val_bCamAttached)
                mapCamAttachedSorted.insertInAnyWay(0.f, item);
            else
                mapHUDSorted.insertInAnyWay(0.f, item);
            return;
        }

        // 3) Opaque HUD (weapon body, hands) — may also have emissive
        if (bEmissive)
        {
            if (val_bCamAttached)
                mapCamAttachedEmissive.insertInAnyWay(0.f, item);
            else
                mapHUDEmissive.insertInAnyWay(0.f, item);
        }

        if (val_bCamAttached)
            mapCamAttached.insertInAnyWay(0.f, item);
        else
            mapHUD.insertInAnyWay(0.f, item);
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
    if (!Models) {
        Msg("![Vulkan] model_Create: Models is NULL!");
        return nullptr;
    }

    IRenderVisual* result = Models->Create(name, data, true);

    if (!result)
    {
        Msg("![Vulkan] model_Create('%s'): FAILED (nullptr)", name ? name : "NULL");
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
    if (id >= 0 && id < (int)Sectors.size()) {
        IRender_Sector* sector = Sectors[id];
        // Extra safety: verify sector is valid
        if (!sector) {
            Msg("![Vulkan] getSector(%d): Sectors array contains nullptr at valid index!", id);
        }
        return sector;
    }
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
    // Ported from DX11 R2: detectSector with two-direction ray cast
    if (Sectors.empty())
        return nullptr;

    Sectors_xrc.ray_options(CDB::OPT_ONLYNEAREST);

    // Try downward ray first
    Fvector dir;
    dir.set(0, -1, 0);
    IRender_Sector* S = detectSector(P, dir);

    // If downward fails, try upward
    if (nullptr == S)
    {
        dir.set(0, 1, 0);
        S = detectSector(P, dir);
    }

    return S;
}

IRender_Sector* CRender::detectSector(const Fvector& P, Fvector& dir)
{
    // Ported from DX11 R2: ray-cast through portal and geometry models
    // to determine which sector contains point P

    // Portal model query
    int id1 = -1;
    float range1 = 500.f;
    if (rmPortals)
    {
        Sectors_xrc.ray_query(rmPortals, P, dir, range1);
        if (Sectors_xrc.r_count())
        {
            CDB::RESULT* RP1 = Sectors_xrc.r_begin();
            id1 = RP1->id;
            range1 = RP1->range;
        }
    }

    // Geometry model query
    int id2 = -1;
    float range2 = range1;
    if (g_pGameLevel && g_pGameLevel->ObjectSpace.GetStaticModel())
    {
        Sectors_xrc.ray_query(g_pGameLevel->ObjectSpace.GetStaticModel(), P, dir, range2);
        if (Sectors_xrc.r_count())
        {
            CDB::RESULT* RP2 = Sectors_xrc.r_begin();
            id2 = RP2->id;
            range2 = RP2->range;
        }
    }

    // Select best hit
    int ID;
    if (id1 >= 0)
    {
        if (id2 >= 0) ID = (range1 <= range2 + EPS) ? id1 : id2;
        else ID = id1;
    }
    else if (id2 >= 0) ID = id2;
    else return nullptr;

    if (ID == id1)
    {
        // Hit portal - get sector facing the point
        CDB::TRI* pTri = rmPortals->get_tris() + ID;
        if (pTri->dummy < Portals.size())
        {
            vkCPortal* pPortal = (vkCPortal*)Portals[pTri->dummy];
            if (pPortal)
                return pPortal->getSectorFacing(P);
        }
        return nullptr;
    }
    else
    {
        // Hit geometry - get sector from triangle
        CDB::TRI* pTri = g_pGameLevel->ObjectSpace.GetStaticTris() + ID;
        return getSector(pTri->sector);
    }
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

    u32 width = T->get_width();
    u32 height = T->get_height();

    RCache.m_Viewport.x = 0;
    RCache.m_Viewport.y = (float)height;
    RCache.m_Viewport.width  = (float)width;
    RCache.m_Viewport.height = -(float)height;
    RCache.m_Viewport.minDepth = 0.f;
    RCache.m_Viewport.maxDepth = 0.02f;

    RCache.m_Scissor.offset.x = 0;
    RCache.m_Scissor.offset.y = 0;
    RCache.m_Scissor.extent.width = width;
    RCache.m_Scissor.extent.height = height;

    RCache.ApplyViewportScissor();
}

void CRender::rmFar()
{
    // Set viewport depth range for sky rendering (back of depth buffer)
    IRender_Target* T = getTarget();
    if (!T) return;

    u32 width = T->get_width();
    u32 height = T->get_height();

    RCache.m_Viewport.x = 0;
    RCache.m_Viewport.y = (float)height;
    RCache.m_Viewport.width  = (float)width;
    RCache.m_Viewport.height = -(float)height;
    RCache.m_Viewport.minDepth = 0.99999f;
    RCache.m_Viewport.maxDepth = 1.f;

    RCache.m_Scissor.offset.x = 0;
    RCache.m_Scissor.offset.y = 0;
    RCache.m_Scissor.extent.width = width;
    RCache.m_Scissor.extent.height = height;

    RCache.ApplyViewportScissor();
}

void CRender::rmNormal()
{
    // Reset viewport depth range to full (normal rendering)
    IRender_Target* T = getTarget();
    if (!T) return;

    u32 width = T->get_width();
    u32 height = T->get_height();

    RCache.m_Viewport.x = 0;
    RCache.m_Viewport.y = (float)height;
    RCache.m_Viewport.width  = (float)width;
    RCache.m_Viewport.height = -(float)height;
    RCache.m_Viewport.minDepth = 0.f;
    RCache.m_Viewport.maxDepth = 1.f;

    RCache.m_Scissor.offset.x = 0;
    RCache.m_Scissor.offset.y = 0;
    RCache.m_Scissor.extent.width = width;
    RCache.m_Scissor.extent.height = height;

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
    // HUD-mode particles (muzzle flashes, impact effects, etc.) are rendered
    // through two mechanisms:
    //
    // 1. Primary path: Particles with HUD mode go through add_leafs_HUD_VK()
    //    into mapHUD and are rendered in the gbuffer phase with HUD projection.
    //
    // 2. Self-handling: vkCParticleEffect::Render() checks GetHudMode() and
    //    switches to HUD projection internally (matching DX11 behavior).
    //
    // This function is called as a safety net after mapHUD rendering.
    // Currently no additional work needed — particles handle themselves.
}
