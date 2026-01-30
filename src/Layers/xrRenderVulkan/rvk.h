// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#pragma once

#include "stdafx.h"
#include "../../xrEngine/render.h"
#include "../../xrEngine/irenderable.h"
#include "../../xrEngine/fmesh.h"  // FSlideWindowItem, ogf_header, etc.
#include "../xrRender/r__dsgraph_structure.h"  // R_dsgraph_structure base class
#include "../xrRender/Light_DB.h"  // CLight_DB
#include "../xrRender/PSLibrary.h" // CPSLibrary for particle system
#include "vk_HOM.h"               // vkCHOM (Vulkan HOM stub)
#include "vk_sun_cascades.h"      // Sun cascade structures
#include "../../xrCDB/Frustum.h"  // CFrustum for visibility culling

// Forward declarations
class dxRender_Visual;
class CRenderTarget;
class CDetailManager;
class CWallmarksEngine;
class vkCWallmarksEngine;
class vkModelPool;
class vkRender_Visual;
class vkFHierrarhyVisual;  // Hierarchy visual for scene graph traversal
class CStreamReader;  // For level.geom loading
class vkCSector;
class CSkeletonWallmark;
class CKinematics;
class vkCPortal;

// Template forward declarations (default argument lives in xrCore/intrusive_ptr.h)
struct intrusive_base;
template<typename T, typename base_type> class intrusive_ptr;

namespace VK {
    class CVulkanBuffer;
    class CVulkanShader;
}

// ============================================================================
// Frustum plane masks for culling
// ============================================================================
#define SF_RENDERING    0x3F    // All 6 frustum planes (LRTB + Near + Far)

// ============================================================================
// CRender - Main Vulkan renderer class
// Inherits from R_dsgraph_structure which implements IRender_interface
// ============================================================================
class CRender : public R_dsgraph_structure
{
public:
    // ========================================================================
    // Render phases
    // ========================================================================
    enum
    {
        PHASE_NORMAL = 0,
        PHASE_SMAP = 1,
    };

    // ========================================================================
    // Render options
    // ========================================================================
    struct _options
    {
        u32 smapsize : 16;
        u32 depth16 : 1;
        u32 mrt : 1;
        u32 mrtmixdepth : 1;
        u32 fp16_filter : 1;
        u32 fp16_blend : 1;
        u32 albedo_wo : 1;
        u32 HW_smap : 1;
        u32 HW_smap_PCF : 1;
        u32 HW_smap_FETCH4 : 1;
        u32 HW_smap_FORMAT : 32;

        u32 nvstencil : 1;
        u32 nvdbt : 1;
        u32 nullrt : 1;
        u32 no_ram_textures : 1;

        u32 distortion : 1;
        u32 distortion_enabled : 1;

        u32 sunfilter : 1;
        u32 sunstatic : 1;
        u32 sjitter : 1;
        u32 noshadows : 1;
        u32 Tshadows : 1;
        u32 disasm : 1;
        u32 advancedpp : 1;
        u32 volumetricfog : 1;

        u32 forcegloss : 1;
        u32 forceskinw : 1;
        u32 ssfx_water : 1;

        float forcegloss_v;
    } o;

    // ========================================================================
    // Statistics
    // ========================================================================
    struct _stats
    {
        u32 l_total, l_visible;
        u32 l_shadowed, l_unshadowed;
        s32 s_used, s_merged, s_finalclip;
        u32 o_queries, o_culled;
        u32 ic_total, ic_culled;
    } stats;

    // ========================================================================
    // State
    // ========================================================================
    u32 phase;
    u32 marker;
    bool pmask[2];
    bool pmask_wmark;

    // Current object being rendered
    IRenderable* val_pObject;
    Fmatrix* val_pTransform;
    BOOL val_bHUD;
    BOOL val_bCamAttached;
    BOOL val_bInvisible;
    BOOL val_bRecordMP;

    // Hemi/sun values for current object
    float o_hemi;
    float o_hemi_cube[6];  // CROS_impl::NUM_FACES
    float o_sun;

    // First frame after reset flag
    bool m_bFirstFrameAfterReset;

    // Counters
    u32 counter_S;
    u32 counter_D;
    BOOL b_loaded;

    // ========================================================================
    // Subsystems
    // ========================================================================
    vkModelPool*    Models;     // Model pool manager
    vkCHOM*         HOM;        // Hierarchical Occlusion Map (pointer - deferred init)
    CLight_DB       Lights;     // Light database (point, spot, directional)
    CPSLibrary      PSLibrary;  // Particle system library
    vkCWallmarksEngine* Wallmarks;  // Wallmark engine (blood, bullet holes, decals)

    // ========================================================================
    // Level data (loaded from level.geom and level file)
    // ========================================================================
    // Level visuals
    xr_vector<IRenderVisual*>   Visuals;

    // Sector/Portal visibility system
    vkCSector*                  pLastSector;
    Fvector                     vLastCameraPos;
    xr_vector<IRender_Portal*>  Portals;
    xr_vector<IRender_Sector*>  Sectors;
    CDB::MODEL*                 rmPortals;

    // Portal access (for sector loading)
    IRender_Portal* getPortal(int id);
    IRender_Sector* getSectorByIndex(int id);

    // Sliding window items (for LOD)
    xr_vector<FSlideWindowItem> SWIs;

    // Level shaders (Vulkan implementation)
    xr_vector<VK::CVulkanShader*>    Shaders;

    // Level vertex/index buffers (normal and extended/fast-path)
    // These are shared buffers that level visuals reference via OGF_GCONTAINER
    xr_vector<VK::CVulkanBuffer*>  nVB, xVB;  // Normal/Extended vertex buffers
    xr_vector<VK::CVulkanBuffer*>  nIB, xIB;  // Normal/Extended index buffers
    xr_vector<u32>                 nVB_Strides;  // Vertex strides for nVB
    xr_vector<u32>                 xVB_Strides;  // Vertex strides for xVB

    // ========================================================================
    // Scene Graph - Render Queues (Phase 1)
    // ========================================================================
    // Simplified Vulkan scene graph: Pipeline -> Material -> Items
    // DX11 uses deeper hierarchy (VS -> GS -> PS -> Constants -> States -> Textures -> Items)
    // but Vulkan pipelines encapsulate all states, so we simplify

    xr_vector<R_dsgraph::_NormalItem> lstNormal;   // Static visuals (level geometry)
    xr_vector<R_dsgraph::_MatrixItem> lstMatrix;   // Dynamic visuals (objects with transforms)

    // Visibility / Frustum culling
    CFrustum ViewBase;    // Main camera frustum
    CFrustum* View;       // Current frustum pointer (for portal traversal)

public:
    CRender();
    virtual ~CRender();

    // ========================================================================
    // IRender_interface - Feature level
    // ========================================================================
    virtual GenerationLevel get_generation() override { return IRender_interface::GENERATION_R2; }
    virtual bool is_sun_static() override { return o.sunstatic != 0; }
    // Report as DX11.1 equivalent (0x000B0001) since Vulkan 1.3 >= DX11 feature level
    virtual DWORD get_dx_level() override { return 0x000B0001; }

    // ========================================================================
    // IRender_interface - Lifecycle
    // ========================================================================
    virtual void create() override;
    virtual void destroy() override;
    virtual void reset_begin() override;
    virtual void reset_end() override;

    // ========================================================================
    // IRender_interface - Level management
    // ========================================================================
    virtual void level_Load(IReader* fs) override;
    virtual void level_Unload() override;

    // ========================================================================
    // IRender_interface - Information
    // ========================================================================
    virtual void Statistics(CGameFont* F) override;
    virtual LPCSTR getShaderPath() override { return "vulkan\\"; }  // Vulkan SPIR-V shaders

    // ========================================================================
    // IRender_interface - Main rendering
    // ========================================================================
    virtual void Calculate() override;
    virtual void Render() override;
    virtual void OnFrame() override;

    // ========================================================================
    // IRender_interface - Object management
    // ========================================================================
    virtual void set_Transform(Fmatrix* M) override
    {
        VERIFY(M);
        val_pTransform = M;
    }
    virtual void set_HUD(BOOL V) override { val_bHUD = V; }
    virtual BOOL get_HUD() override { return val_bHUD; }
    virtual void set_CamAttached(BOOL V) override { val_bCamAttached = V; }
    virtual BOOL get_CamAttached() override { return val_bCamAttached; }
    virtual void set_Invisible(BOOL V) override { val_bInvisible = V; }

    virtual void set_Object(IRenderable* O) override;
    virtual void add_Visual(IRenderVisual* V) override;
    virtual void add_Geometry(IRenderVisual* V) override;
    virtual void add_Occluder(Fbox2& bb_screenspace) override;
    virtual void flush() override;

    // ========================================================================
    // IRender_interface - Model management
    // ========================================================================
    virtual IRenderVisual* model_Create(LPCSTR name, IReader* data = 0) override;
    virtual IRenderVisual* model_CreateChild(LPCSTR name, IReader* data) override;
    virtual IRenderVisual* model_CreateParticles(LPCSTR name) override;
    virtual IRenderVisual* model_Duplicate(IRenderVisual* V) override;
    virtual void model_Delete(IRenderVisual*& V, BOOL bDiscard) override;
    virtual void model_Logging(BOOL bEnable) override;
    virtual void models_Prefetch() override;
    virtual void models_PrefetchOne(LPCSTR name, bool assert_on_fail = true) override;
    virtual void models_Clear(BOOL b_complete) override;
    virtual bool models_Exists(LPCSTR name) override;

    // ========================================================================
    // IRender_interface - Light management
    // ========================================================================
    virtual IRender_Light* light_create() override;
    virtual IRender_Glow* glow_create() override;

    // ========================================================================
    // IRender_interface - Wallmarks
    // ========================================================================
    // ref_shader overloads (internal, called by IWallMarkArray bridge)
    void add_StaticWallmark(ref_shader& S, const Fvector& P, float s, CDB::TRI* T, Fvector* V, float ttl = 0.f, bool ignore_opt = false, bool random_rotation = true);
    void add_StaticWallmark(ref_shader& S, const Fvector& P, float s, CDB::TRI* T, Fvector* V, float ttl, bool ignore_opt, float rotation);
    // IWallMarkArray overloads (virtual interface from IRender)
    virtual void add_StaticWallmark(IWallMarkArray* pArray, const Fvector& P, float s, CDB::TRI* T, Fvector* V, float ttl = 0.f, bool ignore_opt = false, bool random_rotation = true) override;
    virtual void add_StaticWallmark(IWallMarkArray* pArray, const Fvector& P, float s, CDB::TRI* T, Fvector* V, float ttl, bool ignore_opt, float rotation) override;
    virtual void add_StaticWallmark(const wm_shader& S, const Fvector& P, float s, CDB::TRI* T, Fvector* V) override;
    virtual void clear_static_wallmarks() override;
    virtual void add_SkeletonWallmark(const Fmatrix* xf, IKinematics* obj, IWallMarkArray* pArray, const Fvector& start,
                                      const Fvector& dir, float size, float ttl = 0.f, bool ignore_opt = false) override;
    void add_SkeletonWallmark_impl(const CSkeletonWallmark* wm);
    void add_SkeletonWallmark(intrusive_ptr<CSkeletonWallmark> wm);
    void add_SkeletonWallmark(const Fmatrix* xf, CKinematics* obj, ref_shader& sh, const Fvector& start,
                              const Fvector& dir, float size, float ttl = 0.f, bool ignore_opt = false);

    // ========================================================================
    // IRender_interface - ROS
    // ========================================================================
    virtual IRender_ObjectSpecific* ros_create(IRenderable* parent) override;
    virtual void ros_destroy(IRender_ObjectSpecific*& ROS) override;

    // ========================================================================
    // IRender_interface - Sector/Portal
    // ========================================================================
    virtual IRender_Sector* getSector(int id) override;
    virtual IRenderVisual* getVisual(int id) override;
    virtual IRender_Sector* detectSector(const Fvector& P) override;
    virtual IRender_Target* getTarget() override;

    // ========================================================================
    // IRender_interface - Occlusion
    // ========================================================================
    virtual BOOL occ_visible(vis_data& V) override;
    virtual BOOL occ_visible(Fbox& B) override;
    virtual BOOL occ_visible(sPoly& P) override;

    // ========================================================================
    // IRender_interface - Screenshots
    // ========================================================================
    virtual void Screenshot(ScreenshotMode mode = SM_NORMAL, LPCSTR name = 0) override;
    virtual void Screenshot(ScreenshotMode mode, CMemoryWriter& memory_writer) override;
    virtual void ScreenshotAsyncBegin() override;
    virtual void ScreenshotAsyncEnd(CMemoryWriter& memory_writer) override;

    // ========================================================================
    // IRender_interface - Render mode
    // ========================================================================
    virtual void rmNear() override;
    virtual void rmFar() override;
    virtual void rmNormal() override;
    virtual u32 active_phase() override { return phase; }

    // ========================================================================
    // IRender_interface - Shader compilation
    // ========================================================================
    virtual HRESULT shader_compile(
        LPCSTR name,
        DWORD const* pSrcData,
        UINT SrcDataLen,
        LPCSTR pFunctionName,
        LPCSTR pTarget,
        DWORD Flags,
        void*& result) override;

    // ========================================================================
    // IRender_interface - Particles
    // ========================================================================
    virtual void ExportParticles() override;
    virtual void ImportParticles() override;

    // ========================================================================
    // IRender_interface - Memory
    // ========================================================================
    virtual u32 memory_usage() override { return 0; }

    // ========================================================================
    // IRender_interface - RenderToTarget
    // ========================================================================
    virtual void RenderToTarget(RRT target) override;

    // ========================================================================
    // Sun values (anglobes)
    // ========================================================================
    virtual Fvector GetSunPosition() override;
    virtual Fcolor GetSunColor() override;
    virtual float GetSunIntensity() override;
    virtual bool IsSun() override;

    // ========================================================================
    // Selective Screenshot (antglobes)
    // ========================================================================
    virtual void TakeScreenshot(LPCSTR path, Fvector2 dimensions, DxEncoding encoding = eDXE_A8R8G8B8) override;

protected:
    // Screenshot implementation
    virtual void ScreenshotImpl(ScreenshotMode mode, LPCSTR name, CMemoryWriter* memory_writer) override;

private:
    void r_pmask(bool _1, bool _2, bool _wm = false)
    {
        pmask[0] = _1;
        pmask[1] = _2;
        pmask_wmark = _wm;
    }

    // ========================================================================
    // Level loading helpers
    // ========================================================================
    void LoadBuffers(CStreamReader* fs, BOOL _alternative);
    void LoadVisuals(IReader* fs);
    void LoadSectors(IReader* fs);
    void LoadSWIs(CStreamReader* fs);
    void LoadLights(IReader* fs);

    // ========================================================================
    // Sun cascade shadow maps (Phase 2.15)
    // ========================================================================
    void render_sun();                  // Render all sun cascades
    void render_sun_cascades();         // Render cascade chain
    void render_sun_cascade(u32 cascade_ind);  // Render specific cascade
    void init_sun_cascades();           // Initialize cascade parameters

    // ========================================================================
    // Point light shadow cubes (Phase 2.16)
    // ========================================================================
    Fmatrix m_cubemap_face_matrix;      // Current cubemap face matrix (for shadow rendering)
    Fvector m_cubemap_light_pos;        // Current light position (for distance culling)
    float   m_cubemap_light_range;      // Current light range (for distance culling)

    void RenderLevelVisuals_Cubemap(const Fvector& light_pos, float light_range);  // With frustum + distance culling

    // ========================================================================
    // Spot light shadows (Phase 2.17.2)
    // ========================================================================
    Fmatrix m_spot_view_proj;           // Current spot light view-projection matrix
    Fvector m_spot_light_pos;           // Current spot light position (for distance culling)
    float   m_spot_light_range;         // Current spot light range (for distance culling)

    void RenderLevelVisuals_Spot(const Fvector& light_pos, float light_range);  // With frustum + distance culling

public:
    // ========================================================================
    // Scene Graph helpers (Phase 1) - Public для доступа из vk_shared_stubs
    // ========================================================================
    void add_Static(vkRender_Visual* pVisual, u32 planes);       // Add static visual with full culling
    void add_leafs_Static(vkRender_Visual* pVisual);             // Add visual without additional culling
    void add_Static_Simple(vkRender_Visual* pVisual);            // Add static visual (simple version)

    // ========================================================================
    // Shadow rendering methods (called by RenderTarget)
    // ========================================================================
    xr_vector<VK::SunCascade> m_sun_cascades;  // Sun cascade data (accessed by RenderTarget)

    void render_shadow_geometry(u32 cascade_ind);  // Render shadow caster geometry for cascade
    void render_shadow_geometry(const Fmatrix& viewProj, const Fvector& light_pos, float light_range);  // Generic shadow rendering for spot
    void render_shadow_geometry_cubemap(u32 face_index, const Fmatrix& face_matrix, const Fvector& light_pos, float light_range);
    void RenderLevelVisuals();          // Render level visuals (for shadow map or main scene)

    // ========================================================================
    // HUD and Particle Rendering
    // ========================================================================
    void RenderHUDParticles();          // Render HUD-mode particles (muzzle flashes, etc.)

    // ========================================================================
    // Dynamic visual expansion (hierarchy, particles, skeletons)
    // ========================================================================
    void add_leafs_Dynamic_VK(vkRender_Visual* pVisual);

    // ========================================================================
    // Level buffer access (for visuals using OGF_GCONTAINER)
    // ========================================================================
    VK::CVulkanBuffer* getVB(int id, BOOL _alt = FALSE);
    VK::CVulkanBuffer* getIB(int id, BOOL _alt = FALSE);
    u32                getVB_Stride(int id, BOOL _alt = FALSE);
    FSlideWindowItem*  getSWI(int id);
};

// Global render instance
extern CRender RImplementation;
