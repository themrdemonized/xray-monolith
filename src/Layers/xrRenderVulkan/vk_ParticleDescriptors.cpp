// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

// ============================================================================
// vk_ParticleDescriptors.cpp - Descriptor pool implementation
// ============================================================================

#include "stdafx.h"
#include "vk_ParticleDescriptors.h"

// Static member initialization
VkDescriptorPool vkParticleDescriptorPool::s_DescriptorPool = VK_NULL_HANDLE;
bool vkParticleDescriptorPool::s_Initialized = false;

// Global instance
vkParticleDescriptorPool g_ParticleDescriptorPool;

// ============================================================================
// Initialize - Create descriptor pool
// ============================================================================
bool vkParticleDescriptorPool::Initialize(VkDevice device)
{
    if (device == VK_NULL_HANDLE) {
        Msg("![Vulkan] Cannot initialize particle descriptor pool: device is null");
        return false;
    }

    if (s_DescriptorPool != VK_NULL_HANDLE) {
        Msg("[Vulkan] Particle descriptor pool already initialized");
        return true;
    }

    // Pool size for combined image samplers (textures)
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = MAX_COMBINED_IMAGE_SAMPLERS;

    // Create descriptor pool
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = MAX_PARTICLE_EFFECTS;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;  // Allow individual deallocation

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &s_DescriptorPool) != VK_SUCCESS) {
        Msg("![Vulkan] Failed to create particle descriptor pool");
        return false;
    }

    s_Initialized = true;
    Msg("[Vulkan] Particle descriptor pool created (max %u effects)", MAX_PARTICLE_EFFECTS);
    return true;
}

// ============================================================================
// Shutdown - Destroy descriptor pool
// ============================================================================
void vkParticleDescriptorPool::Shutdown(VkDevice device)
{
    if (s_DescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, s_DescriptorPool, nullptr);
        s_DescriptorPool = VK_NULL_HANDLE;
    }

    s_Initialized = false;
    Msg("[Vulkan] Particle descriptor pool destroyed");
}

// ============================================================================
// Allocate - Allocate descriptor set from pool
// ============================================================================
VkDescriptorSet vkParticleDescriptorPool::Allocate(VkDevice device, VkDescriptorSetLayout layout)
{
    if (s_DescriptorPool == VK_NULL_HANDLE) {
        Msg("![Vulkan] Cannot allocate descriptor set: pool not initialized");
        return VK_NULL_HANDLE;
    }

    if (layout == VK_NULL_HANDLE) {
        Msg("![Vulkan] Cannot allocate descriptor set: layout is null");
        return VK_NULL_HANDLE;
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = s_DescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet) != VK_SUCCESS) {
        Msg("![Vulkan] Failed to allocate particle descriptor set");
        return VK_NULL_HANDLE;
    }

    return descriptorSet;
}
