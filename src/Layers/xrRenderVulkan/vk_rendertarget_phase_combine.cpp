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
#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/Environment.h"

namespace VK
{

// ============================================================================
// phase_combine() - Combine Pass (Deferred Shading Finalization)
// ============================================================================
//
// Phase 2.18: Combine Pass
//
// Combine pass - финальный этап deferred shading:
// 1. Sample rt_Accumulator (accumulated lighting)
// 2. Sample rt_Color (albedo)
// 3. Combine: finalColor = lighting * albedo + ambient
// 4. Output to rt_HDR (HDR intermediate, no tonemapping)
//
// Tonemapping is applied later in phase_tonemap() (rt_HDR → swapchain).
// This allows future DLSS to slot in between rt_HDR and tonemap.
//
// Uses fullscreen triangle trick (no vertex buffer needed).
//
// ============================================================================

void CRenderTarget::phase_combine()
{
	VkCommandBuffer cmd = RCache.GetCommandBuffer();
	if (cmd == VK_NULL_HANDLE) return;

	// ========================================================================
	// Step 1: Get rt_HDR as render target
	// ========================================================================
	VkImage hdrImage = rt_HDR.m_Image;
	VkImageView hdrView = rt_HDR.m_ImageView;
	u32 hdrWidth = rt_HDR.m_Width;
	u32 hdrHeight = rt_HDR.m_Height;

	if (hdrImage == VK_NULL_HANDLE || hdrView == VK_NULL_HANDLE) {
		return;
	}

	// ========================================================================
	// Step 2: Transition rt_HDR to COLOR_ATTACHMENT_OPTIMAL
	// ========================================================================
	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.srcAccessMask = 0;
	barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = hdrImage;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;

	vkCmdPipelineBarrier(cmd,
	                     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
	                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
	                     0, 0, nullptr, 0, nullptr, 1, &barrier);

	// ========================================================================
	// Step 3: Begin rendering to rt_HDR
	// ========================================================================
	VkRenderingAttachmentInfo colorAttachment = {};
	colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	colorAttachment.imageView = hdrView;
	colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	// Use environment sky color as clear (HDR values, not tonemapped)
	if (g_pGamePersistent && g_pGamePersistent->Environment().CurrentEnv) {
		Fvector3& sky = g_pGamePersistent->Environment().CurrentEnv->sky_color;
		colorAttachment.clearValue.color = {{sky.x, sky.y, sky.z, 1.0f}};
	} else {
		colorAttachment.clearValue.color = {{0.3f, 0.5f, 0.7f, 1.0f}};  // Fallback blue sky
	}

	VkRenderingInfo renderingInfo = {};
	renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	renderingInfo.renderArea.offset = {0, 0};
	renderingInfo.renderArea.extent = {hdrWidth, hdrHeight};
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
	viewport.width = (float)hdrWidth;
	viewport.height = (float)hdrHeight;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor = {};
	scissor.offset = {0, 0};
	scissor.extent = {hdrWidth, hdrHeight};
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	// ========================================================================
	// Step 5: Load combine shaders
	// ========================================================================
	VkShaderModule vertShader = g_ShaderManager->Load("combine.vert.spv");
	VkShaderModule fragShader = g_ShaderManager->Load("combine.frag.spv");

	if (vertShader == VK_NULL_HANDLE || fragShader == VK_NULL_HANDLE) {
		// Fallback: just clear (shaders not found)
		vkCmdEndRendering(cmd);
		// Transition rt_HDR to SHADER_READ_ONLY for tonemap
		barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.image = hdrImage;
		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier);
		return;
	}

	// ========================================================================
	// Step 6: Create combine pipeline (output to HDR format)
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
	config.colorFormats[0] = VK_FORMAT_R16G16B16A16_SFLOAT;  // HDR output

	VkPipeline pipeline = g_PipelineManager->GetOrCreate(config);
	if (pipeline == VK_NULL_HANDLE) {
		vkCmdEndRendering(cmd);
		barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.image = hdrImage;
		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier);
		return;
	}

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	// ========================================================================
	// Step 7: Bind descriptor sets (G-Buffer textures)
	// ========================================================================
	if (m_GBufferDescSet == VK_NULL_HANDLE) {
		m_GBufferDescSet = CreateGBufferDescriptorSet();
	}

	UpdateGBufferDescriptorSet(m_GBufferDescSet);

	VkPipelineLayout layout = g_PipelineManager->GetLayout();

	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
	                        1, 1, &m_GBufferDescSet, 0, nullptr);

	// ========================================================================
	// Step 8: Push constants (HDR passthrough - no tonemapping or vignette)
	// ========================================================================
	struct CombinePushConstants {
		float exposure;
		float ambientR;
		float ambientG;
		float ambientB;
		u32 toneMappingMode;
		float vignetteInner;
		float vignetteOuter;
		float vignetteIntensity;
		float distortionScale;
		u32 enableDistortion;
		float sunDirX;
		float sunDirY;
		float sunDirZ;
		float sunColorR;
		float sunColorG;
		float sunColorB;
	} pushData;

	pushData.exposure = 2.2f;  // Boost brightness (compensate for missing lightmaps)
	if (g_pGamePersistent && g_pGamePersistent->Environment().CurrentEnv) {
		Fvector3& amb = g_pGamePersistent->Environment().CurrentEnv->ambient;
		// Raise ambient floor to compensate for missing lightmaps
		pushData.ambientR = _max(amb.x, 0.18f);
		pushData.ambientG = _max(amb.y, 0.18f);
		pushData.ambientB = _max(amb.z, 0.20f);

		// Sun direction: transform world-space sun_dir to eye-space
		Fvector3& sunDirWorld = g_pGamePersistent->Environment().CurrentEnv->sun_dir;
		Fvector sunDirEye;
		Device.mView.transform_dir(sunDirEye, sunDirWorld);
		sunDirEye.normalize_safe();
		// Negate so it points toward the sun (sun_dir points FROM sun)
		pushData.sunDirX = -sunDirEye.x;
		pushData.sunDirY = -sunDirEye.y;
		pushData.sunDirZ = -sunDirEye.z;

		// Sun color from environment
		Fvector3& sunCol = g_pGamePersistent->Environment().CurrentEnv->sun_color;
		float sunBoost = 2.5f;
		pushData.sunColorR = sunCol.x * sunBoost;
		pushData.sunColorG = sunCol.y * sunBoost;
		pushData.sunColorB = sunCol.z * sunBoost;

		// Diagnostic: log actual env values periodically
		static u32 s_lastLogFrame = 0;
		if (Device.dwFrame - s_lastLogFrame > 300) {
			s_lastLogFrame = Device.dwFrame;
			Msg("[VK-SUN] amb=(%.3f,%.3f,%.3f) sun_dir=(%.3f,%.3f,%.3f) sun_col=(%.3f,%.3f,%.3f) eye_dir=(%.3f,%.3f,%.3f)",
				amb.x, amb.y, amb.z,
				sunDirWorld.x, sunDirWorld.y, sunDirWorld.z,
				sunCol.x, sunCol.y, sunCol.z,
				pushData.sunDirX, pushData.sunDirY, pushData.sunDirZ);
		}
	} else {
		pushData.ambientR = 0.15f;
		pushData.ambientG = 0.15f;
		pushData.ambientB = 0.15f;
		pushData.sunDirX = 0.0f;
		pushData.sunDirY = 1.0f;
		pushData.sunDirZ = 0.0f;
		pushData.sunColorR = 0.8f;
		pushData.sunColorG = 0.75f;
		pushData.sunColorB = 0.65f;
	}
	pushData.toneMappingMode = 0;  // NO tonemapping — output raw HDR to rt_HDR
	pushData.vignetteInner = 0.0f;
	pushData.vignetteOuter = 1.0f;
	pushData.vignetteIntensity = 0.0f;  // No vignette — moved to tonemap pass

	// Distortion settings
	pushData.distortionScale = 0.08f;
	pushData.enableDistortion = 1;  // Always enable (will show neutral if no distortion)

	vkCmdPushConstants(cmd, layout,
	                   VK_SHADER_STAGE_FRAGMENT_BIT,
	                   0, sizeof(CombinePushConstants), &pushData);

	// ========================================================================
	// Step 9: Draw fullscreen triangle
	// ========================================================================
	vkCmdDraw(cmd, 3, 1, 0, 0);

	// ========================================================================
	// Step 10: End rendering
	// ========================================================================
	vkCmdEndRendering(cmd);

	// ========================================================================
	// Step 11: Transition rt_HDR to SHADER_READ_ONLY for subsequent passes
	// ========================================================================
	// Sky/clouds/details/forward will transition it back to COLOR_ATTACHMENT
	// as needed, and finally tonemap will read it as SHADER_READ_ONLY.
	// For now, leave in COLOR_ATTACHMENT for the next scene pass (sky).
	barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	barrier.image = hdrImage;

	vkCmdPipelineBarrier(cmd,
	                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
	                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
	                     0, 0, nullptr, 0, nullptr, 1, &barrier);
}

} // namespace VK
