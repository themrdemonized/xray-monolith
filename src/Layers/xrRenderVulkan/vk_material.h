// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#pragma once

// ============================================================================
// vk_material.h - Material System for Vulkan Renderer
// ============================================================================
//
// Phase 2.22: Material System
// Phase 2.33: Material Loading with .thm Support
//
// Manages materials and textures for geometry rendering.
// Loads material parameters from .thm files (texture metadata).
//
// Material Structure:
// - Diffuse texture (albedo)
// - Normal map (from .thm bump_name)
// - Specular map (optional)
// - Detail texture (from .thm detail_name)
// - Material parameters (material ID, parallax, etc.)
// - Descriptor set (Set 1 - PerMaterial) for binding to shaders
//
// ============================================================================

#include "vk_texture.h"

// Forward declarations
class CTextureDescrMngr;

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
// Loads parameters from .thm files (texture metadata).
//
// Material Properties:
// - Diffuse texture (base color)
// - Normal map (from .thm bump_name)
// - Specular map (optional)
// - Detail texture (from .thm detail_name)
// - Material ID (0-3: OrenNayar-Blin, Blin-Phong, Phong-Metal, Metal-OrenNayar)
// - Parallax mapping flag
// - Detail scale
//
// ============================================================================
class CMaterial
{
public:
    // Material name
    shared_str m_Name;

    // Textures
    CVulkanTexture* m_TexDiffuse;   // Albedo (base color)
    CVulkanTexture* m_TexNormal;    // Normal map (from .thm bump_name)
    CVulkanTexture* m_TexSpecular;  // Specular map (optional)
    CVulkanTexture* m_TexDetail;    // Detail texture (from .thm detail_name)

    // Material parameters (from .thm)
    float m_fMaterial;              // Material ID (0-3 with weight)
    bool m_bUseSteepParallax;       // Use parallax occlusion mapping
    float m_fDetailScale;           // Detail texture scale

    // Descriptor set (Set 1 - PerMaterial)
    VkDescriptorSet m_DescriptorSet;

public:
    CMaterial();
    ~CMaterial();

    // Lifecycle
    void Create(LPCSTR name);                          // Create material (load textures + .thm)
    void Destroy();                                    // Release resources

    // Texture loading (Phase 2.33 - with .thm support)
    void LoadDiffuse(LPCSTR name);                     // Load diffuse texture
    void LoadNormal(LPCSTR name);                      // Load normal map (from .thm bump_name)
    void LoadSpecular(LPCSTR name);                    // Load specular map
    void LoadDetail(LPCSTR name);                      // Load detail texture (from .thm detail_name)
    void LoadFromTHM(LPCSTR name);                     // Load material parameters from .thm

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
// Manages material lifecycle, caching, and .thm loading.
// Integrates with CTextureDescrMngr for texture metadata.
//
// Features (Phase 2.33):
// - .thm file loading for material parameters
// - Normal map loading from .thm bump_name
// - Detail texture loading from .thm detail_name
// - Material parameter extraction (material ID, parallax, etc.)
// - Texture caching to prevent duplicates
//
// ============================================================================
class CMaterialManager
{
public:
    // Material cache
    xr_map<shared_str, CMaterial*> m_Materials;

    // Texture cache
    xr_map<shared_str, CVulkanTexture*> m_Textures;

    // Default textures
    CVulkanTexture* m_WhiteTexture;    // 1x1 white fallback
    CVulkanTexture* m_BlackTexture;    // 1x1 black fallback
    CVulkanTexture* m_DefaultNormal;   // 1x1 flat normal (0.5, 0.5, 1.0)

    // Default material
    CMaterial* m_DefaultMaterial;      // White material for untextured geometry

    // Texture descriptor manager (.thm loader)
    CTextureDescrMngr* m_TexDescMngr;  // Loads .thm files for material parameters

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

    // Texture management
    CVulkanTexture* FindTexture(LPCSTR name);          // Find texture in cache
    CVulkanTexture* LoadTexture(LPCSTR name, LPCSTR path); // Load texture from file
    void DestroyTexture(CVulkanTexture* tex);          // Release texture

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
