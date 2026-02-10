// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

#pragma once

#include "stdafx.h"
#include "vk_Visual.h"

// Forward declarations
namespace PS
{
    struct SEmitter;
    class CPEDef;
    class CPGDef;
}

// ============================================================================
// vkModelPool - Model instance manager for Vulkan renderer
// Based on CModelPool from xrRender but uses vkRender_Visual
// ============================================================================
class vkModelPool
{
private:
    friend class CRender;

    struct str_pred
    {
        IC bool operator()(const shared_str& x, const shared_str& y) const
        {
            return xr_strcmp(x, y) < 0;
        }
    };

    struct ModelDef
    {
        shared_str          name;
        vkRender_Visual*    model;
        u32                 refs;

        ModelDef() : model(nullptr), refs(0) {}
    };

    typedef xr_multimap<shared_str, vkRender_Visual*, str_pred>  POOL;
    typedef POOL::iterator                                        POOL_IT;
    typedef xr_map<vkRender_Visual*, shared_str>                  REGISTRY;
    typedef REGISTRY::iterator                                    REGISTRY_IT;

private:
    xr_vector<ModelDef>         Models;         // Base/reference models (loaded once)
    xr_vector<vkRender_Visual*> ModelsToDelete; // Deferred deletion queue
    REGISTRY                    Registry;       // Active instances -> name mapping
    POOL                        Pool;           // Inactive/cached instances

    BOOL                        bLogging;
    BOOL                        bForceDiscard;
    BOOL                        bAllowChildrenDuplicate;

    void Destroy();

public:
    vkModelPool();
    virtual ~vkModelPool();

    // ========================================================================
    // Instance management
    // ========================================================================

    // Create visual of specified type (factory)
    vkRender_Visual* Instance_Create(u32 Type);

    // Duplicate existing visual
    vkRender_Visual* Instance_Duplicate(vkRender_Visual* V);

    // Load visual from file
    vkRender_Visual* Instance_Load(LPCSTR N, BOOL allow_register, bool assert_on_fail = true);
    vkRender_Visual* Instance_Load(LPCSTR N, IReader* data, BOOL allow_register);

    // Register visual in base models list
    void Instance_Register(LPCSTR N, vkRender_Visual* V);

    // Find visual by name
    vkRender_Visual* Instance_Find(LPCSTR N);

    // ========================================================================
    // Public API (used by CRender)
    // ========================================================================

    // Create/load model by name
    vkRender_Visual* Create(LPCSTR name, IReader* data = nullptr, bool assert_on_fail = true);

    // Create child visual (for hierarchical models)
    vkRender_Visual* CreateChild(LPCSTR name, IReader* data);

    // Delete visual instance
    void Delete(vkRender_Visual*& V, BOOL bDiscard = FALSE);

    // Fully discard visual (including base)
    void Discard(vkRender_Visual*& V, BOOL b_complete);

    // Internal deletion
    void DeleteInternal(vkRender_Visual*& V, BOOL bDiscard = FALSE);

    // Process deferred deletion queue
    void DeleteQueue();

    // ========================================================================
    // Particle system creation (stubs for now)
    // ========================================================================
    vkRender_Visual* CreatePE(PS::CPEDef* source);
    vkRender_Visual* CreatePG(PS::CPGDef* source);

    // ========================================================================
    // Utility
    // ========================================================================

    // Enable/disable logging
    void Logging(BOOL bEnable) { bLogging = bEnable; }

    // Prefetch models
    void Prefetch();
    void Prefetch_One(LPCSTR N, bool assert_on_fail = true);

    // Check if model exists
    bool Exists(LPCSTR N);

    // Clear pool
    void ClearPool(BOOL b_complete);

    // Debug dump
    void dump();

    // Memory statistics
    void memory_stats(u32& vb_mem_video, u32& vb_mem_system, u32& ib_mem_video, u32& ib_mem_system);
};
