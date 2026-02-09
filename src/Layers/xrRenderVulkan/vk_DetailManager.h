// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#pragma once

#include "stdafx.h"
#include "vk_Detail.h"
#include "vk_compute.h"
#include "../xrRender/detailformat.h"
#include "../../xrCore/xrpool.h"

// ============================================================================
// Detail radius variables (defined in vk_console.cpp at global scope)
// ============================================================================
#ifdef DETAIL_RADIUS
extern u32 dm_size;
extern u32 dm_cache1_line;
extern u32 dm_cache_line;
extern u32 dm_cache_size;
extern float dm_fade;
extern u32 dm_current_size;
extern u32 dm_current_cache1_line;
extern u32 dm_current_cache_line;
extern u32 dm_current_cache_size;
extern float dm_current_fade;
#endif
extern float ps_current_detail_density;
extern float ps_current_detail_height;

namespace VK
{

// ============================================================================
// Constants
// ============================================================================
const int dm_max_decompress = 80;
const int dm_cache1_count = 4;
const int dm_max_objects = 64;
const int dm_obj_in_slot = 4;
const float dm_slot_size = DETAIL_SLOT_SIZE;

#ifndef DETAIL_RADIUS
const int dm_size = 86;
const int dm_cache1_line = dm_size * 2 / dm_cache1_count;
const int dm_cache_line = dm_size + 1 + dm_size;
const int dm_cache_size = dm_cache_line * dm_cache_line;
const float dm_fade = float(2 * dm_size) - 0.5f;
#endif

// ============================================================================
// Instance data for GPU
// ============================================================================
struct DetailInstance
{
    // Transform (3x4 matrix = 12 floats = 48 bytes)
    Fvector4 row0;  // (m11, m12, m13, m14)
    Fvector4 row1;  // (m21, m22, m23, m24)
    Fvector4 row2;  // (m31, m32, m33, m34)

    // Lighting (4 floats = 16 bytes)
    Fvector4 color; // (sun, sun, sun, hemi)

    // Total: 64 bytes per instance (cache-line aligned)
};

// ============================================================================
// GPU-driven pipeline: Extended instance data for persistent SSBO
// ============================================================================
struct GpuDetailInstanceExt
{
    Fvector4 row0;          // (m11*s, m12*s, m13*s, tx)
    Fvector4 row1;          // (m21*s, m22*s, m23*s, ty)
    Fvector4 row2;          // (m31*s, m32*s, m33*s, tz)
    Fvector4 color;         // (sun, sun, sun, hemi)
    u32      obj_id;        // Detail object type [0..63], 0xFFFFFFFF=dead
    float    base_scale;    // Original scale before fade
    float    bv_radius;     // Bounding sphere radius of object type
    u32      _pad;          // Alignment to 80 bytes
    // Total: 80 bytes per instance
};

// GPU-driven pipeline: Push constants for compute cull shader
struct DetailCullConstants
{
    Fmatrix  viewProj;          // 64 bytes
    Fvector4 frustumPlanes[6];  // 96 bytes
    Fvector4 cameraPos;         // 16 bytes (xyz=pos, w=unused)
    Fvector4 fadeParams;        // 16 bytes (fadeStartSq, fadeLimitSq, fadeRangeSq, time)
    // uvec4 counts passed separately since Vulkan push constant limit is 128-256 bytes
    // We split: 192 bytes in push constants, counts in a small UBO or separate push range
};

// GPU-driven pipeline: Per-dispatch params (small enough for second push constant range)
struct DetailCullCounts
{
    u32 totalInstances;
    u32 numObjTypes;
    u32 _pad0;
    u32 _pad1;
};

// ============================================================================
// GPU grass generation: push constants (208 bytes)
// ============================================================================
struct DetailGenPushConstants
{
    Fmatrix  viewProj;          // 64 bytes
    Fvector4 frustumPlanes[6];  // 96 bytes
    Fvector4 cameraPos;         // 16 bytes (xyz=pos, w=time)
    Fvector4 fadeParams;        // 16 bytes (fadeStartSq, fadeLimitSq, fadeRangeSq, density)
    int      slotMinSX;         // 4 bytes  — slot range min X (world slot coords)
    int      slotMinSZ;         // 4 bytes
    int      slotCountX;        // 4 bytes
    int      slotCountZ;        // 4 bytes
    // Total: 208 bytes
};

// GPU grass generation: UBO params (64 bytes, binding 7)
struct DetailGenUBO
{
    float hmOriginX, hmOriginZ; // Heightmap world origin
    float hmInvScaleX, hmInvScaleZ; // 1 / (worldSize / hmResolution) for UV
    u32   totalPositions;       // Total dispatch threads
    u32   numObjTypes;          // Number of detail object types
    u32   outputCapacity;       // GPU_OUTPUT_CAPACITY
    u32   posPerSlot;           // Positions per slot (d_size+1)^2
    float dtOffsX, dtOffsZ;     // dtH.offs_x, dtH.offs_z (as float)
    float dtSizeX, dtSizeZ;     // dtH.size_x, dtH.size_z (as float)
    float slotSize;             // DETAIL_SLOT_SIZE (2.0)
    float detailHeight;         // ps_current_detail_height
    u32   hmWidth, hmHeight;    // Heightmap dimensions
};

// GPU grass generation: packed slot data for SSBO (32 bytes per slot)
struct GpuSlotPacked
{
    float y_base;       // DetailSlot::r_ybase()
    float y_height;     // DetailSlot::r_yheight()
    u32   ids;          // id0:8 | id1:8 | id2:8 | id3:8
    u32   lighting;     // c_dir:16 | c_hemi:16
    u32   palette0;     // obj0: a0:8|a1:8|a2:8|a3:8
    u32   palette1;     // obj1
    u32   palette2;     // obj2
    u32   palette3;     // obj3
};

// GPU grass generation: per-object-type info (16 bytes)
struct GpuDetailObjInfo
{
    float minScale;
    float maxScale;
    float bvRadius;
    u32   flags;        // DO_NO_WAVING etc.
};

// ============================================================================
// CDetailManager - Grass/debris rendering system
// Vulkan port of CDetailManager from DetailManager.h
// ============================================================================
class CDetailManager
{
public:
    // ========================================================================
    // Slot system (same as original)
    // ========================================================================
    struct SlotItem
    {
        float       scale;
        float       scale_calculated;
        Fmatrix     mRotY;
        u32         vis_ID;         // Animation type: 0=still, 1=wave1, 2=wave2
        float       c_hemi;         // Hemispheric lighting
        float       c_sun;          // Sun lighting
        float       distance;       // Distance to camera
        Fvector     position;
        Fvector     normal;
        float       alpha;          // Current alpha
        float       alpha_target;   // Target alpha (for fade)
        u32         gpu_instance_id; // Index into persistent GPU SSBO (0xFFFFFFFF=invalid)
    };

    DEFINE_VECTOR(SlotItem*, SlotItemVec, SlotItemVecIt);

    struct SlotPart
    {
        u32         id;             // Object ID
        SlotItemVec items;          // List of instances
        SlotItemVec r_items[3];     // Render lists [still, wave1, wave2]
    };

    enum SlotType
    {
        stReady = 0,                // Ready to use
        stPending,                  // Pending decompression
        stFORCEDWORD = 0xffffffff
    };

    struct Slot
    {
        struct
        {
            u32 empty : 1;
            u32 type : 1;
            u32 frame : 30;
        };

        int         sx, sz;         // Slot coordinates
        vis_data    vis;            // Visibility data
        SlotPart    G[dm_obj_in_slot];
        bool        hidden;

        Slot()
        {
            frame = 0;
            empty = 1;
            type = stReady;
            sx = sz = 0;
            vis.clear();
            hidden = false;
        }
    };

    struct CacheSlot1
    {
        u32         empty;
        vis_data    vis;
        Slot**      slots[dm_cache1_count * dm_cache1_count];

        CacheSlot1()
        {
            empty = 1;
            vis.clear();
        }
    };

    // ========================================================================
    // Wind animation
    // ========================================================================
    struct SSwingValue
    {
        float rot1;
        float rot2;
        float amp1;
        float amp2;
        float speed;

        void lerp(const SSwingValue& v1, const SSwingValue& v2, float factor);
    };

    // ========================================================================
    // Types
    // ========================================================================
    typedef svector<VK::CDetail*, dm_max_objects> DetailVec;
    typedef DetailVec::iterator DetailIt;
    typedef poolSS<SlotItem, 4096> PSS;

public:
    // ========================================================================
    // Public members
    // ========================================================================
    float           fade_distance;
    Fvector         light_position;

    // File data
    IReader*        dtFS;               // level.details file stream
    DetailHeader    dtH;                // Header
    DetailSlot*     dtSlots;            // Slot array (pointer into VFS)
    DetailSlot      DS_empty;           // Empty slot template

    // Objects
    DetailVec       objects;            // Detail object models

    // Cache system
#ifdef DETAIL_RADIUS
    CacheSlot1**    cache_level1;
    Slot***         cache;
    svector<Slot*, 62001 * 2> cache_task;
    Slot*           cache_pool;
#else
    CacheSlot1      cache_level1[dm_cache1_line][dm_cache1_line];
    Slot*           cache[dm_cache_line][dm_cache_line];
    svector<Slot*, dm_cache_size> cache_task;
    Slot            cache_pool[dm_cache_size];
#endif

    int             cache_cx;
    int             cache_cz;
    PSS             poolSI;             // SlotItem memory pool

    // Collision query (for decompression raycasting)
    xrXRC           xrc;

    // Dither pattern
    int             dither[16][16];

    // Wind animation
    SSwingValue     swing_desc[2];      // Min/max swing parameters
    SSwingValue     swing_current;      // Current interpolated values
    float           m_time_rot_1;
    float           m_time_rot_2;
    float           m_time_pos;
    float           m_global_time_old;

    // MT stuff
    xrCriticalSection MT;
    volatile u32    m_frame_calc;
    volatile u32    m_frame_rendered;

private:
    // Vulkan rendering resources
    VkPipeline              m_Pipeline;             // Detail rendering pipeline
    bool                    m_bCreated;

    // Push constants for shaders
    static const u32 MAX_GRASS_INTERACTORS = 4;
    struct DetailConstants
    {
        Fmatrix     mViewProj;      // View-projection matrix          64B
        Fvector4    vWave;          // (freq_x, freq_z, speed, time)   16B
        Fvector4    vWind;          // (dir.x, 0, dir.z, amplitude)    16B
        Fvector4    vConsts;        // (scale, scale, l_aniso, l_amb)  16B
        Fvector4    vInteractors[MAX_GRASS_INTERACTORS]; // xyz=pos, w=radius (0=unused) 64B
    };  // Total: 176 bytes (within 256B push constant limit of modern GPUs)
    DetailConstants m_Constants;

    // ========================================================================
    // GPU-driven pipeline resources
    // ========================================================================
    // Persistent SSBO: all decompressed instances (uploaded once, updated on slot changes)
    CVulkanBuffer*              m_AllInstancesSSBO;     // 50K × 80 bytes = 4.0 MB
    // Compacted visible instances (output of compute cull) — used as VB binding 1
    CVulkanBuffer*              m_VisibleSSBO;          // 50K × 64 bytes = 3.2 MB
    // Indirect draw commands (one per object type)
    CVulkanBuffer*              m_IndirectCmdBuf;       // 64 × 20 bytes = 1.3 KB
    // Atomic counters for stream compaction (one per object type + base offsets)
    CVulkanBuffer*              m_AtomicCounters;       // 128 × 4 bytes = 512 B

    // CPU-side staging for SSBO upload
    xr_vector<GpuDetailInstanceExt> m_StagingInstances;
    xr_vector<u32>              m_GpuFreeList;          // Reusable SSBO slots (dead instance indices)
    u32                         m_TotalGpuInstances;    // Current count in SSBO
    bool                        m_GpuDataDirty;         // Need to re-upload SSBO

    // Compute pipeline for culling
    CVulkanComputePipeline      m_CullPipeline;
    CVulkanComputePipeline      m_FinalizePipeline;
    VkPipelineLayout            m_ComputeLayout;        // Dedicated compute pipeline layout
    VkDescriptorSetLayout       m_ComputeDescLayout;    // Descriptor set layout for compute
    VkDescriptorPool            m_ComputeDescPool;      // Dedicated descriptor pool
    VkDescriptorSet             m_ComputeDescSet;       // Descriptor set (re-written per frame)

    // HZB (Hierarchical Z-Buffer) for occlusion culling
    VkImage                     m_HZBImage;
    VkDeviceMemory              m_HZBMemory;
    VkImageView                 m_HZBView;              // View for full mip chain
    xr_vector<VkImageView>      m_HZBMipViews;          // Per-mip views
    VkSampler                   m_HZBSampler;
    u32                         m_HZBWidth;
    u32                         m_HZBHeight;
    u32                         m_HZBMipLevels;
    CVulkanComputePipeline      m_HZBBuildPipeline;
    VkPipelineLayout            m_HZBBuildLayout;
    VkDescriptorSetLayout       m_HZBBuildDescLayout;
    VkDescriptorPool            m_HZBBuildDescPool;

    // GPU path enable toggle
    bool                        m_bGpuDrivenEnabled;

    // Per-object-type base offsets for indirect draw (computed after finalize)
    // Each obj_type gets a contiguous section of m_VisibleSSBO
    // Max offset per obj = totalInstances / numObjTypes (approximation)
    static const u32            GPU_MAX_INSTANCES = 200000;
    static const u32            GPU_MAX_OBJ_TYPES = 64;
    // Output buffer: 1.5M instances (~96MB VRAM). With ~25 types,
    // each type gets ~60K section — enough to avoid overflow-induced flickering.
    static const u32            GPU_OUTPUT_CAPACITY = 1500000;

    // ========================================================================
    // GPU grass generation (replaces CPU cache + cull with procedural GPU gen)
    // ========================================================================
    bool                        m_bGpuGenerationEnabled;

    // Heightmap (R32F, baked from collision geometry)
    VkImage                     m_HeightmapImage;
    VkDeviceMemory              m_HeightmapMemory;  // Actually VmaAllocation
    VkImageView                 m_HeightmapView;
    VkSampler                   m_HeightmapSampler;
    u32                         m_HeightmapW, m_HeightmapH;
    float                       m_HMOriginX, m_HMOriginZ;
    float                       m_HMWorldSizeX, m_HMWorldSizeZ;

    // Slot data SSBO (entire level's DetailSlot array, packed)
    CVulkanBuffer*              m_SlotDataSSBO;
    u32                         m_TotalSlots;

    // Object info SSBO (per object type)
    CVulkanBuffer*              m_ObjInfoSSBO;

    // Generation UBO (per-frame params)
    CVulkanBuffer*              m_GenUBO;

    // Generation compute pipeline
    CVulkanComputePipeline      m_GenPipeline;
    VkPipelineLayout            m_GenPipelineLayout;
    VkDescriptorSetLayout       m_GenDescLayout;
    VkDescriptorPool            m_GenDescPool;
    VkDescriptorSet             m_GenDescSet;

public:
    CDetailManager();
    virtual ~CDetailManager();

    // ========================================================================
    // Main API
    // ========================================================================
    void Load();                        // Load from level.details
    void Unload();                      // Unload all data
    void Render();                      // Render all visible details

    // ========================================================================
    // Cache management
    // ========================================================================
    DetailSlot& QueryDB(int sx, int sz);
    void cache_Initialize();
    void cache_Update(int sx, int sz, Fvector& view, int limit);
    void cache_Task(int gx, int gz, Slot* D);
    Slot* cache_Query(int sx, int sz);
    void cache_Decompress(Slot* D);
    BOOL cache_Validate();

    // Cache grid ↔ world coordinate conversion
    int cg2w_X(int x) { return cache_cx - dm_size + x; }
    int cg2w_Z(int z) { return cache_cz - dm_size + (dm_cache_line - 1 - z); }
    int w2cg_X(int x) { return x - cache_cx + dm_size; }
    int w2cg_Z(int z) { return cache_cz - dm_size + (dm_cache_line - 1 - z); }

private:
    // ========================================================================
    // Vulkan-specific helpers
    // ========================================================================
    void CreatePipeline();              // Create detail rendering pipeline
    void DestroyPipeline();             // Destroy pipeline
    void UpdateWindAnimation();         // Update wind constants

    // GPU-driven pipeline helpers (legacy CPU cache path)
    void CreateGpuBuffers();            // Create SSBO, indirect, atomic buffers
    void DestroyGpuBuffers();           // Destroy GPU-driven buffers
    void CreateComputePipeline();       // Create compute cull + finalize pipelines
    void DestroyComputePipeline();      // Destroy compute pipelines
    void CreateHZB();                   // Create HZB texture + build pipeline
    void DestroyHZB();                  // Destroy HZB resources
    void UploadStagingToSSBO();         // Upload dirty staging data to GPU
    void RenderGpuDriven();             // GPU-driven render path (legacy)
    void BuildHZB(VkCommandBuffer cmd); // Dispatch HZB build passes
    void ExtractFrustumPlanes(const Fmatrix& viewProj, Fvector4 planes[6]);

    // GPU grass generation helpers (new: zero pop-in procedural path)
    void BakeHeightmap();               // Rasterize collision tris → R32F heightmap
    void UploadSlotData();              // Pack DetailSlot[] → GPU SSBO
    void UploadObjInfo();               // Pack object params → GPU SSBO
    void CreateGpuGenPipeline();        // Create generation compute pipeline
    void DestroyGpuGenPipeline();       // Destroy generation resources
    void RenderGpuGenerated();          // GPU procedural generation + render path
};

// Free function for dither matrix generation (from DX11)
void bwdithermap(int levels, int magic[16][16]);

} // namespace VK
