// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#include "stdafx.h"
#include "rvk.h"
#include "vk_Visual.h"
#include "../xrRender/FVF.h"
#define FBasicVisualH
#define dxRender_Visual vkRender_Visual
#include "../xrRender/SkeletonCustom.h"
#undef dxRender_Visual
#undef FBasicVisualH
#include "vk_buffer.h"
#include "vk_buffer_pool.h"
#include "vk_ModelPool.h"
#include "vk_sector.h"      // vkCSector, vkCPortal, vkPortalTraverser
#include "vk_d3d_compat.h"  // D3D9 structures without d3d9.lib dependency
#include "vk_WallmarksEngine.h"  // Wallmark engine
#include "vk_shader.h"      // Vulkan shader system
#include "vk_DetailManager.h"    // VK::CDetailManager
#include "3DFluid/vk3DFluidVolume.h"  // 3D Fluid system (Phase 0)
#include "3DFluid/vk3DFluidManager.h" // 3D Fluid manager (Phase 0)
#include "HW_Vulkan.h"                // VulkanHW (for vkDeviceWaitIdle)

using VK::g_FluidManager;

// Engine includes
#include "../../xrEngine/x_ray.h"
#include "../../xrEngine/xrLevel.h"
#include "../../xrEngine/IGame_Persistent.h"
#include "../../xrEngine/IGame_Level.h"  // g_pGameLevel
#include "../../xrEngine/fmesh.h"         // ogf_header
#include "../../xrCore/stream_reader.h"

// ============================================================================
// Level Loading
// ============================================================================
void CRender::level_Load(IReader* fs)
{
    R_ASSERT(0 != g_pGameLevel);
    R_ASSERT(!b_loaded);

    Msg("[Vulkan] CRender::level_Load() started");

    // Begin loading
    pApp->LoadBegin();

    IReader* chunk;

    // ========================================================================
    // Load shaders
    // ========================================================================
    g_pGamePersistent->LoadTitle();
    {
        chunk = fs->open_chunk(fsL_SHADERS);
        R_ASSERT2(chunk, "Level doesn't built correctly - no shaders chunk");

        u32 count = chunk->r_u32();
        Msg("[Vulkan] Loading %d level shaders", count);

        // Safety: Verify shader manager is initialized
        if (!g_VulkanShaderManager) {
            Msg("![Vulkan] CRITICAL: g_VulkanShaderManager not initialized before level_Load()!");
            Msg("![Vulkan] This indicates CRender::create() was not called or failed.");
            Msg("![Vulkan] Cannot load level shaders - will use nullptr placeholders.");
            // Resize array but leave all entries as nullptr
            Shaders.resize(count, nullptr);
            chunk->close();
            // Continue loading to avoid engine crash, but rendering will fail
            goto skip_shaders;
        }

        // Resize shader array
        Shaders.resize(count);

        // Load shader definitions
        for (u32 i = 0; i < count; i++)
        {
            string512 n_sh, n_tlist;
            LPCSTR n = LPCSTR(chunk->pointer());
            chunk->skip_stringZ();

            // Log first 20 shader entries and any grnd entries for diagnosis
            if (i < 20 || (n[0] && strstr(n, "grnd")))
                Msg("[Vulkan] LevelShader[%d]: '%s'", i, n);

            if (0 == n[0]) {
                // Empty shader name - use default
                Shaders[i] = g_VulkanShaderManager->GetDefaultShader();
                continue;
            }

            // Parse shader name and texture list
            // Format: "shader_name/texture_name"
            xr_strcpy(n_sh, n);
            n_tlist[0] = 0;

            LPSTR delim = strchr(n_sh, '/');
            if (delim) {
                *delim = 0;
                xr_strcpy(n_tlist, delim + 1);
            }

            if (i < 20 || strstr(n_tlist, "grnd"))
                Msg("[Vulkan]   -> shader='%s' texture='%s'", n_sh, n_tlist);

            // Create shader with texture
            Shaders[i] = g_VulkanShaderManager->CreateShader(n_sh, n_tlist);

            if (!Shaders[i]) {
                Msg("![Vulkan] Failed to create shader %d: %s", i, n_sh);
                Shaders[i] = g_VulkanShaderManager->GetDefaultShader();

                // Final check: if even default shader failed, warn and leave as nullptr
                if (!Shaders[i]) {
                    Msg("![Vulkan] CRITICAL: Default shader also failed for shader %d!", i);
                    Msg("![Vulkan] This visual will not render correctly.");
                }
            }
        }
        chunk->close();

        Msg("[Vulkan] Loaded %d shaders successfully", Shaders.size());

        // ====================================================================
        // Texture loading note
        // ====================================================================
        Msg("[Vulkan] Shader textures will load on-demand during rendering");
        Msg("[Vulkan]   - CMaterialManager provides texture caching");
        Msg("[Vulkan]   - Default textures (white/black/normal) ready");

    skip_shaders:
        ; // Empty statement for label
    }

    // ========================================================================
    // Skip GPU-intensive loading on dedicated server (no rendering needed)
    // ========================================================================
    if (!g_dedicated_server)
    {
        // ====================================================================
        // Load geometry buffers (level.geom and level.geomx)
        // ====================================================================
        g_pGamePersistent->LoadTitle();
        {
            // Normal geometry
            CStreamReader* geom = FS.rs_open("$level$", "level.geom");
            R_ASSERT2(geom, "level.geom not found");
            LoadBuffers(geom, FALSE);
            LoadSWIs(geom);
            FS.r_close(geom);

            // Extended/fast-path geometry (for shadow maps, etc.)
            geom = FS.rs_open("$level$", "level.geomx");
            R_ASSERT2(geom, "level.geomx not found");
            LoadBuffers(geom, TRUE);
            FS.r_close(geom);
        }

        // ====================================================================
        // Load visuals
        // ====================================================================
        g_pGamePersistent->LoadTitle();
        {
            chunk = fs->open_chunk(fsL_VISUALS);
            R_ASSERT2(chunk, "Level has no visuals");
            LoadVisuals(chunk);
            chunk->close();
        }

        // ====================================================================
        // Details (grass/debris)
        // ====================================================================
        g_pGamePersistent->LoadTitle();
        if (Details)
        {
            Details->Load();
            Msg("[Vulkan] Details loaded");
        }
    }

    // ========================================================================
    // Load sectors and portals
    // ========================================================================
    g_pGamePersistent->LoadTitle();
    LoadSectors(fs);

    // ========================================================================
    // Create subsystems
    // ========================================================================
    Wallmarks = xr_new<vkCWallmarksEngine>();
    Msg("[Vulkan] Wallmarks engine created");

    // HOM (Hierarchical Occlusion Map)
    if (HOM)
    {
        HOM->Load();
        Msg("[Vulkan] HOM loaded");
    }

    // Lights (sun, static, hemispheric)
    LoadLights(fs);

    // 3D Fluid volumes (Phase 0)
    // Initialize fluid manager first
    g_FluidManager.Initialize();
    // Then load volumes from level
    Load3DFluid();

    // End loading
    pApp->LoadEnd();

    // Sanity-clear render lists (prevent stale data on level reload)
    lstLODs.clear();
    lstLODgroups.clear();
    mapLOD.clear();
    mapWater.clear();

    // Signal loaded
    b_loaded = TRUE;

    Msg("[Vulkan] level_Load() complete: %d visuals, %d sectors, %d portals",
        Visuals.size(), Sectors.size(), Portals.size());
}

// ============================================================================
// Level Unloading
// ============================================================================
void CRender::level_Unload()
{
    if (0 == g_pGameLevel) return;
    if (!b_loaded) return;

    Msg("[Vulkan] CRender::level_Unload()");

    // ========================================================================
    // GPU Sync — wait for all in-flight commands to finish before destroying
    // any GPU resources. Without this, VMA/driver heap corrupts because the
    // GPU is still reading buffers we are about to free.
    // ========================================================================
    if (VulkanHW.m_Device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(VulkanHW.m_Device);

    // ========================================================================
    // 3D Fluid Manager (Phase 0)
    // ========================================================================
    g_FluidManager.Destroy();

    // ========================================================================
    // Details (grass/debris)
    // ========================================================================
    if (Details)
    {
        Details->Unload();
        Msg("[Vulkan] Details unloaded");
    }

    // ========================================================================
    // Wallmarks
    // ========================================================================
    if (Wallmarks) {
        xr_delete(Wallmarks);
        Wallmarks = nullptr;
    }

    // ========================================================================
    // Sectors/Portals
    // ========================================================================
    if (rmPortals) {
        xr_delete(rmPortals);
        rmPortals = nullptr;
    }
    pLastSector = nullptr;
    vLastCameraPos.set(0, 0, 0);

    for (u32 i = 0; i < Sectors.size(); i++) {
        vkCSector* S = (vkCSector*)Sectors[i];
        xr_delete(S);
    }
    Sectors.clear();

    for (u32 i = 0; i < Portals.size(); i++) {
        vkCPortal* P = (vkCPortal*)Portals[i];
        xr_delete(P);
    }
    Portals.clear();

    // ========================================================================
    // Visuals
    // ========================================================================
    for (u32 i = 0; i < Visuals.size(); i++)
    {
        if (Visuals[i]) {
            vkRender_Visual* V = static_cast<vkRender_Visual*>(Visuals[i]);
            V->Release();
            xr_delete(V);
        }
    }
    Visuals.clear();

    // ========================================================================
    // Sliding Window Items
    // ========================================================================
    for (u32 i = 0; i < SWIs.size(); i++) {
        if (SWIs[i].sw) {
            xr_free(SWIs[i].sw);
            SWIs[i].sw = nullptr;
        }
    }
    SWIs.clear();

    // ========================================================================
    // Shaders (Phase 2.32)
    // ========================================================================
    // NOTE: Shaders themselves are managed by g_VulkanShaderManager
    // We just clear the references here
    Shaders.clear();

    // ========================================================================
    // Vertex/Index buffers
    // ========================================================================
    for (u32 i = 0; i < nVB.size(); i++) {
        if (nVB[i]) {
            nVB[i]->Destroy();
            xr_delete(nVB[i]);
        }
    }
    nVB.clear();
    nVB_Strides.clear();

    for (u32 i = 0; i < xVB.size(); i++) {
        if (xVB[i]) {
            xVB[i]->Destroy();
            xr_delete(xVB[i]);
        }
    }
    xVB.clear();
    xVB_Strides.clear();

    for (u32 i = 0; i < nIB.size(); i++) {
        if (nIB[i]) {
            nIB[i]->Destroy();
            xr_delete(nIB[i]);
        }
    }
    nIB.clear();

    for (u32 i = 0; i < xIB.size(); i++) {
        if (xIB[i]) {
            xIB[i]->Destroy();
            xr_delete(xIB[i]);
        }
    }
    xIB.clear();

    // Clear model pool if requested
    if (Models) {
        Models->ClearPool(true);
    }

    b_loaded = FALSE;

    Msg("[Vulkan] level_Unload() complete");
}

// ============================================================================
// LoadBuffers - Load vertex and index buffers from level.geom
// ============================================================================
void CRender::LoadBuffers(CStreamReader* base_fs, BOOL _alternative)
{
    R_ASSERT2(base_fs, "Could not load geometry - file not found");

    xr_vector<VK::CVulkanBuffer*>& _VB = _alternative ? xVB : nVB;
    xr_vector<VK::CVulkanBuffer*>& _IB = _alternative ? xIB : nIB;
    xr_vector<u32>& _Strides = _alternative ? xVB_Strides : nVB_Strides;

    Msg("[Vulkan] Loading %s geometry buffers", _alternative ? "extended" : "normal");

    // ========================================================================
    // Load vertex buffers
    // ========================================================================
    {
        CStreamReader* fs = base_fs->open_chunk(fsL_VB);
        R_ASSERT2(fs, "Could not load geometry - fsL_VB chunk not found");

        u32 count = fs->r_u32();
        _VB.resize(count);
        _Strides.resize(count);

        Msg("[Vulkan] Loading %d vertex buffers", count);

        // D3D vertex declaration buffer
        const u32 bufferSize = (MAXD3DDECLLENGTH + 1) * sizeof(D3DVERTEXELEMENT9);
        D3DVERTEXELEMENT9* dcl = (D3DVERTEXELEMENT9*)_alloca(bufferSize);

        for (u32 i = 0; i < count; i++)
        {
            // Read vertex declaration
            fs->r(dcl, bufferSize);
            fs->advance(-(int)bufferSize);

            u32 dcl_len = VK_GetDeclLength(dcl) + 1;
            fs->advance(dcl_len * sizeof(D3DVERTEXELEMENT9));

            // Read vertex count and calculate size
            u32 vCount = fs->r_u32();
            u32 vSize = VK_GetDeclVertexSize(dcl, 0);
            _Strides[i] = vSize;

            Msg("  VB[%d]: %d verts, stride=%d, size=%d KB",
                i, vCount, vSize, (vCount * vSize) / 1024);

            // Log vertex declaration for non-standard strides
            if (vSize != 32) {
                for (u32 e = 0; e < dcl_len - 1; e++) {
                    Msg("    dcl[%d]: stream=%d offset=%d type=%d usage=%d usageIdx=%d",
                        e, dcl[e].Stream, dcl[e].Offset, dcl[e].Type, dcl[e].Usage, dcl[e].UsageIndex);
                }
            }

            // Read vertex data
            BYTE* pData = xr_alloc<BYTE>(vCount * vSize);
            fs->r(pData, vCount * vSize);

            // Create Vulkan vertex buffer
            _VB[i] = xr_new<VK::CVulkanBuffer>();
            _VB[i]->Create(
                vCount * vSize,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
            );
            _VB[i]->Upload(pData, vCount * vSize);

            // Register with buffer pool (Phase 2.23) - allow visuals to find buffers by ID
            // Only register normal (geom) buffers - NOT alternative (geomx) which would overwrite!
            // geomx buffers (stride=12, position-only) are for fast-path/shadows only.
            // Tree visuals and normal geometry use the geom pool (stride=32 with UV/normals).
            if (VK::g_BufferPool && !_alternative) {
                VK::g_BufferPool->RegisterVertexBuffer(i, _VB[i], vSize);
            }

            xr_free(pData);
        }
        fs->close();
    }

    // ========================================================================
    // Load index buffers
    // ========================================================================
    {
        CStreamReader* fs = base_fs->open_chunk(fsL_IB);
        R_ASSERT2(fs, "Could not load geometry - fsL_IB chunk not found");

        u32 count = fs->r_u32();
        _IB.resize(count);

        Msg("[Vulkan] Loading %d index buffers", count);

        for (u32 i = 0; i < count; i++)
        {
            u32 iCount = fs->r_u32();
            u32 iSize = iCount * sizeof(u16);  // 16-bit indices

            Msg("  IB[%d]: %d indices, size=%d KB", i, iCount, iSize / 1024);

            // Read index data
            BYTE* pData = xr_alloc<BYTE>(iSize);
            fs->r(pData, iSize);

            // Create Vulkan index buffer
            _IB[i] = xr_new<VK::CVulkanBuffer>();
            _IB[i]->Create(
                iSize,
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
            );
            _IB[i]->Upload(pData, iSize);

            // Register with buffer pool (Phase 2.23) - allow visuals to find buffers by ID
            // Only register normal (geom) buffers - NOT alternative (geomx)
            if (VK::g_BufferPool && !_alternative) {
                VK::g_BufferPool->RegisterIndexBuffer(i, _IB[i], VK_INDEX_TYPE_UINT16);
            }

            xr_free(pData);
        }
        fs->close();
    }
}

// ============================================================================
// LoadVisuals - Load visual objects from level
// ============================================================================
void CRender::LoadVisuals(IReader* fs)
{
    IReader* chunk = nullptr;
    u32 index = 0;
    ogf_header H;

    Msg("[Vulkan] Loading visuals...");

    while ((chunk = fs->open_chunk(index)) != nullptr)
    {
        // Read OGF header to determine visual type
        chunk->r_chunk_safe(OGF_HEADER, &H, sizeof(H));

        // Create visual of appropriate type
        vkRender_Visual* V = vkVisual_Create(H.type);
        if (V)
        {
            V->Load(nullptr, chunk, 0);
            Visuals.push_back(V);
        }
        else
        {
            // Create dummy visual instead of nullptr to prevent crashes
            Msg("! [Vulkan] Unknown visual type %d at index %d - using dummy visual", H.type, index);
            vkRender_Visual* dummy = vkVisual_CreateDummy();
            Visuals.push_back(dummy);
        }

        chunk->close();
        index++;
    }

    Msg("[Vulkan] Loaded %d visuals", index);
}

// ============================================================================
// LoadSectors - Load sectors and portals
// ============================================================================
void CRender::LoadSectors(IReader* fs)
{
    Msg("[Vulkan] Loading sectors and portals...");

    // ========================================================================
    // Portal structure (from level file)
    // ========================================================================
    struct b_portal
    {
        u16 sector_front;
        u16 sector_back;
        svector<Fvector, 6> vertices;
    };

    // ========================================================================
    // Allocate portals
    // ========================================================================
    u32 size = fs->find_chunk(fsL_PORTALS);
    R_ASSERT(0 == size % sizeof(b_portal));
    u32 portal_count = size / sizeof(b_portal);

    Portals.resize(portal_count);
    for (u32 c = 0; c < portal_count; c++) {
        Portals[c] = xr_new<vkCPortal>();
    }

    // ========================================================================
    // Load sectors
    // ========================================================================
    IReader* S = fs->open_chunk(fsL_SECTORS);
    if (S)
    {
        u32 sector_index = 0;
        for (;;)
        {
            IReader* P = S->open_chunk(sector_index);
            if (!P) break;

            vkCSector* sector = xr_new<vkCSector>();
            sector->load(*P);
            Sectors.push_back(sector);

            P->close();
            sector_index++;
        }
        S->close();
    }

    // ========================================================================
    // Load portal geometry and setup connections
    // ========================================================================
    if (portal_count > 0)
    {
        CDB::Collector CL;
        fs->find_chunk(fsL_PORTALS);

        for (u32 i = 0; i < portal_count; i++)
        {
            b_portal P;
            fs->r(&P, sizeof(P));

            // Get connected sectors (may be nullptr if level data is corrupted)
            vkCSector* sector_front = (vkCSector*)getSector(P.sector_front);
            vkCSector* sector_back = (vkCSector*)getSector(P.sector_back);

            // Validate sectors before portal setup
            if (!sector_front || !sector_back) {
                Msg("![Vulkan] Portal %d has invalid sectors (front=%d back=%d): front=%p back=%p",
                    i, P.sector_front, P.sector_back, sector_front, sector_back);
                // Continue anyway - traverse() will skip null sectors
            }

            // Setup portal with connected sectors
            vkCPortal* portal = (vkCPortal*)Portals[i];
            portal->Setup(
                P.vertices.begin(),
                P.vertices.size(),
                sector_front,
                sector_back
            );

            // Add portal triangles to collision model
            for (u32 j = 2; j < P.vertices.size(); j++) {
                CL.add_face_packed_D(
                    P.vertices[0], P.vertices[j - 1], P.vertices[j],
                    u32(i)
                );
            }
        }

        // Ensure minimum geometry for CDB model
        if (CL.getTS() < 2) {
            Fvector v1, v2, v3;
            v1.set(-20000.f, -20000.f, -20000.f);
            v2.set(-20001.f, -20001.f, -20001.f);
            v3.set(-20002.f, -20002.f, -20002.f);
            CL.add_face_packed_D(v1, v2, v3, 0);
        }

        // Build portal collision model
        rmPortals = xr_new<CDB::MODEL>();
        rmPortals->build(CL.getV(), int(CL.getVS()), CL.getT(), int(CL.getTS()));
    }
    else
    {
        rmPortals = nullptr;
    }

    pLastSector = nullptr;
    vLastCameraPos.set(0, 0, 0);

    Msg("[Vulkan] Loaded %d sectors, %d portals", Sectors.size(), Portals.size());
}

// ============================================================================
// Portal/Sector access helpers
// ============================================================================
IRender_Portal* CRender::getPortal(int id)
{
    if (id >= 0 && id < (int)Portals.size())
        return Portals[id];
    return nullptr;
}

IRender_Sector* CRender::getSectorByIndex(int id)
{
    if (id >= 0 && id < (int)Sectors.size())
        return Sectors[id];
    return nullptr;
}

// ============================================================================
// LoadLights - Load dynamic lights, sun, and hemispheric lighting
// ============================================================================
void CRender::LoadLights(IReader* fs)
{
    // Load dynamic lights (sun, static point/spot lights)
    Lights.Load(fs);
    Msg("[Vulkan] Dynamic lights loaded");

    // Load hemispheric lights from build.lights
    Lights.LoadHemi();
    Msg("[Vulkan] Hemispheric lights loaded");
}

// ============================================================================
// LoadSWIs - Load Sliding Window Items (for LOD meshes)
// ============================================================================
void CRender::LoadSWIs(CStreamReader* base_fs)
{
    if (!base_fs->find_chunk(fsL_SWIS))
        return;

    CStreamReader* fs = base_fs->open_chunk(fsL_SWIS);
    u32 item_count = fs->r_u32();

    Msg("[Vulkan] Loading %d sliding window items", item_count);

    // Clear existing SWIs
    for (auto& swi : SWIs) {
        if (swi.sw) {
            xr_free(swi.sw);
            swi.sw = nullptr;
        }
    }
    SWIs.clear();

    SWIs.resize(item_count);
    for (u32 c = 0; c < item_count; c++)
    {
        FSlideWindowItem& swi = SWIs[c];
        swi.reserved[0] = fs->r_u32();
        swi.reserved[1] = fs->r_u32();
        swi.reserved[2] = fs->r_u32();
        swi.reserved[3] = fs->r_u32();
        swi.count = fs->r_u32();

        swi.sw = xr_alloc<FSlideWindow>(swi.count);
        fs->r(swi.sw, sizeof(FSlideWindow) * swi.count);
    }

    fs->close();
}

// ============================================================================
// Buffer access helpers
// ============================================================================
VK::CVulkanBuffer* CRender::getVB(int id, BOOL _alt)
{
    xr_vector<VK::CVulkanBuffer*>& _VB = _alt ? xVB : nVB;
    if (id >= 0 && id < (int)_VB.size())
        return _VB[id];
    return nullptr;
}

VK::CVulkanBuffer* CRender::getIB(int id, BOOL _alt)
{
    xr_vector<VK::CVulkanBuffer*>& _IB = _alt ? xIB : nIB;
    if (id >= 0 && id < (int)_IB.size())
        return _IB[id];
    return nullptr;
}

u32 CRender::getVB_Stride(int id, BOOL _alt)
{
    xr_vector<u32>& _Strides = _alt ? xVB_Strides : nVB_Strides;
    if (id >= 0 && id < (int)_Strides.size())
        return _Strides[id];
    return 0;
}

FSlideWindowItem* CRender::getSWI(int id)
{
    if (id >= 0 && id < (int)SWIs.size())
        return &SWIs[id];
    return nullptr;
}

// ============================================================================
// 3D Fluid Volume Loading (Phase 0)
// ============================================================================
void CRender::Load3DFluid()
{
    // Проверяем включена ли volumetric smoke
    if (!o.volumetricfog) {
        Msg("[Vulkan] Volumetric fog disabled, skipping 3D fluid volumes");
        return;
    }

    // Проверяем существует ли level.fog_vol
    string_path fn_game;
    if (!FS.exist(fn_game, "$level$", "level.fog_vol")) {
        Msg("[Vulkan] level.fog_vol not found, skipping 3D fluid volumes");
        return;
    }

    Msg("[Vulkan] Loading 3D fluid volumes from %s", fn_game);

    // Открываем файл
    IReader* F = FS.r_open(fn_game);
    if (!F) {
        Msg("![Vulkan] Failed to open level.fog_vol");
        return;
    }

    // Читаем количество volumes
    u32 volumeCount = F->r_u32();
    Msg("[Vulkan] Found %d fluid volumes", volumeCount);

    // Загружаем каждый volume
    for (u32 i = 0; i < volumeCount; i++)
    {
        // Создаём visual
        VK::vk3DFluidVolume* volume = xr_new<VK::vk3DFluidVolume>();

        // Загружаем данные
        string64 volumeName;
        sprintf_s(volumeName, sizeof(volumeName), "fluid_volume_%d", i);
        volume->Load(volumeName, F, 0);

        // Добавляем в соответствующий сектор
        // TODO: Читать sector ID из файла и добавлять в правильный сектор
        // Пока добавим в первый сектор для теста
        if (!Sectors.empty()) {
            vkCSector* sector = (vkCSector*)Sectors[0];
            if (sector) {
                // Добавляем как child в sector root
                // TODO: Правильная интеграция в sector hierarchy
            }
        }

        Msg("[Vulkan] Fluid volume %d loaded", i);
    }

    FS.r_close(F);

    Msg("[Vulkan] 3D fluid volumes loading complete");
}
