// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

// ============================================================================
// vk_ParticlePipeline.h - Particle rendering pipeline
// ============================================================================
//
// Handles creation of Vulkan graphics pipeline for particle billboards.
// Configures blending, depth settings, and shader stages.
//
// ============================================================================

#pragma once

#include <vulkan/vulkan.h>

namespace VK
{
    class CVulkanBuffer;
}

// ============================================================================
// Particle Pipeline Configuration
// ============================================================================
struct ParticlePipelineConfig
{
    bool depthTest = false;                       // No depth test for particles
    bool depthWrite = false;                      // No depth write
    VkCompareOp depthOp = VK_COMPARE_OP_LESS;    // Unused but for consistency
    VkBlendOp blendOp = VK_BLEND_OP_ADD;          // Additive blending
    VkBlendFactor srcBlend = VK_BLEND_FACTOR_SRC_ALPHA;
    VkBlendFactor dstBlend = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    VkCullModeFlagBits cullMode = VK_CULL_MODE_NONE; // No culling for billboards
    bool wireframe = false;
    VkFormat colorFormat = VK_FORMAT_B8G8R8A8_SRGB;  // Swapchain format
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;     // Depth format
};

// ============================================================================
// vkParticlePipeline - Pipeline creation helper
// ============================================================================
class vkParticlePipeline
{
public:
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

    struct ShaderModules
    {
        VkShaderModule vertexModule = VK_NULL_HANDLE;
        VkShaderModule fragmentModule = VK_NULL_HANDLE;
    };

public:
    vkParticlePipeline() = default;
    ~vkParticlePipeline();

    // ========================================================================
    // Pipeline Creation
    // ========================================================================

    // Create complete pipeline with configuration (for dynamic rendering)
    bool Create(
        VkDevice device,
        const ParticlePipelineConfig& config,
        VkDescriptorSetLayout descriptorSetLayout
    );

    // Destroy pipeline and layout
    void Destroy(VkDevice device);

    // ========================================================================
    // Shader Management
    // ========================================================================

    // Load shader from SPIR-V file
    static VkShaderModule LoadShaderModule(
        VkDevice device,
        const char* filename
    );

    // Load both vertex and fragment shaders
    static bool LoadShaders(
        VkDevice device,
        ShaderModules& modules
    );

    // ========================================================================
    // Binding
    // ========================================================================

    // Bind pipeline to command buffer
    void Bind(VkCommandBuffer commandBuffer) const;

private:
    // Create pipeline layout with descriptor set binding
    bool CreatePipelineLayout(
        VkDevice device,
        VkDescriptorSetLayout descriptorSetLayout
    );

    // Create graphics pipeline (with VK_KHR_dynamic_rendering support)
    bool CreateGraphicsPipeline(
        VkDevice device,
        const ParticlePipelineConfig& config,
        const ShaderModules& shaders
    );
};
