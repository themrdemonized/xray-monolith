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
    , m_fMaterial(0.0f)
    , m_bUseSteepParallax(false)
    , m_fDetailScale(1.0f)
    , m_DescriptorSet(VK_NULL_HANDLE)
{
}

CMaterial::~CMaterial()
{
    Destroy();
}

void CMaterial::Create(LPCSTR name)
{
    m_Name = name;

    // Load material parameters from .thm file (Phase 2.33)
    LoadFromTHM(name);

    // Load textures
    LoadDiffuse(name);
    LoadNormal(name);
    LoadSpecular(name);
    LoadDetail(name);

    // Create descriptor set
    CreateDescriptorSet();
    UpdateDescriptorSet();

    Msg("[Vulkan] Material created: %s (material=%.2f, parallax=%d)",
        name, m_fMaterial, m_bUseSteepParallax);
}

void CMaterial::Destroy()
{
    DestroyDescriptorSet();

    // Don't destroy textures here - they're managed by texture manager
    m_TexDiffuse = nullptr;
    m_TexNormal = nullptr;
    m_TexSpecular = nullptr;
    m_TexDetail = nullptr;
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
    string_path fn;

    // Try .dds first (most common in X-Ray)
    if (FS.exist(fn, "$game_textures$", name, ".dds")) {
        m_TexDiffuse = g_MaterialManager->LoadTexture(name, fn);
        if (m_TexDiffuse) {
            Msg("[Vulkan] Loaded diffuse: %s", fn);
            return;
        }
    }

    // Try .tga as fallback
    if (FS.exist(fn, "$game_textures$", name, ".tga")) {
        Msg("![Vulkan] TGA not supported yet: %s, using white texture", fn);
        m_TexDiffuse = g_MaterialManager->GetWhiteTexture();
        return;
    }

    // Fallback to white texture
    Msg("![Vulkan] Texture not found: %s, using white texture", name);
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

        // Load from .thm bump_name
        string_path fn;
        if (FS.exist(fn, "$game_textures$", bump_name.c_str(), ".dds")) {
            m_TexNormal = g_MaterialManager->LoadTexture(bump_name.c_str(), fn);
            if (m_TexNormal) {
                Msg("[Vulkan] Loaded normal map from .thm: %s", fn);
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

        if (FS.exist(fn, "$game_textures$", normal_name, ".dds")) {
            m_TexNormal = g_MaterialManager->LoadTexture(normal_name, fn);
            if (m_TexNormal) {
                Msg("[Vulkan] Loaded normal map (fallback): %s", fn);
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

        if (FS.exist(fn, "$game_textures$", spec_name, ".dds")) {
            m_TexSpecular = g_MaterialManager->LoadTexture(spec_name, fn);
            if (m_TexSpecular) {
                Msg("[Vulkan] Loaded specular map: %s", fn);
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

    // Load detail texture
    string_path fn;
    if (FS.exist(fn, "$game_textures$", detail_name, ".dds")) {
        m_TexDetail = g_MaterialManager->LoadTexture(detail_name, fn);
        if (m_TexDetail) {
            Msg("[Vulkan] Loaded detail texture from .thm: %s (scale=%.2f)", fn, m_fDetailScale);
            return;
        }
    }

    // No detail texture found
    m_TexDetail = nullptr;
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

    // Descriptor image infos
    VkDescriptorImageInfo imageInfos[3] = {};

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

    // Write descriptor sets
    VkWriteDescriptorSet writes[3] = {};

    // Binding 0: Diffuse
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = m_DescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].dstArrayElement = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &imageInfos[0];

    // Binding 1: Normal
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = m_DescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].dstArrayElement = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &imageInfos[1];

    // Binding 2: Specular
    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = m_DescriptorSet;
    writes[2].dstBinding = 2;
    writes[2].dstArrayElement = 0;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorCount = 1;
    writes[2].pImageInfo = &imageInfos[2];

    vkUpdateDescriptorSets(VulkanHW.GetDevice(), 3, writes, 0, nullptr);
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
    if (m_DescriptorSet == VK_NULL_HANDLE) {
        Msg("![Vulkan] Cannot bind material - descriptor set not created");
        return;
    }

    // Bind descriptor set to Set 1 (PerMaterial)
    VkPipelineLayout layout = g_PipelineManager->GetLayout();
    vkCmdBindDescriptorSets(cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        layout,
        1,  // Set 1 (PerMaterial)
        1,  // bind 1 set
        &m_DescriptorSet,
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
