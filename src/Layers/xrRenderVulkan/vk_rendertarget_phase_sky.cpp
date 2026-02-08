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
struct SkyVertex {
	float x, y, z;
};

static const SkyVertex s_SkyVertices[12] = {
	{-1.0f,  1.0f, -1.0f}, { 1.0f,  1.0f, -1.0f},
	{-1.0f,  1.0f,  1.0f}, { 1.0f,  1.0f,  1.0f},
	{-1.0f,  0.0f, -1.0f}, { 1.0f,  0.0f, -1.0f},
	{ 1.0f,  0.0f,  1.0f}, {-1.0f,  0.0f,  1.0f},
	{-1.0f, -1.0f, -1.0f}, { 1.0f, -1.0f, -1.0f},
	{ 1.0f, -1.0f,  1.0f}, {-1.0f, -1.0f,  1.0f},
};

static const u16 s_SkyIndices[60] = {
	0, 2, 1,  1, 2, 3,
	0, 1, 4,  4, 1, 5,  4, 5, 8,  8, 5, 9,
	1, 3, 5,  5, 3, 6,  5, 6, 9,  9, 6, 10,
	3, 2, 6,  6, 2, 7,  6, 7, 10, 10, 7, 11,
	2, 0, 7,  7, 0, 4,  7, 4, 11, 11, 4, 8,
};

// ============================================================================
// CreateSkyGeometry()
// ============================================================================
void CRenderTarget::CreateSkyGeometry()
{
	if (m_bSkyGeometryCreated) return;

	m_SkyVB.Create(sizeof(s_SkyVertices),
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
	m_SkyVB.Upload(s_SkyVertices, sizeof(s_SkyVertices));

	m_SkyIB.Create(sizeof(s_SkyIndices),
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
	m_SkyIB.Upload(s_SkyIndices, sizeof(s_SkyIndices));

	m_bSkyGeometryCreated = true;
	Msg("[Vulkan] Sky geometry created (12 verts, 60 indices)");
}

// ============================================================================
// CreateFallbackCubemap()
// ============================================================================
CVulkanTexture* CRenderTarget::CreateFallbackCubemap(float r, float g, float b)
{
	CVulkanTexture* tex = xr_new<CVulkanTexture>();
	tex->m_bCubemap = true;
	tex->m_ArrayLayers = 6;
	tex->Create(1, 1, VK_FORMAT_R8G8B8A8_UNORM, 1);

	u8 pixel[24];
	u8 R = (u8)(r * 255.0f);
	u8 G = (u8)(g * 255.0f);
	u8 B = (u8)(b * 255.0f);
	for (int i = 0; i < 6; i++) {
		pixel[i*4+0] = R; pixel[i*4+1] = G; pixel[i*4+2] = B; pixel[i*4+3] = 255;
	}

	CVulkanBuffer staging;
	staging.Create(24, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST);
	void* mapped = staging.Map();
	if (mapped) { memcpy(mapped, pixel, 24); staging.Flush(); staging.Unmap(); }

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

CVulkanTexture* CRenderTarget::GetOrCreateFallbackSky()
{
	if (!m_FallbackSky) {
		m_FallbackSky = CreateFallbackCubemap(0.3f, 0.5f, 0.8f);
		Msg("[Vulkan] Created fallback sky cubemap");
	}
	return m_FallbackSky;
}

void CRenderTarget::DestroySkyResources()
{
	if (m_FallbackSky) { m_FallbackSky->Destroy(); xr_delete(m_FallbackSky); }
	m_SkyVB.Destroy();
	m_SkyIB.Destroy();
	m_SkyDescSet = VK_NULL_HANDLE;
	m_bSkyGeometryCreated = false;
}

// ============================================================================
// phase_sky() - Render textured skybox to rt_HDR
// ============================================================================
void CRenderTarget::phase_sky()
{
	VkCommandBuffer cmd = RCache.GetCommandBuffer();
	if (cmd == VK_NULL_HANDLE) return;
	if (!g_pGamePersistent || !g_pGamePersistent->Environment().CurrentEnv) return;

	CEnvironment& env = g_pGamePersistent->Environment();
	CreateSkyGeometry();
	if (!m_bSkyGeometryCreated) return;

	vkEnvDescriptorMixerRender* mixer = static_cast<vkEnvDescriptorMixerRender*>(
		&*env.CurrentEnv->m_pDescriptorMixer);

	CVulkanTexture* skyA = mixer->sky_tex[0];
	CVulkanTexture* skyB = mixer->sky_tex[1];
	if (!skyA || !skyA->IsValid()) skyA = GetOrCreateFallbackSky();
	if (!skyB || !skyB->IsValid()) skyB = GetOrCreateFallbackSky();
	if (!skyA || !skyA->IsValid()) return;
	if (!skyB || !skyB->IsValid()) return;

	// ========================================================================
	// Get rt_HDR and depth resources
	// ========================================================================
	VkImage hdrImage = rt_HDR.m_Image;
	VkImageView hdrView = rt_HDR.m_ImageView;
	u32 hdrWidth = rt_HDR.m_Width;
	u32 hdrHeight = rt_HDR.m_Height;
	if (hdrImage == VK_NULL_HANDLE || hdrView == VK_NULL_HANDLE) return;

	VkImageView depthView = Swapchain.m_DepthView;
	VkImage depthImage = Swapchain.m_DepthImage;
	if (depthView == VK_NULL_HANDLE || depthImage == VK_NULL_HANDLE) return;

	// ========================================================================
	// Transition images for sky rendering
	// ========================================================================
	VkImageMemoryBarrier barriers[2] = {};

	// rt_HDR: already COLOR_ATTACHMENT from combine — execution barrier
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

	// ========================================================================
	// Begin rendering to rt_HDR + read-only depth
	// ========================================================================
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
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_NONE;

	VkRenderingInfo renderingInfo = {};
	renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	renderingInfo.renderArea.offset = {0, 0};
	renderingInfo.renderArea.extent = {hdrWidth, hdrHeight};
	renderingInfo.layerCount = 1;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachments = &colorAttachment;
	renderingInfo.pDepthAttachment = &depthAttachment;

	vkCmdBeginRendering(cmd, &renderingInfo);

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

	VkShaderModule vertShader = g_ShaderManager->Load("sky.vert.spv");
	VkShaderModule fragShader = g_ShaderManager->Load("sky.frag.spv");

	if (vertShader == VK_NULL_HANDLE || fragShader == VK_NULL_HANDLE) {
		vkCmdEndRendering(cmd);
		goto cleanup_barriers;
	}

	{
		PipelineConfig config;
		config.vertShader = vertShader;
		config.fragShader = fragShader;
		config.useDefaultVertexInput = false;
		config.useCustomVertexInput = true;
		config.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		config.cullMode = VK_CULL_MODE_NONE;
		config.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		config.depthTest = true;
		config.depthWrite = false;
		config.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		config.blendEnable = false;
		config.colorAttachmentCount = 1;
		config.colorFormats[0] = VK_FORMAT_R16G16B16A16_SFLOAT;  // HDR output
		config.depthFormat = Swapchain.m_DepthFormat;

		config.customBinding.binding = 0;
		config.customBinding.stride = sizeof(SkyVertex);
		config.customBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		config.customAttributes[0].binding = 0;
		config.customAttributes[0].location = 0;
		config.customAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		config.customAttributes[0].offset = 0;
		config.customAttributeCount = 1;

		VkPipeline pipeline = g_PipelineManager->GetOrCreate(config);
		if (pipeline == VK_NULL_HANDLE) { vkCmdEndRendering(cmd); goto cleanup_barriers; }

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

		struct SkyPushConstants {
			float viewProj[16];
			float skyColorWeight[4];
		} pushData;

		Fmatrix mView;
		mView.set(Device.mView);
		mView._41 = 0.0f; mView._42 = 0.0f; mView._43 = 0.0f;
		Fmatrix mViewProj;
		mViewProj.mul(Device.mProject, mView);
		for (int row = 0; row < 4; row++)
			for (int col = 0; col < 4; col++)
				pushData.viewProj[col * 4 + row] = mViewProj.m[row][col];

		Fvector3& skyColor = env.CurrentEnv->sky_color;
		pushData.skyColorWeight[0] = skyColor.x;
		pushData.skyColorWeight[1] = skyColor.y;
		pushData.skyColorWeight[2] = skyColor.z;
		pushData.skyColorWeight[3] = env.CurrentEnv->weight;

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

		if (m_SkyDescSet == VK_NULL_HANDLE)
			m_SkyDescSet = g_DescriptorManager->AllocatePerMaterial();

		if (m_SkyDescSet != VK_NULL_HANDLE) {
			g_DescriptorManager->UpdateTexture(m_SkyDescSet, 0, skyA->GetView(), skyA->GetSampler());
			g_DescriptorManager->UpdateTexture(m_SkyDescSet, 1, skyB->GetView(), skyB->GetSampler());
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
				1, 1, &m_SkyDescSet, 0, nullptr);
		}

		VkBuffer vbs[] = { m_SkyVB.m_Buffer };
		VkDeviceSize offsets[] = { 0 };
		vkCmdBindVertexBuffers(cmd, 0, 1, vbs, offsets);
		vkCmdBindIndexBuffer(cmd, m_SkyIB.m_Buffer, 0, VK_INDEX_TYPE_UINT16);
		vkCmdDrawIndexed(cmd, 60, 1, 0, 0, 0);
	}

	vkCmdEndRendering(cmd);

cleanup_barriers:
	{
		VkImageMemoryBarrier finalBarriers[2] = {};

		// rt_HDR: stay in COLOR_ATTACHMENT_OPTIMAL for next pass
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
		finalBarriers[1].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		finalBarriers[1].oldLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
		finalBarriers[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		finalBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		finalBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		finalBarriers[1].image = depthImage;
		finalBarriers[1].subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };

		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			0, 0, nullptr, 0, nullptr, 2, finalBarriers);
	}
}

} // namespace VK
