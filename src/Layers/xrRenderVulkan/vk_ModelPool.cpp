// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

#include "stdafx.h"
#include "vk_ModelPool.h"
#include "rvk.h"
#include "../../xrEngine/fmesh.h"  // ogf_header
#include "../../xrEngine/SkeletonMotions.h"  // g_pMotionsContainer

// ============================================================================
// vkModelPool - Constructor/Destructor
// ============================================================================
vkModelPool::vkModelPool()
{
    bLogging = FALSE;
    bForceDiscard = FALSE;
    bAllowChildrenDuplicate = TRUE;

    // Initialize motions container (required for skeletal animations)
    g_pMotionsContainer = xr_new<motions_container>();
}

vkModelPool::~vkModelPool()
{
    Destroy();
    xr_delete(g_pMotionsContainer);
}

void vkModelPool::Destroy()
{
    // Delete all queued models
    DeleteQueue();

    // Delete pool entries
    for (POOL_IT it = Pool.begin(); it != Pool.end(); ++it)
    {
        xr_delete(it->second);
    }
    Pool.clear();

    // Delete base models
    for (u32 i = 0; i < Models.size(); ++i)
    {
        xr_delete(Models[i].model);
    }
    Models.clear();

    Registry.clear();

    // Cleanup motions container
    if (g_pMotionsContainer)
        g_pMotionsContainer->clean(false);
}

// ============================================================================
// Instance Management
// ============================================================================
vkRender_Visual* vkModelPool::Instance_Create(u32 Type)
{
    vkRender_Visual* V = vkVisual_Create(Type);
    return V;
}

vkRender_Visual* vkModelPool::Instance_Duplicate(vkRender_Visual* V)
{
    if (!V) return nullptr;

    // Create new instance of same type
    vkRender_Visual* N = Instance_Create(V->Type);
    if (!N) {
        Msg("![Vulkan] Instance_Duplicate: failed to create instance type %d", V->Type);
        return nullptr;
    }

    // Copy data
    N->Copy(V);

    return N;
}

vkRender_Visual* vkModelPool::Instance_Load(LPCSTR N, BOOL allow_register, bool assert_on_fail)
{
    return Instance_Load(N, nullptr, allow_register);
}

vkRender_Visual* vkModelPool::Instance_Load(LPCSTR N, IReader* data, BOOL allow_register)
{
    // Build filename
    string_path name;
    string_path fn;

    if (!N || !N[0])
    {
        Msg("![Vulkan] Instance_Load: empty name");
        return nullptr;
    }

    // Add extension if missing
    if (0 == strext(N))
    {
        xr_strcpy(name, N);
        xr_strcat(name, ".ogf");
    }
    else
    {
        xr_strcpy(name, N);
    }

    // Convert to lowercase
    strlwr(name);

    // Find file
    IReader* file_data = data;
    bool need_close = false;

    if (!file_data)
    {
        // Try $level$ first, then $game_meshes$
        if (FS.exist(fn, "$level$", name))
        {
            file_data = FS.r_open(fn);
            need_close = true;
        }
        else if (FS.exist(fn, "$game_meshes$", name))
        {
            file_data = FS.r_open(fn);
            need_close = true;
        }
        else
        {
            Msg("![Vulkan] Model not found: %s", name);
            return nullptr;
        }
    }

    // Read header to determine type
    ogf_header H;
    if (!file_data->find_chunk(OGF_HEADER))
    {
        Msg("![Vulkan] Missing OGF_HEADER in %s", name);
        if (need_close) FS.r_close(file_data);
        return nullptr;
    }
    file_data->r(&H, sizeof(H));
    file_data->seek(0); // Rewind for full load

    // Create visual of appropriate type
    vkRender_Visual* V = Instance_Create(H.type);
    if (!V)
    {
        Msg("![Vulkan] Failed to create visual type %u for %s", H.type, name);
        if (need_close) FS.r_close(file_data);
        return nullptr;
    }

    // Load visual data
    V->Load(name, file_data, 0);

    // Register if requested
    if (allow_register)
    {
        Instance_Register(name, V);
    }

    if (need_close)
    {
        FS.r_close(file_data);
    }

    return V;
}

void vkModelPool::Instance_Register(LPCSTR N, vkRender_Visual* V)
{
    // Check if already registered
    for (auto& def : Models)
    {
        if (def.name == N)
        {
            // Already exists
            return;
        }
    }

    // Add to models
    ModelDef def;
    def.name = N;
    def.model = V;
    def.refs = 1;
    Models.push_back(def);
}

vkRender_Visual* vkModelPool::Instance_Find(LPCSTR N)
{
    string_path name;
    xr_strcpy(name, N);

    // Add extension if missing (must match Instance_Load behavior)
    if (0 == strext(N))
        xr_strcat(name, ".ogf");

    strlwr(name);

    for (auto& def : Models)
    {
        if (def.name == name)
        {
            return def.model;
        }
    }

    return nullptr;
}

// ============================================================================
// Public API
// ============================================================================
vkRender_Visual* vkModelPool::Create(LPCSTR name, IReader* data, bool assert_on_fail)
{
    if (!name || !name[0])
    {
        return nullptr;
    }

    string_path low_name;
    xr_strcpy(low_name, name);
    strlwr(low_name);

    // 1. Check pool for cached instance
    POOL_IT it = Pool.find(low_name);
    if (it != Pool.end())
    {
        // Reuse from pool
        vkRender_Visual* V = it->second;
        Pool.erase(it);
        V->Spawn();
        Registry.insert(std::make_pair(V, low_name));
        return V;
    }

    // 2. Find base model
    vkRender_Visual* Base = Instance_Find(low_name);

    // 3. Load if not found
    if (!Base)
    {
        Base = Instance_Load(low_name, data, TRUE);
        if (!Base)
        {
            if (assert_on_fail)
                Msg("![Vulkan] Failed to load model: %s", low_name);
            return nullptr;
        }
    }

    // 4. Duplicate base
    vkRender_Visual* V = Instance_Duplicate(Base);

    // 5. Register instance
    Registry.insert(std::make_pair(V, low_name));

    // 6. Increment reference count
    for (auto& def : Models)
    {
        if (def.name == low_name)
        {
            def.refs++;
            break;
        }
    }

    return V;
}

vkRender_Visual* vkModelPool::CreateChild(LPCSTR name, IReader* data)
{
    if (!data)
    {
        return nullptr;
    }

    string_path low_name;
    xr_strcpy(low_name, name);
    strlwr(low_name);

    // Read header
    ogf_header H;
    if (!data->find_chunk(OGF_HEADER))
    {
        return nullptr;
    }
    data->r(&H, sizeof(H));
    data->seek(0);

    // Create visual
    vkRender_Visual* V = Instance_Create(H.type);
    if (!V)
    {
        return nullptr;
    }

    // Load
    V->Load(low_name, data, 0);

    // Register as active (but not as base model)
    Registry.insert(std::make_pair(V, low_name));

    return V;
}

void vkModelPool::Delete(vkRender_Visual*& V, BOOL bDiscard)
{
    if (!V) return;

    // Add to deletion queue
    ModelsToDelete.push_back(V);

    // Store discard flag (use bForceDiscard as global override)
    if (bDiscard)
    {
        bForceDiscard = TRUE;
    }

    V = nullptr;
}

void vkModelPool::Discard(vkRender_Visual*& V, BOOL b_complete)
{
    DeleteInternal(V, TRUE);
}

void vkModelPool::DeleteInternal(vkRender_Visual*& V, BOOL bDiscard)
{
    if (!V) return;

    // Find in registry
    REGISTRY_IT it = Registry.find(V);
    if (it != Registry.end())
    {
        shared_str name = it->second;
        Registry.erase(it);

        if (bDiscard || bForceDiscard)
        {
            // Full discard - delete immediately
            V->Depart();
            V->Release();
            xr_delete(V);

            // Decrement reference count
            for (auto& def : Models)
            {
                if (def.name == name)
                {
                    def.refs--;
                    if (def.refs == 0)
                    {
                        // Delete base model too
                        xr_delete(def.model);
                        def.model = nullptr;
                    }
                    break;
                }
            }
        }
        else
        {
            // Move to pool for reuse
            V->Depart();
            Pool.insert(std::make_pair(name, V));
        }
    }
    else
    {
        // Not in registry - just delete
        V->Release();
        xr_delete(V);
    }

    V = nullptr;
}

void vkModelPool::DeleteQueue()
{
    for (auto& V : ModelsToDelete)
    {
        DeleteInternal(V, bForceDiscard);
    }
    ModelsToDelete.clear();
    bForceDiscard = FALSE;
}

// ============================================================================
// Particle System Implementation
// ============================================================================

// Particle definitions (need full type for member access)
#include "../xrRender/ParticleEffectDef.h"
#include "../xrRender/ParticleGroup.h"
#include "vk_ParticleEffect.h"
#include "vk_ParticleGroup.h"

vkRender_Visual* vkModelPool::CreatePE(PS::CPEDef* source)
{
    if (!source) {
        Msg("![Vulkan] CreatePE: source definition is nullptr");
        return nullptr;
    }

    vkCParticleEffect* effect = xr_new<vkCParticleEffect>();
    if (!effect->Compile(source)) {
        Msg("![Vulkan] Failed to compile particle effect: %s", source->m_Name.c_str());
        xr_delete(effect);
        return nullptr;
    }

    return effect;
}

vkRender_Visual* vkModelPool::CreatePG(PS::CPGDef* source)
{
    if (!source) {
        Msg("![Vulkan] CreatePG: source definition is nullptr");
        return nullptr;
    }

    vkCParticleGroup* group = xr_new<vkCParticleGroup>();
    if (!group->Compile(source)) {
        Msg("![Vulkan] Failed to compile particle group: %s", source->m_Name.c_str());
        xr_delete(group);
        return nullptr;
    }

    return group;
}

// ============================================================================
// Utility
// ============================================================================
void vkModelPool::Prefetch()
{
    // TODO: Implement model prefetching
    Msg("[Vulkan] vkModelPool::Prefetch() - not implemented");
}

void vkModelPool::Prefetch_One(LPCSTR N, bool assert_on_fail)
{
    // Load into pool without creating instance
    vkRender_Visual* V = Instance_Find(N);
    if (!V)
    {
        V = Instance_Load(N, TRUE, assert_on_fail);
    }
}

bool vkModelPool::Exists(LPCSTR N)
{
    string_path fn;
    string_path name;

    if (0 == strext(N))
    {
        xr_strcpy(name, N);
        xr_strcat(name, ".ogf");
    }
    else
    {
        xr_strcpy(name, N);
    }

    return FS.exist(fn, "$level$", name) || FS.exist(fn, "$game_meshes$", name);
}

void vkModelPool::ClearPool(BOOL b_complete)
{
    // Delete pooled instances
    for (POOL_IT it = Pool.begin(); it != Pool.end(); ++it)
    {
        it->second->Release();
        xr_delete(it->second);
    }
    Pool.clear();

    if (b_complete)
    {
        // Delete base models too
        for (auto& def : Models)
        {
            if (def.model)
            {
                def.model->Release();
                xr_delete(def.model);
            }
        }
        Models.clear();
    }
}

void vkModelPool::dump()
{
    Msg("[Vulkan] === Model Pool Dump ===");
    Msg("[Vulkan] Base models: %u", Models.size());
    for (auto& def : Models)
    {
        Msg("[Vulkan]   %s (refs: %u)", def.name.c_str(), def.refs);
    }
    Msg("[Vulkan] Active instances: %u", Registry.size());
    Msg("[Vulkan] Pooled instances: %u", Pool.size());
    Msg("[Vulkan] Pending deletion: %u", ModelsToDelete.size());
}

void vkModelPool::memory_stats(u32& vb_mem_video, u32& vb_mem_system, u32& ib_mem_video, u32& ib_mem_system)
{
    // TODO: Implement memory statistics
    vb_mem_video = 0;
    vb_mem_system = 0;
    ib_mem_video = 0;
    ib_mem_system = 0;
}
