// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

// Vulkan Wallmarks Engine
// Handles blood decals, bullet holes, and other surface marks
// Replaces the DX11 CWallmarksEngine with Vulkan-native rendering

#pragma once

#include "stdafx.h"
#include "../xrRender/FVF.h"
#include "../xrRender/Shader.h"
#include "../../xrCDB/Frustum.h"
#include "../../xrCDB/xrXRC.h"
#include "../../xrCore/xrPool.h"
#include <vulkan/vulkan.h>

class CKinematics;
class CSkeletonWallmark;

namespace VK {
    class CVulkanBuffer;
    class CVulkanTexture;
}

// ============================================================================
// vkCWallmarksEngine - Vulkan wallmark engine
// ============================================================================
class vkCWallmarksEngine
{
public:
    // Static wallmark - projected geometry stored as FVF::LIT vertices
    struct static_wallmark
    {
        Fsphere bounds;
        xr_vector<FVF::LIT> verts;
        float m_fTimeStart;
        float m_fTimeEnd;

        IC float TimeStart() { return m_fTimeStart; }
        IC float TimeEnd() { return m_fTimeEnd; }
    };

    DEFINE_VECTOR(static_wallmark*, StaticWMVec, StaticWMVecIt);

    // Wallmark slot - groups wallmarks by shader
    struct wm_slot
    {
        ref_shader shader;
        StaticWMVec static_items;

        wm_slot(ref_shader sh)
        {
            shader = sh;
            static_items.reserve(256);
        }
    };

    DEFINE_VECTOR(wm_slot*, WMSlotVec, WMSlotVecIt);

public:
    vkCWallmarksEngine();
    ~vkCWallmarksEngine();

    // Add static wallmark (projected onto level geometry)
    void AddStaticWallmark(CDB::TRI* pTri, const Fvector* pVerts, const Fvector& contact_point,
                           ref_shader hShader, float sz, float ttl = 0.f, bool ignore_opt = false,
                           bool random_rotation = true);

    // Add static wallmark with explicit rotation
    void AddStaticWallmark(CDB::TRI* pTri, const Fvector* pVerts, const Fvector& contact_point,
                           ref_shader hShader, float sz, float ttl, bool ignore_opt, float rotation);

    // Add skeleton wallmark (projected onto animated model)
    void AddSkeletonWallmark(intrusive_ptr<CSkeletonWallmark> wm);
    void AddSkeletonWallmark(const Fmatrix* xf, CKinematics* obj, ref_shader& sh,
                             const Fvector& start, const Fvector& dir, float size,
                             float ttl = 0.f, bool ignore_opt = false);

    // Render all wallmarks
    void Render();

    // Clear all wallmarks
    void clear();

private:
    // Slot management
    wm_slot* FindSlot(ref_shader shader);
    wm_slot* AppendSlot(ref_shader shader);

    // Geometry generation (CPU)
    void BuildMatrix(Fmatrix& dest, float invsz, const Fvector& from);
    void RecurseTri(u32 T, Fmatrix& mView, static_wallmark& W);
    void AddWallmark_internal(CDB::TRI* pTri, const Fvector* pVerts, const Fvector& contact_point,
                              ref_shader hShader, float sz, float ttl, float rotation);
    void AddWallmark_internal(CDB::TRI* pTri, const Fvector* pVerts, const Fvector& contact_point,
                              ref_shader hShader, float sz, float ttl, bool random_rotation);

    // Static wallmark pool management
    static_wallmark* static_wm_allocate();
    void static_wm_render(static_wallmark* W, FVF::LIT*& V);
    void static_wm_destroy(static_wallmark* W);

    // Skeleton wallmark rendering
    void skeleton_wm_render(intrusive_ptr<CSkeletonWallmark> wm, FVF::LIT*& V);

    // Vulkan GPU rendering
    bool InitVulkanResources();
    void DestroyVulkanResources();
    bool CreatePipeline();
    bool CreateDynamicBuffer();
    void RenderSlots();

    // Per-slot texture cache entry
    struct TextureCacheEntry
    {
        VK::CVulkanTexture* texture = nullptr;
        VkDescriptorSet     descriptorSet = VK_NULL_HANDLE;
    };

    // Get or create cached texture + descriptor for a slot's shader
    TextureCacheEntry* GetOrCreateSlotTexture(wm_slot* slot);
    shared_str          ExtractTextureName(ref_shader& shader);

private:
    StaticWMVec static_pool;
    WMSlotVec marks;

    // Geometry generation temporaries
    Fvector sml_normal;
    CFrustum sml_clipper;
    sPoly sml_poly_dest;
    sPoly sml_poly_src;

    xrXRC xrc;
    CDB::Collector sml_collector;
    xr_vector<u32> sml_adjacency;

    xrCriticalSection lock;

    // Vulkan rendering resources
    VkPipeline              m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout        m_pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout   m_descriptorSetLayout = VK_NULL_HANDLE;
    VK::CVulkanBuffer*      m_dynamicVB = nullptr;
    bool                    m_vulkanReady = false;
    static constexpr u32    MAX_WM_VERTS = 65536;

    // Fallback white texture (used when slot texture fails to load)
    VK::CVulkanTexture*     m_whiteTexture = nullptr;
    VkDescriptorSet         m_whiteDescriptorSet = VK_NULL_HANDLE;

    // Per-slot texture cache: texture name → {VulkanTexture, DescriptorSet}
    xr_map<shared_str, TextureCacheEntry> m_textureCache;
};
