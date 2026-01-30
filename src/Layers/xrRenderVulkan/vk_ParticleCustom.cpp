// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

// ============================================================================
// vk_ParticleCustom.cpp - Base particle implementation
// ============================================================================

#include "stdafx.h"
#include "vk_ParticleCustom.h"
#include "vk_core.h"

// ============================================================================
// vkParticleCustom - Constructor
// ============================================================================
vkParticleCustom::vkParticleCustom()
{
    vis.box.set(Fvector{-1, -1, -1}, Fvector{1, 1, 1});
    vis.sphere.P.set(0, 0, 0);
    vis.sphere.R = 1.f;
}

// ============================================================================
// vkParticleCustom - Destructor
// ============================================================================
vkParticleCustom::~vkParticleCustom()
{
    DestroyPipeline();
    DestroyDescriptorSet();
}

// ============================================================================
// DestroyPipeline
// ============================================================================
void vkParticleCustom::DestroyPipeline()
{
    if (m_pipeline != VK_NULL_HANDLE) {
        // Pipeline will be destroyed when device is destroyed
        // For now, just null it out
        m_pipeline = VK_NULL_HANDLE;
    }

    if (m_pipelineLayout != VK_NULL_HANDLE) {
        m_pipelineLayout = VK_NULL_HANDLE;
    }
}

// ============================================================================
// DestroyDescriptorSet
// ============================================================================
void vkParticleCustom::DestroyDescriptorSet()
{
    if (m_descriptorSet != VK_NULL_HANDLE) {
        // Descriptor sets are automatically freed when pool is destroyed
        m_descriptorSet = VK_NULL_HANDLE;
    }

    if (m_descriptorSetLayout != VK_NULL_HANDLE) {
        // Layout can be reused, so we don't destroy it here
        // It will be destroyed when the device is destroyed
        m_descriptorSetLayout = VK_NULL_HANDLE;
    }
}

// ============================================================================
// CreateDescriptorSetLayout - Helper to create descriptor set layout
// ============================================================================
// static
bool vkParticleCustom::CreateDescriptorSetLayout(
    VkDevice device,
    VkDescriptorSetLayout& outLayout)
{
    // Particle shader has one texture binding
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    binding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &outLayout) != VK_SUCCESS) {
        Msg("![Vulkan] Failed to create particle descriptor set layout");
        return false;
    }

    return true;
}

// ============================================================================
// AllocateDescriptorSet - Allocate a descriptor set for this particle
// ============================================================================
bool vkParticleCustom::AllocateDescriptorSet(
    VkDevice device,
    VkDescriptorPool pool,
    VkDescriptorSetLayout layout)
{
    VERIFY(device != VK_NULL_HANDLE);
    VERIFY(pool != VK_NULL_HANDLE);
    VERIFY(layout != VK_NULL_HANDLE);

    m_descriptorSetLayout = layout;

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    if (vkAllocateDescriptorSets(device, &allocInfo, &m_descriptorSet) != VK_SUCCESS) {
        Msg("![Vulkan] Failed to allocate particle descriptor set");
        return false;
    }

    return true;
}

// ============================================================================
// UpdateTextureBinding - Update texture in descriptor set
// ============================================================================
void vkParticleCustom::UpdateTextureBinding(
    VkDevice device,
    VkImageView textureView,
    VkSampler sampler)
{
    if (m_descriptorSet == VK_NULL_HANDLE) {
        return;  // Not allocated yet
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = textureView;
    imageInfo.sampler = sampler;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_descriptorSet;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}
