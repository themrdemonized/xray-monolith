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
#include "vk_buffer_pool.h"
#include "vk_ModelPool.h"
#include "vk_Visual.h"

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
float r_ssaDISCARD       = 4.f;   // Discard objects smaller than this SSA
float r_ssaLOD_A         = 64.f;  // Start LOD transition at this SSA
float r_ssaLOD_B         = 48.f;  // End LOD transition at this SSA

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

    VulkanDiagWriteRvk("[DIAG] CRender ctor: step 6 - stats");
    memset(&stats, 0, sizeof(stats));  // memset instead of ZeroMemory (static init safe)
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

    // Create managers if not already created
    if (!g_ShaderManager) {
        g_ShaderManager = xr_new<VK::CVulkanShaderManager>();
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
    // Scene graph calculation / visibility culling
    // TODO: Implement frustum culling, portal/sector visibility
    // TODO: Build render queues (mapNormalPasses, mapMatrixPasses, etc.)
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
        g_pGamePersistent->OnRenderPPUI_main();
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

        // Render directional light with cascade shadows
        // TODO: Render all 3 cascades (NEAR/MIDDLE/FAR)
        // For now, just render FAR cascade as test
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
    // PASS 4: Combine Pass (accumulator → swapchain)
    // ========================================================================
    // Phase 2.18: Combine accumulated lighting with albedo and output to swapchain
    VkDiagFrame("[RENDER] PASS 4: combine");
    if (RTarget) {
        RTarget->phase_combine();
        VkDiagFrame("[RENDER] PASS 4: combine done");
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
    // PASS 5: Forward Pass (transparent objects, particles, etc.)
    // ========================================================================
    // Phase 2.19: Forward rendering для transparent objects
    VkDiagFrame("[RENDER] PASS 5: forward");
    if (RTarget) {
        RTarget->phase_forward();
    }

    // TODO: Portal fade rendering: vkPortalTraverser.fade_render()
    // TODO: Sorted geometry rendering

    // ========================================================================
    // PASS 6: Post-Process Pass
    // ========================================================================
    // TODO: Bloom, tone mapping, color grading, etc.

    // ========================================================================
    // PASS 7: UI Pass (in-game UI when level is loaded)
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
    // Add visual to render queue
    // TODO: Implement scene graph insertion
}

void CRender::add_Geometry(IRenderVisual* V)
{
    // Add geometry with culling
    // TODO: Implement with frustum culling
}

void CRender::add_Occluder(Fbox2& bb_screenspace)
{
    // Add occluder for occlusion culling
    // TODO: Implement HOM integration
}

void CRender::flush()
{
    // Flush render queue
    // TODO: Actually render queued objects
}

// ============================================================================
// Model management
// ============================================================================
IRenderVisual* CRender::model_Create(LPCSTR name, IReader* data)
{
    if (!Models) return nullptr;
    return Models->Create(name, data, true);
}

IRenderVisual* CRender::model_CreateChild(LPCSTR name, IReader* data)
{
    if (!Models) return nullptr;
    return Models->CreateChild(name, data);
}

IRenderVisual* CRender::model_CreateParticles(LPCSTR name)
{
    // TODO: Implement particle system creation
    // return Models->CreatePE(...) or Models->CreatePG(...)
    Msg("[Vulkan] model_CreateParticles not implemented: %s", name);
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
// Light management (stubs for now)
// ============================================================================
IRender_Light* CRender::light_create()
{
    // TODO: Implement light creation
    return nullptr;
}

IRender_Glow* CRender::glow_create()
{
    // TODO: Implement glow creation
    return nullptr;
}

// ============================================================================
// Wallmarks (stubs for now)
// ============================================================================
void CRender::add_StaticWallmark(IWallMarkArray* pArray, const Fvector& P, float s, CDB::TRI* T, Fvector* V, float ttl, bool ignore_opt, bool random_rotation)
{
    // TODO: Implement
}

void CRender::add_StaticWallmark(IWallMarkArray* pArray, const Fvector& P, float s, CDB::TRI* T, Fvector* V, float ttl, bool ignore_opt, float rotation)
{
    // TODO: Implement
}

void CRender::add_StaticWallmark(const wm_shader& S, const Fvector& P, float s, CDB::TRI* T, Fvector* V)
{
    // TODO: Implement
}

void CRender::clear_static_wallmarks()
{
    // TODO: Implement
}

void CRender::add_SkeletonWallmark(const Fmatrix* xf, IKinematics* obj, IWallMarkArray* pArray, const Fvector& start,
                                   const Fvector& dir, float size, float ttl, bool ignore_opt)
{
    // TODO Phase 2.x: Implement skeleton wallmark rendering (blood decals on animated models)
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
    // TODO Phase 2.x: Add pre-created wallmark to rendering queue
}

// Wrapper for intrusive_ptr overload (used by SkeletonCustom.cpp)
void CRender::add_SkeletonWallmark(intrusive_ptr<CSkeletonWallmark> wm)
{
    add_SkeletonWallmark_impl(wm.get());
}

void CRender::add_SkeletonWallmark(const Fmatrix* xf, CKinematics* obj, ref_shader& sh, const Fvector& start,
                                   const Fvector& dir, float size, float ttl, bool ignore_opt)
{
    // TODO Phase 2.x: Create skeleton wallmark with explicit shader
}

// ============================================================================
// ROS (stubs for now)
// ============================================================================

IRender_ObjectSpecific* CRender::ros_create(IRenderable* parent)
{
    // TODO: Implement ROS creation
    return nullptr;
}

void CRender::ros_destroy(IRender_ObjectSpecific*& ROS)
{
    // TODO: Implement
    ROS = nullptr;
}

// ============================================================================
// Sector/Portal (stubs for now)
// ============================================================================
IRender_Sector* CRender::getSector(int id)
{
    // TODO: Implement
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
    // TODO: Implement sector detection
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
    // Set near clipping plane for weapon rendering
    // TODO: Implement
}

void CRender::rmFar()
{
    // Set far clipping plane
    // TODO: Implement
}

void CRender::rmNormal()
{
    // Reset to normal clipping planes
    // TODO: Implement
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
