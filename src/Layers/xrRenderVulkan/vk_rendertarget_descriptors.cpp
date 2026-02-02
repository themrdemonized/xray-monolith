// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#include "stdafx.h"
#include "vk_rendertarget.h"
#include "vk_descriptors.h"
#include "HW_Vulkan.h"
#include "rvk.h"

namespace VK
{

// ============================================================================
// CreateGBufferDescriptorSet() - Create descriptor set for G-Buffer textures
// ============================================================================

VkDescriptorSet CRenderTarget::CreateGBufferDescriptorSet()
{
	if (!g_DescriptorManager) {
		Msg("![Vulkan] DescriptorManager not initialized");
		return VK_NULL_HANDLE;
	}

	// Allocate from Set 1 (PerMaterial layout)
	// В deferred shading мы используем Set 1 для G-Buffer текстур
	VkDescriptorSet set = g_DescriptorManager->AllocatePerMaterial();

	if (set == VK_NULL_HANDLE) {
		Msg("![Vulkan] Failed to allocate G-Buffer descriptor set");
		return VK_NULL_HANDLE;
	}

	// Update bindings
	UpdateGBufferDescriptorSet(set);

	Msg("[Vulkan] G-Buffer descriptor set created");
	return set;
}

// ============================================================================
// CreateSunDescriptorSet() - Create descriptor set for shadow map + sun uniforms
// ============================================================================

VkDescriptorSet CRenderTarget::CreateSunDescriptorSet()
{
	if (!g_DescriptorManager) {
		Msg("![Vulkan] DescriptorManager not initialized");
		return VK_NULL_HANDLE;
	}

	// Allocate from Set 3 (Lighting layout)
	VkDescriptorSet set = g_DescriptorManager->AllocateLighting();

	if (set == VK_NULL_HANDLE) {
		Msg("![Vulkan] Failed to allocate sun descriptor set");
		return VK_NULL_HANDLE;
	}

	Msg("[Vulkan] Sun descriptor set created (bindings will be updated per-cascade)");
	return set;
}

// ============================================================================
// UpdateGBufferDescriptorSet() - Update G-Buffer texture bindings
// ============================================================================

void CRenderTarget::UpdateGBufferDescriptorSet(VkDescriptorSet set)
{
	if (set == VK_NULL_HANDLE) {
		Msg("![Vulkan] Invalid descriptor set");
		return;
	}

	// G-Buffer bindings (Set 1):
	// - Binding 0: rt_Position   (eye-space position)
	// - Binding 1: rt_Normal     (eye-space normal + hemi)
	// - Binding 2: rt_Color      (albedo/diffuse)
	// - Binding 3: rt_Material   (PBR: metallic/roughness/SSS/AO)
	// - Binding 4: rt_Accumulator (accumulated lighting) - Phase 2.18
	// - Binding 5: rt_Distortion  (distortion map for magnifier) - Phase 2.20

	VkDescriptorImageInfo imageInfos[6] = {};

	// Binding 0: Position
	imageInfos[0].imageView = rt_Position.m_ImageView;
	imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	imageInfos[0].sampler = rt_Position.GetSampler();

	// Binding 1: Normal
	imageInfos[1].imageView = rt_Normal.m_ImageView;
	imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	imageInfos[1].sampler = rt_Normal.GetSampler();

	// Binding 2: Diffuse/Color
	imageInfos[2].imageView = rt_Color.m_ImageView;
	imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	imageInfos[2].sampler = rt_Color.GetSampler();

	// Binding 3: Material
	imageInfos[3].imageView = rt_Material.m_ImageView;
	imageInfos[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	imageInfos[3].sampler = rt_Material.GetSampler();

	// Binding 4: Accumulator (Phase 2.18)
	imageInfos[4].imageView = rt_Accumulator.m_ImageView;
	imageInfos[4].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	imageInfos[4].sampler = rt_Accumulator.GetSampler();

	// Binding 5: Distortion (Phase 2.20 - magnifier glass effect)
	imageInfos[5].imageView = rt_Distortion.m_ImageView;
	imageInfos[5].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	imageInfos[5].sampler = rt_Distortion.GetSampler();

	// Update descriptor set
	VkWriteDescriptorSet writes[6] = {};

	for (u32 i = 0; i < 6; ++i) {
		writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[i].dstSet = set;
		writes[i].dstBinding = i;
		writes[i].dstArrayElement = 0;
		writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[i].descriptorCount = 1;
		writes[i].pImageInfo = &imageInfos[i];
	}

	vkUpdateDescriptorSets(VulkanHW.m_Device, 6, writes, 0, nullptr);
}

// ============================================================================
// UpdateSunDescriptorSet() - Update shadow map + sun uniform bindings
// ============================================================================

void CRenderTarget::UpdateSunDescriptorSet(VkDescriptorSet set, u32 cascade_ind)
{
	if (set == VK_NULL_HANDLE) {
		Msg("![Vulkan] Invalid descriptor set");
		return;
	}

	// Sun bindings (Set 3):
	// - Binding 0: Shadow map (sampler2DShadow for hardware PCF)
	// - Binding 1: Sun uniform buffer (light dir, color, cascade matrices, splits)

	VkWriteDescriptorSet writes[2] = {};
	u32 writeCount = 0;

	// ========================================================================
	// Binding 0: Shadow map (depth texture)
	// ========================================================================

	VkDescriptorImageInfo shadowMapInfo = {};
	shadowMapInfo.imageView = rt_smap_depth.m_ImageView;
	shadowMapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	shadowMapInfo.sampler = rt_smap_depth.GetSampler();  // TODO: Create shadow sampler with compare mode

	writes[writeCount].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[writeCount].dstSet = set;
	writes[writeCount].dstBinding = 0;
	writes[writeCount].dstArrayElement = 0;
	writes[writeCount].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	writes[writeCount].descriptorCount = 1;
	writes[writeCount].pImageInfo = &shadowMapInfo;
	writeCount++;

	// ========================================================================
	// Binding 1: Sun uniform buffer
	// ========================================================================

	// TODO Phase 2.15 (production): Create sun uniform buffer
	// Должен содержать:
	// - vec4 Ldynamic_dir      (light direction in view space)
	// - vec4 Ldynamic_color    (RGB + specular intensity)
	// - mat4 m_shadow_near     (cascade 0 matrix)
	// - mat4 m_shadow_middle   (cascade 1 matrix)
	// - mat4 m_shadow_far      (cascade 2 matrix)
	// - vec4 cascade_splits    (split distances: near, middle, far, unused)
	//
	// Для простоты, можно использовать push constants (уже реализованы)
	// или создать small uniform buffer

	// For now, skip uniform buffer binding (will use push constants)
	// writes[writeCount].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	// writes[writeCount].dstSet = set;
	// writes[writeCount].dstBinding = 1;
	// ...

	// Update descriptor set
	vkUpdateDescriptorSets(VulkanHW.m_Device, writeCount, writes, 0, nullptr);

	// Sun descriptor set updated
}

// ============================================================================
// CreatePointDescriptorSet() - Create descriptor set для point light
// ============================================================================
//
// Phase 2.16.5: Point Light Accumulation
//
// Set 3 (Lighting):
//   Binding 0: samplerCube s_smap (shadow cubemap)
//   Binding 1: uniform buffer (point light data)
//
// ============================================================================

VkDescriptorSet CRenderTarget::CreatePointDescriptorSet()
{
	Msg("[Vulkan] Creating point light descriptor set...");

	// Allocate descriptor set (using Set 3 layout)
	VkDescriptorSet set = g_DescriptorManager->AllocatePerMaterial();

	if (set == VK_NULL_HANDLE) {
		Msg("![Vulkan] Failed to allocate point light descriptor set");
		return VK_NULL_HANDLE;
	}

	Msg("[Vulkan] Point light descriptor set created (will be updated on use)");
	return set;
}

// ============================================================================
// UpdatePointDescriptorSet() - Update point light descriptor set
// ============================================================================
//
// Updates bindings for current point light:
// - Shadow cubemap (rt_smap_cube)
// - Light uniforms (position, color, range)
//
// ============================================================================

void CRenderTarget::UpdatePointDescriptorSet(VkDescriptorSet set, light* L)
{
	// Updating point light descriptor set

	if (set == VK_NULL_HANDLE) {
		Msg("![Vulkan] Cannot update NULL descriptor set");
		return;
	}

	if (!L) {
		Msg("![Vulkan] Cannot update descriptor set: NULL light");
		return;
	}

	// Validate cubemap resources before updating descriptor set
	if (rt_smap_cube.m_ImageView == VK_NULL_HANDLE || rt_smap_cube.GetSampler() == VK_NULL_HANDLE)
	{
		Msg("![Vulkan] Point light descriptor set update skipped: cubemap not ready (imageView=%p, sampler=%p)",
			(void*)rt_smap_cube.m_ImageView, (void*)rt_smap_cube.GetSampler());
		return;
	}

	VkWriteDescriptorSet writes[2] = {};
	u32 writeCount = 0;

	// ========================================================================
	// Binding 0: Shadow cubemap (samplerCube)
	// ========================================================================
	VkDescriptorImageInfo cubeInfo = {};
	cubeInfo.imageView = rt_smap_cube.m_ImageView;
	cubeInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	cubeInfo.sampler = rt_smap_cube.GetSampler();

	writes[writeCount].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[writeCount].dstSet = set;
	writes[writeCount].dstBinding = 0;
	writes[writeCount].dstArrayElement = 0;
	writes[writeCount].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	writes[writeCount].descriptorCount = 1;
	writes[writeCount].pImageInfo = &cubeInfo;
	writeCount++;

	// Update descriptor set
	vkUpdateDescriptorSets(VulkanHW.m_Device, writeCount, writes, 0, nullptr);
}

// ============================================================================
// CreateSpotDescriptorSet() - Create spot light descriptor set
// ============================================================================
//
// Phase 2.17.5: Spot Light Accumulation
//
// Allocates descriptor set for spot light data (Set 3):
// - Binding 0: 2D shadow map (rt_smap_depth)
//
// ============================================================================

VkDescriptorSet CRenderTarget::CreateSpotDescriptorSet()
{
	Msg("[Vulkan] Creating spot light descriptor set...");

	// Allocate descriptor set (using Set 3 layout)
	VkDescriptorSet set = g_DescriptorManager->AllocatePerMaterial();

	if (set == VK_NULL_HANDLE) {
		Msg("![Vulkan] Failed to allocate spot light descriptor set");
		return VK_NULL_HANDLE;
	}

	Msg("[Vulkan] Spot light descriptor set created (will be updated on use)");
	return set;
}

// ============================================================================
// UpdateSpotDescriptorSet() - Update spot light descriptor set
// ============================================================================
//
// Phase 2.17.5: Spot Light Accumulation
//
// Updates bindings for current spot light:
// - 2D shadow map (rt_smap_depth with sampler)
//
// Note: Light parameters (position, direction, color, shadow matrix, cone params)
//       are passed via push constants (192 bytes total).
//
// ============================================================================

void CRenderTarget::UpdateSpotDescriptorSet(VkDescriptorSet set, light* L)
{
	// Updating spot light descriptor set

	if (set == VK_NULL_HANDLE) {
		Msg("![Vulkan] Cannot update NULL descriptor set");
		return;
	}

	if (!L) {
		Msg("![Vulkan] Cannot update descriptor set: NULL light");
		return;
	}

	VkWriteDescriptorSet writes[1] = {};
	u32 writeCount = 0;

	// ========================================================================
	// Binding 0: 2D Shadow map (sampler2D)
	// ========================================================================
	// Spot light uses rt_smap_depth (2D shadow map with atlasing)
	// Sampler should have:
	// - VK_FILTER_LINEAR for smooth shadows
	// - VK_COMPARE_OP_LESS_OR_EQUAL for shadow comparison
	// - VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE (to avoid atlas bleeding)

	// Validate shadow map resources before updating descriptor set
	if (rt_smap_depth.m_ImageView == VK_NULL_HANDLE || rt_smap_depth.GetSampler() == VK_NULL_HANDLE)
	{
		Msg("![Vulkan] Spot light descriptor set update skipped: shadow map not ready (imageView=%p, sampler=%p)",
			(void*)rt_smap_depth.m_ImageView, (void*)rt_smap_depth.GetSampler());
		return;
	}

	VkDescriptorImageInfo smapInfo = {};
	smapInfo.imageView = rt_smap_depth.m_ImageView;
	smapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	smapInfo.sampler = rt_smap_depth.GetSampler();

	writes[writeCount].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[writeCount].dstSet = set;
	writes[writeCount].dstBinding = 0;
	writes[writeCount].dstArrayElement = 0;
	writes[writeCount].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	writes[writeCount].descriptorCount = 1;
	writes[writeCount].pImageInfo = &smapInfo;
	writeCount++;

	// Update descriptor set
	vkUpdateDescriptorSets(VulkanHW.m_Device, writeCount, writes, 0, nullptr);
}

} // namespace VK
