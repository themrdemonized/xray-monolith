// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#pragma once

#include "stdafx.h"
#include "vk_Detail.h"
#include "../xrRender/detailformat.h"
#include "../../xrCore/xrpool.h"

namespace VK
{

// ============================================================================
// Constants
// ============================================================================
const int dm_max_decompress = 7;
const int dm_cache1_count = 4;
const int dm_max_objects = 64;
const int dm_obj_in_slot = 4;
const float dm_slot_size = DETAIL_SLOT_SIZE;

#ifdef DETAIL_RADIUS
// Variable detail radius support
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
extern float ps_current_detail_density;
extern float ps_current_detail_height;
#else
const int dm_size = 24;
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
// CDetailInstanceBuffer - GPU instance buffer manager
// ============================================================================
class CDetailInstanceBuffer
{
private:
    VK::CVulkanBuffer*  m_Buffer;       // GPU buffer
    DetailInstance*     m_Mapped;       // CPU-mapped memory
    u32                 m_Capacity;     // Max instances
    u32                 m_Count;        // Current count

public:
    CDetailInstanceBuffer();
    ~CDetailInstanceBuffer();

    void Create(u32 capacity);
    void Destroy();

    void BeginUpdate();
    void AddInstance(const Fmatrix& transform, float sun, float hemi, float scale);
    u32 EndUpdate();  // Returns instance count

    VK::CVulkanBuffer* GetBuffer() const { return m_Buffer; }
    u32 GetCount() const { return m_Count; }
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
    typedef xr_vector<xr_vector<SlotItemVec*>> vis_list;
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

    // Visibility lists (per animation type)
    vis_list        m_visibles[3];      // 0=still, 1=wave1, 2=wave2

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
    CDetailInstanceBuffer   m_InstanceBuffer;       // Shared instance buffer
    VkPipeline              m_Pipeline;             // Detail rendering pipeline
    bool                    m_bCreated;

    // Push constants for shaders
    struct DetailConstants
    {
        Fmatrix     mViewProj;      // View-projection matrix
        Fvector4    vWave;          // (freq_x, freq_z, speed, time)
        Fvector4    vWind;          // (dir.x, 0, dir.z, amplitude)
        Fvector4    vConsts;        // (scale, scale, l_aniso, l_ambient)
    };
    DetailConstants m_Constants;

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

    // ========================================================================
    // Visibility
    // ========================================================================
    void UpdateVisibleM();              // Update visibility (main thread)
    void UpdateVisibleS();              // Update visibility (secondary)
    void details_clear();               // Clear visibility lists

    // ========================================================================
    // MT synchronization
    // ========================================================================
    void MT_CALC();
    IC void MT_SYNC()
    {
        if (m_frame_calc == RDEVICE.dwFrame)
            return;
        MT_CALC();
    }

private:
    // ========================================================================
    // Vulkan-specific helpers
    // ========================================================================
    void CreatePipeline();              // Create detail rendering pipeline
    void DestroyPipeline();             // Destroy pipeline
    void UpdateWindAnimation();         // Update wind constants
    void BuildInstanceData();           // Build instance buffer from visible slots
};

} // namespace VK
