// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

// ============================================================================
// vk_ParticleDescriptors.h - Particle descriptor pool management
// ============================================================================

#pragma once

#include <vulkan/vulkan.h>

// ============================================================================
// Global Descriptor Pool for Particles
// ============================================================================
class vkParticleDescriptorPool
{
public:
    // Initialize descriptor pool for particles
    static bool Initialize(VkDevice device);

    // Shutdown descriptor pool
    static void Shutdown(VkDevice device);

    // Allocate descriptor set from pool
    static VkDescriptorSet Allocate(VkDevice device, VkDescriptorSetLayout layout);

    // Get the pool handle
    static VkDescriptorPool GetPool() { return s_DescriptorPool; }

private:
    static VkDescriptorPool s_DescriptorPool;
    static bool s_Initialized;

    // Max 1000 particle effects with textures
    static constexpr u32 MAX_PARTICLE_EFFECTS = 1000;
    static constexpr u32 MAX_COMBINED_IMAGE_SAMPLERS = 1000;
};

// Global particle descriptor pool
extern vkParticleDescriptorPool g_ParticleDescriptorPool;
