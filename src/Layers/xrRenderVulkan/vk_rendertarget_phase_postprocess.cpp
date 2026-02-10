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

namespace VK
{

// ============================================================================
// phase_postprocess() - Post-Processing Pass
// ============================================================================
//
// Phase 2.20: Post-Processing
//
// Post-processing эффекты применяемые к финальному изображению:
// - Vignette (edge darkening)
// - Film grain (optional)
// - Chromatic aberration (optional)
// - Bloom (future - requires multi-pass)
//
// For MVP: Simple vignette effect в single pass.
//
// Process:
// 1. Render to swapchain (after forward pass)
// 2. Sample swapchain previous content
// 3. Apply vignette
// 4. Output to swapchain
//
// Note: For simple effects (vignette), we can modify swapchain in-place.
//       For complex effects (bloom), need ping-pong buffers.
//
// ============================================================================

void CRenderTarget::phase_postprocess()
{
	Msg("[Vulkan] phase_postprocess: Applying post-processing effects");

	VkCommandBuffer cmd = RCache.GetCommandBuffer();

	// ========================================================================
	// Step 1: Check if post-processing is enabled
	// ========================================================================
	// For MVP: Always enable vignette
	// TODO: Add cvars для enabling/disabling effects

	bool enableVignette = true;  // TODO: r_vignette cvar
	bool enableBloom = false;    // TODO: r_bloom cvar (future)

	if (!enableVignette && !enableBloom) {
		Msg("[Vulkan] phase_postprocess: No effects enabled, skipping");
		return;
	}

	// ========================================================================
	// Step 2: Simple vignette pass (in-place)
	// ========================================================================
	// For MVP: Apply vignette directly to swapchain
	// No need для separate render target

	// For now: Skip actual rendering (placeholder)
	// В production: нужен separate pass или интеграция в combine/forward

	// ========================================================================
	// Future: Bloom implementation
	// ========================================================================
	// Bloom требует multi-pass:
	// 1. Threshold pass (extract bright pixels) → rt_PostProcess_0
	// 2. Downsample → rt_Bloom[1], rt_Bloom[2], rt_Bloom[3]
	// 3. Blur (separable) → horizontal + vertical
	// 4. Upsample + combine → swapchain
	//
	// For MVP: Skip bloom (too complex)

	Msg("[Vulkan] phase_postprocess() complete (placeholder - vignette in combine shader)");
}

// ============================================================================
// phase_distortion() - Distortion Map Rendering
// ============================================================================
//
// Renders PP-UI elements (like the main menu magnifier) to the distortion map.
// The distortion map stores UV offsets that are applied in the combine pass
// to create glass/refraction effects.
//
// Format: R8G8B8A8_UNORM
// - R channel: X offset (127 = no offset)
// - B channel: Y offset (127 = no offset)
// - A channel: Blur amount (for soft refraction)
//
// Clear color: (127, 127, 0, 127) - neutral (no distortion)
//
// ============================================================================

void CRenderTarget::phase_distortion()
{
	VkCommandBuffer cmd = RCache.GetCommandBuffer();
	if (cmd == VK_NULL_HANDLE) {
		return;
	}

	// Check if rt_Distortion is valid
	if (rt_Distortion.GetImage() == VK_NULL_HANDLE) {
		Msg("![Vulkan] phase_distortion: rt_Distortion not created!");
		return;
	}

	// ========================================================================
	// Step 1: Transition rt_Distortion to TRANSFER_DST for clearing
	// ========================================================================
	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.srcAccessMask = 0;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = rt_Distortion.GetImage();
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;

	vkCmdPipelineBarrier(cmd,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		0,
		0, nullptr,
		0, nullptr,
		1, &barrier);

	// ========================================================================
	// Step 2: Clear distortion map to neutral (127, 127, 0, 127)
	// ========================================================================
	// 127/255 = 0.498 ≈ 0.5 (neutral offset)
	// R channel = X offset (127 = no X offset)
	// G channel = unused (set to 127 for consistency)
	// B channel = Y offset (127 = no Y offset)
	// A channel = blur amount (127 = no blur)
	VkClearColorValue clearColor = { 127.0f/255.0f, 127.0f/255.0f, 0.0f, 127.0f/255.0f };

	VkImageSubresourceRange clearRange = {};
	clearRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	clearRange.baseMipLevel = 0;
	clearRange.levelCount = 1;
	clearRange.baseArrayLayer = 0;
	clearRange.layerCount = 1;

	vkCmdClearColorImage(cmd, rt_Distortion.GetImage(),
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &clearRange);

	// ========================================================================
	// Step 3: Transition to shader read for combine pass
	// ========================================================================
	// NOTE: For now, we skip actually rendering PP-UI to rt_Distortion
	// because the UI system's pipelines are configured for swapchain format,
	// not rt_Distortion format. This means magnifier won't have distortion
	// effect yet, but at least the game won't crash.
	//
	// TODO Phase 2.20.2: Create dedicated UI pipeline for distortion rendering
	// that is compatible with rt_Distortion's R8G8B8A8_UNORM format.
	//
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	vkCmdPipelineBarrier(cmd,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		0,
		0, nullptr,
		0, nullptr,
		1, &barrier);
}

// ============================================================================
// Future: Bloom-specific methods
// ============================================================================

#if 0
void CRenderTarget::phase_bloom_threshold()
{
	// Extract bright pixels (brightness > threshold)
	// Input: Swapchain (full res)
	// Output: rt_PostProcess_0 (full res)
}

void CRenderTarget::phase_bloom_downsample()
{
	// Downsample chain (1/2, 1/4, 1/8)
	// Input: rt_PostProcess_0
	// Output: rt_Bloom[1], rt_Bloom[2], rt_Bloom[3]
}

void CRenderTarget::phase_bloom_blur()
{
	// Gaussian blur (separable, 2-pass)
	// Input: rt_Bloom[3] (smallest)
	// Output: rt_PostProcess_1 (blurred)
}

void CRenderTarget::phase_bloom_combine()
{
	// Upsample + add bloom to original
	// Input: Swapchain + rt_PostProcess_1
	// Output: Swapchain
}
#endif

} // namespace VK
