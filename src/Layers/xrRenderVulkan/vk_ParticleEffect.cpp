// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

// ============================================================================
// vk_ParticleEffect.cpp - Particle effect rendering implementation
// ============================================================================
//
// Phase 2.24: Particle System - Vulkan Rendering
//
// Implements billboard-based particle rendering with dynamic vertex buffers.
// Particles are CPU-simulated and rendered as camera-facing quads.
//
// ============================================================================

#include "stdafx.h"
#include "vk_ParticleEffect.h"
#include "vk_ParticlePipeline.h"
#include "vk_ParticleDescriptors.h"
#include "vk_command_buffer.h"  // For CommandManager
#include "vk_R_Backend.h"       // RCache
#include "rvk.h"                // RImplementation
#include "HW_Vulkan.h"
#include "vk_material.h"
#include "../xrRender/ParticleEffectDef.h"  // PS::CPEDef full definition
#include <array>

// External command manager
extern CVulkanCommandManager CommandManager;

// ============================================================================
// VkParticleVertex implementation
// ============================================================================

VkVertexInputBindingDescription VkParticleVertex::GetBindingDescription()
{
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(VkParticleVertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return bindingDescription;
}

std::array<VkVertexInputAttributeDescription, 3> VkParticleVertex::GetAttributeDescriptions()
{
    std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};

    // Position
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(VkParticleVertex, pos);

    // Color
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R8G8B8A8_UNORM;
    attributeDescriptions[1].offset = offsetof(VkParticleVertex, color);

    // UV
    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(VkParticleVertex, uv);

    return attributeDescriptions;
}

// ============================================================================
// vkCParticleEffect - Constructor
// ============================================================================
vkCParticleEffect::vkCParticleEffect()
    : m_Def(nullptr),
      m_HandleEffect(-1),
      m_dynamicVB(nullptr),
      m_maxParticles(10000),
      m_fElapsedTime(0.f),
      m_fTimeLimit(-1.f)
{
    m_XFORM.identity();

    // Initialize visibility data
    vis.box.set(Fvector{-10, -10, -10}, Fvector{10, 10, 10});
    vis.sphere.P.set(0, 0, 0);
    vis.sphere.R = 10.f;
}

// ============================================================================
// vkCParticleEffect - Destructor
// ============================================================================
vkCParticleEffect::~vkCParticleEffect()
{
    OnDeviceDestroy();
    m_HandleEffect = -1;
}

// ============================================================================
// Compile - Initialize from definition
// ============================================================================
BOOL vkCParticleEffect::Compile(PS::CPEDef* def)
{
    if (!def) {
        Msg("![Vulkan] Cannot compile particle effect: definition is nullptr");
        return FALSE;
    }

    m_Def = def;

    // Get parameters from definition
    m_maxParticles = (def->m_MaxParticles > 0) ? def->m_MaxParticles : 1000;
    m_fTimeLimit = def->m_fTimeLimit;

    Msg("[Vulkan] Particle effect compiled: %s (max_particles=%u, time_limit=%.2f)",
        def->m_Name.c_str(), m_maxParticles, m_fTimeLimit);

    return TRUE;
}

// ============================================================================
// Play - Start playing the effect
// ============================================================================
void vkCParticleEffect::Play()
{
    m_RT_Flags.set(flRT_Playing, TRUE);
    m_RT_Flags.set(flRT_DeferredStop, FALSE);
    m_fElapsedTime = 0.f;
}

// ============================================================================
// Stop - Stop playing the effect
// ============================================================================
void vkCParticleEffect::Stop(BOOL bDeferredStop)
{
    if (bDeferredStop) {
        m_RT_Flags.set(flRT_DeferredStop, TRUE);
    } else {
        m_RT_Flags.set(flRT_Playing, FALSE);
    }
}

// ============================================================================
// OnFrame - Update with elapsed time
// ============================================================================
void vkCParticleEffect::OnFrame(u32 dt)
{
    if (!IsPlaying()) {
        return;
    }

    m_fElapsedTime += dt / 1000.f;

    // Check time limit
    if (m_fTimeLimit > 0.f && m_fElapsedTime >= m_fTimeLimit) {
        Stop(FALSE);
    }
}

// ============================================================================
// Render - Main rendering method
// ============================================================================
void vkCParticleEffect::Render(float LOD)
{
    if (!IsPlaying() || !m_Def) {
        return;
    }

    // Lazy-create pipeline on first render
    if (!IsPipelineReady()) {
        if (!CreatePipeline()) {
            Msg("![Vulkan] Failed to create particle pipeline for rendering");
            return;
        }
    }

    // Check if we have resources
    if (!m_dynamicVB) {
        Msg("![Vulkan] No vertex buffer for particle rendering");
        return;
    }

    // Calculate number of particles to render
    u32 particleCount = ParticlesCount();
    if (particleCount == 0) {
        return;
    }

    // Clamp to max particles
    if (particleCount > m_maxParticles) {
        particleCount = m_maxParticles;
    }

    // HUD mode handling — switch to HUD projection for muzzle flashes etc.
    Fmatrix FTold;
    bool bHudMode = !!GetHudMode();
    if (bHudMode)
    {
        FTold = Device.mFullTransform;
        Device.mFullTransform = Device.mFullTransformHud;
        RCache.set_xform_project(Device.mProjectHud);
        RImplementation.rmNear();
    }

    // Allocate temporary vertex buffer for particle quads
    u32 vertexCount = particleCount * 6;  // 6 vertices per particle (2 triangles)
    VkParticleVertex* vertices = (VkParticleVertex*)_alloca(vertexCount * sizeof(VkParticleVertex));

    // Generate billboard quads for particles
    GenerateBillboardQuads(vertices, particleCount);

    // Update dynamic vertex buffer
    UpdateDynamicBuffer(vertices, vertexCount);

    // Get current command buffer
    VkCommandBuffer cmd = CommandManager.GetCurrentCommandBuffer();
    if (cmd == VK_NULL_HANDLE) {
        if (bHudMode) {
            RImplementation.rmNormal();
            Device.mFullTransform = FTold;
            RCache.set_xform_project(Device.mProject);
        }
        return;
    }

    // Bind all resources (pipeline, VB, descriptors, push constants)
    BindResources(cmd);

    // Draw particles
    vkCmdDraw(cmd, vertexCount, 1, 0, 0);

    // Restore projection if HUD mode
    if (bHudMode)
    {
        RImplementation.rmNormal();
        Device.mFullTransform = FTold;
        RCache.set_xform_project(Device.mProject);
    }
}

// ============================================================================
// GetTimeLimit
// ============================================================================
float vkCParticleEffect::GetTimeLimit()
{
    return m_fTimeLimit;
}

// ============================================================================
// Name - Get effect name
// ============================================================================
const shared_str vkCParticleEffect::Name()
{
    if (!m_Def) {
        return shared_str("");
    }
    return m_Def->m_Name;
}

// ============================================================================
// ParticlesCount - Get current particle count
// ============================================================================
u32 vkCParticleEffect::ParticlesCount()
{
    // TODO: Integrate with ParticleManager/PAPI for real particle count
    // For now, return estimate based on max particles
    if (!IsPlaying()) return 0;

    // Stub: return a fraction of max particles based on elapsed time
    if (m_fTimeLimit > 0.f && m_fElapsedTime > 0.f) {
        float progress = m_fElapsedTime / m_fTimeLimit;
        progress = fmodf(progress, 1.0f);  // Loop
        return (u32)(m_maxParticles * progress);
    }

    return m_maxParticles / 2;  // Default: half of max
}

// ============================================================================
// OnDeviceCreate - Initialize Vulkan resources
// ============================================================================
void vkCParticleEffect::OnDeviceCreate()
{
    if (!m_Def) {
        Msg("![Vulkan] Cannot create particle resources: no definition");
        return;
    }

    // Create dynamic vertex buffer for particles
    // Each particle = 6 vertices (2 triangles)
    u32 maxVertices = m_maxParticles * 6;
    u32 bufferSize = maxVertices * sizeof(VkParticleVertex);

    if (bufferSize > 0) {
        m_dynamicVB = xr_new<VK::CVulkanBuffer>();

        // Create dynamic vertex buffer (HOST_VISIBLE for CPU updates)
        m_dynamicVB->Create(
            bufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU  // CPU writes, GPU reads
        );

        if (!m_dynamicVB->IsValid()) {
            Msg("![Vulkan] Failed to create particle vertex buffer");
            xr_delete(m_dynamicVB);
            return;
        }

        Msg("[Vulkan] Particle vertex buffer created: %u vertices (%u KB)",
            maxVertices, bufferSize / 1024);
    }

    // Get texture from material (if shader was created)
    // TODO: Implement texture extraction from shader when shader system is ready
    if (m_Def->m_CachedShader._get()) {
        Msg("[Vulkan] Particle shader available: %s", m_Def->m_TextureName.c_str());
    }

    // Create descriptor set for texture binding
    // TODO: Allocate from descriptor pool and update with texture

    Msg("[Vulkan] Particle effect resources created: %s", m_Def->m_Name.c_str());
}

// ============================================================================
// OnDeviceDestroy - Device destruction
// ============================================================================
void vkCParticleEffect::OnDeviceDestroy()
{
    DestroyPipeline();
    DestroyDescriptorSet();
}

// ============================================================================
// CreatePipeline - Create particle rendering pipeline on-demand
// ============================================================================
bool vkCParticleEffect::CreatePipeline()
{
    if (m_pipeline != VK_NULL_HANDLE) {
        return true;  // Already created
    }

    VkDevice device = VulkanHW.GetDevice();
    if (device == VK_NULL_HANDLE) {
        Msg("![Vulkan] Cannot create particle pipeline: device not initialized");
        return false;
    }

    // Create descriptor set layout for particle texture
    if (m_descriptorSetLayout == VK_NULL_HANDLE) {
        if (!vkParticleCustom::CreateDescriptorSetLayout(device, m_descriptorSetLayout)) {
            Msg("![Vulkan] Failed to create particle descriptor set layout");
            return false;
        }
    }

    // Configure pipeline for particle rendering
    ParticlePipelineConfig config;
    config.depthTest = false;    // No depth test for particles
    config.depthWrite = false;   // No depth write
    config.cullMode = VK_CULL_MODE_NONE;  // Billboards need both sides
    config.srcBlend = VK_BLEND_FACTOR_SRC_ALPHA;
    config.dstBlend = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;  // Alpha blending
    config.blendOp = VK_BLEND_OP_ADD;

    // Create pipeline helper
    vkParticlePipeline* pipelineHelper = xr_new<vkParticlePipeline>();
    if (!pipelineHelper->Create(device, config, m_descriptorSetLayout)) {
        Msg("![Vulkan] Failed to create particle pipeline");
        xr_delete(pipelineHelper);
        return false;
    }

    // Store pipeline and layout
    m_pipeline = pipelineHelper->pipeline;
    m_pipelineLayout = pipelineHelper->pipelineLayout;

    // Don't delete the helper yet - we stored references
    // Pipeline will be cleaned up in DestroyPipeline()

    Msg("[Vulkan] Particle pipeline created successfully");
    return true;
}

// ============================================================================
// Private Helper Methods
// ============================================================================

// ============================================================================
// UpdateDynamicBuffer - Update vertex buffer with particle data
// ============================================================================
void vkCParticleEffect::UpdateDynamicBuffer(VkParticleVertex* data, u32 vertexCount)
{
    if (!m_dynamicVB || vertexCount == 0) {
        return;
    }

    // Check if buffer is large enough
    u32 requiredSize = vertexCount * sizeof(VkParticleVertex);
    if (requiredSize > m_dynamicVB->GetSize()) {
        Msg("![Vulkan] Particle buffer too small: need %u bytes, have %u",
            requiredSize, m_dynamicVB->GetSize());
        return;
    }

    // Copy data to buffer (buffer is HOST_VISIBLE and mapped)
    void* mappedData = m_dynamicVB->Map();
    if (mappedData) {
        memcpy(mappedData, data, requiredSize);
        m_dynamicVB->Unmap();
    } else {
        Msg("![Vulkan] Failed to map particle vertex buffer");
    }
}

// ============================================================================
// BindResources - Bind resources to command buffer
// ============================================================================
void vkCParticleEffect::BindResources(VkCommandBuffer cmd)
{
    if (m_pipeline == VK_NULL_HANDLE) {
        return;
    }

    // Bind pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    // Bind vertex buffer
    if (m_dynamicVB) {
        VkBuffer vertexBuffers[] = { m_dynamicVB->GetHandle() };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    }

    // Bind descriptor set (texture)
    if (m_descriptorSet != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(
            cmd,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_pipelineLayout,
            0,  // Set 0
            1,
            &m_descriptorSet,
            0,
            nullptr
        );
    }

    // Set push constants (view-projection matrix)
    Fmatrix viewProj;
    viewProj.mul(Device.mProject, Device.mView);
    vkCmdPushConstants(
        cmd,
        m_pipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT,
        0,
        sizeof(Fmatrix),
        &viewProj
    );

    // Set viewport and scissor (dynamic state)
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)Device.dwWidth;
    viewport.height = (float)Device.dwHeight;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {Device.dwWidth, Device.dwHeight};
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

// ============================================================================
// GenerateBillboardQuads - Generate billboard geometry for particles
// ============================================================================
void vkCParticleEffect::GenerateBillboardQuads(VkParticleVertex* vertices, u32 particleCount)
{
    if (!vertices || particleCount == 0) {
        return;
    }

    // Get camera right and up vectors for billboarding from view matrix
    Fmatrix& viewMatrix = Device.mView;
    Fvector camRight = {viewMatrix._11, viewMatrix._21, viewMatrix._31};
    Fvector camUp = {viewMatrix._12, viewMatrix._22, viewMatrix._32};

    // Generate stub particles in a circle pattern (for testing)
    float angleStep = (2.0f * 3.14159f) / particleCount;

    for (u32 i = 0; i < particleCount; i++)
    {
        // Calculate particle position (circular pattern)
        float angle = i * angleStep;
        float radius = 5.0f;
        Fvector particlePos;
        particlePos.x = cosf(angle) * radius;
        particlePos.y = 2.0f + sinf(m_fElapsedTime * 2.0f + angle) * 0.5f;  // Bobbing
        particlePos.z = sinf(angle) * radius;

        // Apply transformation matrix
        m_XFORM.transform_tiny(particlePos);

        // Particle size
        float size = 0.5f;

        // Particle color (fade based on age)
        float alpha = 1.0f - (float)i / (float)particleCount;
        u32 color = color_rgba_f(1.0f, 1.0f, 1.0f, alpha);

        // Generate quad (6 vertices = 2 triangles)
        Fvector v0 = particlePos; v0.mad(camRight, -size); v0.mad(camUp, -size);
        Fvector v1 = particlePos; v1.mad(camRight,  size); v1.mad(camUp, -size);
        Fvector v2 = particlePos; v2.mad(camRight,  size); v2.mad(camUp,  size);
        Fvector v3 = particlePos; v3.mad(camRight, -size); v3.mad(camUp,  size);

        u32 idx = i * 6;

        // Triangle 1 (v0, v1, v2)
        vertices[idx + 0].pos[0] = v0.x; vertices[idx + 0].pos[1] = v0.y; vertices[idx + 0].pos[2] = v0.z;
        vertices[idx + 0].color = color;
        vertices[idx + 0].uv[0] = 0.0f; vertices[idx + 0].uv[1] = 0.0f;

        vertices[idx + 1].pos[0] = v1.x; vertices[idx + 1].pos[1] = v1.y; vertices[idx + 1].pos[2] = v1.z;
        vertices[idx + 1].color = color;
        vertices[idx + 1].uv[0] = 1.0f; vertices[idx + 1].uv[1] = 0.0f;

        vertices[idx + 2].pos[0] = v2.x; vertices[idx + 2].pos[1] = v2.y; vertices[idx + 2].pos[2] = v2.z;
        vertices[idx + 2].color = color;
        vertices[idx + 2].uv[0] = 1.0f; vertices[idx + 2].uv[1] = 1.0f;

        // Triangle 2 (v0, v2, v3)
        vertices[idx + 3].pos[0] = v0.x; vertices[idx + 3].pos[1] = v0.y; vertices[idx + 3].pos[2] = v0.z;
        vertices[idx + 3].color = color;
        vertices[idx + 3].uv[0] = 0.0f; vertices[idx + 3].uv[1] = 0.0f;

        vertices[idx + 4].pos[0] = v2.x; vertices[idx + 4].pos[1] = v2.y; vertices[idx + 4].pos[2] = v2.z;
        vertices[idx + 4].color = color;
        vertices[idx + 4].uv[0] = 1.0f; vertices[idx + 4].uv[1] = 1.0f;

        vertices[idx + 5].pos[0] = v3.x; vertices[idx + 5].pos[1] = v3.y; vertices[idx + 5].pos[2] = v3.z;
        vertices[idx + 5].color = color;
        vertices[idx + 5].uv[0] = 0.0f; vertices[idx + 5].uv[1] = 1.0f;
    }
}
