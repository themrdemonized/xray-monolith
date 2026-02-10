// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

#include "stdafx.h"
#include "vk_rendertarget.h"
#include "rvk.h"
#include "vk_R_Backend.h"
#include "HW_Vulkan.h"
#include "vk_shaders.h"
#include "vk_pipeline.h"

namespace VK
{

// ============================================================================
// accum_direct_cascades() - Directional light accumulation with cascade shadows
// ============================================================================
//
// Полная версия sun accumulation с cascade shadow maps.
// Для каждого каскада:
// 1. Shadow map уже отрендерена в rt_smap_depth
// 2. Вычисляем lighting с shadow sampling
// 3. Accumulate в rt_Accumulator
//
// ============================================================================

void CRenderTarget::accum_direct_cascades(u32 sub_phase)
{
	// accum_direct_cascades

	// Get sun light
	light* sun = (light*)RImplementation.Lights.sun_adapted._get();
	if (!sun) {
		return;
	}

	VkCommandBuffer cmd = RCache.GetCommandBuffer();

	// ========================================================================
	// Step 1: Transition rt_Accumulator to COLOR_ATTACHMENT
	// ========================================================================

	// TODO: Implement proper image layout transitions
	// For now assume accumulator is already in correct layout from phase_accumulator()

	// ========================================================================
	// Step 2: Begin rendering to accumulator
	// ========================================================================

	VkRenderingAttachmentInfo colorAttachment = {};
	colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	colorAttachment.imageView = rt_Accumulator.m_ImageView;
	colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;  // Preserve previous lighting
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

	VkRenderingInfo renderingInfo = {};
	renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	renderingInfo.renderArea.offset = {0, 0};
	renderingInfo.renderArea.extent = {m_Width, m_Height};
	renderingInfo.layerCount = 1;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachments = &colorAttachment;

	vkCmdBeginRendering(cmd, &renderingInfo);

	// ========================================================================
	// Step 3: Setup viewport and scissor
	// ========================================================================

	VkViewport viewport = {};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float)m_Width;
	viewport.height = (float)m_Height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor = {};
	scissor.offset = {0, 0};
	scissor.extent = {m_Width, m_Height};
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	// ========================================================================
	// Step 4: Get cascade shadow shader pipeline
	// ========================================================================

	// Load shaders
	VkShaderModule vertShader = g_ShaderManager->Load("accum_sun_simple.vert.spv");
	VkShaderModule fragShader = g_ShaderManager->Load("accum_sun_cascades.frag.spv");

	if (vertShader == VK_NULL_HANDLE || fragShader == VK_NULL_HANDLE) {
		Msg("![Vulkan] Failed to load cascade accumulation shaders");
		vkCmdEndRendering(cmd);
		return;
	}

	// Configure pipeline
	PipelineConfig config = {};
	config.vertShader = vertShader;
	config.fragShader = fragShader;
	config.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	config.cullMode = VK_CULL_MODE_NONE;  // Fullscreen quad
	config.depthTest = false;  // No depth testing for lighting accumulation
	config.depthWrite = false;
	config.blendEnable = true;  // Additive blending for light accumulation
	config.colorAttachmentCount = 1;
	config.colorFormats[0] = VK_FORMAT_R16G16B16A16_SFLOAT;  // rt_Accumulator format

	VkPipeline pipeline = g_PipelineManager->GetOrCreate(config);
	if (pipeline == VK_NULL_HANDLE) {
		Msg("![Vulkan] Failed to create cascade accumulation pipeline");
		vkCmdEndRendering(cmd);
		return;
	}

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	// ========================================================================
	// Step 5: Bind descriptor sets
	// ========================================================================

	// Get or create descriptor sets
	if (m_GBufferDescSet == VK_NULL_HANDLE) {
		m_GBufferDescSet = CreateGBufferDescriptorSet();
	}
	if (m_SunDescSet == VK_NULL_HANDLE) {
		m_SunDescSet = CreateSunDescriptorSet();
	}

	// Update descriptor sets for this cascade
	UpdateGBufferDescriptorSet(m_GBufferDescSet);
	UpdateSunDescriptorSet(m_SunDescSet, sub_phase);

	// Bind descriptor sets
	VkPipelineLayout layout = g_PipelineManager->GetLayout();

	VkDescriptorSet sets[4] = {
		VK_NULL_HANDLE,      // Set 0: PerFrame (not used for now)
		m_GBufferDescSet,    // Set 1: G-Buffer textures
		VK_NULL_HANDLE,      // Set 2: PerObject (not used for fullscreen quad)
		m_SunDescSet         // Set 3: Shadow map + sun uniforms
	};

	// Bind Set 1 (G-Buffer) and Set 3 (Shadow map)
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
	                        1, 1, &sets[1], 0, nullptr);  // Set 1
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
	                        3, 1, &sets[3], 0, nullptr);  // Set 3

	// Descriptor sets bound

	// ========================================================================
	// Step 6: Draw fullscreen quad
	// ========================================================================

	// TODO: Render fullscreen quad
	// For now, just placeholder
	// vkCmdDraw(cmd, 6, 1, 0, 0);  // 2 triangles = 6 vertices

	// TODO: Draw fullscreen quad with cascade shadow sampling

	// ========================================================================
	// Step 7: End rendering
	// ========================================================================

	vkCmdEndRendering(cmd);

	// accum_direct_cascades complete
}

} // namespace VK
