// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

#include "stdafx.h"
#include "vk_rendertarget.h"
#include "vk_shaders.h"
#include "vk_pipeline.h"

namespace VK
{

// ============================================================================
// GetShadowPipeline() - Get or create depth-only pipeline for shadow maps
// ============================================================================

VkPipeline CRenderTarget::GetShadowPipeline()
{
	// Return cached pipeline if already created
	if (m_ShadowPipeline != VK_NULL_HANDLE)
		return m_ShadowPipeline;

	Msg("[Vulkan] Creating shadow map depth-only pipeline...");

	// ========================================================================
	// Step 1: Load shadow shaders
	// ========================================================================

	VkShaderModule vertShader = g_ShaderManager->Load("shadow_depth.vert.spv");
	VkShaderModule fragShader = g_ShaderManager->Load("shadow_depth.frag.spv");

	if (vertShader == VK_NULL_HANDLE || fragShader == VK_NULL_HANDLE) {
		Msg("![Vulkan] Failed to load shadow shaders");
		Msg("![Vulkan] Make sure to compile shaders:");
		Msg("![Vulkan]   cd gamedata/shaders/vulkan");
		Msg("![Vulkan]   compile_shadow.bat");
		return VK_NULL_HANDLE;
	}

	// ========================================================================
	// Step 2: Configure depth-only pipeline
	// ========================================================================

	PipelineConfig config = {};
	config.vertShader = vertShader;
	config.fragShader = fragShader;

	// Topology
	config.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	// Rasterization
	config.cullMode = VK_CULL_MODE_BACK_BIT;  // Cull back faces
	config.frontFace = VK_FRONT_FACE_CLOCKWISE;
	config.polygonMode = VK_POLYGON_MODE_FILL;

	// Depth testing
	config.depthTest = true;
	config.depthWrite = true;
	config.depthCompareOp = VK_COMPARE_OP_LESS;
	config.depthFormat = VK_FORMAT_D32_SFLOAT;  // Shadow map format

	// Color attachments - NONE for depth-only rendering
	config.colorAttachmentCount = 0;
	config.blendEnable = false;

	// Vertex input - use default (position only is enough for shadows)
	config.useDefaultVertexInput = true;

	// ========================================================================
	// Step 3: Create or get cached pipeline
	// ========================================================================

	m_ShadowPipeline = g_PipelineManager->GetOrCreate(config);

	if (m_ShadowPipeline == VK_NULL_HANDLE) {
		Msg("![Vulkan] Failed to create shadow pipeline");
		return VK_NULL_HANDLE;
	}

	Msg("[Vulkan] Shadow pipeline created successfully");
	return m_ShadowPipeline;
}

} // namespace VK
