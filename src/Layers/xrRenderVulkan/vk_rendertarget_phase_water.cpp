// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

#include "stdafx.h"
#include "vk_rendertarget.h"
#include "HW_Vulkan.h"
#include "vk_R_Backend.h"
#include "vk_shaders.h"
#include "vk_pipeline.h"
#include "vk_swapchain.h"
#include "rvk.h"
#include "../../xrEngine/device.h"
#include "../../xrEngine/IGame_Persistent.h"
#include "../../xrEngine/Environment.h"

namespace VK
{

// ============================================================================
// Helper: Transition image layout using VkImageMemoryBarrier
// ============================================================================
static void TransitionImage(VkCommandBuffer cmd, VkImage image,
	VkImageLayout oldLayout, VkImageLayout newLayout,
	VkAccessFlags srcAccess, VkAccessFlags dstAccess,
	VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
	VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT)
{
	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.srcAccessMask = srcAccess;
	barrier.dstAccessMask = dstAccess;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = aspect;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;

	vkCmdPipelineBarrier(cmd, srcStage, dstStage,
		0, 0, nullptr, 0, nullptr, 1, &barrier);
}

// ============================================================================
// phase_water_ssr() - Water SSR Pre-Pass
// ============================================================================
// Renders water geometry at reduced resolution to compute screen-space
// reflections. Result is copied to rt_ssfx_water for use by the main water pass.
// ============================================================================

void CRenderTarget::phase_water_ssr()
{
	VkCommandBuffer cmd = RCache.GetCommandBuffer();
	if (cmd == VK_NULL_HANDLE) return;

	// Skip if no water geometry
	if (RImplementation.mapWater.size() == 0) return;

	// ========================================================================
	// Step 1: Clear temporary RTs
	// ========================================================================
	TransitionImage(cmd, rt_ssfx_temp.m_Image,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		0, VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

	VkClearColorValue clearColor = {{0.0f, 0.0f, 0.0f, 0.0f}};
	VkImageSubresourceRange clearRange = {};
	clearRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	clearRange.baseMipLevel = 0;
	clearRange.levelCount = 1;
	clearRange.baseArrayLayer = 0;
	clearRange.layerCount = 1;
	vkCmdClearColorImage(cmd, rt_ssfx_temp.m_Image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &clearRange);

	// Transition to color attachment for rendering
	TransitionImage(cmd, rt_ssfx_temp.m_Image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

	// ========================================================================
	// Step 2: Begin rendering to rt_ssfx_temp at reduced resolution
	// ========================================================================
	// Reduced resolution factor (DX11 uses ps_ssfx_water.x, default ~2)
	u32 reducedW = m_Width / 2;
	u32 reducedH = m_Height / 2;
	if (reducedW < 1) reducedW = 1;
	if (reducedH < 1) reducedH = 1;

	VkRenderingAttachmentInfo colorAttachment = {};
	colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	colorAttachment.imageView = rt_ssfx_temp.m_ImageView;
	colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

	VkRenderingInfo renderingInfo = {};
	renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	renderingInfo.renderArea.offset = {0, 0};
	renderingInfo.renderArea.extent = {reducedW, reducedH};
	renderingInfo.layerCount = 1;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachments = &colorAttachment;

	vkCmdBeginRendering(cmd, &renderingInfo);

	// Set reduced viewport
	VkViewport viewport = {};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float)reducedW;
	viewport.height = (float)reducedH;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor = {};
	scissor.offset = {0, 0};
	scissor.extent = {reducedW, reducedH};
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	// ========================================================================
	// Step 3: Load and bind water SSR pipeline
	// ========================================================================
	VkShaderModule vertShader = g_ShaderManager->Load("water_ssr.vert.spv");
	VkShaderModule fragShader = g_ShaderManager->Load("water_ssr.frag.spv");

	if (vertShader != VK_NULL_HANDLE && fragShader != VK_NULL_HANDLE)
	{
		if (m_WaterSSRPipeline == VK_NULL_HANDLE)
		{
			PipelineConfig config = {};
			config.vertShader = vertShader;
			config.fragShader = fragShader;
			config.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			config.cullMode = VK_CULL_MODE_NONE;
			config.depthTest = false;
			config.depthWrite = false;
			config.blendEnable = false;
			config.colorAttachmentCount = 1;
			config.colorFormats[0] = VK_FORMAT_R16G16B16A16_SFLOAT;

			m_WaterSSRPipeline = g_PipelineManager->GetOrCreate(config);
		}

		if (m_WaterSSRPipeline != VK_NULL_HANDLE)
		{
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_WaterSSRPipeline);

			// Push constants: camera position (previous frame), VP matrices
			struct WaterSSRPushConstants {
				float camPosX, camPosY, camPosZ, pad0;
				float vpCurrMatrix[16];
				float vpPrevMatrix[16];
			} pushData;

			Fvector camPos = Device.vCameraPosition;
			pushData.camPosX = camPos.x;
			pushData.camPosY = camPos.y;
			pushData.camPosZ = camPos.z;
			pushData.pad0 = 0.0f;

			// Current VP matrix
			Fmatrix vpCurr;
			vpCurr.mul(Device.mProject, Device.mView);
			CopyMemory(pushData.vpCurrMatrix, &vpCurr, sizeof(float) * 16);

			// Previous VP matrix
			Fmatrix vpPrev;
			vpPrev.mul(Device.mProject_prev, Device.mView_prev);
			CopyMemory(pushData.vpPrevMatrix, &vpPrev, sizeof(float) * 16);

			VkPipelineLayout layout = g_PipelineManager->GetLayout();
			vkCmdPushConstants(cmd, layout,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				0, sizeof(WaterSSRPushConstants), &pushData);

			// Render water geometry for SSR
			RImplementation.r_dsgraph_render_water_ssr();
		}
	}

	vkCmdEndRendering(cmd);

	// ========================================================================
	// Step 4: Copy rt_ssfx_temp -> rt_ssfx_water
	// ========================================================================
	TransitionImage(cmd, rt_ssfx_temp.m_Image,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

	TransitionImage(cmd, rt_ssfx_water.m_Image,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		0, VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

	VkImageCopy copyRegion = {};
	copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copyRegion.srcSubresource.layerCount = 1;
	copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copyRegion.dstSubresource.layerCount = 1;
	copyRegion.extent.width = reducedW;
	copyRegion.extent.height = reducedH;
	copyRegion.extent.depth = 1;

	vkCmdCopyImage(cmd,
		rt_ssfx_temp.m_Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		rt_ssfx_water.m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &copyRegion);

	// Transition rt_ssfx_water to shader read for subsequent passes
	TransitionImage(cmd, rt_ssfx_water.m_Image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

	Msg("[Vulkan] phase_water_ssr: SSR pass complete (%dx%d)", reducedW, reducedH);
}

// ============================================================================
// phase_water_blur() - SSR Blur Post-Process
// ============================================================================
// Applies Gaussian blur to the water SSR result for smooth reflections.
// Horizontal pass: rt_ssfx_water -> rt_ssfx_temp2
// Vertical pass: rt_ssfx_temp2 -> rt_ssfx_temp
// Final copy: rt_ssfx_temp -> rt_ssfx_water
// ============================================================================

void CRenderTarget::phase_water_blur()
{
	VkCommandBuffer cmd = RCache.GetCommandBuffer();
	if (cmd == VK_NULL_HANDLE) return;

	// Skip if no water geometry
	if (RImplementation.mapWater.size() == 0) return;

	VkShaderModule vertShader = g_ShaderManager->Load("combine.vert.spv");  // Fullscreen triangle
	VkShaderModule blurFragShader = g_ShaderManager->Load("water_blur.frag.spv");

	if (vertShader == VK_NULL_HANDLE || blurFragShader == VK_NULL_HANDLE)
	{
		// No blur shaders available - skip blur pass
		return;
	}

	if (m_WaterBlurPipeline == VK_NULL_HANDLE)
	{
		PipelineConfig config = {};
		config.vertShader = vertShader;
		config.fragShader = blurFragShader;
		config.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		config.cullMode = VK_CULL_MODE_NONE;
		config.depthTest = false;
		config.depthWrite = false;
		config.blendEnable = false;
		config.colorAttachmentCount = 1;
		config.colorFormats[0] = VK_FORMAT_R16G16B16A16_SFLOAT;

		m_WaterBlurPipeline = g_PipelineManager->GetOrCreate(config);
	}

	if (m_WaterBlurPipeline == VK_NULL_HANDLE) return;

	VkPipelineLayout layout = g_PipelineManager->GetLayout();

	// ========================================================================
	// Horizontal blur: rt_ssfx_water -> rt_ssfx_temp2
	// ========================================================================
	TransitionImage(cmd, rt_ssfx_temp2.m_Image,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

	VkRenderingAttachmentInfo colorAttachment = {};
	colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	colorAttachment.imageView = rt_ssfx_temp2.m_ImageView;
	colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.clearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}};

	VkRenderingInfo renderingInfo = {};
	renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	renderingInfo.renderArea.offset = {0, 0};
	renderingInfo.renderArea.extent = {m_Width, m_Height};
	renderingInfo.layerCount = 1;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachments = &colorAttachment;

	vkCmdBeginRendering(cmd, &renderingInfo);

	VkViewport viewport = {};
	viewport.width = (float)m_Width;
	viewport.height = (float)m_Height;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor = {};
	scissor.extent = {m_Width, m_Height};
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_WaterBlurPipeline);

	// Push constants: blur direction (1,0) = horizontal
	struct BlurPushConstants {
		float dirX, dirY;
		float texelSizeX, texelSizeY;
	} pushData;
	pushData.dirX = 1.0f;
	pushData.dirY = 0.0f;
	pushData.texelSizeX = 1.0f / (float)m_Width;
	pushData.texelSizeY = 1.0f / (float)m_Height;

	vkCmdPushConstants(cmd, layout,
		VK_SHADER_STAGE_FRAGMENT_BIT,
		0, sizeof(BlurPushConstants), &pushData);

	vkCmdDraw(cmd, 3, 1, 0, 0);
	vkCmdEndRendering(cmd);

	// ========================================================================
	// Vertical blur: rt_ssfx_temp2 -> rt_ssfx_temp
	// ========================================================================
	TransitionImage(cmd, rt_ssfx_temp2.m_Image,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

	TransitionImage(cmd, rt_ssfx_temp.m_Image,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

	colorAttachment.imageView = rt_ssfx_temp.m_ImageView;
	renderingInfo.pColorAttachments = &colorAttachment;

	vkCmdBeginRendering(cmd, &renderingInfo);

	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_WaterBlurPipeline);

	// Push constants: blur direction (0,1) = vertical
	pushData.dirX = 0.0f;
	pushData.dirY = 1.0f;

	vkCmdPushConstants(cmd, layout,
		VK_SHADER_STAGE_FRAGMENT_BIT,
		0, sizeof(BlurPushConstants), &pushData);

	vkCmdDraw(cmd, 3, 1, 0, 0);
	vkCmdEndRendering(cmd);

	// ========================================================================
	// Copy rt_ssfx_temp -> rt_ssfx_water (final blurred result)
	// ========================================================================
	TransitionImage(cmd, rt_ssfx_temp.m_Image,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

	TransitionImage(cmd, rt_ssfx_water.m_Image,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

	VkImageCopy copyRegion = {};
	copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copyRegion.srcSubresource.layerCount = 1;
	copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copyRegion.dstSubresource.layerCount = 1;
	copyRegion.extent.width = m_Width;
	copyRegion.extent.height = m_Height;
	copyRegion.extent.depth = 1;

	vkCmdCopyImage(cmd,
		rt_ssfx_temp.m_Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		rt_ssfx_water.m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &copyRegion);

	// Transition rt_ssfx_water back to shader read
	TransitionImage(cmd, rt_ssfx_water.m_Image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

// ============================================================================
// phase_water_waves() - Wave Simulation
// ============================================================================
// Renders wave height/normal data to rt_ssfx_water_waves (512x512).
// Used by the final water pass for displacement and normal perturbation.
// ============================================================================

void CRenderTarget::phase_water_waves()
{
	VkCommandBuffer cmd = RCache.GetCommandBuffer();
	if (cmd == VK_NULL_HANDLE) return;

	// Skip if no water geometry
	if (RImplementation.mapWater.size() == 0) return;

	VkShaderModule vertShader = g_ShaderManager->Load("combine.vert.spv");  // Fullscreen triangle
	VkShaderModule wavesFragShader = g_ShaderManager->Load("water_waves.frag.spv");

	if (vertShader == VK_NULL_HANDLE || wavesFragShader == VK_NULL_HANDLE)
	{
		// No wave shaders available - skip
		return;
	}

	if (m_WaterWavesPipeline == VK_NULL_HANDLE)
	{
		PipelineConfig config = {};
		config.vertShader = vertShader;
		config.fragShader = wavesFragShader;
		config.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		config.cullMode = VK_CULL_MODE_NONE;
		config.depthTest = false;
		config.depthWrite = false;
		config.blendEnable = false;
		config.colorAttachmentCount = 1;
		config.colorFormats[0] = VK_FORMAT_R8G8B8A8_UNORM;

		m_WaterWavesPipeline = g_PipelineManager->GetOrCreate(config);
	}

	if (m_WaterWavesPipeline == VK_NULL_HANDLE) return;

	// Transition wave RT
	TransitionImage(cmd, rt_ssfx_water_waves.m_Image,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

	VkRenderingAttachmentInfo colorAttachment = {};
	colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	colorAttachment.imageView = rt_ssfx_water_waves.m_ImageView;
	colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.clearValue.color = {{0.5f, 0.5f, 0.0f, 0.0f}};

	VkRenderingInfo renderingInfo = {};
	renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	renderingInfo.renderArea.offset = {0, 0};
	renderingInfo.renderArea.extent = {512, 512};
	renderingInfo.layerCount = 1;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachments = &colorAttachment;

	vkCmdBeginRendering(cmd, &renderingInfo);

	VkViewport viewport = {};
	viewport.width = 512.0f;
	viewport.height = 512.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor = {};
	scissor.extent = {512, 512};
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_WaterWavesPipeline);

	// Push time value for wave animation
	struct WavesPushConstants {
		float time;
		float windDir;
		float windVel;
		float pad;
	} pushData;

	pushData.time = Device.fTimeGlobal;

	if (g_pGamePersistent && g_pGamePersistent->Environment().CurrentEnv)
	{
		pushData.windDir = g_pGamePersistent->Environment().CurrentEnv->wind_direction;
		pushData.windVel = g_pGamePersistent->Environment().CurrentEnv->wind_velocity;
	}
	else
	{
		pushData.windDir = 0.0f;
		pushData.windVel = 0.0f;
	}
	pushData.pad = 0.0f;

	VkPipelineLayout layout = g_PipelineManager->GetLayout();
	vkCmdPushConstants(cmd, layout,
		VK_SHADER_STAGE_FRAGMENT_BIT,
		0, sizeof(WavesPushConstants), &pushData);

	vkCmdDraw(cmd, 3, 1, 0, 0);
	vkCmdEndRendering(cmd);

	// Transition to shader read for final water pass
	TransitionImage(cmd, rt_ssfx_water_waves.m_Image,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

// ============================================================================
// phase_water() - Final Water Rendering
// ============================================================================
// Renders water geometry to the swapchain with SSR reflections and wave animation.
// Reads rt_ssfx_water (blurred SSR) and rt_ssfx_water_waves (wave data).
// ============================================================================

void CRenderTarget::phase_water()
{
	VkCommandBuffer cmd = RCache.GetCommandBuffer();
	if (cmd == VK_NULL_HANDLE) return;

	// Skip if no water geometry
	if (RImplementation.mapWater.size() == 0) return;

	VkImage swapchainImage = Swapchain.GetCurrentImage();
	VkImageView swapchainView = Swapchain.GetCurrentImageView();
	u32 swapWidth = Swapchain.GetWidth();
	u32 swapHeight = Swapchain.GetHeight();

	if (swapchainImage == VK_NULL_HANDLE || swapchainView == VK_NULL_HANDLE) return;

	// ========================================================================
	// Transition swapchain to COLOR_ATTACHMENT_OPTIMAL (load existing content)
	// ========================================================================
	TransitionImage(cmd, swapchainImage,
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
		VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

	// ========================================================================
	// Begin rendering to swapchain (LOAD existing content, don't clear)
	// ========================================================================
	VkRenderingAttachmentInfo colorAttachment = {};
	colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	colorAttachment.imageView = swapchainView;
	colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

	// Depth attachment for depth testing water geometry
	VkRenderingAttachmentInfo depthAttachment = {};
	depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	depthAttachment.imageView = Swapchain.m_DepthView;
	depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

	VkRenderingInfo renderingInfo = {};
	renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	renderingInfo.renderArea.offset = {0, 0};
	renderingInfo.renderArea.extent = {swapWidth, swapHeight};
	renderingInfo.layerCount = 1;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachments = &colorAttachment;
	renderingInfo.pDepthAttachment = &depthAttachment;

	vkCmdBeginRendering(cmd, &renderingInfo);

	VkViewport viewport = {};
	viewport.width = (float)swapWidth;
	viewport.height = (float)swapHeight;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor = {};
	scissor.extent = {swapWidth, swapHeight};
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	// ========================================================================
	// Load and bind water pipeline
	// ========================================================================
	VkShaderModule vertShader = g_ShaderManager->Load("water.vert.spv");
	VkShaderModule fragShader = g_ShaderManager->Load("water.frag.spv");

	if (vertShader != VK_NULL_HANDLE && fragShader != VK_NULL_HANDLE)
	{
		if (m_WaterPipeline == VK_NULL_HANDLE)
		{
			PipelineConfig config = {};
			config.vertShader = vertShader;
			config.fragShader = fragShader;
			config.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			config.cullMode = VK_CULL_MODE_NONE;  // Water visible from both sides
			config.depthTest = true;
			config.depthWrite = true;
			config.blendEnable = true;   // Alpha blending for water transparency
			config.colorAttachmentCount = 1;
			config.colorFormats[0] = Swapchain.GetFormat();
			config.depthFormat = VK_FORMAT_D32_SFLOAT;

			m_WaterPipeline = g_PipelineManager->GetOrCreate(config);
		}

		if (m_WaterPipeline != VK_NULL_HANDLE)
		{
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_WaterPipeline);

			// Push constants: wind direction, velocity, water params
			struct WaterPushConstants {
				float windDir;
				float windVel;
				float time;
				float waterAlpha;
			} pushData;

			if (g_pGamePersistent && g_pGamePersistent->Environment().CurrentEnv)
			{
				pushData.windDir = g_pGamePersistent->Environment().CurrentEnv->wind_direction;
				pushData.windVel = g_pGamePersistent->Environment().CurrentEnv->wind_velocity;
			}
			else
			{
				pushData.windDir = 0.0f;
				pushData.windVel = 0.0f;
			}
			pushData.time = Device.fTimeGlobal;
			pushData.waterAlpha = 0.75f;  // Default water alpha

			VkPipelineLayout layout = g_PipelineManager->GetLayout();
			vkCmdPushConstants(cmd, layout,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				0, sizeof(WaterPushConstants), &pushData);

			// Render water geometry (this also clears mapWater)
			RImplementation.r_dsgraph_render_water();
		}
	}
	else
	{
		// No water shaders - still need to clear mapWater
		RImplementation.r_dsgraph_render_water();
	}

	vkCmdEndRendering(cmd);

	// Transition swapchain back to PRESENT_SRC
	TransitionImage(cmd, swapchainImage,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
}

} // namespace VK
