// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

// ============================================================================
// vk_shader.cpp - Vulkan Shader System Implementation
// ============================================================================
//
// Phase 2.32: Shader System Implementation
//
// Implements shader loading and pipeline mapping for Vulkan renderer.
//
// ============================================================================

#include "stdafx.h"
#include "vk_shader.h"
#include "vk_pipeline.h"
#include "vk_material.h"

// Global instance
VK::CVulkanShaderManager* g_VulkanShaderManager = nullptr;

namespace VK
{

// ============================================================================
// CVulkanShader Implementation
// ============================================================================

CVulkanShader::CVulkanShader()
    : m_Material(nullptr)
    , m_bEmissive(false)
    , m_bDistort(false)
    , m_bLandscape(false)
    , m_bWmark(false)
    , m_bAlphaRef(false)
{
    // Initialize default pipeline config
    m_PipelineConfig.depthTest = true;
    m_PipelineConfig.depthWrite = true;
    m_PipelineConfig.depthCompareOp = VK_COMPARE_OP_LESS;
    m_PipelineConfig.cullMode = VK_CULL_MODE_BACK_BIT;
    m_PipelineConfig.frontFace = VK_FRONT_FACE_CLOCKWISE;
    m_PipelineConfig.blendEnable = false;
}

CVulkanShader::~CVulkanShader()
{
    Destroy();
}

void CVulkanShader::Create(LPCSTR name, LPCSTR tex_diffuse)
{
    m_Name = name;

    // Texture list from level shaders has format: "diffuse,lmap1,lmap2"
    // Extract only the first texture name (diffuse) before the comma
    if (tex_diffuse && tex_diffuse[0])
    {
        string256 diffuse_only;
        xr_strcpy(diffuse_only, tex_diffuse);
        LPSTR comma = strchr(diffuse_only, ',');
        if (comma) *comma = 0;
        m_TexDiffuse = diffuse_only;
    }
    else
    {
        m_TexDiffuse = tex_diffuse;
    }

    // Parse shader name for flags
    // Example: "def_shaders\def_aref" - alpha reference (alpha test)
    // Example: "def_shaders\lod_def" - LOD shader
    xr_string shader_lower = name;
    std::transform(shader_lower.begin(), shader_lower.end(), shader_lower.begin(), ::tolower);

    // Check for alpha test (aref)
    if (shader_lower.find("aref") != xr_string::npos ||
        shader_lower.find("alpha") != xr_string::npos ||
        shader_lower.find("trans") != xr_string::npos) {
        // Alpha test shaders use discard in fragment shader (alphaRef > 0)
        m_bAlphaRef = true;
        m_PipelineConfig.blendEnable = false;  // Will use shader discard for now
    }

    // Check for additive shaders (effects, glows)
    if (shader_lower.find("add") != xr_string::npos ||
        shader_lower.find("glow") != xr_string::npos) {
        m_bEmissive = true;
        m_PipelineConfig.blendEnable = true;
        m_PipelineConfig.srcColorBlend = VK_BLEND_FACTOR_ONE;
        m_PipelineConfig.dstColorBlend = VK_BLEND_FACTOR_ONE;  // Additive
        m_PipelineConfig.depthWrite = false;  // No depth write for effects
    }

    // Check for distortion
    if (shader_lower.find("dist") != xr_string::npos) {
        m_bDistort = true;
    }

    // Check for landscape
    if (shader_lower.find("terrain") != xr_string::npos ||
        shader_lower.find("landscape") != xr_string::npos) {
        m_bLandscape = true;
    }

    // Check for wallmarks (decals)
    if (shader_lower.find("wmark") != xr_string::npos ||
        shader_lower.find("decal") != xr_string::npos) {
        m_bWmark = true;
        m_PipelineConfig.blendEnable = true;
        m_PipelineConfig.srcColorBlend = VK_BLEND_FACTOR_SRC_ALPHA;
        m_PipelineConfig.dstColorBlend = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        m_PipelineConfig.depthWrite = false;  // No depth write for decals
    }

    Msg("[Vulkan] Shader created: %s (diffuse: %s)", name, tex_diffuse);
}

void CVulkanShader::Destroy()
{
    // Don't destroy material here - it's managed by MaterialManager
    m_Material = nullptr;
}

VK::CMaterial* CVulkanShader::GetMaterial()
{
    // Lazy material creation
    if (!m_Material && m_TexDiffuse.size() > 0) {
        if (g_MaterialManager) {
            // Use texture name as material name
            m_Material = g_MaterialManager->CreateMaterial(m_TexDiffuse.c_str());
        }
    }

    return m_Material;
}

VkPipeline CVulkanShader::GetPipeline()
{
    if (!g_PipelineManager) {
        Msg("![Vulkan] Cannot get pipeline - PipelineManager not initialized");
        return VK_NULL_HANDLE;
    }

    // Get or create pipeline from config
    return g_PipelineManager->GetOrCreate(m_PipelineConfig);
}

// ============================================================================
// CVulkanShaderManager Implementation
// ============================================================================

CVulkanShaderManager::CVulkanShaderManager()
    : m_DefaultShader(nullptr)
    , m_bCreated(false)
{
}

CVulkanShaderManager::~CVulkanShaderManager()
{
    Destroy();
}

void CVulkanShaderManager::Create()
{
    if (m_bCreated) return;

    Msg("[Vulkan] Creating Vulkan Shader Manager...");

    // Create default shader (white material)
    m_DefaultShader = xr_new<CVulkanShader>();
    m_DefaultShader->Create("default", "");  // Empty texture = white fallback

    m_bCreated = true;
    Msg("[Vulkan] Vulkan Shader Manager created successfully");
}

void CVulkanShaderManager::Destroy()
{
    if (!m_bCreated) return;

    Msg("[Vulkan] Destroying Vulkan Shader Manager...");

    // Destroy default shader
    if (m_DefaultShader) {
        m_DefaultShader->Destroy();
        xr_delete(m_DefaultShader);
    }

    // Destroy all shaders
    for (auto& pair : m_Shaders) {
        if (pair.second) {
            pair.second->Destroy();
            xr_delete(pair.second);
        }
    }
    m_Shaders.clear();

    m_bCreated = false;
    Msg("[Vulkan] Vulkan Shader Manager destroyed");
}

CVulkanShader* CVulkanShaderManager::CreateShader(LPCSTR name, LPCSTR tex_diffuse)
{
    if (!name || !name[0]) {
        Msg("![Vulkan] Cannot create shader - empty name");
        return m_DefaultShader;
    }

    // Build unique key from shader name + texture name
    // (same shader can be used with different textures in the level shader table)
    string512 key;
    xr_sprintf(key, "%s#%s", name, tex_diffuse ? tex_diffuse : "");

    // Check if shader already exists
    auto it = m_Shaders.find(key);
    if (it != m_Shaders.end()) {
        return it->second;
    }

    // Create new shader
    CVulkanShader* shader = xr_new<CVulkanShader>();
    shader->Create(name, tex_diffuse ? tex_diffuse : "");

    // Add to cache
    m_Shaders[key] = shader;

    return shader;
}

CVulkanShader* CVulkanShaderManager::GetShader(LPCSTR name)
{
    if (!name || !name[0]) {
        return m_DefaultShader;
    }

    // Check cache
    auto it = m_Shaders.find(name);
    if (it != m_Shaders.end()) {
        return it->second;
    }

    // Return default shader if not found
    Msg("![Vulkan] Shader not found: %s, using default", name);
    return m_DefaultShader;
}

void CVulkanShaderManager::DestroyShader(CVulkanShader* shader)
{
    if (!shader) return;

    // Remove from cache
    for (auto it = m_Shaders.begin(); it != m_Shaders.end(); ++it) {
        if (it->second == shader) {
            shader->Destroy();
            xr_delete(shader);
            m_Shaders.erase(it);
            return;
        }
    }
}

} // namespace VK
