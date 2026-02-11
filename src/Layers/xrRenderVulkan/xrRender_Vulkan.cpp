// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

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

	// Helper: transition a swapchain image between layouts
	static void TransitionSwapchain(VkCommandBuffer cmd, VkImage image,
		VkImageLayout oldLayout, VkImageLayout newLayout,
		VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
		VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
	{
		VkImageMemoryBarrier2 barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		barrier.srcStageMask = srcStage;
		barrier.srcAccessMask = srcAccess;
		barrier.dstStageMask = dstStage;
		barrier.dstAccessMask = dstAccess;
		barrier.oldLayout = oldLayout;
		barrier.newLayout = newLayout;
		barrier.image = image;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.layerCount = 1;

		VkDependencyInfo depInfo = {};
		depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		depInfo.imageMemoryBarrierCount = 1;
		depInfo.pImageMemoryBarriers = &barrier;
		vkCmdPipelineBarrier2(cmd, &depInfo);
	}

	// End UI pass (called at end of frame before command buffer submit)
	static void EndUIPass()
	{
		if (!s_bUIPassActive) return;

		VkCommandBuffer cmd = RCache.m_Cmd;
		if (!cmd) return;

		// End dynamic rendering
		vkCmdEndRendering(cmd);

		// Transition: COLOR_ATTACHMENT -> PRESENT_SRC
		u32 imageIndex = Swapchain.m_CurrentImageIndex;
		TransitionSwapchain(cmd, Swapchain.m_Images[imageIndex],
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, VK_ACCESS_2_NONE);

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
		TransitionSwapchain(cmd, swapchainImage,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

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

// Console initialization (implemented in vk_console.cpp)
extern void xrRender_initconsole();

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
}

