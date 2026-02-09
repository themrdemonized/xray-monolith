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
#include "vk_descriptors.h"
#include "vk_dlss.h"

namespace VK
{

// ============================================================================
// phase_tonemap() - HDR to LDR Tonemap Pass (rt_HDR/rt_DlssOutput → Swapchain)
// ============================================================================
//
// Final pass before UI: reads HDR input, applies ACES tonemapping, vignette,
// and gamma correction, then outputs to swapchain (LDR).
//
// When DLSS is active:
//   ... → forward(→rt_HDR) → DLSS(rt_HDR→rt_DlssOutput) → tonemap(rt_DlssOutput→swapchain)
// When DLSS is off:
//   ... → forward(→rt_HDR) → tonemap(rt_HDR→swapchain)
//
// Uses fullscreen triangle trick (no vertex buffer needed).
//
// ============================================================================

void CRenderTarget::phase_tonemap()
{
	VkCommandBuffer cmd = RCache.GetCommandBuffer();
	if (cmd == VK_NULL_HANDLE) return;

	// ========================================================================
	// Step 1: Get swapchain as render target
	// ========================================================================
	VkImage swapImage = Swapchain.GetCurrentImage();
	VkImageView swapView = Swapchain.GetCurrentImageView();
	u32 swapWidth = Swapchain.GetWidth();
	u32 swapHeight = Swapchain.GetHeight();

	if (swapImage == VK_NULL_HANDLE || swapView == VK_NULL_HANDLE) {
		return;
	}

	// Choose input source: DLSS output (display res) or rt_HDR (render res)
	bool bDlssActive = g_DlssManager.IsFeatureCreated() && rt_DlssOutput.m_Image != VK_NULL_HANDLE;
	CRT& tonemapInput = bDlssActive ? rt_DlssOutput : rt_HDR;

	VkImage hdrImage = tonemapInput.m_Image;
	if (hdrImage == VK_NULL_HANDLE) {
		return;
	}

	// ========================================================================
	// Step 2: Transition swapchain to COLOR_ATTACHMENT
	// ========================================================================
	// NOTE: When DLSS is active, rt_DlssOutput is already in SHADER_READ_ONLY
	// (transitioned by phase_dlss). When DLSS is off, rt_HDR transition to
	// SHADER_READ_ONLY is done by phase_exposure(). Fallback here if exposure
	// is not ready and DLSS is not active.
	if (!m_bExposureReady && !bDlssActive)
	{
		VkImageMemoryBarrier hdrBarrier = {};
		hdrBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		hdrBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		hdrBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		hdrBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		hdrBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		hdrBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		hdrBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		hdrBarrier.image = hdrImage;
		hdrBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &hdrBarrier);
	}

	// Swapchain: UNDEFINED → COLOR_ATTACHMENT
	{
		VkImageMemoryBarrier swapBarrier = {};
		swapBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		swapBarrier.srcAccessMask = 0;
		swapBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		swapBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		swapBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		swapBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		swapBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		swapBarrier.image = swapImage;
		swapBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			0, 0, nullptr, 0, nullptr, 1, &swapBarrier);
	}

	// ========================================================================
	// Step 3: Begin rendering to swapchain
	// ========================================================================
	VkRenderingAttachmentInfo colorAttachment = {};
	colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	colorAttachment.imageView = swapView;
	colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

	VkRenderingInfo renderingInfo = {};
	renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	renderingInfo.renderArea.offset = {0, 0};
	renderingInfo.renderArea.extent = {swapWidth, swapHeight};
	renderingInfo.layerCount = 1;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachments = &colorAttachment;

	vkCmdBeginRendering(cmd, &renderingInfo);

	// ========================================================================
	// Step 4: Setup viewport and scissor
	// ========================================================================
	VkViewport viewport = {};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float)swapWidth;
	viewport.height = (float)swapHeight;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor = {};
	scissor.offset = {0, 0};
	scissor.extent = {swapWidth, swapHeight};
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	// ========================================================================
	// Step 5: Load tonemap shaders
	// ========================================================================
	VkShaderModule vertShader = g_ShaderManager->Load("tonemap.vert.spv");
	VkShaderModule fragShader = g_ShaderManager->Load("tonemap.frag.spv");

	if (vertShader == VK_NULL_HANDLE || fragShader == VK_NULL_HANDLE) {
		// Fallback: just clear swapchain
		vkCmdEndRendering(cmd);
		// Transition swapchain to PRESENT_SRC
		VkImageMemoryBarrier presentBarrier = {};
		presentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		presentBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		presentBarrier.dstAccessMask = 0;
		presentBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		presentBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		presentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		presentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		presentBarrier.image = swapImage;
		presentBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			0, 0, nullptr, 0, nullptr, 1, &presentBarrier);
		Swapchain.m_bRenderedThisFrame = true;
		return;
	}

	// ========================================================================
	// Step 6: Create tonemap pipeline (output to swapchain LDR format)
	// ========================================================================
	PipelineConfig config;
	config.vertShader = vertShader;
	config.fragShader = fragShader;
	config.useDefaultVertexInput = false;  // Fullscreen triangle - no vertex buffer
	config.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	config.cullMode = VK_CULL_MODE_NONE;
	config.depthTest = false;
	config.depthWrite = false;
	config.blendEnable = false;
	config.colorAttachmentCount = 1;
	config.colorFormats[0] = Swapchain.GetFormat();  // LDR swapchain format

	VkPipeline pipeline = g_PipelineManager->GetOrCreate(config);
	if (pipeline == VK_NULL_HANDLE) {
		vkCmdEndRendering(cmd);
		VkImageMemoryBarrier presentBarrier = {};
		presentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		presentBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		presentBarrier.dstAccessMask = 0;
		presentBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		presentBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		presentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		presentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		presentBarrier.image = swapImage;
		presentBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			0, 0, nullptr, 0, nullptr, 1, &presentBarrier);
		Swapchain.m_bRenderedThisFrame = true;
		return;
	}

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	// ========================================================================
	// Step 7: Bind descriptor set (rt_HDR as input texture)
	// ========================================================================
	// Allocate a PerMaterial descriptor set and bind tonemap input + exposure
	if (g_DescriptorManager) {
		VkDescriptorSet tonemapDesc = g_DescriptorManager->AllocatePerMaterial();
		if (tonemapDesc != VK_NULL_HANDLE) {
			// Binding 0: tonemap input (rt_DlssOutput when DLSS active, rt_HDR otherwise)
			VkDescriptorImageInfo hdrImageInfo = {};
			hdrImageInfo.sampler = tonemapInput.GetSampler();
			hdrImageInfo.imageView = tonemapInput.m_ImageView;
			hdrImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			// Binding 1: auto-exposure (1x1 R32F) or fallback to rt_HDR if not ready
			VkDescriptorImageInfo exposureImageInfo = {};
			if (m_bExposureReady && m_ExposureView != VK_NULL_HANDLE && m_ExposureSampler != VK_NULL_HANDLE)
			{
				exposureImageInfo.sampler = m_ExposureSampler;
				exposureImageInfo.imageView = m_ExposureView;
				exposureImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
			}
			else
			{
				// Fallback: bind rt_HDR (shader will read some HDR value, but push constant
				// exposure was 2.2 before — the default in the exposure image handles this)
				exposureImageInfo = hdrImageInfo;
			}

			// Fill all 8 PerMaterial bindings (layout expects 8 combined_image_sampler)
			VkDescriptorImageInfo imageInfos[8] = {};
			VkWriteDescriptorSet writes[8] = {};
			imageInfos[0] = hdrImageInfo;
			imageInfos[1] = exposureImageInfo;
			for (int i = 2; i < 8; i++) {
				imageInfos[i] = hdrImageInfo;  // Unused bindings point to rt_HDR
			}
			for (int i = 0; i < 8; i++) {
				writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writes[i].dstSet = tonemapDesc;
				writes[i].dstBinding = i;
				writes[i].dstArrayElement = 0;
				writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writes[i].descriptorCount = 1;
				writes[i].pImageInfo = &imageInfos[i];
			}
			vkUpdateDescriptorSets(VulkanHW.GetDevice(), 8, writes, 0, nullptr);

			VkPipelineLayout layout = g_PipelineManager->GetLayout();
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
			                        1, 1, &tonemapDesc, 0, nullptr);
		}
	}

	// ========================================================================
	// Step 8: Push constants (tonemapping parameters)
	// ========================================================================
	struct TonemapPushConstants {
		float exposure;
		float vignetteInner;
		float vignetteOuter;
		float vignetteIntensity;
		u32 toneMappingMode;
		float pad0, pad1, pad2;       // Padding to match combine push constant layout
		float pad3;                   // distortionScale
		u32 pad4;                     // enableDistortion
		float pad5, pad6, pad7;       // sunDir
		float pad8, pad9, pad10;      // sunColor
	} pushData = {};

	pushData.exposure = 2.2f;
	pushData.vignetteInner = 0.4f;
	pushData.vignetteOuter = 1.0f;
	pushData.vignetteIntensity = 0.3f;
	pushData.toneMappingMode = 2;  // ACES

	VkPipelineLayout layout = g_PipelineManager->GetLayout();
	vkCmdPushConstants(cmd, layout,
	                   VK_SHADER_STAGE_FRAGMENT_BIT,
	                   0, sizeof(TonemapPushConstants), &pushData);

	// ========================================================================
	// Step 9: Draw fullscreen triangle
	// ========================================================================
	vkCmdDraw(cmd, 3, 1, 0, 0);

	// ========================================================================
	// Step 10: End rendering
	// ========================================================================
	vkCmdEndRendering(cmd);

	// ========================================================================
	// Step 11: Transition swapchain to PRESENT_SRC
	// ========================================================================
	VkImageMemoryBarrier presentBarrier = {};
	presentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	presentBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	presentBarrier.dstAccessMask = 0;
	presentBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	presentBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	presentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	presentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	presentBarrier.image = swapImage;
	presentBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

	vkCmdPipelineBarrier(cmd,
	                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
	                     VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
	                     0, 0, nullptr, 0, nullptr, 1, &presentBarrier);

	Swapchain.m_bRenderedThisFrame = true;
}

} // namespace VK
