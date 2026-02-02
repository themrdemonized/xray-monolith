// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#include "stdafx.h"
#include "vk_rendertarget.h"
#include "vk_env_render.h"
#include "HW_Vulkan.h"
#include "vk_R_Backend.h"
#include "vk_shaders.h"
#include "vk_pipeline.h"
#include "vk_swapchain.h"
#include "vk_descriptors.h"
#include "vk_command_buffer.h"
#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/Environment.h"

namespace VK
{

// ============================================================================
// Half-box geometry for skybox rendering
// ============================================================================
// 12 vertices forming a full box shape
// Positions serve as cubemap lookup directions

struct SkyVertex {
	float x, y, z;
};

static const SkyVertex s_SkyVertices[12] = {
	// Top ring (y=1)
	{-1.0f,  1.0f, -1.0f},  // 0
	{ 1.0f,  1.0f, -1.0f},  // 1
	{-1.0f,  1.0f,  1.0f},  // 2
	{ 1.0f,  1.0f,  1.0f},  // 3
	// Middle ring (y=0)
	{-1.0f,  0.0f, -1.0f},  // 4
	{ 1.0f,  0.0f, -1.0f},  // 5
	{ 1.0f,  0.0f,  1.0f},  // 6
	{-1.0f,  0.0f,  1.0f},  // 7
	// Bottom ring (y=-1)
	{-1.0f, -1.0f, -1.0f},  // 8
	{ 1.0f, -1.0f, -1.0f},  // 9
	{ 1.0f, -1.0f,  1.0f},  // 10
	{-1.0f, -1.0f,  1.0f},  // 11
};

static const u16 s_SkyIndices[60] = {
	// Top face
	0, 2, 1,  1, 2, 3,
	// Front face (z=-1)
	0, 1, 4,  4, 1, 5,
	4, 5, 8,  8, 5, 9,
	// Right face (x=1)
	1, 3, 5,  5, 3, 6,
	5, 6, 9,  9, 6, 10,
	// Back face (z=1)
	3, 2, 6,  6, 2, 7,
	6, 7, 10, 10, 7, 11,
	// Left face (x=-1)
	2, 0, 7,  7, 0, 4,
	7, 4, 11, 11, 4, 8,
};

// ============================================================================
// CreateSkyGeometry() - Create half-box VB/IB once
// ============================================================================

void CRenderTarget::CreateSkyGeometry()
{
	if (m_bSkyGeometryCreated) return;

	// Create vertex buffer
	m_SkyVB.Create(
		sizeof(s_SkyVertices),
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
	);
	m_SkyVB.Upload(s_SkyVertices, sizeof(s_SkyVertices));

	// Create index buffer
	m_SkyIB.Create(
		sizeof(s_SkyIndices),
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
	);
	m_SkyIB.Upload(s_SkyIndices, sizeof(s_SkyIndices));

	m_bSkyGeometryCreated = true;
	Msg("[Vulkan] Sky geometry created (12 verts, 60 indices)");
}

// ============================================================================
// CreateFallbackCubemap() - Create 1x1x6 solid-color cubemap
// ============================================================================

CVulkanTexture* CRenderTarget::CreateFallbackCubemap(float r, float g, float b)
{
	CVulkanTexture* tex = xr_new<CVulkanTexture>();

	// Setup as cubemap before Create
	tex->m_bCubemap = true;
	tex->m_ArrayLayers = 6;

	tex->Create(1, 1, VK_FORMAT_R8G8B8A8_UNORM, 1);

	// 6 faces * 4 bytes each = 24 bytes
	u8 pixel[24];
	u8 R = (u8)(r * 255.0f);
	u8 G = (u8)(g * 255.0f);
	u8 B = (u8)(b * 255.0f);
	for (int i = 0; i < 6; i++) {
		pixel[i*4+0] = R;
		pixel[i*4+1] = G;
		pixel[i*4+2] = B;
		pixel[i*4+3] = 255;
	}

	CVulkanBuffer staging;
	staging.Create(24, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST);
	void* mapped = staging.Map();
	if (mapped) {
		memcpy(mapped, pixel, 24);
		staging.Flush();
		staging.Unmap();
	}

	// One-shot command buffer for upload
	VkCommandBuffer cmd = CommandManager.BeginImmediate();
	if (cmd != VK_NULL_HANDLE) {
		tex->TransitionLayout(cmd, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

		VkBufferImageCopy regions[6] = {};
		for (u32 face = 0; face < 6; face++) {
			regions[face].bufferOffset = face * 4;
			regions[face].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			regions[face].imageSubresource.mipLevel = 0;
			regions[face].imageSubresource.baseArrayLayer = face;
			regions[face].imageSubresource.layerCount = 1;
			regions[face].imageExtent = {1, 1, 1};
		}
		vkCmdCopyBufferToImage(cmd, staging.m_Buffer, tex->GetImage(),
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 6, regions);

		tex->TransitionLayout(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		CommandManager.EndAndSubmitImmediate(cmd);
	}

	staging.Destroy();
	return tex;
}

// ============================================================================
// GetOrCreateFallbackSky() - Lazy-create fallback cubemap
// ============================================================================

CVulkanTexture* CRenderTarget::GetOrCreateFallbackSky()
{
	if (!m_FallbackSky) {
		m_FallbackSky = CreateFallbackCubemap(0.3f, 0.5f, 0.8f);  // Default sky blue
		Msg("[Vulkan] Created fallback sky cubemap");
	}
	return m_FallbackSky;
}

// ============================================================================
// DestroySkyResources()
// ============================================================================

void CRenderTarget::DestroySkyResources()
{
	if (m_FallbackSky) {
		m_FallbackSky->Destroy();
		xr_delete(m_FallbackSky);
	}

	m_SkyVB.Destroy();
	m_SkyIB.Destroy();
	m_SkyDescSet = VK_NULL_HANDLE;
	m_bSkyGeometryCreated = false;
}

// ============================================================================
// phase_sky() - Render textured skybox
// ============================================================================
//
// Renders sky AFTER phase_combine() but BEFORE phase_forward().
// Uses hardware depth test with read-only depth attachment:
// - Vertex shader outputs depth = 1.0 via gl_Position = pos.xyww trick
// - depthCompareOp = LESS_OR_EQUAL, depthWrite = false
// - Where geometry exists (depth < 1.0) -> sky fails depth test -> combine output preserved
// - Where no geometry (depth = 1.0)     -> sky passes (1.0 <= 1.0) -> sky texture drawn
//
// Sky textures come from vkEnvDescriptorMixerRender (populated by engine's
// CEnvDescriptorMixer::lerp() every frame). This ensures Lua weather overrides
// via set_weather_value_string() are picked up automatically.
//

void CRenderTarget::phase_sky()
{
	VkCommandBuffer cmd = RCache.GetCommandBuffer();
	if (cmd == VK_NULL_HANDLE) return;

	// Skip if no environment data
	if (!g_pGamePersistent || !g_pGamePersistent->Environment().CurrentEnv) return;

	CEnvironment& env = g_pGamePersistent->Environment();

	// Create geometry on first use
	CreateSkyGeometry();
	if (!m_bSkyGeometryCreated) return;

	// ========================================================================
	// Get sky textures from the mixer (populated by engine lerp() each frame)
	// ========================================================================
	vkEnvDescriptorMixerRender* mixer = static_cast<vkEnvDescriptorMixerRender*>(
		&*env.CurrentEnv->m_pDescriptorMixer);

	CVulkanTexture* skyA = mixer->sky_tex[0];
	CVulkanTexture* skyB = mixer->sky_tex[1];

	// Use fallback cubemap if mixer textures are not available
	if (!skyA || !skyA->IsValid())
		skyA = GetOrCreateFallbackSky();
	if (!skyB || !skyB->IsValid())
		skyB = GetOrCreateFallbackSky();

	if (!skyA || !skyA->IsValid()) return;
	if (!skyB || !skyB->IsValid()) return;

	// ========================================================================
	// Step 1: Get swapchain and depth resources
	// ========================================================================
	VkImage swapchainImage = Swapchain.GetCurrentImage();
	VkImageView swapchainView = Swapchain.GetCurrentImageView();
	u32 swapWidth = Swapchain.GetWidth();
	u32 swapHeight = Swapchain.GetHeight();

	if (swapchainImage == VK_NULL_HANDLE || swapchainView == VK_NULL_HANDLE) return;

	VkImageView depthView = Swapchain.m_DepthView;
	VkImage depthImage = Swapchain.m_DepthImage;
	if (depthView == VK_NULL_HANDLE || depthImage == VK_NULL_HANDLE) return;

	// ========================================================================
	// Step 2: Transition images for sky rendering
	// ========================================================================
	VkImageMemoryBarrier barriers[2] = {};

	// Swapchain: PRESENT_SRC -> COLOR_ATTACHMENT_OPTIMAL
	barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barriers[0].srcAccessMask = 0;
	barriers[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	barriers[0].oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	barriers[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barriers[0].image = swapchainImage;
	barriers[0].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

	// Depth: DEPTH_ATTACHMENT_OPTIMAL -> DEPTH_READ_ONLY_OPTIMAL (for read-only depth test)
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

	// ========================================================================
	// Step 3: Begin rendering with color + read-only depth attachment
	// ========================================================================
	VkRenderingAttachmentInfo colorAttachment = {};
	colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	colorAttachment.imageView = swapchainView;
	colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // Preserve combine output
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

	VkRenderingAttachmentInfo depthAttachment = {};
	depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	depthAttachment.imageView = depthView;
	depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;   // Keep existing depth
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_NONE; // We don't write depth

	VkRenderingInfo renderingInfo = {};
	renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	renderingInfo.renderArea.offset = {0, 0};
	renderingInfo.renderArea.extent = {swapWidth, swapHeight};
	renderingInfo.layerCount = 1;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachments = &colorAttachment;
	renderingInfo.pDepthAttachment = &depthAttachment;

	vkCmdBeginRendering(cmd, &renderingInfo);

	// ========================================================================
	// Step 4: Viewport and scissor
	// ========================================================================
	VkViewport viewport = {};
	viewport.x = 0.0f;
	viewport.y = (float)swapHeight;
	viewport.width = (float)swapWidth;
	viewport.height = -(float)swapHeight;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor = {};
	scissor.offset = {0, 0};
	scissor.extent = {swapWidth, swapHeight};
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	// ========================================================================
	// Step 5: Load sky shaders
	// ========================================================================
	VkShaderModule vertShader = g_ShaderManager->Load("sky.vert.spv");
	VkShaderModule fragShader = g_ShaderManager->Load("sky.frag.spv");

	if (vertShader == VK_NULL_HANDLE || fragShader == VK_NULL_HANDLE) {
		vkCmdEndRendering(cmd);
		goto cleanup_barriers;
	}

	{
		// ====================================================================
		// Step 6: Create sky pipeline (with hardware depth test)
		// ====================================================================
		PipelineConfig config;
		config.vertShader = vertShader;
		config.fragShader = fragShader;
		config.useDefaultVertexInput = false;
		config.useCustomVertexInput = true;
		config.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		config.cullMode = VK_CULL_MODE_NONE;
		config.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		config.depthTest = true;                         // Hardware depth test
		config.depthWrite = false;                       // Don't modify depth buffer
		config.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL; // sky depth=1.0 passes where gbuffer depth=1.0
		config.blendEnable = false;
		config.colorAttachmentCount = 1;
		config.colorFormats[0] = Swapchain.GetFormat();
		config.depthFormat = Swapchain.m_DepthFormat;    // Actual depth format

		// Custom vertex input: vec3 position only (stride=12)
		config.customBinding.binding = 0;
		config.customBinding.stride = sizeof(SkyVertex);
		config.customBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		config.customAttributes[0].binding = 0;
		config.customAttributes[0].location = 0;
		config.customAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		config.customAttributes[0].offset = 0;
		config.customAttributeCount = 1;

		VkPipeline pipeline = g_PipelineManager->GetOrCreate(config);
		if (pipeline == VK_NULL_HANDLE) {
			vkCmdEndRendering(cmd);
			goto cleanup_barriers;
		}

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

		// ====================================================================
		// Step 7: Build push constants
		// ====================================================================
		struct SkyPushConstants {
			float viewProj[16];      // mat4 (64 bytes)
			float skyColorWeight[4]; // vec4 (16 bytes)
		} pushData;

		// Camera rotation-only view-projection matrix (no translation = sky follows camera)
		Fmatrix mView;
		mView.set(Device.mView);
		mView._41 = 0.0f;
		mView._42 = 0.0f;
		mView._43 = 0.0f;

		Fmatrix mViewProj;
		mViewProj.mul(Device.mProject, mView);

		// Transpose for GLSL (X-Ray row-major -> GLSL column-major)
		for (int row = 0; row < 4; row++) {
			for (int col = 0; col < 4; col++) {
				pushData.viewProj[col * 4 + row] = mViewProj.m[row][col];
			}
		}

		// Sky color tint from environment (time-of-day modulation)
		Fvector3& skyColor = env.CurrentEnv->sky_color;
		pushData.skyColorWeight[0] = skyColor.x;
		pushData.skyColorWeight[1] = skyColor.y;
		pushData.skyColorWeight[2] = skyColor.z;
		pushData.skyColorWeight[3] = env.CurrentEnv->weight;

		// Diagnostic: log sky params periodically
		static u32 s_dbg_frame = 0;
		if (Device.dwFrame > s_dbg_frame + 60) {
			s_dbg_frame = Device.dwFrame;
			Msg("[Vulkan Sky] color=(%.4f,%.4f,%.4f) weight=%.3f tex0=%s(%dx%d) tex1=%s(%dx%d)",
				skyColor.x, skyColor.y, skyColor.z, env.CurrentEnv->weight,
				skyA->IsCubemap() ? "cube" : "2D", skyA->GetWidth(), skyA->GetHeight(),
				skyB->IsCubemap() ? "cube" : "2D", skyB->GetWidth(), skyB->GetHeight());
		}

		VkPipelineLayout layout = g_PipelineManager->GetLayout();
		vkCmdPushConstants(cmd, layout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof(SkyPushConstants), &pushData);

		// ====================================================================
		// Step 8: Bind sky cubemap descriptor set
		// ====================================================================
		if (m_SkyDescSet == VK_NULL_HANDLE) {
			m_SkyDescSet = g_DescriptorManager->AllocatePerMaterial();
		}

		if (m_SkyDescSet != VK_NULL_HANDLE) {
			// Update descriptor set with mixer sky cubemaps (bindings 0 and 1)
			g_DescriptorManager->UpdateTexture(m_SkyDescSet, 0,
				skyA->GetView(), skyA->GetSampler());
			g_DescriptorManager->UpdateTexture(m_SkyDescSet, 1,
				skyB->GetView(), skyB->GetSampler());

			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
				1, 1, &m_SkyDescSet, 0, nullptr);
		}

		// ====================================================================
		// Step 9: Bind vertex/index buffers and draw
		// ====================================================================
		VkBuffer vbs[] = { m_SkyVB.m_Buffer };
		VkDeviceSize offsets[] = { 0 };
		vkCmdBindVertexBuffers(cmd, 0, 1, vbs, offsets);
		vkCmdBindIndexBuffer(cmd, m_SkyIB.m_Buffer, 0, VK_INDEX_TYPE_UINT16);

		vkCmdDrawIndexed(cmd, 60, 1, 0, 0, 0);
	}

	// ========================================================================
	// Step 10: End rendering
	// ========================================================================
	vkCmdEndRendering(cmd);

cleanup_barriers:
	// ========================================================================
	// Step 11: Transition images back for next passes
	// ========================================================================
	{
		VkImageMemoryBarrier finalBarriers[2] = {};

		// Swapchain: COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR
		finalBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		finalBarriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		finalBarriers[0].dstAccessMask = 0;
		finalBarriers[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		finalBarriers[0].newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		finalBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		finalBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		finalBarriers[0].image = swapchainImage;
		finalBarriers[0].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

		// Depth: DEPTH_READ_ONLY_OPTIMAL -> DEPTH_ATTACHMENT_OPTIMAL (for forward pass)
		finalBarriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		finalBarriers[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		finalBarriers[1].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		finalBarriers[1].oldLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
		finalBarriers[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		finalBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		finalBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		finalBarriers[1].image = depthImage;
		finalBarriers[1].subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };

		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			0, 0, nullptr, 0, nullptr, 2, finalBarriers);
	}
}

} // namespace VK
