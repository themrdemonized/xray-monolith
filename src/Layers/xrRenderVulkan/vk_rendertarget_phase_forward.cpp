// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#include "stdafx.h"
#include "vk_rendertarget.h"
#include "HW_Vulkan.h"
#include "vk_R_Backend.h"
#include "vk_shaders.h"
#include "vk_pipeline.h"
#include "vk_swapchain.h"
#include "rvk.h"

// vk_WallmarksEngine.h NOT included here — CSkeletonWallmark dependency issues.
// Wallmarks->Render() is called from rvk.cpp between phase_wallmarks_begin/end.
// #include "vk_DetailManager.h"  // VK::CDetailManager full definition

namespace VK
{

// ============================================================================
// phase_forward() - Forward Rendering Pass
// ============================================================================
//
// Phase 2.19: Forward Pass
//
// Forward pass рендерит прозрачные объекты, частицы и эффекты которые
// не подходят для deferred rendering (требуют alpha blending).
//
// Process:
// 1. Render to swapchain (после combine pass)
// 2. Use G-Buffer depth (read-only) для correct occlusion
// 3. Alpha blending enabled
// 4. Back-to-front sorting для transparency
// 5. Per-object lighting (directional + N closest point lights)
//
// Differences from Deferred:
// - Alpha blending support
// - Multiple materials per pixel
// - Per-object lighting (not screen-space)
// - Higher cost (O(objects * lights) vs O(pixels))
//
// ============================================================================

void CRenderTarget::phase_forward()
{

	VkCommandBuffer cmd = RCache.GetCommandBuffer();

	// ========================================================================
	// Step 1: Get current swapchain image
	// ========================================================================
	// Forward pass рендерит поверх combine pass result into rt_HDR
	VkImage hdrImage = RTarget->rt_HDR.m_Image;
	VkImageView hdrView = RTarget->rt_HDR.m_ImageView;
	u32 hdrWidth = RTarget->rt_HDR.m_Width;
	u32 hdrHeight = RTarget->rt_HDR.m_Height;

	if (hdrImage == VK_NULL_HANDLE || hdrView == VK_NULL_HANDLE) {
		Msg("![Vulkan] phase_forward: Invalid rt_HDR");
		return;
	}

	// ========================================================================
	// Step 2: Check if we have transparent objects to render
	// ========================================================================
	// TODO Phase 2.19.2: Implement transparent object list + sorting
	//
	// For now: skip if no transparent objects
	// В будущем:
	// - Собрать список transparent visuals
	// - Отсортировать back-to-front (по distance to camera)
	// - Render в правильном порядке

	// Check if we have transparent/sorted geometry or particles to render
	bool hasTransparentObjects = (RImplementation.mapSorted.size() > 0) ||
	                             (RImplementation.mapDistort.size() > 0) ||
	                             (RImplementation.mapEmissive.size() > 0) ||
	                             (RImplementation.lstParticles.size() > 0);

	if (!hasTransparentObjects) {
		return;
	}

	// ========================================================================
	// Step 3: Transition swapchain to COLOR_ATTACHMENT_OPTIMAL
	// ========================================================================
	// rt_HDR already in COLOR_ATTACHMENT_OPTIMAL from previous pass — execution barrier

	VkImageMemoryBarrier colorBarrier = {};
	colorBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	colorBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	colorBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	colorBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	colorBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	colorBarrier.image = hdrImage;
	colorBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	colorBarrier.subresourceRange.baseMipLevel = 0;
	colorBarrier.subresourceRange.levelCount = 1;
	colorBarrier.subresourceRange.baseArrayLayer = 0;
	colorBarrier.subresourceRange.layerCount = 1;

	vkCmdPipelineBarrier(cmd,
	                     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
	                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
	                     0, 0, nullptr, 0, nullptr, 1, &colorBarrier);

	// ========================================================================
	// Step 4: Transition depth buffer to READ-ONLY
	// ========================================================================
	// rt_ZBuffer используется только для depth testing (not writing)
	// G-Buffer pass записал depth, forward pass только читает

	VkImageMemoryBarrier depthBarrier = {};
	depthBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	depthBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	depthBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
	depthBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
	depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
	depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	depthBarrier.image = Swapchain.m_DepthImage;
	depthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	depthBarrier.subresourceRange.baseMipLevel = 0;
	depthBarrier.subresourceRange.levelCount = 1;
	depthBarrier.subresourceRange.baseArrayLayer = 0;
	depthBarrier.subresourceRange.layerCount = 1;

	vkCmdPipelineBarrier(cmd,
	                     VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
	                     VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
	                     0, 0, nullptr, 0, nullptr, 1, &depthBarrier);

	// ========================================================================
	// Step 5: Begin rendering to swapchain (с depth buffer)
	// ========================================================================
	VkRenderingAttachmentInfo colorAttachment = {};
	colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	colorAttachment.imageView = hdrView;
	colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;  // Preserve combine pass result!
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

	VkRenderingAttachmentInfo depthAttachment = {};
	depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	depthAttachment.imageView = Swapchain.m_DepthView;
	depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;   // Keep existing depth
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_NONE;  // Read-only, no writes

	VkRenderingInfo renderingInfo = {};
	renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	renderingInfo.renderArea.offset = {0, 0};
	renderingInfo.renderArea.extent = {hdrWidth, hdrHeight};
	renderingInfo.layerCount = 1;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachments = &colorAttachment;
	renderingInfo.pDepthAttachment = &depthAttachment;

	vkCmdBeginRendering(cmd, &renderingInfo);

	// ========================================================================
	// Step 6: Setup viewport and scissor
	// ========================================================================
	VkViewport viewport = {};
	viewport.x = 0.0f;
	viewport.y = (float)hdrHeight;
	viewport.width = (float)hdrWidth;
	viewport.height = -(float)hdrHeight;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor = {};
	scissor.offset = {0, 0};
	scissor.extent = {hdrWidth, hdrHeight};
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	// ========================================================================
	// Step 7: Load forward shaders
	// ========================================================================
	VkShaderModule vertShader = g_ShaderManager->Load("forward.vert.spv");
	VkShaderModule fragShader = g_ShaderManager->Load("forward.frag.spv");

	if (vertShader == VK_NULL_HANDLE || fragShader == VK_NULL_HANDLE) {
		Msg("![Vulkan] Failed to load forward shaders");
		vkCmdEndRendering(cmd);
		return;
	}

	// ========================================================================
	// Step 8: Create forward pipeline (alpha blending + depth test)
	// ========================================================================
	PipelineConfig config = {};
	config.vertShader = vertShader;
	config.fragShader = fragShader;
	config.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	config.cullMode = VK_CULL_MODE_BACK_BIT;  // Cull back faces

	// Depth testing (read-only)
	config.depthTest = true;
	config.depthWrite = false;  // Don't write depth (read-only)
	config.depthCompare = VK_COMPARE_OP_LESS_OR_EQUAL;

	// Alpha blending
	config.blendEnable = true;
	config.srcColorBlend = VK_BLEND_FACTOR_SRC_ALPHA;
	config.dstColorBlend = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	config.srcAlphaBlend = VK_BLEND_FACTOR_ONE;
	config.dstAlphaBlend = VK_BLEND_FACTOR_ZERO;

	// Render targets
	config.colorAttachmentCount = 1;
	config.colorFormats[0] = VK_FORMAT_R16G16B16A16_SFLOAT;  // HDR output
	config.depthFormat = VK_FORMAT_D32_SFLOAT;          // rt_ZBuffer

	VkPipeline pipeline = g_PipelineManager->GetOrCreate(config);
	if (pipeline == VK_NULL_HANDLE) {
		Msg("![Vulkan] Failed to create forward pipeline");
		vkCmdEndRendering(cmd);
		return;
	}

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	// ========================================================================
	// Step 9: Render transparent objects
	// ========================================================================
	// Forward pass renders multiple types of geometry:
	// 1. LOD objects (flora imposters, distance geometry)
	// 2. Sorted transparent geometry (glass, water, effects)
	// 3. Emissive geometry (glowing objects, lights)
	// 4. Distortion effects (heat shimmer, glass refraction)

	// ========================================================================
	// 9.1: Render LODs (flora, distance imposters)
	// ========================================================================
	// LODs are rendered first with Z-buffer setup for proper depth
	RImplementation.r_dsgraph_render_lods(true, true);

	// ========================================================================
	// 9.2: Render sorted transparent geometry (back-to-front)
	// ========================================================================
	// Glass, water surfaces, etc.
	RImplementation.r_dsgraph_render_sorted();

	// ========================================================================
	// 9.2.1: Render particle effects (own pipeline, alpha-blended)
	// ========================================================================
	RImplementation.r_dsgraph_render_particles();

	// ========================================================================
	// 9.3: Render emissive geometry (self-illuminated objects)
	// ========================================================================
	// Glowing signs, lights, weapon sights, etc.
	// renderHUD=true to include weapon sights and HUD emissives
	RImplementation.r_dsgraph_render_emissive(true, true);

	// ========================================================================
	// 9.4: Render distortion effects (heat shimmer, refraction)
	// ========================================================================
	// Must be rendered after main geometry for proper effect
	RImplementation.r_dsgraph_render_distort();

	// ========================================================================
	// Step 10: End rendering
	// ========================================================================
	vkCmdEndRendering(cmd);

	// ========================================================================
	// Step 11: Transition swapchain back to PRESENT_SRC
	// ========================================================================
	colorBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	colorBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	colorBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorBarrier.image = hdrImage;

	vkCmdPipelineBarrier(cmd,
	                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
	                     VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
	                     0, 0, nullptr, 0, nullptr, 1, &colorBarrier);

	// ========================================================================
	// Step 12: Transition depth buffer back to ATTACHMENT_OPTIMAL
	// ========================================================================
	depthBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
	depthBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	depthBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
	depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
	depthBarrier.image = Swapchain.m_DepthImage;

	vkCmdPipelineBarrier(cmd,
	                     VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
	                     VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
	                     0, 0, nullptr, 0, nullptr, 1, &depthBarrier);

}

// ============================================================================
// phase_wallmarks_begin() - Begin Wallmarks Render Pass
// ============================================================================
void CRenderTarget::phase_wallmarks_begin()
{
	VkCommandBuffer cmd = RCache.GetCommandBuffer();

	VkImage hdrImage = rt_HDR.m_Image;
	VkImageView hdrView = rt_HDR.m_ImageView;
	u32 hdrWidth = rt_HDR.m_Width;
	u32 hdrHeight = rt_HDR.m_Height;

	if (hdrImage == VK_NULL_HANDLE || hdrView == VK_NULL_HANDLE)
		return;

	VkImage depthImage = Swapchain.m_DepthImage;
	VkImageView depthView = Swapchain.m_DepthView;
	if (depthImage == VK_NULL_HANDLE || depthView == VK_NULL_HANDLE)
		return;

	// Transition images for wallmarks rendering
	VkImageMemoryBarrier barriers[2] = {};

	// rt_HDR: already COLOR_ATTACHMENT — execution barrier
	barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	barriers[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	barriers[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	barriers[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barriers[0].image = hdrImage;
	barriers[0].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

	// Depth: DEPTH_ATTACHMENT_OPTIMAL -> DEPTH_READ_ONLY_OPTIMAL
	barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barriers[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	barriers[1].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
	barriers[1].oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
	barriers[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
	barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barriers[1].image = depthImage;
	barriers[1].subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };

	vkCmdPipelineBarrier(cmd,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
		0, 0, nullptr, 0, nullptr, 2, barriers);

	// Begin rendering (rt_HDR + depth read-only)
	VkRenderingAttachmentInfo colorAttachment = {};
	colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	colorAttachment.imageView = hdrView;
	colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

	VkRenderingAttachmentInfo depthAttachment = {};
	depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	depthAttachment.imageView = depthView;
	depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_NONE;  // Read-only, no writes

	VkRenderingInfo renderingInfo = {};
	renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	renderingInfo.renderArea.offset = {0, 0};
	renderingInfo.renderArea.extent = {hdrWidth, hdrHeight};
	renderingInfo.layerCount = 1;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachments = &colorAttachment;
	renderingInfo.pDepthAttachment = &depthAttachment;

	vkCmdBeginRendering(cmd, &renderingInfo);
}

// ============================================================================
// phase_wallmarks_end() - End Wallmarks Render Pass
// ============================================================================
void CRenderTarget::phase_wallmarks_end()
{
	VkCommandBuffer cmd = RCache.GetCommandBuffer();

	vkCmdEndRendering(cmd);

	VkImage hdrImage = rt_HDR.m_Image;

	// Transition images back
	VkImageMemoryBarrier finalBarriers[2] = {};

	// rt_HDR: stay in COLOR_ATTACHMENT_OPTIMAL for next pass (tonemap)
	finalBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	finalBarriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	finalBarriers[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	finalBarriers[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	finalBarriers[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	finalBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	finalBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	finalBarriers[0].image = hdrImage;
	finalBarriers[0].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

	// Depth: DEPTH_READ_ONLY -> DEPTH_ATTACHMENT_OPTIMAL
	finalBarriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	finalBarriers[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
	finalBarriers[1].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	finalBarriers[1].oldLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
	finalBarriers[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
	finalBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	finalBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	finalBarriers[1].image = Swapchain.m_DepthImage;
	finalBarriers[1].subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };

	vkCmdPipelineBarrier(cmd,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
		0, 0, nullptr, 0, nullptr, 2, finalBarriers);
}

} // namespace VK
