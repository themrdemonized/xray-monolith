// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#pragma once
#include "../vk_core.h"
#include "../SH_RT_Vulkan.h"

namespace VK
{

// Forward declarations
class vk3DFluidData;
class vk3DFluidGrid;

/**
 * 3D Fluid Renderer
 *
 * Raycasting рендеринг для fluid volumes.
 *
 * RT для рендеринга:
 * - RayDataTex (full res) - ray entry/exit points
 * - RayDataTexSmall (downsampled) - for optimization
 * - RayCastTex (intermediate) - raymarching result
 * - EdgeTex (edge mask) - для adaptive sampling
 *
 * Pipeline:
 * Back faces → Front faces → Downsample → Edge detect → Raycast → Copy to screen
 */
class vk3DFluidRenderer
{
public:
    vk3DFluidRenderer();
    ~vk3DFluidRenderer();

    /**
     * Initialize renderer
     */
    void Initialize();

    /**
     * Render volume
     */
    void RenderVolume(VkCommandBuffer cmd, vk3DFluidData* volume);

    /**
     * Destroy renderer
     */
    void Destroy();

    /**
     * Set screen size (for RT creation)
     */
    void SetScreenSize(u32 width, u32 height);

private:
    enum RendererRT {
        RRT_RayDataTex = 0,      // Ray entry/exit points (full res)
        RRT_RayDataTexSmall,     // Downsampled ray data
        RRT_RayCastTex,          // Raycast result
        RRT_EdgeTex,             // Edge detection
        RRT_NumRT
    };

    void CreateRenderTargets();
    void CreatePipelines();
    void CreateRenderPasses();
    void CreateFramebuffers();
    void CreateGraphicsPipelines();
    void DestroyRenderTargets();
    void DestroyPipelines();
    void DestroyRenderPasses();
    void DestroyFramebuffers();

    void ComputeRayData(VkCommandBuffer cmd, vk3DFluidData* volume);
    void Raycast(VkCommandBuffer cmd, vk3DFluidData* volume);

private:
    bool m_bInitialized = false;

    u32 m_ScreenWidth = 1920;
    u32 m_ScreenHeight = 1080;

    // Render targets
    CRT* m_RTs[RRT_NumRT];

    // Render passes
    VkRenderPass m_RenderPass_RayData = VK_NULL_HANDLE;   // For ray entry/exit points
    VkRenderPass m_RenderPass_Raycast = VK_NULL_HANDLE;  // For volumetric raymarch

    // Framebuffers (recreated on resize)
    VkFramebuffer m_Framebuffer_RayData = VK_NULL_HANDLE;
    VkFramebuffer m_Framebuffer_Raycast = VK_NULL_HANDLE;

    // Graphics pipelines
    VkPipeline m_Pipeline_RayDataBack = VK_NULL_HANDLE;
    VkPipeline m_Pipeline_RayDataFront = VK_NULL_HANDLE;
    VkPipeline m_Pipeline_Raycast = VK_NULL_HANDLE;

    VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;

    // Samplers
    VkSampler m_Sampler3D = VK_NULL_HANDLE;
    VkSampler m_Sampler2D = VK_NULL_HANDLE;

    // Grid geometry
    vk3DFluidGrid* m_Grid = nullptr;

    // Shader modules
    VkShaderModule m_Shader_RayDataBack_Vert = VK_NULL_HANDLE;
    VkShaderModule m_Shader_RayDataBack_Frag = VK_NULL_HANDLE;
    VkShaderModule m_Shader_RayDataFront_Vert = VK_NULL_HANDLE;
    VkShaderModule m_Shader_RayDataFront_Frag = VK_NULL_HANDLE;
    VkShaderModule m_Shader_Fullscreen_Vert = VK_NULL_HANDLE;
    VkShaderModule m_Shader_RaycastFog_Frag = VK_NULL_HANDLE;

    // Helper для загрузки shader module
    VkShaderModule LoadShaderModule(const char* filename);
};

} // namespace VK
