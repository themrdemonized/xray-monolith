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
#include "../../xrEngine/xrHemisphere.h"

namespace VK
{

// ============================================================================
// Cloud vertex: position + packed wind + packed color (20 bytes)
// ============================================================================

struct CloudVertex {
	float x, y, z;  // 12 bytes - position
	u32 wind;        // 4 bytes  - R8G8B8A8_UNORM wind direction
	u32 color;       // 4 bytes  - R8G8B8A8_UNORM cloud vertex color
};

// ============================================================================
// CreateCloudGeometry() - Create hemisphere IB (static) + VB (host-visible)
// ============================================================================

void CRenderTarget::CreateCloudGeometry()
{
	if (m_bCloudGeometryCreated) return;

	// Get hemisphere indices (quality 2 = 91 verts, 160 faces, 480 indices)
	const u16* hemiIndices = nullptr;
	int indexCount = xrHemisphereIndices(2, hemiIndices);
	if (!hemiIndices || indexCount <= 0) {
		Msg("![Vulkan] Failed to get hemisphere indices for clouds");
		return;
	}

	// Create static index buffer
	u32 ibSize = indexCount * sizeof(u16);
	m_CloudIB.Create(
		ibSize,
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
	);
	m_CloudIB.Upload(hemiIndices, ibSize);

	// Create host-visible vertex buffer (updated every frame)
	// 91 vertices * 20 bytes = 1820 bytes
	const Fvector* hemiVerts = nullptr;
	int vertCount = xrHemisphereVertices(2, hemiVerts);
	u32 vbSize = vertCount * sizeof(CloudVertex);

	m_CloudVB.Create(
		vbSize,
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		VMA_MEMORY_USAGE_AUTO_PREFER_HOST
	);

	m_bCloudGeometryCreated = true;
	Msg("[Vulkan] Cloud geometry created (%d verts, %d indices)", vertCount, indexCount);
}

// ============================================================================
// GetOrCreateFallbackCloud() - Lazy-create 1x1 white fallback texture
// ============================================================================

CVulkanTexture* CRenderTarget::GetOrCreateFallbackCloud()
{
	if (m_FallbackCloud)
		return m_FallbackCloud;

	m_FallbackCloud = xr_new<CVulkanTexture>();
	m_FallbackCloud->Create(1, 1, VK_FORMAT_R8G8B8A8_UNORM, 1);

	u8 white[4] = { 255, 255, 255, 128 };
	CVulkanBuffer staging;
	staging.Create(4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST);
	void* mapped = staging.Map();
	if (mapped) {
		memcpy(mapped, white, 4);
		staging.Flush();
		staging.Unmap();
	}

	VkCommandBuffer cmd = CommandManager.BeginImmediate();
	if (cmd != VK_NULL_HANDLE) {
		m_FallbackCloud->TransitionLayout(cmd, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		VkBufferImageCopy region = {};
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.layerCount = 1;
		region.imageExtent = { 1, 1, 1 };
		vkCmdCopyBufferToImage(cmd, staging.m_Buffer, m_FallbackCloud->GetImage(),
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
		m_FallbackCloud->TransitionLayout(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		CommandManager.EndAndSubmitImmediate(cmd);
	}
	staging.Destroy();

	Msg("[Vulkan] Created fallback cloud texture");
	return m_FallbackCloud;
}

// ============================================================================
// DestroyCloudResources()
// ============================================================================

void CRenderTarget::DestroyCloudResources()
{
	if (m_FallbackCloud) {
		m_FallbackCloud->Destroy();
		xr_delete(m_FallbackCloud);
	}

	m_CloudVB.Destroy();
	m_CloudIB.Destroy();
	m_CloudDescSet = VK_NULL_HANDLE;
	m_bCloudGeometryCreated = false;
}

// ============================================================================
// Helper: pack float4 color to R8G8B8A8_UNORM u32
// ============================================================================

static u32 PackColorU32(float r, float g, float b, float a)
{
	u32 R = (u32)_min(255.f, _max(0.f, r * 255.f));
	u32 G = (u32)_min(255.f, _max(0.f, g * 255.f));
	u32 B = (u32)_min(255.f, _max(0.f, b * 255.f));
	u32 A = (u32)_min(255.f, _max(0.f, a * 255.f));
	return R | (G << 8) | (B << 16) | (A << 24);
}

// ============================================================================
// phase_clouds() - Render cloud hemisphere after sky, before forward
// ============================================================================
//
// Cloud textures come from vkEnvDescriptorMixerRender (populated by engine's
// CEnvDescriptorMixer::lerp() every frame). This ensures Lua weather overrides
// via set_weather_value_string() are picked up automatically.
//

void CRenderTarget::phase_clouds()
{
	// Diagnostic: log entry once
	static bool s_logged_entry = false;
	if (!s_logged_entry) {
		Msg("[Vulkan Clouds] phase_clouds() entered");
		s_logged_entry = true;
	}

	VkCommandBuffer cmd = RCache.GetCommandBuffer();
	if (cmd == VK_NULL_HANDLE) { static bool s1 = false; if (!s1) { Msg("![Vulkan Clouds] no cmd buffer"); s1 = true; } return; }

	// Skip if no environment data
	if (!g_pGamePersistent || !g_pGamePersistent->Environment().CurrentEnv) { static bool s2 = false; if (!s2) { Msg("![Vulkan Clouds] no env data"); s2 = true; } return; }

	CEnvironment& env = g_pGamePersistent->Environment();

	// Periodic diagnostic: log clouds state every 120 frames
	{
		static u32 s_dbg_cloud_frame = 0;
		if (Device.dwFrame > s_dbg_cloud_frame + 120) {
			s_dbg_cloud_frame = Device.dwFrame;
			vkEnvDescriptorMixerRender* dbg_mixer = static_cast<vkEnvDescriptorMixerRender*>(
				&*env.CurrentEnv->m_pDescriptorMixer);
			Msg("[Vulkan Clouds] clouds_color=(%.3f,%.3f,%.3f,%.4f) weight=%.3f tex0=%p tex1=%p clouds_name='%s'",
				env.CurrentEnv->clouds_color.x, env.CurrentEnv->clouds_color.y,
				env.CurrentEnv->clouds_color.z, env.CurrentEnv->clouds_color.w,
				env.CurrentEnv->weight,
				dbg_mixer->clouds_tex[0], dbg_mixer->clouds_tex[1],
				env.CurrentEnv->clouds_texture_name.size() ? env.CurrentEnv->clouds_texture_name.c_str() : "(empty)");
		}
	}

	// Skip if clouds alpha is zero (no clouds in this weather)
	if (env.CurrentEnv->clouds_color.w < 0.001f)
		return;

	// Create geometry on first use
	CreateCloudGeometry();
	if (!m_bCloudGeometryCreated) { static bool s4 = false; if (!s4) { Msg("![Vulkan Clouds] geometry not created"); s4 = true; } return; }

	// ========================================================================
	// Get cloud textures from the mixer (populated by engine lerp() each frame)
	// ========================================================================
	vkEnvDescriptorMixerRender* mixer = static_cast<vkEnvDescriptorMixerRender*>(
		&*env.CurrentEnv->m_pDescriptorMixer);

	CVulkanTexture* cloudA = mixer->clouds_tex[0];
	CVulkanTexture* cloudB = mixer->clouds_tex[1];

	// Anomaly sets clouds via Lua on CurrentEnv->m_pDescriptor (not on Current[0]/[1]).
	// lerp() overwrites clouds_tex[] from A/B each frame, so mixer textures may be null.
	// Fall back to the texture loaded directly on the mixer's own descriptor.
	if ((!cloudA || !cloudA->IsValid()) || (!cloudB || !cloudB->IsValid()))
	{
		vkEnvDescriptorRender* mixerDesc = static_cast<vkEnvDescriptorRender*>(
			&*env.CurrentEnv->m_pDescriptor);
		if (mixerDesc && mixerDesc->clouds_texture && mixerDesc->clouds_texture->IsValid())
		{
			if (!cloudA || !cloudA->IsValid()) cloudA = mixerDesc->clouds_texture;
			if (!cloudB || !cloudB->IsValid()) cloudB = mixerDesc->clouds_texture;
		}
	}

	// Use fallback texture if still not available
	if (!cloudA || !cloudA->IsValid())
		cloudA = GetOrCreateFallbackCloud();
	if (!cloudB || !cloudB->IsValid())
		cloudB = GetOrCreateFallbackCloud();

	if (!cloudA || !cloudA->IsValid()) { static bool s5 = false; if (!s5) { Msg("![Vulkan Clouds] cloudA invalid"); s5 = true; } return; }
	if (!cloudB || !cloudB->IsValid()) { static bool s6 = false; if (!s6) { Msg("![Vulkan Clouds] cloudB invalid"); s6 = true; } return; }

	// ========================================================================
	// Step 1: Update vertex buffer with current frame data
	// ========================================================================
	const Fvector* hemiVerts = nullptr;
	int vertCount = xrHemisphereVertices(2, hemiVerts);
	if (!hemiVerts || vertCount <= 0) return;

	// Wind direction as packed UNORM (remap [-1..1] -> [0..1])
	// Use environment wind direction
	float windAngle = env.CurrentEnv->wind_direction * (PI / 180.f);
	float wx = _cos(windAngle) * 0.5f + 0.5f;  // pack to [0..1]
	float wz = _sin(windAngle) * 0.5f + 0.5f;
	u32 windPacked = PackColorU32(wx, 0.5f, wz, 0.5f);

	// Cloud color from environment
	Fvector4& cc = env.CurrentEnv->clouds_color;
	u32 colorPacked = PackColorU32(cc.x, cc.y, cc.z, cc.w);

	// Map VB and fill
	void* mappedVB = m_CloudVB.Map();
	if (!mappedVB) return;

	CloudVertex* verts = (CloudVertex*)mappedVB;
	for (int i = 0; i < vertCount; i++) {
		verts[i].x = hemiVerts[i].x;
		verts[i].y = hemiVerts[i].y;
		verts[i].z = hemiVerts[i].z;
		verts[i].wind = windPacked;
		verts[i].color = colorPacked;
	}

	m_CloudVB.Flush();
	m_CloudVB.Unmap();

	// ========================================================================
	// Step 2: Get rt_HDR and depth resources
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
	// Step 3: Transition images for cloud rendering
	// ========================================================================
	VkImageMemoryBarrier barriers[2] = {};

	// rt_HDR: already COLOR_ATTACHMENT from previous pass — execution barrier
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
	// Step 4: Begin rendering with color + read-only depth
	// ========================================================================
	VkRenderingAttachmentInfo colorAttachment = {};
	colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	colorAttachment.imageView = hdrView;
	colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // Preserve sky output
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

	VkRenderingAttachmentInfo depthAttachment = {};
	depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	depthAttachment.imageView = depthView;
	depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_NONE;

	VkRenderingInfo renderingInfo = {};
	renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	renderingInfo.renderArea.offset = { 0, 0 };
	renderingInfo.renderArea.extent = { hdrWidth, hdrHeight };
	renderingInfo.layerCount = 1;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachments = &colorAttachment;
	renderingInfo.pDepthAttachment = &depthAttachment;

	vkCmdBeginRendering(cmd, &renderingInfo);

	// ========================================================================
	// Step 5: Viewport and scissor
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
	scissor.offset = { 0, 0 };
	scissor.extent = { hdrWidth, hdrHeight };
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	// ========================================================================
	// Step 6: Load cloud shaders
	// ========================================================================
	VkShaderModule vertShader = g_ShaderManager->Load("clouds.vert.spv");
	VkShaderModule fragShader = g_ShaderManager->Load("clouds.frag.spv");

	if (vertShader == VK_NULL_HANDLE || fragShader == VK_NULL_HANDLE) {
		vkCmdEndRendering(cmd);
		goto cleanup_barriers;
	}

	{
		// ====================================================================
		// Step 7: Create cloud pipeline (alpha blend, depth test, no depth write)
		// ====================================================================
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
		config.blendEnable = true;
		config.srcColorBlend = VK_BLEND_FACTOR_SRC_ALPHA;
		config.dstColorBlend = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		config.srcAlphaBlend = VK_BLEND_FACTOR_ONE;
		config.dstAlphaBlend = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		config.colorAttachmentCount = 1;
		config.colorFormats[0] = VK_FORMAT_R16G16B16A16_SFLOAT;  // HDR output
		config.depthFormat = Swapchain.m_DepthFormat;

		// Custom vertex input: CloudVertex (20 bytes stride, 3 attributes)
		config.customBinding.binding = 0;
		config.customBinding.stride = sizeof(CloudVertex);
		config.customBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		// location 0: vec3 position (offset 0)
		config.customAttributes[0].binding = 0;
		config.customAttributes[0].location = 0;
		config.customAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		config.customAttributes[0].offset = offsetof(CloudVertex, x);

		// location 1: vec4 wind (offset 12, R8G8B8A8_UNORM)
		config.customAttributes[1].binding = 0;
		config.customAttributes[1].location = 1;
		config.customAttributes[1].format = VK_FORMAT_R8G8B8A8_UNORM;
		config.customAttributes[1].offset = offsetof(CloudVertex, wind);

		// location 2: vec4 color (offset 16, R8G8B8A8_UNORM)
		config.customAttributes[2].binding = 0;
		config.customAttributes[2].location = 2;
		config.customAttributes[2].format = VK_FORMAT_R8G8B8A8_UNORM;
		config.customAttributes[2].offset = offsetof(CloudVertex, color);

		config.customAttributeCount = 3;

		VkPipeline pipeline = g_PipelineManager->GetOrCreate(config);
		if (pipeline == VK_NULL_HANDLE) {
			vkCmdEndRendering(cmd);
			goto cleanup_barriers;
		}

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

		// ====================================================================
		// Step 8: Build push constants
		// ====================================================================
		struct CloudPushConstants {
			float worldViewProj[16]; // mat4 (64 bytes)
			float cloudsColor[4];    // vec4 (16 bytes)
			float weight;            // float (4 bytes)
			float time;              // float (4 bytes)
		} pushData;

		// Build world matrix: scale(10, 0.4, 10) * rotateY(sky_rotation), translate to camera
		Fmatrix mScale;
		mScale.scale(10.0f, 0.4f, 10.0f);

		Fmatrix mRotY;
		mRotY.rotateY(env.CurrentEnv->sky_rotation);

		Fmatrix mWorld;
		mWorld.mul(mScale, mRotY);

		// Translate to camera position
		Fvector camPos = Device.vCameraPosition;
		mWorld._41 += camPos.x;
		mWorld._42 += camPos.y + 0.3f; // Slight upward offset
		mWorld._43 += camPos.z;

		// WVP = Projection * View * World
		Fmatrix mWV;
		mWV.mul(Device.mView, mWorld);

		Fmatrix mWVP;
		mWVP.mul(Device.mProject, mWV);

		// Transpose for GLSL (X-Ray row-major -> GLSL column-major)
		for (int row = 0; row < 4; row++) {
			for (int col = 0; col < 4; col++) {
				pushData.worldViewProj[col * 4 + row] = mWVP.m[row][col];
			}
		}

		// Cloud color from environment
		pushData.cloudsColor[0] = env.CurrentEnv->clouds_color.x;
		pushData.cloudsColor[1] = env.CurrentEnv->clouds_color.y;
		pushData.cloudsColor[2] = env.CurrentEnv->clouds_color.z;
		pushData.cloudsColor[3] = env.CurrentEnv->clouds_color.w;

		// Weight and time
		pushData.weight = env.CurrentEnv->weight;
		pushData.time = Device.fTimeGlobal;

		VkPipelineLayout layout = g_PipelineManager->GetLayout();
		vkCmdPushConstants(cmd, layout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof(CloudPushConstants), &pushData);

		// ====================================================================
		// Step 9: Bind cloud texture descriptor set
		// ====================================================================
		if (m_CloudDescSet == VK_NULL_HANDLE) {
			m_CloudDescSet = g_DescriptorManager->AllocatePerMaterial();
		}

		if (m_CloudDescSet != VK_NULL_HANDLE) {
			g_DescriptorManager->UpdateTexture(m_CloudDescSet, 0,
				cloudA->GetView(), cloudA->GetSampler());
			g_DescriptorManager->UpdateTexture(m_CloudDescSet, 1,
				cloudB->GetView(), cloudB->GetSampler());

			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
				1, 1, &m_CloudDescSet, 0, nullptr);
		}

		// ====================================================================
		// Step 10: Bind vertex/index buffers and draw
		// ====================================================================
		VkBuffer vbs[] = { m_CloudVB.m_Buffer };
		VkDeviceSize offsets[] = { 0 };
		vkCmdBindVertexBuffers(cmd, 0, 1, vbs, offsets);
		vkCmdBindIndexBuffer(cmd, m_CloudIB.m_Buffer, 0, VK_INDEX_TYPE_UINT16);

		// 160 triangles * 3 = 480 indices
		const u16* hemiIdx = nullptr;
		int indexCount = xrHemisphereIndices(2, hemiIdx);
		vkCmdDrawIndexed(cmd, (u32)indexCount, 1, 0, 0, 0);
	}

	// ========================================================================
	// Step 11: End rendering
	// ========================================================================
	vkCmdEndRendering(cmd);

cleanup_barriers:
	// ========================================================================
	// Step 12: Transition images back for next passes
	// ========================================================================
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

		// Depth: DEPTH_READ_ONLY_OPTIMAL -> DEPTH_ATTACHMENT_OPTIMAL
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
