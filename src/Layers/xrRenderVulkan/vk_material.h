// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#pragma once

// ============================================================================
// vk_material.h - Material System for Vulkan Renderer
// ============================================================================
//
// Phase 2.22: Material System
//
// Manages materials and textures for geometry rendering. Simplified MVP
// implementation - basic texture loading and binding to shaders.
//
// Material Structure:
// - Diffuse texture (albedo)
// - Normal map (optional)
// - Specular map (optional)
// - Descriptor set (Set 2) for binding to shaders
//
// ============================================================================

#include "vk_texture.h"

// Forward declarations
namespace VK {
    class CMaterial;
    class CMaterialManager;
}

// Глобальный экземпляр
extern VK::CMaterialManager* g_MaterialManager;

namespace VK
{

// ============================================================================
// CMaterial - Single Material
// ============================================================================
//
// Represents one material with its textures and properties.
// For MVP: just diffuse texture + descriptor set.
//
// Future (Phase 2.23+):
// - Normal maps
// - PBR properties (metallic, roughness)
// - Material parameters (.thm parsing)
//
// ============================================================================
class CMaterial
{
public:
    // Material name
    shared_str m_Name;

    // Textures
    CVulkanTexture* m_TexDiffuse;   // Albedo (base color)
    CVulkanTexture* m_TexNormal;    // Normal map (optional, Phase 2.23)
    CVulkanTexture* m_TexSpecular;  // Specular map (optional, Phase 2.23)

    // Descriptor set (Set 2 - material textures)
    VkDescriptorSet m_DescriptorSet;

public:
    CMaterial();
    ~CMaterial();

    // Lifecycle
    void Create(LPCSTR name);                          // Create material (load textures)
    void Destroy();                                    // Release resources

    // Texture loading
    void LoadDiffuse(LPCSTR name);                     // Load diffuse texture
    void LoadNormal(LPCSTR name);                      // Load normal map (TODO Phase 2.23)
    void LoadSpecular(LPCSTR name);                    // Load specular map (TODO Phase 2.23)

    // Descriptor set
    void CreateDescriptorSet();                        // Create VkDescriptorSet
    void UpdateDescriptorSet();                        // Update descriptor bindings
    void DestroyDescriptorSet();                       // Release descriptor set

    // Binding
    void Bind(VkCommandBuffer cmd);                    // Bind material to pipeline (Set 2)

    // Helpers
    bool IsValid() const { return m_TexDiffuse != nullptr; }
};

// ============================================================================
// CMaterialManager - Material Management
// ============================================================================
//
// Manages material lifecycle and caching.
// Prevents duplicate texture loading.
//
// For MVP:
// - Create default white material
// - Load materials on demand
// - Simple cache (name → material)
//
// Future (Phase 2.23+):
// - Material preloading
// - Material batching
// - Material streaming
//
// ============================================================================
class CMaterialManager
{
public:
    // Material cache
    xr_map<shared_str, CMaterial*> m_Materials;

    // Default textures
    CVulkanTexture* m_WhiteTexture;    // 1x1 white fallback
    CVulkanTexture* m_BlackTexture;    // 1x1 black fallback
    CVulkanTexture* m_DefaultNormal;   // 1x1 flat normal (0.5, 0.5, 1.0)

    // Default material
    CMaterial* m_DefaultMaterial;      // White material for untextured geometry

public:
    CMaterialManager();
    ~CMaterialManager();

    // Lifecycle
    void Create();                                     // Initialize material system
    void Destroy();                                    // Cleanup all materials

    // Material management
    CMaterial* CreateMaterial(LPCSTR name);            // Create or get existing material
    CMaterial* GetMaterial(LPCSTR name);               // Get existing material (or default)
    void DestroyMaterial(CMaterial* mat);              // Release material

    // Default textures
    void CreateDefaultTextures();                      // Create white/black/normal fallbacks
    void DestroyDefaultTextures();                     // Release default textures

    CVulkanTexture* GetWhiteTexture() const { return m_WhiteTexture; }
    CVulkanTexture* GetBlackTexture() const { return m_BlackTexture; }
    CVulkanTexture* GetDefaultNormal() const { return m_DefaultNormal; }
    CMaterial* GetDefaultMaterial() const { return m_DefaultMaterial; }

private:
    bool m_bCreated = false;
};

} // namespace VK
