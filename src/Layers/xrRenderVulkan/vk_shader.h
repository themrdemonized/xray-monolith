// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#pragma once

// ============================================================================
// vk_shader.h - Vulkan Shader System
// ============================================================================
//
// Phase 2.32: Shader System Implementation
//
// Maps X-Ray shader definitions to Vulkan pipelines and materials.
// Simplifies the complex DX9/DX11 shader system to Vulkan's model.
//
// Key Differences from DX9/DX11:
// - No runtime shader compilation (SPIR-V precompiled)
// - Pipeline state is immutable (DX9 had state blocks)
// - Descriptor sets replace constant buffers
// - Material-based system (texture lists from .thm files)
//
// ============================================================================

#include "vk_pipeline.h"
#include "vk_material.h"

namespace VK
{

// ============================================================================
// VulkanShader - Single Shader
// ============================================================================
//
// Represents one X-Ray shader mapped to Vulkan pipeline configuration.
// Stores texture names and pipeline parameters.
//
// For MVP:
// - Single pipeline per shader (no geometry types yet)
// - Texture list for material creation
// - Basic pipeline state (depth, blend, cull)
//
// Future (Phase 3+):
// - Multiple geometry types (static, skinned, tree, etc.)
// - Advanced states from blenders
// - Multi-pass rendering
//
// ============================================================================
class CVulkanShader
{
public:
    // Shader name (e.g. "def_shaders\def_aref")
    shared_str m_Name;

    // Texture names for material creation
    shared_str m_TexDiffuse;   // Base texture (T_Base from .thm)
    shared_str m_TexNormal;    // Normal map (T_Bump from .thm)
    shared_str m_TexSpecular;  // Specular map (optional)

    // Pipeline configuration
    VK::PipelineConfig m_PipelineConfig;

    // Material reference (created on first use)
    VK::CMaterial* m_Material;

    // Shader flags (from ShaderElement)
    bool m_bEmissive;     // Self-illuminated (additive blend)
    bool m_bDistort;      // Distortion effect (heat shimmer)
    bool m_bLandscape;    // Landscape shader (multi-pass)
    bool m_bWmark;        // Wallmark shader (decals)
    bool m_bAlphaRef;     // Alpha-reference shader (discard in fragment shader)

public:
    CVulkanShader();
    ~CVulkanShader();

    // Lifecycle
    void Create(LPCSTR name, LPCSTR tex_diffuse);
    void Destroy();

    // Material access (lazy creation)
    VK::CMaterial* GetMaterial();

    // Pipeline access
    VkPipeline GetPipeline();

    // Helpers
    bool IsValid() const { return m_Name.size() > 0; }
};

// ============================================================================
// CVulkanShaderManager - Shader Management
// ============================================================================
//
// Manages shader lifecycle and caching during level loading.
//
// For MVP:
// - Load shaders from level.geom
// - Create default shader for missing references
// - Cache shaders by name
//
// Future (Phase 3+):
// - Runtime shader compilation from .s files
// - Blender support for complex materials
// - Shader hot-reloading
//
// ============================================================================
class CVulkanShaderManager
{
public:
    // Shader cache (name -> shader)
    xr_map<shared_str, CVulkanShader*> m_Shaders;

    // Default shader (white material, basic pipeline)
    CVulkanShader* m_DefaultShader;

public:
    CVulkanShaderManager();
    ~CVulkanShaderManager();

    // Lifecycle
    void Create();                                         // Initialize shader system
    void Destroy();                                        // Cleanup all shaders

    // Shader management
    CVulkanShader* CreateShader(LPCSTR name, LPCSTR tex_diffuse);  // Create shader with texture
    CVulkanShader* GetShader(LPCSTR name);                 // Get existing shader (or default)
    void DestroyShader(CVulkanShader* shader);             // Release shader

    // Default shader (lazy-creates if needed to avoid nullptr)
    CVulkanShader* GetDefaultShader()
    {
        if (!m_DefaultShader)
        {
            m_DefaultShader = xr_new<CVulkanShader>();
            m_DefaultShader->Create("default", "");
        }
        return m_DefaultShader;
    }

private:
    bool m_bCreated;
};

} // namespace VK

// Global instance
extern VK::CVulkanShaderManager* g_VulkanShaderManager;
