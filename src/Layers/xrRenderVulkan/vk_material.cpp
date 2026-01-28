// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

// ============================================================================
// vk_material.cpp - Material System Implementation
// ============================================================================
//
// Phase 2.22: Material System
//
// Implements material loading, texture management, and descriptor set binding.
//
// ============================================================================

#include "stdafx.h"
#include "vk_material.h"
#include "HW_Vulkan.h"
#include "vk_descriptors.h"
#include "vk_pipeline.h"

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

    // Load diffuse texture
    LoadDiffuse(name);

    // TODO Phase 2.23: Load normal and specular maps
    // LoadNormal(...);
    // LoadSpecular(...);

    // Create descriptor set
    CreateDescriptorSet();
    UpdateDescriptorSet();

    Msg("[Vulkan] Material created: %s", name);
}

void CMaterial::Destroy()
{
    DestroyDescriptorSet();

    // Don't destroy textures here - they're managed by texture manager
    m_TexDiffuse = nullptr;
    m_TexNormal = nullptr;
    m_TexSpecular = nullptr;
}

void CMaterial::LoadDiffuse(LPCSTR name)
{
    // For MVP: Use white texture fallback
    // Phase 2.23 will implement actual texture loading from gamedata

    if (g_MaterialManager) {
        m_TexDiffuse = g_MaterialManager->GetWhiteTexture();
        Msg("[Vulkan] Material '%s': Using white texture (fallback)", name);
    }

    // TODO Phase 2.23: Actual texture loading
    //
    // string_path fn;
    // if (FS.exist(fn, "$game_textures$", name, ".dds")) {
    //     m_TexDiffuse = LoadTextureDDS(fn);
    // } else if (FS.exist(fn, "$game_textures$", name, ".tga")) {
    //     m_TexDiffuse = LoadTextureTGA(fn);
    // } else {
    //     m_TexDiffuse = g_MaterialManager->GetWhiteTexture();
    // }
}

void CMaterial::LoadNormal(LPCSTR name)
{
    // TODO Phase 2.23: Load normal map
    // For now, use default flat normal
    if (g_MaterialManager) {
        m_TexNormal = g_MaterialManager->GetDefaultNormal();
    }
}

void CMaterial::LoadSpecular(LPCSTR name)
{
    // TODO Phase 2.23: Load specular map
    // For now, use white texture (no specular)
    if (g_MaterialManager) {
        m_TexSpecular = g_MaterialManager->GetWhiteTexture();
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

    // Destroy default textures
    DestroyDefaultTextures();

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
