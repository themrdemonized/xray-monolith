// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

// ============================================================================
// vk_material.cpp - Material System Implementation
// ============================================================================
//
// Phase 2.22: Material System
// Phase 2.33: Material Loading with .thm Support
//
// Implements material loading, texture management, and descriptor set binding.
// Integrates with CTextureDescrMngr for .thm file loading.
//
// ============================================================================

#include "stdafx.h"
#include "vk_material.h"
#include "HW_Vulkan.h"
#include "vk_descriptors.h"
#include "vk_pipeline.h"
#include "../xrRender/TextureDescrManager.h"  // .thm loader
#include "vk_detail_scaler.h"                 // For cl_dt_scaler

// Глобальный экземпляр
VK::CMaterialManager* g_MaterialManager = nullptr;

namespace VK
{

// ============================================================================
// CMaterial Implementation
// ============================================================================

CMaterial::CMaterial()
    : m_TexDiffuse(nullptr)
    , m_TexNormal(nullptr)
    , m_TexSpecular(nullptr)
    , m_TexDetail(nullptr)
    , m_TexMask(nullptr)
    , m_TexDetailR(nullptr)
    , m_TexDetailG(nullptr)
    , m_TexDetailB(nullptr)
    , m_TexDetailA(nullptr)
    , m_fMaterial(0.0f)
    , m_bUseSteepParallax(false)
    , m_fDetailScale(1.0f)
    , m_bTerrain(false)
    , m_DescriptorSet(VK_NULL_HANDLE)
    , m_CachedFrame(0xFFFFFFFF)
    , m_CachedFrameSet(VK_NULL_HANDLE)
{
}

CMaterial::~CMaterial()
{
    Destroy();
}

void CMaterial::Create(LPCSTR name)
{
    m_Name = name;

    // Detect terrain by texture name prefix
    m_bTerrain = (name && (strstr(name, "terrain\\") == name || strstr(name, "terrain/") == name));

    // Load material parameters from .thm file (Phase 2.33)
    LoadFromTHM(name);

    // Load textures
    LoadDiffuse(name);
    LoadNormal(name);
    LoadSpecular(name);
    LoadDetail(name);

    // Load terrain-specific textures (mask + 4 detail layers)
    if (m_bTerrain) {
        LoadTerrainTextures(name);
    }

    // Create descriptor set
    CreateDescriptorSet();
    UpdateDescriptorSet();

    Msg("[Vulkan] Material created: %s (material=%.2f, parallax=%d, terrain=%d)",
        name, m_fMaterial, m_bUseSteepParallax, m_bTerrain);
}

void CMaterial::Destroy()
{
    DestroyDescriptorSet();

    // Don't destroy textures here - they're managed by texture manager
    m_TexDiffuse = nullptr;
    m_TexNormal = nullptr;
    m_TexSpecular = nullptr;
    m_TexDetail = nullptr;
    m_TexMask = nullptr;
    m_TexDetailR = nullptr;
    m_TexDetailG = nullptr;
    m_TexDetailB = nullptr;
    m_TexDetailA = nullptr;
}

void CMaterial::LoadDiffuse(LPCSTR name)
{
    if (!name || !name[0]) {
        m_TexDiffuse = g_MaterialManager->GetWhiteTexture();
        return;
    }

    // Check texture cache first
    m_TexDiffuse = g_MaterialManager->FindTexture(name);
    if (m_TexDiffuse) {
        // Already loaded
        return;
    }

    // Build texture path
    // Match DX11 Texture.cpp search order: $level$ -> $game_saves$ -> $game_textures$
    string_path fn;

    // Try $level$ first (level-specific textures: terrain, lightmaps)
    if (FS.exist(fn, "$level$", name, ".dds")) {
        Msg("[Vulkan] Diffuse found in $level$: %s -> %s", name, fn);
        m_TexDiffuse = g_MaterialManager->LoadTexture(name, fn);
        if (m_TexDiffuse) {
            return;
        }
        Msg("![Vulkan] LoadTexture FAILED for: %s", fn);
    }

    // Try $game_saves$ (saved game textures)
    if (FS.exist(fn, "$game_saves$", name, ".dds")) {
        Msg("[Vulkan] Diffuse found in $game_saves$: %s -> %s", name, fn);
        m_TexDiffuse = g_MaterialManager->LoadTexture(name, fn);
        if (m_TexDiffuse) {
            return;
        }
        Msg("![Vulkan] LoadTexture FAILED for: %s", fn);
    }

    // Try $game_textures$ (main texture archive)
    if (FS.exist(fn, "$game_textures$", name, ".dds")) {
        Msg("[Vulkan] Diffuse found in $game_textures$: %s -> %s", name, fn);
        m_TexDiffuse = g_MaterialManager->LoadTexture(name, fn);
        if (m_TexDiffuse) {
            return;
        }
        Msg("![Vulkan] LoadTexture FAILED for: %s", fn);
    }

    // Fallback to white texture
    Msg("![Vulkan] Texture NOT FOUND anywhere: %s (searched $level$, $game_saves$, $game_textures$)", name);
    m_TexDiffuse = g_MaterialManager->GetWhiteTexture();
}

void CMaterial::LoadFromTHM(LPCSTR name)
{
    if (!name || !name[0]) {
        m_fMaterial = 0.0f;
        m_bUseSteepParallax = false;
        m_fDetailScale = 1.0f;
        return;
    }

    if (!g_MaterialManager->m_TexDescMngr) {
        // .thm manager not initialized
        m_fMaterial = 0.0f;
        m_bUseSteepParallax = false;
        m_fDetailScale = 1.0f;
        return;
    }

    // Get material ID from .thm (0-3: OrenNayar-Blin, Blin-Phong, Phong-Metal, Metal-OrenNayar)
    m_fMaterial = g_MaterialManager->m_TexDescMngr->GetMaterial(name);

    // Get parallax flag from .thm
    m_bUseSteepParallax = g_MaterialManager->m_TexDescMngr->UseSteepParallax(name);

    // Note: Detail scale is loaded separately by GetDetailTexture()
    m_fDetailScale = 1.0f;  // Will be set in LoadDetail()
}

void CMaterial::LoadNormal(LPCSTR name)
{
    if (!name || !name[0]) {
        m_TexNormal = g_MaterialManager->GetDefaultNormal();
        return;
    }

    // Try to get bump name from .thm file first (Phase 2.33)
    shared_str bump_name;
    if (g_MaterialManager->m_TexDescMngr) {
        bump_name = g_MaterialManager->m_TexDescMngr->GetBumpName(name);
    }

    // If .thm specifies a bump map, use it
    if (bump_name.size() > 0) {
        // Check cache first
        m_TexNormal = g_MaterialManager->FindTexture(bump_name.c_str());
        if (m_TexNormal) {
            return;
        }

        // Load from .thm bump_name (search $level$ -> $game_textures$)
        string_path fn;
        if (FS.exist(fn, "$level$", bump_name.c_str(), ".dds") ||
            FS.exist(fn, "$game_textures$", bump_name.c_str(), ".dds")) {
            m_TexNormal = g_MaterialManager->LoadTexture(bump_name.c_str(), fn);
            if (m_TexNormal) {
                return;
            }
        }
    }

    // Fallback: Try common normal map naming conventions
    string_path fn;
    string_path normal_name;

    const char* suffixes[] = {"_bump", "_nmap", "_n", nullptr};
    for (int i = 0; suffixes[i] != nullptr; i++) {
        xr_sprintf(normal_name, "%s%s", name, suffixes[i]);

        // Check cache first
        m_TexNormal = g_MaterialManager->FindTexture(normal_name);
        if (m_TexNormal) {
            return;
        }

        if (FS.exist(fn, "$level$", normal_name, ".dds") ||
            FS.exist(fn, "$game_textures$", normal_name, ".dds")) {
            m_TexNormal = g_MaterialManager->LoadTexture(normal_name, fn);
            if (m_TexNormal) {
                return;
            }
        }
    }

    // Fallback to default flat normal
    m_TexNormal = g_MaterialManager->GetDefaultNormal();
}

void CMaterial::LoadSpecular(LPCSTR name)
{
    if (!name || !name[0]) {
        m_TexSpecular = g_MaterialManager->GetWhiteTexture();
        return;
    }

    // Check texture cache first
    m_TexSpecular = g_MaterialManager->FindTexture(name);
    if (m_TexSpecular) {
        return;
    }

    // Build specular map name (usually base_name + "_spec" or "_s")
    string_path fn;
    string_path spec_name;

    // Try common specular map naming conventions
    const char* suffixes[] = {"_spec", "_s", nullptr};
    for (int i = 0; suffixes[i] != nullptr; i++) {
        xr_sprintf(spec_name, "%s%s", name, suffixes[i]);

        if (FS.exist(fn, "$level$", spec_name, ".dds") ||
            FS.exist(fn, "$game_textures$", spec_name, ".dds")) {
            m_TexSpecular = g_MaterialManager->LoadTexture(spec_name, fn);
            if (m_TexSpecular) {
                return;
            }
        }
    }

    // Fallback to white texture (no specular)
    m_TexSpecular = g_MaterialManager->GetWhiteTexture();
}

void CMaterial::LoadDetail(LPCSTR name)
{
    if (!name || !name[0]) {
        m_TexDetail = nullptr;
        return;
    }

    if (!g_MaterialManager->m_TexDescMngr) {
        // .thm manager not initialized
        m_TexDetail = nullptr;
        return;
    }

    // Get detail texture from .thm
    LPCSTR detail_name = nullptr;
    R_constant_setup* detail_scaler = nullptr;
    BOOL has_detail = g_MaterialManager->m_TexDescMngr->GetDetailTexture(name, detail_name, detail_scaler);

    if (!has_detail || !detail_name || !detail_name[0]) {
        m_TexDetail = nullptr;
        return;
    }

    // Get detail scale from constant setup
    if (detail_scaler) {
        // detail_scaler is always cl_dt_scaler* (from GetDetailTexture contract)
        cl_dt_scaler* scaler = static_cast<cl_dt_scaler*>(detail_scaler);
        m_fDetailScale = scaler->scale;
    } else {
        m_fDetailScale = 1.0f;  // No scaler provided, use default
    }

    // Check cache first
    m_TexDetail = g_MaterialManager->FindTexture(detail_name);
    if (m_TexDetail) {
        Msg("[Vulkan] Using cached detail texture: %s", detail_name);
        return;
    }

    // Load detail texture (search $level$ -> $game_textures$)
    string_path fn;
    if (FS.exist(fn, "$level$", detail_name, ".dds") ||
        FS.exist(fn, "$game_textures$", detail_name, ".dds")) {
        m_TexDetail = g_MaterialManager->LoadTexture(detail_name, fn);
        if (m_TexDetail) {
            return;
        }
    }

    // No detail texture found
    m_TexDetail = nullptr;
}

void CMaterial::LoadTerrainTextures(LPCSTR name)
{
    if (!name || !name[0]) return;

    // ========================================================================
    // Load terrain mask texture (base_name + "_mask")
    // The mask RGBA channels control blending weights for 4 detail textures
    // ========================================================================
    string_path mask_name;
    xr_sprintf(mask_name, "%s_mask", name);

    m_TexMask = g_MaterialManager->FindTexture(mask_name);
    if (!m_TexMask) {
        string_path fn;
        if (FS.exist(fn, "$level$", mask_name, ".dds") ||
            FS.exist(fn, "$game_textures$", mask_name, ".dds")) {
            m_TexMask = g_MaterialManager->LoadTexture(mask_name, fn);
        }
    }

    if (m_TexMask) {
        Msg("[Vulkan] Terrain mask loaded: %s", mask_name);
    } else {
        Msg("![Vulkan] Terrain mask not found: %s", mask_name);
    }

    // ========================================================================
    // Load 4 detail textures (default names from CBlender_BmmD)
    // R = grass, G = asphalt, B = earth, A = yantar
    // ========================================================================
    const char* detail_names[4] = {
        "detail\\detail_grnd_grass",
        "detail\\detail_grnd_asphalt",
        "detail\\detail_grnd_earth",
        "detail\\detail_grnd_yantar"
    };

    CVulkanTexture** detail_ptrs[4] = {
        &m_TexDetailR, &m_TexDetailG, &m_TexDetailB, &m_TexDetailA
    };

    for (int i = 0; i < 4; i++) {
        *detail_ptrs[i] = g_MaterialManager->FindTexture(detail_names[i]);
        if (!*detail_ptrs[i]) {
            string_path fn;
            if (FS.exist(fn, "$game_textures$", detail_names[i], ".dds") ||
                FS.exist(fn, "$level$", detail_names[i], ".dds")) {
                *detail_ptrs[i] = g_MaterialManager->LoadTexture(detail_names[i], fn);
            }
        }

        if (*detail_ptrs[i]) {
            Msg("[Vulkan] Terrain detail[%d] loaded: %s", i, detail_names[i]);
        } else {
            Msg("![Vulkan] Terrain detail[%d] not found: %s", i, detail_names[i]);
        }
    }
}

void CMaterial::CreateDescriptorSet()
{
    if (!g_DescriptorManager) {
        Msg("![Vulkan] Cannot create descriptor set - DescriptorManager not initialized");
        return;
    }

    // Allocate descriptor set from DescriptorManager (Set 1 - PerMaterial)
    m_DescriptorSet = g_DescriptorManager->AllocatePerMaterial();

    if (m_DescriptorSet == VK_NULL_HANDLE) {
        Msg("![Vulkan] Failed to allocate material descriptor set");
    }
}

void CMaterial::UpdateDescriptorSet()
{
    if (m_DescriptorSet == VK_NULL_HANDLE) {
        Msg("![Vulkan] Cannot update descriptor set - set not created");
        return;
    }

    // Get fallback textures if needed
    CVulkanTexture* diffuse = m_TexDiffuse ? m_TexDiffuse : g_MaterialManager->GetWhiteTexture();
    CVulkanTexture* normal = m_TexNormal ? m_TexNormal : g_MaterialManager->GetDefaultNormal();
    CVulkanTexture* specular = m_TexSpecular ? m_TexSpecular : g_MaterialManager->GetWhiteTexture();

    // Terrain textures fallback to white (1x1) — shader detects terrain via textureSize
    CVulkanTexture* white = g_MaterialManager->GetWhiteTexture();
    CVulkanTexture* mask = m_TexMask ? m_TexMask : white;
    CVulkanTexture* detailR = m_TexDetailR ? m_TexDetailR : white;
    CVulkanTexture* detailG = m_TexDetailG ? m_TexDetailG : white;
    CVulkanTexture* detailB = m_TexDetailB ? m_TexDetailB : white;
    CVulkanTexture* detailA = m_TexDetailA ? m_TexDetailA : white;

    // Descriptor image infos for all 8 bindings
    VkDescriptorImageInfo imageInfos[8] = {};

    // Binding 0: Diffuse texture
    imageInfos[0].sampler = diffuse->GetSampler();
    imageInfos[0].imageView = diffuse->GetView();
    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Binding 1: Normal map
    imageInfos[1].sampler = normal->GetSampler();
    imageInfos[1].imageView = normal->GetView();
    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Binding 2: Specular map
    imageInfos[2].sampler = specular->GetSampler();
    imageInfos[2].imageView = specular->GetView();
    imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Binding 3: Terrain mask
    imageInfos[3].sampler = mask->GetSampler();
    imageInfos[3].imageView = mask->GetView();
    imageInfos[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Binding 4: Detail R (grass)
    imageInfos[4].sampler = detailR->GetSampler();
    imageInfos[4].imageView = detailR->GetView();
    imageInfos[4].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Binding 5: Detail G (asphalt)
    imageInfos[5].sampler = detailG->GetSampler();
    imageInfos[5].imageView = detailG->GetView();
    imageInfos[5].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Binding 6: Detail B (earth)
    imageInfos[6].sampler = detailB->GetSampler();
    imageInfos[6].imageView = detailB->GetView();
    imageInfos[6].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Binding 7: Detail A (yantar)
    imageInfos[7].sampler = detailA->GetSampler();
    imageInfos[7].imageView = detailA->GetView();
    imageInfos[7].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Write all 8 descriptor bindings
    VkWriteDescriptorSet writes[8] = {};
    for (int i = 0; i < 8; i++) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = m_DescriptorSet;
        writes[i].dstBinding = i;
        writes[i].dstArrayElement = 0;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].descriptorCount = 1;
        writes[i].pImageInfo = &imageInfos[i];
    }

    vkUpdateDescriptorSets(VulkanHW.GetDevice(), 8, writes, 0, nullptr);
}

void CMaterial::DestroyDescriptorSet()
{
    if (m_DescriptorSet != VK_NULL_HANDLE) {
        // Descriptor sets are freed when pool is destroyed
        // No need to manually free individual sets
        m_DescriptorSet = VK_NULL_HANDLE;
    }
}

void CMaterial::Bind(VkCommandBuffer cmd)
{
    if (!g_DescriptorManager || !m_TexDiffuse) return;

    VkPipelineLayout layout = g_PipelineManager->GetLayout();

    // Per-frame caching: if this material already allocated a descriptor set
    // this frame, re-use it instead of allocating a new one.
    // This is critical because the pool supports ~2000 material sets per frame,
    // but there can be 7000+ draw calls sharing ~1300 unique materials.
    if (m_CachedFrame == Device.dwFrame && m_CachedFrameSet != VK_NULL_HANDLE)
    {
        vkCmdBindDescriptorSets(cmd,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            layout,
            1,  // Set 1 (PerMaterial)
            1,  // bind 1 set
            &m_CachedFrameSet,
            0, nullptr);
        return;
    }

    // Allocate fresh descriptor set for this frame
    VkDescriptorSet frameSet = g_DescriptorManager->AllocatePerMaterial();
    if (frameSet == VK_NULL_HANDLE) {
        static u32 s_allocFailCount = 0;
        if (s_allocFailCount < 10)
            Msg("![Vulkan] PerMaterial descriptor pool exhausted (frame %u, fail #%u)",
                Device.dwFrame, ++s_allocFailCount);
        return;
    }

    // Temporarily swap in the fresh set, update it, then restore
    VkDescriptorSet savedSet = m_DescriptorSet;
    m_DescriptorSet = frameSet;
    UpdateDescriptorSet();
    m_DescriptorSet = savedSet;

    // Cache for this frame
    m_CachedFrame = Device.dwFrame;
    m_CachedFrameSet = frameSet;

    // Bind to Set 1 (PerMaterial)
    vkCmdBindDescriptorSets(cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        layout,
        1,  // Set 1 (PerMaterial)
        1,  // bind 1 set
        &frameSet,
        0, nullptr);
}

// ============================================================================
// CMaterialManager Implementation
// ============================================================================

CMaterialManager::CMaterialManager()
    : m_WhiteTexture(nullptr)
    , m_BlackTexture(nullptr)
    , m_DefaultNormal(nullptr)
    , m_DefaultMaterial(nullptr)
    , m_TexDescMngr(nullptr)
    , m_bCreated(false)
{
}

CMaterialManager::~CMaterialManager()
{
    Destroy();
}

void CMaterialManager::Create()
{
    if (m_bCreated) return;

    Msg("[Vulkan] Creating Material Manager...");

    // NOTE: Descriptor set layout (Set 1 - PerMaterial) is managed by g_DescriptorManager
    // We use existing infrastructure instead of creating our own

    // Create texture descriptor manager (.thm loader) - Phase 2.33
    if (!m_TexDescMngr) {
        m_TexDescMngr = xr_new<CTextureDescrMngr>();
        m_TexDescMngr->Load();  // Load all .thm files from $game_textures$ and $level$
        Msg("[Vulkan] Texture descriptor manager created (.thm files loaded)");
    }

    // Create default textures (white, black, normal)
    CreateDefaultTextures();

    // Create default material
    m_DefaultMaterial = xr_new<CMaterial>();
    m_DefaultMaterial->Create("default");

    m_bCreated = true;
    Msg("[Vulkan] Material Manager created successfully");
    Msg("[Vulkan] Texture loading system ready - textures will load on-demand");
}

void CMaterialManager::Destroy()
{
    if (!m_bCreated) return;

    Msg("[Vulkan] Destroying Material Manager...");

    // Destroy default material
    if (m_DefaultMaterial) {
        m_DefaultMaterial->Destroy();
        xr_delete(m_DefaultMaterial);
    }

    // Destroy all materials
    for (auto& pair : m_Materials) {
        if (pair.second) {
            pair.second->Destroy();
            xr_delete(pair.second);
        }
    }
    m_Materials.clear();

    // Destroy all cached textures
    for (auto& pair : m_Textures) {
        if (pair.second) {
            pair.second->Destroy();
            xr_delete(pair.second);
        }
    }
    m_Textures.clear();

    // Destroy default textures
    DestroyDefaultTextures();

    // Destroy texture descriptor manager (.thm loader) - Phase 2.33
    if (m_TexDescMngr) {
        m_TexDescMngr->UnLoad();  // Unload .thm data
        xr_delete(m_TexDescMngr);
        m_TexDescMngr = nullptr;
        Msg("[Vulkan] Texture descriptor manager destroyed");
    }

    // NOTE: Descriptor sets are freed when g_DescriptorManager is destroyed
    // No need to manually destroy them here

    m_bCreated = false;
    Msg("[Vulkan] Material Manager destroyed");
}

CMaterial* CMaterialManager::CreateMaterial(LPCSTR name)
{
    // Check if material already exists
    auto it = m_Materials.find(name);
    if (it != m_Materials.end()) {
        return it->second;
    }

    // Create new material
    CMaterial* mat = xr_new<CMaterial>();
    mat->Create(name);

    // Add to cache
    m_Materials[name] = mat;

    return mat;
}

CMaterial* CMaterialManager::GetMaterial(LPCSTR name)
{
    // Check cache
    auto it = m_Materials.find(name);
    if (it != m_Materials.end()) {
        return it->second;
    }

    // Return default material if not found
    return m_DefaultMaterial;
}

void CMaterialManager::DestroyMaterial(CMaterial* mat)
{
    if (!mat) return;

    // Remove from cache
    for (auto it = m_Materials.begin(); it != m_Materials.end(); ++it) {
        if (it->second == mat) {
            mat->Destroy();
            xr_delete(mat);
            m_Materials.erase(it);
            return;
        }
    }
}

// ============================================================================
// Texture Management
// ============================================================================

CVulkanTexture* CMaterialManager::FindTexture(LPCSTR name)
{
    if (!name || !name[0])
        return nullptr;

    // Check cache
    auto it = m_Textures.find(name);
    if (it != m_Textures.end()) {
        return it->second;
    }

    return nullptr;
}

CVulkanTexture* CMaterialManager::LoadTexture(LPCSTR name, LPCSTR path)
{
    if (!name || !name[0] || !path || !path[0])
        return nullptr;

    // Check if already loaded
    CVulkanTexture* existing = FindTexture(name);
    if (existing) {
        return existing;
    }

    // Load texture
    CVulkanTexture* tex = xr_new<CVulkanTexture>();
    if (!tex->LoadDDS(path)) {
        // Load failed
        Msg("![Vulkan] Failed to load texture: %s", path);
        xr_delete(tex);
        return nullptr;
    }

    // Add to cache
    m_Textures[name] = tex;

    return tex;
}

void CMaterialManager::DestroyTexture(CVulkanTexture* tex)
{
    if (!tex) return;

    // Find and remove from cache
    for (auto it = m_Textures.begin(); it != m_Textures.end(); ++it) {
        if (it->second == tex) {
            tex->Destroy();
            xr_delete(tex);
            m_Textures.erase(it);
            return;
        }
    }
}

void CMaterialManager::CreateDefaultTextures()
{
    Msg("[Vulkan] Creating default textures...");

    // ========================================================================
    // White texture (1x1 RGBA = 255,255,255,255)
    // ========================================================================
    {
        u32 whitePixel = 0xFFFFFFFF;  // RGBA white
        m_WhiteTexture = xr_new<CVulkanTexture>();
        m_WhiteTexture->CreateFromData(
            &whitePixel,                       // pixel data
            1, 1,                               // 1x1 size
            VK_FORMAT_R8G8B8A8_UNORM,          // RGBA format
            sizeof(u32)                        // data size
        );
        Msg("[Vulkan] White texture created (1x1)");
    }

    // ========================================================================
    // Black texture (1x1 RGBA = 0,0,0,255)
    // ========================================================================
    {
        u32 blackPixel = 0xFF000000;  // RGBA black (alpha=255)
        m_BlackTexture = xr_new<CVulkanTexture>();
        m_BlackTexture->CreateFromData(
            &blackPixel,
            1, 1,
            VK_FORMAT_R8G8B8A8_UNORM,
            sizeof(u32)
        );
        Msg("[Vulkan] Black texture created (1x1)");
    }

    // ========================================================================
    // Default normal map (1x1 RGB = 128,128,255 = flat normal 0,0,1)
    // ========================================================================
    {
        u32 normalPixel = 0xFF8080FF;  // RGBA (128,128,255,255) = (0.5,0.5,1.0,1.0)
        m_DefaultNormal = xr_new<CVulkanTexture>();
        m_DefaultNormal->CreateFromData(
            &normalPixel,
            1, 1,
            VK_FORMAT_R8G8B8A8_UNORM,
            sizeof(u32)
        );
        Msg("[Vulkan] Default normal map created (1x1)");
    }
}

void CMaterialManager::DestroyDefaultTextures()
{
    if (m_WhiteTexture) {
        m_WhiteTexture->Destroy();
        xr_delete(m_WhiteTexture);
    }

    if (m_BlackTexture) {
        m_BlackTexture->Destroy();
        xr_delete(m_BlackTexture);
    }

    if (m_DefaultNormal) {
        m_DefaultNormal->Destroy();
        xr_delete(m_DefaultNormal);
    }
}

} // namespace VK
