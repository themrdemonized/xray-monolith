// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#include "stdafx.h"
#include "HW_Vulkan.h"
#include "vk_swapchain.h"
#include "vk_command_buffer.h"
#include "vk_sync.h"
#include "SH_RT_Vulkan.h"
#include "vk_rendertarget.h"
#include "vk_shaders.h"
#include "vk_descriptors.h"
#include "vk_pipeline.h"
#include "vk_buffer.h"
#include "vk_texture.h"
#include "vk_R_Backend.h"
#include "vk_ui_shader.h"
#include "rvk.h"
#include "../xrRender/dxRenderFactory.h"
#include "../xrRender/FVF.h"
#include "../../Include/xrRender/UIRender.h"
#include "../../Include/xrRender/DebugRender.h"
#include "../../Include/xrRender/DebugShader.h"
#include "../../Include/xrRender/DrawUtils.h"
#include "vk_debug.h"

// Vulkan RenderFactory (defined in vk_RenderFactory.cpp)
extern dxRenderFactory RenderFactoryImpl;

// Forward declarations for external access
extern "C" {
	void VulkanUI_EndPass();
}

#include <chrono>
#include <thread>

// ============================================================================
// VulkanUI Infrastructure - Static resources for UI rendering
// ============================================================================
namespace VulkanUI
{
	// Resources
	static VK::CVulkanBuffer s_VertexBuffer;
	static VkDescriptorSetLayout s_DescriptorSetLayout = VK_NULL_HANDLE;
	static VkDescriptorPool s_DescriptorPool = VK_NULL_HANDLE;
	static VkPipelineLayout s_PipelineLayout = VK_NULL_HANDLE;
	static VkPipeline s_Pipeline = VK_NULL_HANDLE;
	static VK::CVulkanTexture s_WhiteTexture;
	static VkDescriptorSet s_WhiteTextureSet = VK_NULL_HANDLE;

	// Frame state
	static bool s_bUIPassActive = false;
	static u32 s_UIVertexOffset = 0;
	static void* s_pMappedVB = nullptr;

	// Constants
	static const VkDeviceSize VERTEX_BUFFER_SIZE = 256 * 1024;  // 256KB

	// ====================================================================
	// Deferred UI rendering
	// ====================================================================
	// When UI draw calls happen during FrameMove (before the Vulkan command
	// buffer is started in Begin()), we buffer them here and replay after
	// the combine pass finishes. This is necessary because Vulkan requires
	// an explicit command buffer, unlike D3D11's always-available context.
	// ====================================================================
	struct DeferredUICmd
	{
		enum Type { Draw, Scissor, ResetScissor };
		Type type;
		// Draw params
		u32 vertexBufferOffset;
		u32 vertexCount;
		VkDescriptorSet textureSet;
		// Scissor params
		VkRect2D scissorRect;
	};
	static const u32 MAX_DEFERRED_CMDS = 4096;
	static DeferredUICmd s_DeferredCmds[MAX_DEFERRED_CMDS];
	static u32 s_DeferredCmdCount = 0;

	// Per-frame UI diagnostic stats
	static UIFrameStats s_FrameStats;

	// Create all UI resources
	static void Create()
	{
		Msg("[Vulkan UI] Creating UI infrastructure...");

		// 1. Create persistent-mapped vertex buffer
		s_VertexBuffer.Create(
			VERTEX_BUFFER_SIZE,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VMA_MEMORY_USAGE_AUTO_PREFER_HOST
		);
		s_pMappedVB = s_VertexBuffer.Map();
		if (!s_pMappedVB) {
			Msg("![Vulkan UI] Failed to map vertex buffer");
			return;
		}

		// 2. Create descriptor set layout (1 combined image sampler)
		VkDescriptorSetLayoutBinding samplerBinding = {};
		samplerBinding.binding = 0;
		samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		samplerBinding.descriptorCount = 1;
		samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutCreateInfo layoutInfo = {};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = 1;
		layoutInfo.pBindings = &samplerBinding;
		VK_CHECK(vkCreateDescriptorSetLayout(VulkanHW.m_Device, &layoutInfo, nullptr, &s_DescriptorSetLayout));

		// 3. Create descriptor pool (256 sets — enough for all UI textures:
		//    fonts, cursors, menu backgrounds, buttons, icons, etc.)
		VkDescriptorPoolSize poolSize = {};
		poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSize.descriptorCount = 256;

		VkDescriptorPoolCreateInfo poolInfo = {};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.maxSets = 256;
		poolInfo.poolSizeCount = 1;
		poolInfo.pPoolSizes = &poolSize;
		VK_CHECK(vkCreateDescriptorPool(VulkanHW.m_Device, &poolInfo, nullptr, &s_DescriptorPool));

		// 4. Create pipeline layout (1 descriptor set + push constants)
		VkPushConstantRange pushConstant = {};
		pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		pushConstant.offset = 0;
		pushConstant.size = 8;  // vec2 screenSize

		VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutInfo.setLayoutCount = 1;
		pipelineLayoutInfo.pSetLayouts = &s_DescriptorSetLayout;
		pipelineLayoutInfo.pushConstantRangeCount = 1;
		pipelineLayoutInfo.pPushConstantRanges = &pushConstant;
		VK_CHECK(vkCreatePipelineLayout(VulkanHW.m_Device, &pipelineLayoutInfo, nullptr, &s_PipelineLayout));

		// 5. Create white 1x1 texture
		u32 whitePixel = 0xFFFFFFFF;
		s_WhiteTexture.CreateFromData(&whitePixel, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, 4);

		// 6. Allocate descriptor set for white texture
		VkDescriptorSetAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = s_DescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &s_DescriptorSetLayout;
		VK_CHECK(vkAllocateDescriptorSets(VulkanHW.m_Device, &allocInfo, &s_WhiteTextureSet));

		// Update descriptor set
		VkDescriptorImageInfo imageInfo = {};
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfo.imageView = s_WhiteTexture.GetView();
		imageInfo.sampler = s_WhiteTexture.GetSampler();

		VkWriteDescriptorSet write = {};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = s_WhiteTextureSet;
		write.dstBinding = 0;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.descriptorCount = 1;
		write.pImageInfo = &imageInfo;
		vkUpdateDescriptorSets(VulkanHW.m_Device, 1, &write, 0, nullptr);

		// 7. Load UI shaders and create pipeline manually
		if (!g_ShaderManager) {
			Msg("![Vulkan UI] ShaderManager not initialized");
			return;
		}

		VkShaderModule vertShader = g_ShaderManager->Load("ui.vert.spv");
		VkShaderModule fragShader = g_ShaderManager->Load("ui.frag.spv");

		if (vertShader == VK_NULL_HANDLE || fragShader == VK_NULL_HANDLE) {
			Msg("![Vulkan UI] Failed to load UI shaders (ui.vert.spv / ui.frag.spv)");
			return;
		}

		// Create pipeline manually with custom vertex input for FVF::TL format
		// FVF::TL layout: vec4 pos (16 bytes), uint color (4 bytes), vec2 uv (8 bytes) = 28 bytes

		VkPipelineShaderStageCreateInfo shaderStages[2] = {};
		shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		shaderStages[0].module = vertShader;
		shaderStages[0].pName = "main";

		shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		shaderStages[1].module = fragShader;
		shaderStages[1].pName = "main";

		// Vertex input: FVF::TL format (28 bytes stride)
		VkVertexInputBindingDescription binding = {};
		binding.binding = 0;
		binding.stride = 28;  // sizeof(FVF::TL)
		binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		VkVertexInputAttributeDescription attributes[3] = {};
		// location 0: position (vec4)
		attributes[0].binding = 0;
		attributes[0].location = 0;
		attributes[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attributes[0].offset = 0;

		// location 1: color (vec4)
		attributes[1].binding = 0;
		attributes[1].location = 1;
		attributes[1].format = VK_FORMAT_B8G8R8A8_UNORM;
		attributes[1].offset = 16;

		// location 2: texcoord (vec2)
		attributes[2].binding = 0;
		attributes[2].location = 2;
		attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
		attributes[2].offset = 20;

		VkPipelineVertexInputStateCreateInfo vertexInput = {};
		vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertexInput.vertexBindingDescriptionCount = 1;
		vertexInput.pVertexBindingDescriptions = &binding;
		vertexInput.vertexAttributeDescriptionCount = 3;
		vertexInput.pVertexAttributeDescriptions = attributes;

		// Input assembly
		VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
		inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		// Viewport state (dynamic)
		VkPipelineViewportStateCreateInfo viewportState = {};
		viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;

		// Rasterization
		VkPipelineRasterizationStateCreateInfo rasterizer = {};
		rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = VK_CULL_MODE_NONE;
		rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

		// Multisampling (disabled)
		VkPipelineMultisampleStateCreateInfo multisampling = {};
		multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		// Depth/Stencil (disabled for UI)
		VkPipelineDepthStencilStateCreateInfo depthStencil = {};
		depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthStencil.depthTestEnable = VK_FALSE;
		depthStencil.depthWriteEnable = VK_FALSE;

		// Color blending (alpha blending)
		VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
		colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
		                                       VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		colorBlendAttachment.blendEnable = VK_TRUE;
		colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
		colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

		VkPipelineColorBlendStateCreateInfo colorBlending = {};
		colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;

		// Dynamic state
		VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicState = {};
		dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamicState.dynamicStateCount = 2;
		dynamicState.pDynamicStates = dynamicStates;

		// Dynamic Rendering info (Vulkan 1.3)
		VkFormat colorFormat = Swapchain.m_Format;
		VkPipelineRenderingCreateInfo renderingInfo = {};
		renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
		renderingInfo.colorAttachmentCount = 1;
		renderingInfo.pColorAttachmentFormats = &colorFormat;

		// Create graphics pipeline
		VkGraphicsPipelineCreateInfo pipelineInfo = {};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineInfo.pNext = &renderingInfo;
		pipelineInfo.stageCount = 2;
		pipelineInfo.pStages = shaderStages;
		pipelineInfo.pVertexInputState = &vertexInput;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pDepthStencilState = &depthStencil;
		pipelineInfo.pColorBlendState = &colorBlending;
		pipelineInfo.pDynamicState = &dynamicState;
		pipelineInfo.layout = s_PipelineLayout;

		VkResult result = vkCreateGraphicsPipelines(VulkanHW.m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &s_Pipeline);
		if (result != VK_SUCCESS) {
			Msg("![Vulkan UI] Failed to create UI pipeline! Error: %d", result);
			return;
		}

		Msg("[Vulkan UI] UI infrastructure created successfully");
	}

	// Destroy all UI resources
	static void Destroy()
	{
		if (VulkanHW.m_Device == VK_NULL_HANDLE) return;

		Msg("[Vulkan UI] Destroying UI infrastructure...");

		vkDeviceWaitIdle(VulkanHW.m_Device);

		if (s_Pipeline != VK_NULL_HANDLE) {
			vkDestroyPipeline(VulkanHW.m_Device, s_Pipeline, nullptr);
			s_Pipeline = VK_NULL_HANDLE;
		}

		if (s_PipelineLayout != VK_NULL_HANDLE) {
			vkDestroyPipelineLayout(VulkanHW.m_Device, s_PipelineLayout, nullptr);
			s_PipelineLayout = VK_NULL_HANDLE;
		}

		s_WhiteTexture.Destroy();

		if (s_DescriptorPool != VK_NULL_HANDLE) {
			vkDestroyDescriptorPool(VulkanHW.m_Device, s_DescriptorPool, nullptr);
			s_DescriptorPool = VK_NULL_HANDLE;
		}

		if (s_DescriptorSetLayout != VK_NULL_HANDLE) {
			vkDestroyDescriptorSetLayout(VulkanHW.m_Device, s_DescriptorSetLayout, nullptr);
			s_DescriptorSetLayout = VK_NULL_HANDLE;
		}

		if (s_pMappedVB) {
			s_VertexBuffer.Unmap();
			s_pMappedVB = nullptr;
		}
		s_VertexBuffer.Destroy();

		Msg("[Vulkan UI] UI infrastructure destroyed");
	}

	// End UI pass (called at end of frame before command buffer submit)
	static void EndUIPass()
	{
		if (!s_bUIPassActive) return;

		VkCommandBuffer cmd = RCache.m_Cmd;
		if (!cmd) return;

		// End dynamic rendering
		vkCmdEndRendering(cmd);

		// Get swapchain image
		u32 imageIndex = Swapchain.m_CurrentImageIndex;
		VkImage swapchainImage = Swapchain.m_Images[imageIndex];

		// Transition: COLOR_ATTACHMENT -> PRESENT_SRC
		VkImageMemoryBarrier2 barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
		barrier.dstAccessMask = VK_ACCESS_2_NONE;
		barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		barrier.image = swapchainImage;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.layerCount = 1;

		VkDependencyInfo depInfo = {};
		depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		depInfo.imageMemoryBarrierCount = 1;
		depInfo.pImageMemoryBarriers = &barrier;
		vkCmdPipelineBarrier2(cmd, &depInfo);

		Swapchain.m_bRenderedThisFrame = true;

		s_bUIPassActive = false;
		s_UIVertexOffset = 0;  // Reset for next frame
	}

	// Begin UI pass (shared implementation for both immediate and deferred paths)
	static void BeginUIPassInternal()
	{
		VkCommandBuffer cmd = RCache.m_Cmd;
		if (!cmd) return;
		if (s_bUIPassActive) return;

		// Get swapchain image and view
		u32 imageIndex = Swapchain.m_CurrentImageIndex;
		if (imageIndex >= Swapchain.m_Images.size() || imageIndex >= Swapchain.m_ImageViews.size()) {
			vk_warn(std::format("Invalid swapchain imageIndex={} (images={}, views={})",
				imageIndex, (u32)Swapchain.m_Images.size(), (u32)Swapchain.m_ImageViews.size()));
			return;
		}
		VkImage swapchainImage = Swapchain.m_Images[imageIndex];
		VkImageView swapchainView = Swapchain.m_ImageViews[imageIndex];

		if (swapchainImage == VK_NULL_HANDLE || swapchainView == VK_NULL_HANDLE) {
			vk_warn("Swapchain image or view is null, skipping UI pass");
			return;
		}

		// Transition: PRESENT_SRC -> COLOR_ATTACHMENT
		VkImageMemoryBarrier2 barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
		barrier.srcAccessMask = VK_ACCESS_2_NONE;
		barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.image = swapchainImage;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.layerCount = 1;

		VkDependencyInfo depInfo = {};
		depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		depInfo.imageMemoryBarrierCount = 1;
		depInfo.pImageMemoryBarriers = &barrier;
		vkCmdPipelineBarrier2(cmd, &depInfo);

		// Begin dynamic rendering (load previous content - preserves combine output)
		VkRenderingAttachmentInfo colorAttachment = {};
		colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		colorAttachment.imageView = swapchainView;
		colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

		VkRenderingInfo renderInfo = {};
		renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		renderInfo.renderArea.offset = { 0, 0 };
		renderInfo.renderArea.extent = Swapchain.m_Extent;
		renderInfo.layerCount = 1;
		renderInfo.colorAttachmentCount = 1;
		renderInfo.pColorAttachments = &colorAttachment;

		vkCmdBeginRendering(cmd, &renderInfo);

		// Set viewport and scissor
		VkViewport viewport = {};
		viewport.width = (float)Swapchain.m_Extent.width;
		viewport.height = (float)Swapchain.m_Extent.height;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(cmd, 0, 1, &viewport);

		VkRect2D scissor = {};
		scissor.extent = Swapchain.m_Extent;
		vkCmdSetScissor(cmd, 0, 1, &scissor);

		s_bUIPassActive = true;
	}

	// Replay deferred UI draw calls (called from End() after combine pass)
	static void ReplayDeferredUI()
	{
		if (s_DeferredCmdCount == 0) return;

		// Count draws for frame stats
		u32 drawCount = 0;
		for (u32 i = 0; i < s_DeferredCmdCount; i++)
			if (s_DeferredCmds[i].type == DeferredUICmd::Draw) drawCount++;
		s_FrameStats.deferredCmds += s_DeferredCmdCount;
		s_FrameStats.deferredDraws += drawCount;

		VkCommandBuffer cmd = RCache.m_Cmd;
		if (!cmd) {
			s_DeferredCmdCount = 0;
			return;
		}

		// Flush vertex buffer to make all deferred vertex data GPU-visible
		s_VertexBuffer.Flush();

		// Begin UI pass if not already active
		BeginUIPassInternal();

		if (s_Pipeline == VK_NULL_HANDLE) {
			Msg("![Vulkan UI] Cannot replay deferred UI - pipeline not created");
			s_DeferredCmdCount = 0;
			return;
		}

		// Replay all deferred commands
		for (u32 i = 0; i < s_DeferredCmdCount; i++)
		{
			const DeferredUICmd& dcmd = s_DeferredCmds[i];
			switch (dcmd.type)
			{
			case DeferredUICmd::Draw:
			{
				// Bind pipeline
				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s_Pipeline);

				// Push screen dimensions — use Device logical resolution, not swapchain extent
				float screenSize[2] = { (float)Device.dwWidth, (float)Device.dwHeight };
				vkCmdPushConstants(cmd, s_PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, 8, screenSize);

				// Bind vertex buffer at recorded offset
				VkBuffer vbs[] = { s_VertexBuffer.GetHandle() };
				VkDeviceSize offsets[] = { dcmd.vertexBufferOffset };
				vkCmdBindVertexBuffers(cmd, 0, 1, vbs, offsets);

				// Bind texture descriptor
				VkDescriptorSet texSet = (dcmd.textureSet != VK_NULL_HANDLE)
					? dcmd.textureSet : s_WhiteTextureSet;
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
					s_PipelineLayout, 0, 1, &texSet, 0, nullptr);

				// Draw
				vkCmdDraw(cmd, dcmd.vertexCount, 1, 0, 0);
				RCache.stat.calls++;
				RCache.stat.verts += dcmd.vertexCount;
				break;
			}
			case DeferredUICmd::Scissor:
				vkCmdSetScissor(cmd, 0, 1, &dcmd.scissorRect);
				break;
			case DeferredUICmd::ResetScissor:
			{
				VkRect2D fullScissor = {};
				fullScissor.extent = Swapchain.m_Extent;
				vkCmdSetScissor(cmd, 0, 1, &fullScissor);
				break;
			}
			}
		}

		s_DeferredCmdCount = 0;
	}
}

// Extern C functions for vk_RenderFactory.cpp
extern "C" void VulkanUI_EndPass()
{
	VulkanUI::EndUIPass();
}

extern "C" VkDescriptorSetLayout VulkanUI_GetDescriptorSetLayout()
{
	return VulkanUI::s_DescriptorSetLayout;
}

extern "C" VkDescriptorPool VulkanUI_GetDescriptorPool()
{
	return VulkanUI::s_DescriptorPool;
}

extern "C" void VulkanUI_ReplayDeferred()
{
	VulkanUI::ReplayDeferredUI();
}

extern "C" void VulkanUI_ResetState()
{
	VulkanUI::s_bUIPassActive = false;
	VulkanUI::s_UIVertexOffset = 0;
}

extern "C" void VulkanUI_ResetFrameStats(u32 frame)
{
	VulkanUI::s_FrameStats.reset(frame);
}

extern "C" void VulkanUI_LogFrameStats()
{
	VulkanUI::s_FrameStats.log_if_active();
}

// ============================================================================
// Vulkan UIRender - Full implementation
// ============================================================================
class vkUIRender : public IUIRender
{
public:
	vkUIRender() : m_PrimitiveType(ptNone), m_PointType(pttNone), m_pCurrentShader(nullptr), m_CurrentTextureSet(VK_NULL_HANDLE) {}

	void CreateUIGeom() override
	{
		VulkanUI::Create();
	}

	void DestroyUIGeom() override
	{
		VulkanUI::Destroy();
	}

	void SetShader(IUIShader& shader) override
	{
		m_pCurrentShader = &shader;

		// Try to cast to IVkUIShader to get Vulkan-specific resources
		IVkUIShader* vkShader = dynamic_cast<IVkUIShader*>(&shader);
		if (vkShader) {
			m_CurrentTextureSet = vkShader->GetDescriptorSet();
			// If shader doesn't have a texture, GetDescriptorSet() returns VK_NULL_HANDLE
			if (m_CurrentTextureSet == VK_NULL_HANDLE) {
				m_CurrentTextureSet = VulkanUI::s_WhiteTextureSet;
				// Log once per unique shader to help diagnose white square issues
				static xr_set<const void*> s_warnedShaders;
				if (s_warnedShaders.find(vkShader) == s_warnedShaders.end()) {
					s_warnedShaders.insert(vkShader);
					Msg("! [Vulkan UI] SetShader: descriptor=NULL, using white fallback (texW=%u texH=%u)",
						vkShader->GetTextureWidth(), vkShader->GetTextureHeight());
				}
			}
		} else {
			vk_warn("Shader not IVkUIShader, using white fallback");
			m_CurrentTextureSet = VulkanUI::s_WhiteTextureSet;
		}
	}

	void SetAlphaRef(int aref) override
	{
		// Alpha ref not used in Vulkan (handled via blending)
	}

	void SetScissor(Irect* rect) override
	{
		VkCommandBuffer cmd = RCache.m_Cmd;

		if (!cmd) {
			// DEFERRED: buffer scissor change for later replay
			if (VulkanUI::s_DeferredCmdCount < VulkanUI::MAX_DEFERRED_CMDS) {
				VulkanUI::DeferredUICmd& dcmd = VulkanUI::s_DeferredCmds[VulkanUI::s_DeferredCmdCount++];
				if (rect) {
					dcmd.type = VulkanUI::DeferredUICmd::Scissor;
					dcmd.scissorRect.offset.x = rect->x1;
					dcmd.scissorRect.offset.y = rect->y1;
					dcmd.scissorRect.extent.width = rect->x2 - rect->x1;
					dcmd.scissorRect.extent.height = rect->y2 - rect->y1;
				} else {
					dcmd.type = VulkanUI::DeferredUICmd::ResetScissor;
				}
			} else {
				vk_warn("Deferred buffer full, dropping scissor");
				VulkanUI::s_FrameStats.droppedCmds++;
			}
			return;
		}

		// IMMEDIATE: issue scissor command now
		if (!VulkanUI::s_bUIPassActive) return;

		if (rect) {
			VkRect2D scissor = {};
			scissor.offset.x = rect->x1;
			scissor.offset.y = rect->y1;
			scissor.extent.width = rect->x2 - rect->x1;
			scissor.extent.height = rect->y2 - rect->y1;
			vkCmdSetScissor(cmd, 0, 1, &scissor);
		} else {
			VkRect2D scissor = {};
			scissor.offset = { 0, 0 };
			scissor.extent = Swapchain.m_Extent;
			vkCmdSetScissor(cmd, 0, 1, &scissor);
		}
	}

	void GetActiveTextureResolution(Fvector2& res) override
	{
		// Try to get texture resolution from current shader
		if (m_pCurrentShader) {
			IVkUIShader* vkShader = dynamic_cast<IVkUIShader*>(m_pCurrentShader);
			if (vkShader) {
				res.set(float(vkShader->GetTextureWidth()), float(vkShader->GetTextureHeight()));
				return;
			}
		}

		// Fallback to default size
		res.set(1024.f, 1024.f);
	}

	void StartPrimitive(u32 maxVerts, ePrimitiveType primType, ePointType pointType) override
	{
		m_PrimitiveType = primType;
		m_PointType = pointType;
		m_MaxVerts = maxVerts;
		m_VertCount = 0;

		if (!VulkanUI::s_pMappedVB) {
			vk_error("Vertex buffer not mapped");
			m_pWriteTL = nullptr;
			m_pWriteLIT = nullptr;
			return;
		}

		// Calculate required bytes
		u32 vertexSize = (pointType == pttTL) ? sizeof(FVF::TL) : sizeof(FVF::LIT);
		u32 requiredBytes = maxVerts * vertexSize;

		// Check if we need to wrap around
		if (VulkanUI::s_UIVertexOffset + requiredBytes > VulkanUI::VERTEX_BUFFER_SIZE) {
			VulkanUI::s_UIVertexOffset = 0;
			VulkanUI::s_FrameStats.bufferWraps++;
		}

		// Set write pointer
		if (pointType == pttTL) {
			m_pWriteTL = (FVF::TL*)((u8*)VulkanUI::s_pMappedVB + VulkanUI::s_UIVertexOffset);
			m_pWriteTLStart = m_pWriteTL;
		} else {
			m_pWriteLIT = (FVF::LIT*)((u8*)VulkanUI::s_pMappedVB + VulkanUI::s_UIVertexOffset);
			m_pWriteLITStart = m_pWriteLIT;
		}
	}

	void PushPoint(float x, float y, float z, u32 C, float u, float v) override
	{
		if (m_PointType == pttTL) {
			if (m_pWriteTL) {
				m_pWriteTL->set(x, y, z, 1.0f, C, u, v);
				m_pWriteTL++;
				m_VertCount++;
			}
		} else if (m_PointType == pttLIT) {
			if (m_pWriteLIT) {
				m_pWriteLIT->set(x, y, z, C, u, v);
				m_pWriteLIT++;
				m_VertCount++;
			}
		}
	}

	void FlushPrimitive() override
	{
		u32 vertCount = m_VertCount;
		if (vertCount == 0) {
			m_pWriteTL = nullptr;
			m_pWriteLIT = nullptr;
			return;
		}

		VkCommandBuffer cmd = RCache.m_Cmd;
		if (!cmd) {
			// ============================================================
			// DEFERRED MODE: No command buffer yet (called from FrameMove
			// before Begin()). Buffer the draw call for later replay.
			// ============================================================
			if (VulkanUI::s_DeferredCmdCount < VulkanUI::MAX_DEFERRED_CMDS) {
				VulkanUI::DeferredUICmd& dcmd = VulkanUI::s_DeferredCmds[VulkanUI::s_DeferredCmdCount++];
				dcmd.type = VulkanUI::DeferredUICmd::Draw;
				dcmd.vertexBufferOffset = VulkanUI::s_UIVertexOffset;
				dcmd.vertexCount = vertCount;
				dcmd.textureSet = m_CurrentTextureSet;
			} else {
				vk_warn("Deferred buffer full, dropping draw");
				VulkanUI::s_FrameStats.droppedCmds++;
			}

			VulkanUI::s_FrameStats.totalVerts += vertCount;

			// Advance vertex buffer offset (vertex data is already written)
			u32 vertexSize = (m_PointType == pttTL) ? sizeof(FVF::TL) : sizeof(FVF::LIT);
			VulkanUI::s_UIVertexOffset += vertCount * vertexSize;

			m_pWriteTL = nullptr;
			m_pWriteLIT = nullptr;
			m_VertCount = 0;
			return;
		}

		// ============================================================
		// IMMEDIATE MODE: Command buffer available, draw now
		// ============================================================
		VulkanUI::s_VertexBuffer.Flush();

		if (!VulkanUI::s_bUIPassActive) {
			VulkanUI::BeginUIPassInternal();
			// Log dimensions once when UI pass starts
			static bool s_dimLogged = false;
			if (!s_dimLogged) {
				Msg("[Vulkan UI] Dimensions: Device=%ux%u Swapchain=%ux%u",
					Device.dwWidth, Device.dwHeight,
					Swapchain.m_Extent.width, Swapchain.m_Extent.height);
				s_dimLogged = true;
			}
		}

		if (VulkanUI::s_Pipeline == VK_NULL_HANDLE) {
			vk_error("UI pipeline not created, cannot draw");
			return;
		}

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, VulkanUI::s_Pipeline);

		// Use Device dimensions (game's logical resolution) — UI vertices are in
		// [0..dwWidth, 0..dwHeight] space, NOT swapchain extent which may differ.
		float screenSize[2] = { (float)Device.dwWidth, (float)Device.dwHeight };
		vkCmdPushConstants(cmd, VulkanUI::s_PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, 8, screenSize);

		VkBuffer vertexBuffers[] = { VulkanUI::s_VertexBuffer.GetHandle() };
		VkDeviceSize offsets[] = { VulkanUI::s_UIVertexOffset };
		vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);

		VkDescriptorSet textureSet = (m_CurrentTextureSet != VK_NULL_HANDLE) ? m_CurrentTextureSet : VulkanUI::s_WhiteTextureSet;
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			VulkanUI::s_PipelineLayout, 0, 1, &textureSet, 0, nullptr);

		vkCmdDraw(cmd, vertCount, 1, 0, 0);

		RCache.stat.calls++;
		RCache.stat.verts += vertCount;
		VulkanUI::s_FrameStats.immediateCalls++;
		VulkanUI::s_FrameStats.totalVerts += vertCount;

		u32 vertexSize = (m_PointType == pttTL) ? sizeof(FVF::TL) : sizeof(FVF::LIT);
		VulkanUI::s_UIVertexOffset += vertCount * vertexSize;

		m_pWriteTL = nullptr;
		m_pWriteLIT = nullptr;
		m_VertCount = 0;
	}

	LPCSTR UpdateShaderName(LPCSTR tex_name, LPCSTR sh_name) override
	{
		return sh_name;
	}

	void CacheSetXformWorld(const Fmatrix& M) override
	{
		// Not used for UI rendering
	}

	void CacheSetCullMode(CullMode mode) override
	{
		// Not used for UI rendering
	}

private:
	ePrimitiveType m_PrimitiveType;
	ePointType m_PointType;
	IUIShader* m_pCurrentShader;
    VkDescriptorSet m_CurrentTextureSet;
	u32 m_MaxVerts;
	u32 m_VertCount;

	// Write pointers
	FVF::TL* m_pWriteTL;
	FVF::TL* m_pWriteTLStart;
	FVF::LIT* m_pWriteLIT;
	FVF::LIT* m_pWriteLITStart;
};
static vkUIRender VulkanUIRenderImpl;

// ============================================================================
// Vulkan stub: DebugRender
// ============================================================================
class vkDebugRender : public IDebugRender
{
public:
	void Render() override {}
	void add_lines(Fvector const*, u32 const&, u16 const*, u32 const&, u32 const&, bool) override {}
	void NextSceneMode() override {}
	void ZEnable(bool) override {}
	void OnFrameEnd() override {}
	void SetShader(const debug_shader&) override {}
	void CacheSetXformWorld(const Fmatrix&) override {}
	void CacheSetCullMode(CullMode) override {}
	void SetAmbient(u32) override {}
	void SetDebugShader(dbgShaderHandle) override {}
	void DestroyDebugShader(dbgShaderHandle) override {}
	void dbg_DrawTRI(Fmatrix&, Fvector&, Fvector&, Fvector&, u32) override {}
};
static vkDebugRender VulkanDebugRenderImpl;

static VK::CRT g_TestRT;
static VK::CVulkanTexture g_TestTexture;

// Console initialization (implemented in vk_console.cpp)
extern void xrRender_initconsole();

// Тест CRT класса
static void TestCRT_Create()
{
    Msg("[Vulkan TEST] Creating test render target...");

    // Создаём тестовый RT (RGBA8, 512x512)
    g_TestRT.Create(
        VK_FORMAT_R8G8B8A8_UNORM,
        512, 512,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        false  // not depth
    );

    Msg("[Vulkan TEST] Test RT created: %dx%d, format %d",
        g_TestRT.m_Width, g_TestRT.m_Height, g_TestRT.m_Format);
}

static void TestCRT_Destroy()
{
    Msg("[Vulkan TEST] Destroying test render target...");
    g_TestRT.Destroy();
}

// Тест G-Buffer
static void TestGBuffer_Create()
{
    Msg("[Vulkan TEST] Creating G-Buffer...");

    // Создаём глобальный RTarget
    RTarget = xr_new<VK::CRenderTarget>();

    // Создаём G-Buffer с размерами swapchain
    RTarget->Create(Swapchain.m_Extent.width, Swapchain.m_Extent.height);

    Msg("[Vulkan TEST] G-Buffer created successfully");
}

// Тест Shader Manager
static void TestShaderManager_Create()
{
    Msg("[Vulkan TEST] Creating Shader Manager...");

    // Создаём глобальный SPIR-V Loader
    g_ShaderManager = xr_new<VK::CVulkanSPIRVLoader>();

    Msg("[Vulkan TEST] SPIR-V Loader created");
}

static void TestShaderManager_Load()
{
    if (!g_ShaderManager) {
        Msg("![Vulkan TEST] ShaderManager not initialized");
        return;
    }

    Msg("[Vulkan TEST] Testing shader loading...");

    // Загружаем упрощённые шейдеры (без uniform buffers)
    VkShaderModule testSimpleVS = g_ShaderManager->Load("test_simple.vert.spv");
    VkShaderModule testSimpleFS = g_ShaderManager->Load("test_simple.frag.spv");

    if (testSimpleVS != VK_NULL_HANDLE) {
        Msg("[Vulkan TEST] test_simple.vert.spv loaded ✓");
    } else {
        Msg("![Vulkan TEST] test_simple.vert.spv NOT found!");
    }

    if (testSimpleFS != VK_NULL_HANDLE) {
        Msg("[Vulkan TEST] test_simple.frag.spv loaded ✓");
    } else {
        Msg("![Vulkan TEST] test_simple.frag.spv NOT found!");
    }

    // Пытаемся загрузить тестовый шейдер (оригинальные - с uniform buffers)
    VkShaderModule testVS = g_ShaderManager->Load("test.vert.spv");
    VkShaderModule testFS = g_ShaderManager->Load("test.frag.spv");

    if (testVS != VK_NULL_HANDLE) {
        Msg("[Vulkan TEST] Vertex shader loaded successfully");
    } else {
        Msg("[Vulkan TEST] Vertex shader NOT found (expected - create test.vert.spv)");
    }

    if (testFS != VK_NULL_HANDLE) {
        Msg("[Vulkan TEST] Fragment shader loaded successfully");
    } else {
        Msg("[Vulkan TEST] Fragment shader NOT found (expected - create test.frag.spv)");
    }

    // Тест кэширования
    if (testVS != VK_NULL_HANDLE) {
        VkShaderModule cachedVS = g_ShaderManager->Load("test.vert.spv");
        if (cachedVS == testVS) {
            Msg("[Vulkan TEST] Shader caching works (same module returned)");
        } else {
            Msg("![Vulkan TEST] Shader caching FAILED");
        }
    }

    Msg("[Vulkan TEST] Shader Manager test complete (loaded: %u shaders)",
        g_ShaderManager->GetLoadedCount());
}

static void TestShaderManager_Destroy()
{
    if (g_ShaderManager) {
        Msg("[Vulkan TEST] Destroying Shader Manager...");
        xr_delete(g_ShaderManager);
        g_ShaderManager = nullptr;
        Msg("[Vulkan TEST] Shader Manager destroyed");
    }
}

// Тест Descriptor Manager
static void TestDescriptorManager_Create()
{
    Msg("[Vulkan TEST] Creating Descriptor Manager...");

    // Создаём глобальный DescriptorManager
    g_DescriptorManager = xr_new<VK::CVulkanDescriptorManager>();

    // Создаём layouts и pool
    g_DescriptorManager->Create();

    Msg("[Vulkan TEST] Descriptor Manager created");
}

static void TestDescriptorManager_Allocate()
{
    if (!g_DescriptorManager) {
        Msg("![Vulkan TEST] DescriptorManager not initialized");
        return;
    }

    Msg("[Vulkan TEST] Testing descriptor set allocation...");

    // Тест allocation для каждого типа
    VkDescriptorSet perFrameSet = g_DescriptorManager->AllocatePerFrame();
    if (perFrameSet != VK_NULL_HANDLE) {
        Msg("[Vulkan TEST] PerFrame descriptor set allocated");
    } else {
        Msg("![Vulkan TEST] Failed to allocate PerFrame set");
    }

    VkDescriptorSet perMaterialSet = g_DescriptorManager->AllocatePerMaterial();
    if (perMaterialSet != VK_NULL_HANDLE) {
        Msg("[Vulkan TEST] PerMaterial descriptor set allocated");
    } else {
        Msg("![Vulkan TEST] Failed to allocate PerMaterial set");
    }

    VkDescriptorSet perObjectSet = g_DescriptorManager->AllocatePerObject();
    if (perObjectSet != VK_NULL_HANDLE) {
        Msg("[Vulkan TEST] PerObject descriptor set allocated");
    } else {
        Msg("![Vulkan TEST] Failed to allocate PerObject set");
    }

    VkDescriptorSet lightingSet = g_DescriptorManager->AllocateLighting();
    if (lightingSet != VK_NULL_HANDLE) {
        Msg("[Vulkan TEST] Lighting descriptor set allocated");
    } else {
        Msg("![Vulkan TEST] Failed to allocate Lighting set");
    }

    Msg("[Vulkan TEST] Descriptor allocation test complete (allocated: %u sets)",
        g_DescriptorManager->GetAllocatedSets());
}

static void TestDescriptorManager_Destroy()
{
    if (g_DescriptorManager) {
        Msg("[Vulkan TEST] Destroying Descriptor Manager...");
        xr_delete(g_DescriptorManager);
        g_DescriptorManager = nullptr;
        Msg("[Vulkan TEST] Descriptor Manager destroyed");
    }
}

// Тест Pipeline Manager
static void TestPipelineManager_Create()
{
    Msg("[Vulkan TEST] Creating Pipeline Manager...");

    // Создаём глобальный PipelineManager
    VK::g_PipelineManager = xr_new<VK::CVulkanPipelineManager>();

    // Создаём pipeline layout и cache
    VK::g_PipelineManager->Create();

    Msg("[Vulkan TEST] Pipeline Manager created");
}

static void TestPipelineManager_CreatePipeline()
{
    if (!VK::g_PipelineManager || !g_ShaderManager) {
        Msg("![Vulkan TEST] PipelineManager or ShaderManager not initialized");
        return;
    }

    Msg("[Vulkan TEST] Testing graphics pipeline creation...");

    // Загружаем test шейдеры
    VkShaderModule testVS = g_ShaderManager->Load("test.vert.spv");
    VkShaderModule testFS = g_ShaderManager->Load("test.frag.spv");

    if (testVS == VK_NULL_HANDLE || testFS == VK_NULL_HANDLE) {
        Msg("[Vulkan TEST] Test shaders not found, skipping pipeline test");
        return;
    }

    // Создаём конфигурацию для G-Buffer pass
    VK::PipelineConfig config = {};
    config.vertShader = testVS;
    config.fragShader = testFS;
    config.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    config.cullMode = VK_CULL_MODE_BACK_BIT;
    config.frontFace = VK_FRONT_FACE_CLOCKWISE;
    config.depthTest = true;
    config.depthWrite = true;
    config.depthCompareOp = VK_COMPARE_OP_LESS;
    config.blendEnable = false;  // No blending для G-Buffer

    // G-Buffer имеет 4 color attachments
    config.colorAttachmentCount = 4;
    config.colorFormats[0] = VK_FORMAT_R32G32B32A32_SFLOAT;  // rt_Position
    config.colorFormats[1] = VK_FORMAT_R32G32B32A32_SFLOAT;  // rt_Normal
    config.colorFormats[2] = VK_FORMAT_R8G8B8A8_SRGB;        // rt_Color
    config.colorFormats[3] = VK_FORMAT_R8G8B8A8_UNORM;       // rt_Material

    config.depthFormat = VK_FORMAT_D32_SFLOAT;
    config.useDefaultVertexInput = true;  // position, texcoord, normal

    // Создаём pipeline
    VkPipeline pipeline = VK::g_PipelineManager->GetOrCreate(config);

    if (pipeline != VK_NULL_HANDLE) {
        Msg("[Vulkan TEST] Graphics pipeline created successfully");

        // Тест кэширования - повторный вызов должен вернуть тот же pipeline
        VkPipeline cachedPipeline = VK::g_PipelineManager->GetOrCreate(config);
        if (cachedPipeline == pipeline) {
            Msg("[Vulkan TEST] Pipeline caching works (same handle returned)");
        } else {
            Msg("![Vulkan TEST] Pipeline caching FAILED");
        }
    } else {
        Msg("![Vulkan TEST] Failed to create graphics pipeline");
    }

    Msg("[Vulkan TEST] Pipeline test complete (cached: %u pipelines)",
        VK::g_PipelineManager->GetCachedPipelineCount());
}

static void TestPipelineManager_Destroy()
{
    if (VK::g_PipelineManager) {
        Msg("[Vulkan TEST] Destroying Pipeline Manager...");
        xr_delete(VK::g_PipelineManager);
        VK::g_PipelineManager = nullptr;
        Msg("[Vulkan TEST] Pipeline Manager destroyed");
    }
}

static VK::CVulkanBuffer g_TestVertexBuffer;
static VK::CVulkanBuffer g_TestIndexBuffer;
static VK::CVulkanBuffer g_TestUniformBuffer;

// Глобальные descriptor sets для рендера
static VkDescriptorSet g_PerFrameSet = VK_NULL_HANDLE;
static VkDescriptorSet g_PerObjectSet = VK_NULL_HANDLE;

// Глобальный pipeline для рендера треугольника
static VkPipeline g_TrianglePipeline = VK_NULL_HANDLE;

// Тест Buffers
static void TestBuffers_Create()
{
    Msg("[Vulkan TEST] Testing buffer creation...");

    // ========================================================================
    // Test 1: Vertex Buffer
    // ========================================================================
    {
        // Тестовые vertex данные (треугольник)
        // Format: vec3 position + vec2 texcoord + vec3 normal
        float vertices[] = {
            // Position          TexCoord   Normal
            -0.5f, -0.5f, 0.0f,  0.0f, 0.0f, 0.0f, 0.0f, 1.0f,  // Vertex 0
             0.5f, -0.5f, 0.0f,  1.0f, 0.0f, 0.0f, 0.0f, 1.0f,  // Vertex 1
             0.0f,  0.5f, 0.0f,  0.5f, 1.0f, 0.0f, 0.0f, 1.0f   // Vertex 2
        };

        VkDeviceSize vertexBufferSize = sizeof(vertices);

        Msg("[Vulkan TEST] Creating vertex buffer (%zu bytes)...", vertexBufferSize);

        g_TestVertexBuffer.Create(
            vertexBufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
        );

        if (g_TestVertexBuffer.IsValid()) {
            // Upload vertex data
            g_TestVertexBuffer.Upload(vertices, vertexBufferSize);
            Msg("[Vulkan TEST] Vertex buffer created and uploaded (3 vertices)");
        } else {
            Msg("![Vulkan TEST] Failed to create vertex buffer");
        }
    }

    // ========================================================================
    // Test 2: Index Buffer
    // ========================================================================
    {
        // Тестовые индексы (треугольник)
        u16 indices[] = { 0, 1, 2 };

        VkDeviceSize indexBufferSize = sizeof(indices);

        Msg("[Vulkan TEST] Creating index buffer (%zu bytes)...", indexBufferSize);

        g_TestIndexBuffer.Create(
            indexBufferSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
        );

        if (g_TestIndexBuffer.IsValid()) {
            // Upload index data
            g_TestIndexBuffer.Upload(indices, indexBufferSize);
            Msg("[Vulkan TEST] Index buffer created and uploaded (3 indices)");
        } else {
            Msg("![Vulkan TEST] Failed to create index buffer");
        }
    }

    // ========================================================================
    // Test 3: Uniform Buffer (persistent-mapped)
    // ========================================================================
    {
        // Тестовый uniform buffer для матриц
        struct PerFrameUBO {
            float viewProj[16];  // 4x4 matrix
            float cameraPos[4];  // vec4
        };

        VkDeviceSize uniformBufferSize = sizeof(PerFrameUBO);

        Msg("[Vulkan TEST] Creating uniform buffer (%zu bytes)...", uniformBufferSize);

        g_TestUniformBuffer.Create(
            uniformBufferSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_HOST  // Host-visible для persistent mapping
        );

        if (g_TestUniformBuffer.IsValid()) {
            Msg("[Vulkan TEST] Uniform buffer created");

            // Проверяем persistent mapping
            if (g_TestUniformBuffer.IsMapped()) {
                Msg("[Vulkan TEST] Uniform buffer is persistent-mapped ✓");

                // Записываем тестовые данные
                PerFrameUBO* ubo = (PerFrameUBO*)g_TestUniformBuffer.m_Mapped;

                // Identity matrix
                for (int i = 0; i < 16; ++i) {
                    ubo->viewProj[i] = (i % 5 == 0) ? 1.0f : 0.0f;
                }

                // Camera at origin
                ubo->cameraPos[0] = 0.0f;
                ubo->cameraPos[1] = 0.0f;
                ubo->cameraPos[2] = 0.0f;
                ubo->cameraPos[3] = 1.0f;

                g_TestUniformBuffer.Flush();

                Msg("[Vulkan TEST] Uniform buffer data written");
            } else {
                Msg("![Vulkan TEST] Uniform buffer not persistent-mapped");
            }
        } else {
            Msg("![Vulkan TEST] Failed to create uniform buffer");
        }
    }

    Msg("[Vulkan TEST] Buffer creation test complete");
    Msg("[Vulkan TEST]   - Vertex buffer:  %s", g_TestVertexBuffer.IsValid() ? "OK" : "FAILED");
    Msg("[Vulkan TEST]   - Index buffer:   %s", g_TestIndexBuffer.IsValid() ? "OK" : "FAILED");
    Msg("[Vulkan TEST]   - Uniform buffer: %s", g_TestUniformBuffer.IsValid() ? "OK" : "FAILED");
}

static void TestBuffers_Destroy()
{
    Msg("[Vulkan TEST] Destroying test buffers...");

    g_TestVertexBuffer.Destroy();
    g_TestIndexBuffer.Destroy();
    g_TestUniformBuffer.Destroy();

    Msg("[Vulkan TEST] Test buffers destroyed");
}

// Setup Triangle Render (descriptor sets + pipeline)
static void SetupTriangleRender()
{
    if (!g_DescriptorManager || !VK::g_PipelineManager || !g_ShaderManager) {
        Msg("![Vulkan TEST] Managers not initialized");
        return;
    }

    Msg("[Vulkan TEST] Setting up triangle render...");

    // ========================================================================
    // 1. Allocate descriptor sets
    // ========================================================================
    g_PerFrameSet = g_DescriptorManager->AllocatePerFrame();
    g_PerObjectSet = g_DescriptorManager->AllocatePerObject();

    if (g_PerFrameSet == VK_NULL_HANDLE || g_PerObjectSet == VK_NULL_HANDLE) {
        Msg("![Vulkan TEST] Failed to allocate descriptor sets");
        return;
    }

    // ========================================================================
    // 2. Update descriptor sets с uniform buffers
    // ========================================================================
    // PerFrame set: binding 0 = uniform buffer (viewProj, camera)
    g_DescriptorManager->UpdateBuffer(
        g_PerFrameSet,
        0,
        g_TestUniformBuffer.GetHandle(),
        g_TestUniformBuffer.GetSize()
    );

    // PerObject set: binding 0 = uniform buffer (world matrix)
    // TODO: Создадим отдельный PerObject UBO позже, пока используем тот же
    g_DescriptorManager->UpdateBuffer(
        g_PerObjectSet,
        0,
        g_TestUniformBuffer.GetHandle(),
        g_TestUniformBuffer.GetSize()
    );

    Msg("[Vulkan TEST] Descriptor sets updated");

    // ========================================================================
    // 3. Create graphics pipeline для G-Buffer pass
    // ========================================================================
    // TEMP: Используем упрощённые шейдеры без uniform buffers
    VkShaderModule testVS = g_ShaderManager->Get("test_simple.vert.spv");
    VkShaderModule testFS = g_ShaderManager->Get("test_simple.frag.spv");

    if (testVS == VK_NULL_HANDLE || testFS == VK_NULL_HANDLE) {
        Msg("![Vulkan TEST] Test shaders not loaded");
        return;
    }

    VK::PipelineConfig config = {};
    config.vertShader = testVS;
    config.fragShader = testFS;
    config.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    config.cullMode = VK_CULL_MODE_BACK_BIT;
    config.frontFace = VK_FRONT_FACE_CLOCKWISE;
    config.depthTest = true;
    config.depthWrite = true;
    config.depthCompareOp = VK_COMPARE_OP_LESS;
    config.blendEnable = false;  // No blending для G-Buffer

    // G-Buffer: 4 color attachments
    config.colorAttachmentCount = 4;
    config.colorFormats[0] = VK_FORMAT_R32G32B32A32_SFLOAT;  // rt_Position
    config.colorFormats[1] = VK_FORMAT_R32G32B32A32_SFLOAT;  // rt_Normal
    config.colorFormats[2] = VK_FORMAT_R8G8B8A8_SRGB;        // rt_Color
    config.colorFormats[3] = VK_FORMAT_R8G8B8A8_UNORM;       // rt_Material

    config.depthFormat = VK_FORMAT_D32_SFLOAT;
    config.useDefaultVertexInput = true;

    g_TrianglePipeline = VK::g_PipelineManager->GetOrCreate(config);

    if (g_TrianglePipeline != VK_NULL_HANDLE) {
        Msg("[Vulkan TEST] Triangle pipeline ready");
    } else {
        Msg("![Vulkan TEST] Failed to create triangle pipeline");
    }
}

static void TestGBuffer_Destroy()
{
    Msg("[Vulkan TEST] Destroying G-Buffer...");

    if (RTarget) {
        xr_delete(RTarget);
        RTarget = nullptr;
    }

    Msg("[Vulkan TEST] G-Buffer destroyed");
}

// Тестовый render frame (clear screen)
void TestRenderFrame()
{
    // Lazy initialization - создаём ресурсы при первом вызове
    // (альтернатива: создание через CRender::create())
    static bool resourcesCreated = false;
    static bool triangleSetupDone = false;

    if (!resourcesCreated && VulkanHW.m_Device != VK_NULL_HANDLE && Swapchain.m_Swapchain != VK_NULL_HANDLE) {
        // Create test RT
        TestCRT_Create();

        // Create G-Buffer (if not created by CRender::create())
        if (!RTarget) {
            TestGBuffer_Create();
        }

        // Create managers (if not created by CRender::create())
        if (!g_ShaderManager) {
            TestShaderManager_Create();
        }
        TestShaderManager_Load();  // Always load shaders

        if (!g_DescriptorManager) {
            TestDescriptorManager_Create();
        }
        TestDescriptorManager_Allocate();

        if (!VK::g_PipelineManager) {
            TestPipelineManager_Create();
        }
        TestPipelineManager_CreatePipeline();

        // Create test buffers
        TestBuffers_Create();

        resourcesCreated = true;
    }

    // Setup triangle render после создания буферов
    if (resourcesCreated && !triangleSetupDone) {
        SetupTriangleRender();
        triangleSetupDone = true;
    }

    if (!Swapchain.ShouldRender()) {
        // Окно минимизировано
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return;
    }

    u32 frameIndex = CommandManager.GetCurrentFrame();
    FrameSync& sync = Sync.GetCurrentFrame(frameIndex);

    // 1. Ждём завершения предыдущего frame
    if (!Sync.WaitForFence(frameIndex))
        return;

    // 2. Acquire swapchain image
    u32 imageIndex = Swapchain.AcquireNextImage(sync.imageAvailable);
    if (imageIndex == UINT32_MAX) {
        // Нужно пересоздать swapchain
        return;
    }

    // 3. Сбрасываем fence только после успешного acquire
    Sync.ResetFence(frameIndex);

    // 4. Begin command buffer
    VkCommandBuffer cmd = CommandManager.Begin();

    // ========================================================================
    // PHASE 1: Render Triangle to G-Buffer (если pipeline готов)
    // ========================================================================
    if (triangleSetupDone && g_TrianglePipeline != VK_NULL_HANDLE && RTarget) {
        // 5a. Transition G-Buffer images: UNDEFINED -> COLOR_ATTACHMENT
        VkImageMemoryBarrier2 gbufferBarriers[4] = {};

        // rt_Position
        gbufferBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        gbufferBarriers[0].srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        gbufferBarriers[0].srcAccessMask = VK_ACCESS_2_NONE;
        gbufferBarriers[0].dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        gbufferBarriers[0].dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        gbufferBarriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        gbufferBarriers[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        gbufferBarriers[0].image = RTarget->rt_Position.m_Image;
        gbufferBarriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        gbufferBarriers[0].subresourceRange.baseMipLevel = 0;
        gbufferBarriers[0].subresourceRange.levelCount = 1;
        gbufferBarriers[0].subresourceRange.baseArrayLayer = 0;
        gbufferBarriers[0].subresourceRange.layerCount = 1;

        // rt_Normal
        gbufferBarriers[1] = gbufferBarriers[0];
        gbufferBarriers[1].image = RTarget->rt_Normal.m_Image;

        // rt_Color
        gbufferBarriers[2] = gbufferBarriers[0];
        gbufferBarriers[2].image = RTarget->rt_Color.m_Image;

        // rt_Material
        gbufferBarriers[3] = gbufferBarriers[0];
        gbufferBarriers[3].image = RTarget->rt_Material.m_Image;

        VkDependencyInfo gbufferDep = {};
        gbufferDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        gbufferDep.imageMemoryBarrierCount = 4;
        gbufferDep.pImageMemoryBarriers = gbufferBarriers;
        vkCmdPipelineBarrier2(cmd, &gbufferDep);

        // 5b. Transition G-Buffer depth: UNDEFINED -> DEPTH_ATTACHMENT
        VkImageMemoryBarrier2 depthBarrier = {};
        depthBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        depthBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        depthBarrier.srcAccessMask = VK_ACCESS_2_NONE;
        depthBarrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
        depthBarrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        depthBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depthBarrier.image = Swapchain.m_DepthImage;
        depthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        depthBarrier.subresourceRange.baseMipLevel = 0;
        depthBarrier.subresourceRange.levelCount = 1;
        depthBarrier.subresourceRange.baseArrayLayer = 0;
        depthBarrier.subresourceRange.layerCount = 1;

        VkDependencyInfo depthDep = {};
        depthDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depthDep.imageMemoryBarrierCount = 1;
        depthDep.pImageMemoryBarriers = &depthBarrier;
        vkCmdPipelineBarrier2(cmd, &depthDep);

        // 6. Begin G-Buffer rendering (4 color attachments + depth)
        VkRenderingAttachmentInfo colorAttachments[4] = {};

        // rt_Position
        colorAttachments[0].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachments[0].imageView = RTarget->rt_Position.m_ImageView;
        colorAttachments[0].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachments[0].clearValue.color = { 0.0f, 0.0f, 0.0f, 0.0f };

        // rt_Normal
        colorAttachments[1].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachments[1].imageView = RTarget->rt_Normal.m_ImageView;
        colorAttachments[1].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachments[1].clearValue.color = { 0.0f, 0.0f, 0.0f, 0.0f };

        // rt_Color
        colorAttachments[2].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachments[2].imageView = RTarget->rt_Color.m_ImageView;
        colorAttachments[2].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachments[2].clearValue.color = { 0.0f, 0.0f, 0.0f, 0.0f };

        // rt_Material
        colorAttachments[3].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachments[3].imageView = RTarget->rt_Material.m_ImageView;
        colorAttachments[3].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachments[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachments[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachments[3].clearValue.color = { 0.0f, 0.0f, 0.0f, 0.0f };

        // Depth attachment
        VkRenderingAttachmentInfo depthAttachment = {};
        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachment.imageView = Swapchain.m_DepthView;
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

        VkRenderingInfo gbufferRenderInfo = {};
        gbufferRenderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        gbufferRenderInfo.renderArea.offset = { 0, 0 };
        gbufferRenderInfo.renderArea.extent = Swapchain.m_Extent;
        gbufferRenderInfo.layerCount = 1;
        gbufferRenderInfo.colorAttachmentCount = 4;
        gbufferRenderInfo.pColorAttachments = colorAttachments;
        gbufferRenderInfo.pDepthAttachment = &depthAttachment;

        vkCmdBeginRendering(cmd, &gbufferRenderInfo);

        // 7. Bind pipeline
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_TrianglePipeline);

        // 8. Bind descriptor sets - TEMP DISABLED (test_simple шейдеры не используют uniform buffers)
        // VkDescriptorSet descriptorSets[2] = { g_PerFrameSet, g_PerObjectSet };
        // vkCmdBindDescriptorSets(
        //     cmd,
        //     VK_PIPELINE_BIND_POINT_GRAPHICS,
        //     VK::g_PipelineManager->GetLayout(),
        //     0, 2, descriptorSets,
        //     0, nullptr
        // );

        // 9. Bind vertex buffer
        VkBuffer vertexBuffers[] = { g_TestVertexBuffer.GetHandle() };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);

        // 10. Bind index buffer
        vkCmdBindIndexBuffer(cmd, g_TestIndexBuffer.GetHandle(), 0, VK_INDEX_TYPE_UINT16);

        // 11. Set viewport and scissor
        VkViewport viewport = {};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (float)Swapchain.m_Extent.width;
        viewport.height = (float)Swapchain.m_Extent.height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor = {};
        scissor.offset = { 0, 0 };
        scissor.extent = Swapchain.m_Extent;
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        // 12. Draw indexed (3 indices = 1 triangle)
        vkCmdDrawIndexed(cmd, 3, 1, 0, 0, 0);

        // Update RCache statistics
        RCache.stat.calls++;
        RCache.stat.polys += 1;
        RCache.stat.verts += 3;

        vkCmdEndRendering(cmd);

        // 13. Transition rt_Color: COLOR_ATTACHMENT -> TRANSFER_SRC (для копирования в swapchain)
        VkImageMemoryBarrier2 colorToTransferSrc = {};
        colorToTransferSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        colorToTransferSrc.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        colorToTransferSrc.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        colorToTransferSrc.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        colorToTransferSrc.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        colorToTransferSrc.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorToTransferSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        colorToTransferSrc.image = RTarget->rt_Color.m_Image;
        colorToTransferSrc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        colorToTransferSrc.subresourceRange.baseMipLevel = 0;
        colorToTransferSrc.subresourceRange.levelCount = 1;
        colorToTransferSrc.subresourceRange.baseArrayLayer = 0;
        colorToTransferSrc.subresourceRange.layerCount = 1;

        VkDependencyInfo colorToTransferDep = {};
        colorToTransferDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        colorToTransferDep.imageMemoryBarrierCount = 1;
        colorToTransferDep.pImageMemoryBarriers = &colorToTransferSrc;
        vkCmdPipelineBarrier2(cmd, &colorToTransferDep);
    }

    // ========================================================================
    // PHASE 2: Copy rt_Color to Swapchain (для визуализации)
    // ========================================================================

    // 14. Transition swapchain: UNDEFINED -> TRANSFER_DST
    VkImageMemoryBarrier2 swapchainToTransferDst = {};
    swapchainToTransferDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    swapchainToTransferDst.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    swapchainToTransferDst.srcAccessMask = VK_ACCESS_2_NONE;
    swapchainToTransferDst.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    swapchainToTransferDst.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    swapchainToTransferDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    swapchainToTransferDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    swapchainToTransferDst.image = Swapchain.m_Images[imageIndex];
    swapchainToTransferDst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    swapchainToTransferDst.subresourceRange.baseMipLevel = 0;
    swapchainToTransferDst.subresourceRange.levelCount = 1;
    swapchainToTransferDst.subresourceRange.baseArrayLayer = 0;
    swapchainToTransferDst.subresourceRange.layerCount = 1;

    VkDependencyInfo swapchainToTransferDep = {};
    swapchainToTransferDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    swapchainToTransferDep.imageMemoryBarrierCount = 1;
    swapchainToTransferDep.pImageMemoryBarriers = &swapchainToTransferDst;
    vkCmdPipelineBarrier2(cmd, &swapchainToTransferDep);

    // 15. Copy rt_Color -> Swapchain (если треугольник отрендерен)
    if (triangleSetupDone && g_TrianglePipeline != VK_NULL_HANDLE && RTarget) {
        VkImageBlit blitRegion = {};
        blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blitRegion.srcSubresource.layerCount = 1;
        blitRegion.srcOffsets[0] = { 0, 0, 0 };
        blitRegion.srcOffsets[1] = { (s32)Swapchain.m_Extent.width, (s32)Swapchain.m_Extent.height, 1 };
        blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blitRegion.dstSubresource.layerCount = 1;
        blitRegion.dstOffsets[0] = { 0, 0, 0 };
        blitRegion.dstOffsets[1] = { (s32)Swapchain.m_Extent.width, (s32)Swapchain.m_Extent.height, 1 };

        vkCmdBlitImage(
            cmd,
            RTarget->rt_Color.m_Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            Swapchain.m_Images[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blitRegion,
            VK_FILTER_NEAREST
        );
    } else {
        // Fallback: просто clear swapchain в синий (если нет треугольника)
        VkClearColorValue clearColor = { 0.0f, 0.2f, 0.4f, 1.0f };
        VkImageSubresourceRange clearRange = {};
        clearRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        clearRange.baseMipLevel = 0;
        clearRange.levelCount = 1;
        clearRange.baseArrayLayer = 0;
        clearRange.layerCount = 1;
        vkCmdClearColorImage(cmd, Swapchain.m_Images[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &clearRange);
    }

    // 16. Transition swapchain: TRANSFER_DST -> PRESENT
    VkImageMemoryBarrier2 barrierToPresent = {};
    barrierToPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrierToPresent.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barrierToPresent.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrierToPresent.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
    barrierToPresent.dstAccessMask = VK_ACCESS_2_NONE;
    barrierToPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrierToPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrierToPresent.image = Swapchain.m_Images[imageIndex];
    barrierToPresent.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrierToPresent.subresourceRange.baseMipLevel = 0;
    barrierToPresent.subresourceRange.levelCount = 1;
    barrierToPresent.subresourceRange.baseArrayLayer = 0;
    barrierToPresent.subresourceRange.layerCount = 1;

    VkDependencyInfo depInfo2 = {};
    depInfo2.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo2.imageMemoryBarrierCount = 1;
    depInfo2.pImageMemoryBarriers = &barrierToPresent;
    vkCmdPipelineBarrier2(cmd, &depInfo2);

    // 17. End и submit command buffer
    CommandManager.End(cmd);
    CommandManager.Submit(cmd, sync.imageAvailable, sync.renderFinished, sync.inFlightFence);

    // 18. Present
    Swapchain.Present(sync.renderFinished, imageIndex);

    // 19. Переход к следующему frame
    CommandManager.NextFrame();
}

// Проверка поддержки Vulkan
bool SupportsVulkanRendering()
{
    Msg("[Vulkan] Checking Vulkan support...");

    // Попытка создать instance для проверки
    VkInstance testInstance = VK_NULL_HANDLE;
    if (VK_CreateInstance(&testInstance)) {
        VK_DestroyInstance(testInstance);
        Msg("[Vulkan] Vulkan 1.3 is supported!");
        return true;
    }

    Msg("![Vulkan] Vulkan 1.3 is not supported on this system");
    return false;
}

// Точка входа для статической линковки с движком
BOOL DllMainXrRenderVulkan(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        Msg("[Vulkan] DllMain: DLL_PROCESS_ATTACH");

        // Phase 2: Установка глобальных объектов рендера
        ::Render = &RImplementation;
        ::RenderFactory = &RenderFactoryImpl;
        UIRender = &VulkanUIRenderImpl;
        DRender = &VulkanDebugRenderImpl;

        xrRender_initconsole();
        break;

    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;

    case DLL_PROCESS_DETACH:
        Msg("[Vulkan] DllMain: DLL_PROCESS_DETACH");

        // Cleanup (обратный порядок создания)
        TestBuffers_Destroy();            // Cleanup test buffers
        TestPipelineManager_Destroy();    // Cleanup PipelineManager
        TestDescriptorManager_Destroy();  // Cleanup DescriptorManager
        TestShaderManager_Destroy();      // Cleanup ShaderManager
        TestGBuffer_Destroy();            // Cleanup G-Buffer
        TestCRT_Destroy();                // Cleanup test RT
        break;
    }
    return TRUE;
}

// Экспорт для проверки поддержки
extern "C" {
    bool SupportVulkanRendering()
    {
        return SupportsVulkanRendering();
    }

    // Экспорт для инициализации тестового рендера
    void InitVulkanTest(void* hwnd)
    {
        if (!hwnd) {
            return;
        }

        // Шаг 1: Создаем Vulkan Device
        if (!VulkanHW.CreateDevice((HWND)hwnd)) {
            Msg("![Vulkan Test] Failed to create Vulkan device - aborting test");
            return;
        }

        // Шаг 2: Создаем Swapchain
        RECT rect;
        GetClientRect((HWND)hwnd, &rect);
        u32 width = rect.right - rect.left;
        u32 height = rect.bottom - rect.top;
        Swapchain.Create(width, height);

        return;
    }

    // Простой Vulkan рендер-цикл с треугольником
    void VulkanRenderLoop(void* hwnd)
    {
        HWND hWnd = (HWND)hwnd;

        Msg("[VulkanRenderLoop] Starting triangle render loop...");

        // ========================================================================
        // STEP 1: Создаём command pool и buffer
        // ========================================================================
        VkCommandPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = VulkanHW.m_GraphicsFamily;

        VkCommandPool commandPool;
        VK_CHECK(vkCreateCommandPool(VulkanHW.m_Device, &poolInfo, nullptr, &commandPool));

        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        VK_CHECK(vkAllocateCommandBuffers(VulkanHW.m_Device, &allocInfo, &commandBuffer));

        // ========================================================================
        // STEP 2: Создаём sync objects
        // ========================================================================
        VkSemaphoreCreateInfo semaphoreInfo = {};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        VkSemaphore imageAvailableSemaphore, renderFinishedSemaphore;
        VkFence inFlightFence;

        VK_CHECK(vkCreateSemaphore(VulkanHW.m_Device, &semaphoreInfo, nullptr, &imageAvailableSemaphore));
        VK_CHECK(vkCreateSemaphore(VulkanHW.m_Device, &semaphoreInfo, nullptr, &renderFinishedSemaphore));
        VK_CHECK(vkCreateFence(VulkanHW.m_Device, &fenceInfo, nullptr, &inFlightFence));

        // ========================================================================
        // STEP 3: Загружаем SPIR-V шейдеры (прямой доступ через fopen)
        // ========================================================================
        MessageBox(hWnd, "Loading shaders...", "Vulkan Triangle", MB_OK);

        // Прямой путь к шейдерам (относительно bin\ папки)
        const char* vsPath = "..\\gamedata\\shaders\\vulkan\\textured.vert.spv";
        const char* fsPath = "..\\gamedata\\shaders\\vulkan\\textured.frag.spv";

        VkShaderModule vertShader = VK_NULL_HANDLE;
        VkShaderModule fragShader = VK_NULL_HANDLE;

        // Загрузка vertex shader через fopen
        FILE* vsFile = fopen(vsPath, "rb");
        if (vsFile) {
            fseek(vsFile, 0, SEEK_END);
            long vsSize = ftell(vsFile);
            fseek(vsFile, 0, SEEK_SET);

            xr_vector<u32> vsCode((vsSize + 3) / 4);
            fread(vsCode.data(), 1, vsSize, vsFile);
            fclose(vsFile);

            VkShaderModuleCreateInfo vsCreateInfo = {};
            vsCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            vsCreateInfo.codeSize = vsSize;
            vsCreateInfo.pCode = vsCode.data();

            if (vkCreateShaderModule(VulkanHW.m_Device, &vsCreateInfo, nullptr, &vertShader) == VK_SUCCESS) {
                char msg[256];
                sprintf(msg, "Vertex shader loaded: %ld bytes", vsSize);
                MessageBox(hWnd, msg, "Vulkan", MB_OK);
            } else {
                MessageBox(hWnd, "Failed to create vertex shader module!", "Vulkan ERROR", MB_ICONERROR);
            }
        } else {
            char msg[512];
            sprintf(msg, "Failed to open vertex shader:\n%s\n\nErrno: %d", vsPath, errno);
            MessageBox(hWnd, msg, "Vulkan ERROR", MB_ICONERROR);
        }

        // Загрузка fragment shader через fopen
        FILE* fsFile = fopen(fsPath, "rb");
        if (fsFile) {
            fseek(fsFile, 0, SEEK_END);
            long fsSize = ftell(fsFile);
            fseek(fsFile, 0, SEEK_SET);

            xr_vector<u32> fsCode((fsSize + 3) / 4);
            fread(fsCode.data(), 1, fsSize, fsFile);
            fclose(fsFile);

            VkShaderModuleCreateInfo fsCreateInfo = {};
            fsCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            fsCreateInfo.codeSize = fsSize;
            fsCreateInfo.pCode = fsCode.data();

            if (vkCreateShaderModule(VulkanHW.m_Device, &fsCreateInfo, nullptr, &fragShader) == VK_SUCCESS) {
                char msg[256];
                sprintf(msg, "Fragment shader loaded: %ld bytes", fsSize);
                MessageBox(hWnd, msg, "Vulkan", MB_OK);
            } else {
                MessageBox(hWnd, "Failed to create fragment shader module!", "Vulkan ERROR", MB_ICONERROR);
            }
        } else {
            char msg[512];
            sprintf(msg, "Failed to open fragment shader:\n%s\n\nErrno: %d", fsPath, errno);
            MessageBox(hWnd, msg, "Vulkan ERROR", MB_ICONERROR);
        }

        // ========================================================================
        // STEP 4: Создаём vertex buffer (Quad с UV координатами)
        // ========================================================================
        Msg("[VulkanRenderLoop] Creating vertex buffer...");

        // Vertex data: position (vec3) + texcoord (vec2)
        struct Vertex {
            float pos[3];
            float uv[2];
        };

        // Quad из 2 треугольников (CCW winding)
        Vertex quadVertices[] = {
            // Triangle 1
            {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f}},  // Bottom-left
            {{ 0.5f, -0.5f, 0.0f}, {1.0f, 0.0f}},  // Bottom-right
            {{ 0.5f,  0.5f, 0.0f}, {1.0f, 1.0f}},  // Top-right
            // Triangle 2
            {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f}},  // Bottom-left
            {{ 0.5f,  0.5f, 0.0f}, {1.0f, 1.0f}},  // Top-right
            {{-0.5f,  0.5f, 0.0f}, {0.0f, 1.0f}},  // Top-left
        };

        VkDeviceSize vertexBufferSize = sizeof(quadVertices);

        // Создаём vertex buffer через VMA
        VkBufferCreateInfo vbInfo = {};
        vbInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        vbInfo.size = vertexBufferSize;
        vbInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        vbInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo vbAllocInfo = {};
        vbAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        vbAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                           VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VkBuffer vertexBuffer;
        VmaAllocation vertexAllocation;
        VmaAllocationInfo vertexAllocInfo;

        VkResult vbResult = vmaCreateBuffer(VulkanHW.m_Allocator, &vbInfo, &vbAllocInfo,
            &vertexBuffer, &vertexAllocation, &vertexAllocInfo);

        if (vbResult == VK_SUCCESS) {
            // Копируем данные
            memcpy(vertexAllocInfo.pMappedData, quadVertices, vertexBufferSize);
            Msg("[VulkanRenderLoop] Vertex buffer created (%zu bytes)", vertexBufferSize);
        } else {
            Msg("![VulkanRenderLoop] Failed to create vertex buffer! Error: %d", vbResult);
        }

        // ========================================================================
        // STEP 4b: Создаём тестовую текстуру (шахматная доска)
        // ========================================================================
        Msg("[VulkanRenderLoop] Creating checkerboard texture...");

        const u32 texWidth = 256;
        const u32 texHeight = 256;
        const u32 checkSize = 32;

        // Генерируем RGBA данные для шахматной доски
        xr_vector<u8> textureData(texWidth * texHeight * 4);
        for (u32 y = 0; y < texHeight; ++y) {
            for (u32 x = 0; x < texWidth; ++x) {
                u32 idx = (y * texWidth + x) * 4;
                bool isWhite = ((x / checkSize) + (y / checkSize)) % 2 == 0;
                u8 color = isWhite ? 255 : 64;
                textureData[idx + 0] = color;  // R
                textureData[idx + 1] = color;  // G
                textureData[idx + 2] = color;  // B
                textureData[idx + 3] = 255;    // A
            }
        }

        g_TestTexture.CreateFromData(textureData.data(), texWidth, texHeight, VK_FORMAT_R8G8B8A8_UNORM);

        Msg("[VulkanRenderLoop] Checkerboard texture created: %dx%d", texWidth, texHeight);

        // ========================================================================
        // STEP 5: Создаём descriptor set layout и pool для текстуры
        // ========================================================================
        Msg("[VulkanRenderLoop] Creating descriptor set layout for texture...");

        // Descriptor set layout: 1 combined image sampler at binding 0
        VkDescriptorSetLayoutBinding samplerBinding = {};
        samplerBinding.binding = 0;
        samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerBinding.descriptorCount = 1;
        samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        samplerBinding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutCreateInfo setLayoutInfo = {};
        setLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        setLayoutInfo.bindingCount = 1;
        setLayoutInfo.pBindings = &samplerBinding;

        VkDescriptorSetLayout textureSetLayout;
        VK_CHECK(vkCreateDescriptorSetLayout(VulkanHW.m_Device, &setLayoutInfo, nullptr, &textureSetLayout));

        // Descriptor pool
        VkDescriptorPoolSize poolSize = {};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = 1;

        VkDescriptorPoolCreateInfo poolInfo2 = {};
        poolInfo2.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo2.poolSizeCount = 1;
        poolInfo2.pPoolSizes = &poolSize;
        poolInfo2.maxSets = 1;

        VkDescriptorPool descriptorPool;
        VK_CHECK(vkCreateDescriptorPool(VulkanHW.m_Device, &poolInfo2, nullptr, &descriptorPool));

        // Allocate descriptor set
        VkDescriptorSetAllocateInfo setAllocInfo = {};
        setAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        setAllocInfo.descriptorPool = descriptorPool;
        setAllocInfo.descriptorSetCount = 1;
        setAllocInfo.pSetLayouts = &textureSetLayout;

        VkDescriptorSet textureDescriptorSet;
        VK_CHECK(vkAllocateDescriptorSets(VulkanHW.m_Device, &setAllocInfo, &textureDescriptorSet));

        // Update descriptor set with texture
        VkDescriptorImageInfo imageInfo = {};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = g_TestTexture.GetView();
        imageInfo.sampler = g_TestTexture.GetSampler();

        VkWriteDescriptorSet descriptorWrite = {};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = textureDescriptorSet;
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(VulkanHW.m_Device, 1, &descriptorWrite, 0, nullptr);
        Msg("[VulkanRenderLoop] Descriptor set updated with texture");

        // Pipeline layout с descriptor set
        VkPipelineLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &textureSetLayout;
        layoutInfo.pushConstantRangeCount = 0;
        layoutInfo.pPushConstantRanges = nullptr;

        VkPipelineLayout pipelineLayout;
        VK_CHECK(vkCreatePipelineLayout(VulkanHW.m_Device, &layoutInfo, nullptr, &pipelineLayout));

        // ========================================================================
        // STEP 6: Создаём graphics pipeline
        // ========================================================================
        Msg("[VulkanRenderLoop] Creating graphics pipeline...");

        VkPipeline trianglePipeline = VK_NULL_HANDLE;

        if (vertShader != VK_NULL_HANDLE && fragShader != VK_NULL_HANDLE) {
            // Shader stages
            VkPipelineShaderStageCreateInfo shaderStages[2] = {};

            shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            shaderStages[0].module = vertShader;
            shaderStages[0].pName = "main";

            shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            shaderStages[1].module = fragShader;
            shaderStages[1].pName = "main";

            // Vertex input: position (location 0) + texcoord (location 1)
            VkVertexInputBindingDescription bindingDesc = {};
            bindingDesc.binding = 0;
            bindingDesc.stride = sizeof(Vertex);
            bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            VkVertexInputAttributeDescription attrDescs[2] = {};
            // Position (vec3)
            attrDescs[0].binding = 0;
            attrDescs[0].location = 0;
            attrDescs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            attrDescs[0].offset = offsetof(Vertex, pos);
            // TexCoord (vec2)
            attrDescs[1].binding = 0;
            attrDescs[1].location = 1;
            attrDescs[1].format = VK_FORMAT_R32G32_SFLOAT;
            attrDescs[1].offset = offsetof(Vertex, uv);

            VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
            vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertexInputInfo.vertexBindingDescriptionCount = 1;
            vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
            vertexInputInfo.vertexAttributeDescriptionCount = 2;
            vertexInputInfo.pVertexAttributeDescriptions = attrDescs;

            // Input assembly
            VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
            inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            inputAssembly.primitiveRestartEnable = VK_FALSE;

            // Dynamic state (viewport и scissor)
            VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
            VkPipelineDynamicStateCreateInfo dynamicState = {};
            dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamicState.dynamicStateCount = 2;
            dynamicState.pDynamicStates = dynamicStates;

            // Viewport state (будет задан динамически)
            VkPipelineViewportStateCreateInfo viewportState = {};
            viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewportState.viewportCount = 1;
            viewportState.scissorCount = 1;

            // Rasterization
            VkPipelineRasterizationStateCreateInfo rasterizer = {};
            rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterizer.depthClampEnable = VK_FALSE;
            rasterizer.rasterizerDiscardEnable = VK_FALSE;
            rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
            rasterizer.lineWidth = 1.0f;
            rasterizer.cullMode = VK_CULL_MODE_NONE;  // No culling для простоты
            rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
            rasterizer.depthBiasEnable = VK_FALSE;

            // Multisampling (disabled)
            VkPipelineMultisampleStateCreateInfo multisampling = {};
            multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisampling.sampleShadingEnable = VK_FALSE;
            multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            // Color blending (simple overwrite)
            VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
            colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                   VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            colorBlendAttachment.blendEnable = VK_FALSE;

            VkPipelineColorBlendStateCreateInfo colorBlending = {};
            colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            colorBlending.logicOpEnable = VK_FALSE;
            colorBlending.attachmentCount = 1;
            colorBlending.pAttachments = &colorBlendAttachment;

            // Dynamic Rendering (Vulkan 1.3) - указываем формат attachments
            VkFormat colorFormat = Swapchain.m_Format;
            VkPipelineRenderingCreateInfo renderingInfo = {};
            renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
            renderingInfo.colorAttachmentCount = 1;
            renderingInfo.pColorAttachmentFormats = &colorFormat;
            renderingInfo.depthAttachmentFormat = VK_FORMAT_UNDEFINED;  // No depth for triangle

            // Create pipeline
            VkGraphicsPipelineCreateInfo pipelineInfo = {};
            pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipelineInfo.pNext = &renderingInfo;  // Dynamic Rendering
            pipelineInfo.stageCount = 2;
            pipelineInfo.pStages = shaderStages;
            pipelineInfo.pVertexInputState = &vertexInputInfo;
            pipelineInfo.pInputAssemblyState = &inputAssembly;
            pipelineInfo.pViewportState = &viewportState;
            pipelineInfo.pRasterizationState = &rasterizer;
            pipelineInfo.pMultisampleState = &multisampling;
            pipelineInfo.pDepthStencilState = nullptr;
            pipelineInfo.pColorBlendState = &colorBlending;
            pipelineInfo.pDynamicState = &dynamicState;
            pipelineInfo.layout = pipelineLayout;
            pipelineInfo.renderPass = VK_NULL_HANDLE;  // Dynamic Rendering
            pipelineInfo.subpass = 0;

            VkResult pipelineResult = vkCreateGraphicsPipelines(
                VulkanHW.m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &trianglePipeline);

            if (pipelineResult == VK_SUCCESS) {
                Msg("[VulkanRenderLoop] Graphics pipeline created!");
            } else {
                Msg("![VulkanRenderLoop] Failed to create pipeline! Error: %d", pipelineResult);
            }
        } else {
            Msg("![VulkanRenderLoop] Shaders not loaded, skipping pipeline creation");
        }

        Msg("[VulkanRenderLoop] Initialization complete, entering render loop...");

        // ========================================================================
        // RENDER LOOP
        // ========================================================================
        MSG msg = {};
        bool running = true;

        while (running)
        {
            // Обработка Windows сообщений
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
            {
                if (msg.message == WM_QUIT) {
                    running = false;
                }
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }

            if (!running) break;

            // Проверяем что окно не свёрнуто
            if (IsIconic(hWnd)) {
                Sleep(100);
                continue;
            }

            // Ждём fence
            vkWaitForFences(VulkanHW.m_Device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);
            vkResetFences(VulkanHW.m_Device, 1, &inFlightFence);

            // Получаем следующее изображение
            u32 imageIndex;
            VkResult result = vkAcquireNextImageKHR(VulkanHW.m_Device, Swapchain.m_Swapchain,
                UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

            if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
                continue;
            }

            // Записываем команды
            vkResetCommandBuffer(commandBuffer, 0);

            VkCommandBufferBeginInfo beginInfo = {};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

            // Transition: UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL
            VkImageMemoryBarrier2 toColorAttach = {};
            toColorAttach.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            toColorAttach.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            toColorAttach.srcAccessMask = VK_ACCESS_2_NONE;
            toColorAttach.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            toColorAttach.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            toColorAttach.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            toColorAttach.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            toColorAttach.image = Swapchain.m_Images[imageIndex];
            toColorAttach.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            toColorAttach.subresourceRange.baseMipLevel = 0;
            toColorAttach.subresourceRange.levelCount = 1;
            toColorAttach.subresourceRange.baseArrayLayer = 0;
            toColorAttach.subresourceRange.layerCount = 1;

            VkDependencyInfo depInfo = {};
            depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            depInfo.imageMemoryBarrierCount = 1;
            depInfo.pImageMemoryBarriers = &toColorAttach;
            vkCmdPipelineBarrier2(commandBuffer, &depInfo);

            // Begin Dynamic Rendering
            VkRenderingAttachmentInfo colorAttachment = {};
            colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colorAttachment.imageView = Swapchain.m_ImageViews[imageIndex];
            colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.clearValue.color = { 0.1f, 0.1f, 0.2f, 1.0f };  // Dark blue background

            VkRenderingInfo renderingInfoCmd = {};
            renderingInfoCmd.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            renderingInfoCmd.renderArea.offset = { 0, 0 };
            renderingInfoCmd.renderArea.extent = Swapchain.m_Extent;
            renderingInfoCmd.layerCount = 1;
            renderingInfoCmd.colorAttachmentCount = 1;
            renderingInfoCmd.pColorAttachments = &colorAttachment;

            vkCmdBeginRendering(commandBuffer, &renderingInfoCmd);

            // Рисуем текстурированный квад (если pipeline создан)
            if (trianglePipeline != VK_NULL_HANDLE && vertexBuffer != VK_NULL_HANDLE) {
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, trianglePipeline);

                // Bind descriptor set с текстурой
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipelineLayout, 0, 1, &textureDescriptorSet, 0, nullptr);

                // Set viewport
                VkViewport viewport = {};
                viewport.x = 0.0f;
                viewport.y = 0.0f;
                viewport.width = (float)Swapchain.m_Extent.width;
                viewport.height = (float)Swapchain.m_Extent.height;
                viewport.minDepth = 0.0f;
                viewport.maxDepth = 1.0f;
                vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

                // Set scissor
                VkRect2D scissor = {};
                scissor.offset = { 0, 0 };
                scissor.extent = Swapchain.m_Extent;
                vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

                // Bind vertex buffer
                VkDeviceSize offset = 0;
                vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &offset);

                // Draw quad (6 vertices = 2 triangles)
                vkCmdDraw(commandBuffer, 6, 1, 0, 0);
            }

            vkCmdEndRendering(commandBuffer);

            // Transition: COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC
            VkImageMemoryBarrier2 toPresent = {};
            toPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            toPresent.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            toPresent.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            toPresent.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
            toPresent.dstAccessMask = VK_ACCESS_2_NONE;
            toPresent.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            toPresent.image = Swapchain.m_Images[imageIndex];
            toPresent.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            toPresent.subresourceRange.baseMipLevel = 0;
            toPresent.subresourceRange.levelCount = 1;
            toPresent.subresourceRange.baseArrayLayer = 0;
            toPresent.subresourceRange.layerCount = 1;

            depInfo.pImageMemoryBarriers = &toPresent;
            vkCmdPipelineBarrier2(commandBuffer, &depInfo);

            VK_CHECK(vkEndCommandBuffer(commandBuffer));

            // Submit
            VkSubmitInfo submitInfo = {};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores = &imageAvailableSemaphore;
            submitInfo.pWaitDstStageMask = &waitStage;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffer;
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = &renderFinishedSemaphore;

            VK_CHECK(vkQueueSubmit(VulkanHW.m_GraphicsQueue, 1, &submitInfo, inFlightFence));

            // Present
            VkPresentInfoKHR presentInfo = {};
            presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            presentInfo.waitSemaphoreCount = 1;
            presentInfo.pWaitSemaphores = &renderFinishedSemaphore;
            presentInfo.swapchainCount = 1;
            presentInfo.pSwapchains = &Swapchain.m_Swapchain;
            presentInfo.pImageIndices = &imageIndex;

            vkQueuePresentKHR(VulkanHW.m_PresentQueue, &presentInfo);
        }

        // ========================================================================
        // CLEANUP
        // ========================================================================
        Msg("[VulkanRenderLoop] Cleaning up...");

        vkDeviceWaitIdle(VulkanHW.m_Device);

        if (trianglePipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(VulkanHW.m_Device, trianglePipeline, nullptr);
        }
        vkDestroyPipelineLayout(VulkanHW.m_Device, pipelineLayout, nullptr);

        if (vertShader != VK_NULL_HANDLE) {
            vkDestroyShaderModule(VulkanHW.m_Device, vertShader, nullptr);
        }
        if (fragShader != VK_NULL_HANDLE) {
            vkDestroyShaderModule(VulkanHW.m_Device, fragShader, nullptr);
        }

        if (vertexBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(VulkanHW.m_Allocator, vertexBuffer, vertexAllocation);
        }

        // Cleanup texture
        g_TestTexture.Destroy();

        // Cleanup descriptor resources
        vkDestroyDescriptorPool(VulkanHW.m_Device, descriptorPool, nullptr);
        vkDestroyDescriptorSetLayout(VulkanHW.m_Device, textureSetLayout, nullptr);

        vkDestroySemaphore(VulkanHW.m_Device, imageAvailableSemaphore, nullptr);
        vkDestroySemaphore(VulkanHW.m_Device, renderFinishedSemaphore, nullptr);
        vkDestroyFence(VulkanHW.m_Device, inFlightFence, nullptr);
        vkDestroyCommandPool(VulkanHW.m_Device, commandPool, nullptr);

        Msg("[VulkanRenderLoop] Cleanup complete");

        return;

        // COMMENTED OUT FOR NOW - will enable step by step
        /*
        if (!hwnd) {
            Msg("![Vulkan TEST] ERROR: hwnd is NULL!");
            OutputDebugStringA("![Vulkan TEST] ERROR: hwnd is NULL!\n");
            return;
        }

        __try {
            // 0. Инициализация Vulkan HW (КРИТИЧЕСКИ ВАЖНО!)
            OutputDebugStringA("[Vulkan TEST] Step 0: Initializing Vulkan VulkanHW...\n");
            Msg("[Vulkan TEST] Step 0: Initializing Vulkan VulkanHW...");
            if (!VulkanHW.CreateDevice((HWND)hwnd)) {
                OutputDebugStringA("[Vulkan TEST] Step 0: FAILED - CreateDevice returned false\n");
                Msg("![Vulkan TEST] Step 0: FAILED - CreateDevice returned false");
                Msg("![Vulkan TEST] Aborting test - check log for details");
                return;
            }
            OutputDebugStringA("[Vulkan TEST] Step 0: SUCCESS\n");
            Msg("[Vulkan TEST] Step 0: Vulkan HW initialized successfully");

            // 0b. Инициализация Swapchain
            Msg("[Vulkan TEST] Step 0b: Creating Swapchain...");
            Swapchain.Create(1920, 1080);  // TODO: получить реальные размеры из Device
            Msg("[Vulkan TEST] Step 0b: Swapchain created successfully");

            // Инициализация всех компонентов в правильном порядке
            Msg("[Vulkan TEST] Step 1: Creating Test RT...");
            TestCRT_Create();

            Msg("[Vulkan TEST] Step 2: Creating G-Buffer...");
            TestGBuffer_Create();

            Msg("[Vulkan TEST] Step 3: Creating Shader Manager...");
            TestShaderManager_Create();

            Msg("[Vulkan TEST] Step 4: Loading shaders...");
            TestShaderManager_Load();

            Msg("[Vulkan TEST] Step 5: Creating Descriptor Manager...");
            TestDescriptorManager_Create();

            Msg("[Vulkan TEST] Step 6: Allocating descriptor sets...");
            TestDescriptorManager_Allocate();

            Msg("[Vulkan TEST] Step 7: Creating Pipeline Manager...");
            TestPipelineManager_Create();

            Msg("[Vulkan TEST] Step 8: Creating test pipeline...");
            TestPipelineManager_CreatePipeline();

            Msg("[Vulkan TEST] Step 9: Creating buffers...");
            TestBuffers_Create();

            Msg("[Vulkan TEST] Step 10: Setting up triangle render...");
            SetupTriangleRender();

            Msg("[Vulkan TEST] ========================================");
            Msg("[Vulkan TEST] ALL INITIALIZATION COMPLETE!");
            Msg("[Vulkan TEST] ========================================");
        }
        __except(EXCEPTION_EXECUTE_HANDLER) {
            char buf[256];
            sprintf(buf, "![Vulkan TEST] EXCEPTION during initialization! Code: 0x%X\n", GetExceptionCode());
            OutputDebugStringA(buf);
            Msg("![Vulkan TEST] EXCEPTION during initialization! Code: 0x%X", GetExceptionCode());
        }
        */
    }
}
