// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

#include "stdafx.h"
#include "vk_rendertarget.h"
#include "HW_Vulkan.h"

namespace VK
{

// ============================================================================
// vk_cone_volume.cpp - Cone Volume Geometry for Spot Lights
// ============================================================================
//
// Phase 2.17.4: Cone Geometry Generation
//
// Generates a cone mesh for spot light volume rendering.
// Cone is oriented along -Z axis (apex at origin, base at Z = -1).
//
// Geometry:
// - 32 segments around base circle
// - 1 apex vertex at origin
// - 32 vertices around base circle
// - 1 center vertex at base
// - Total: 34 vertices
//
// Triangles:
// - 32 side triangles (apex → base edge)
// - 32 base triangles (base center → base edge)
// - Total: 64 triangles, 192 indices
//
// ============================================================================

// ============================================================================
// CreateSpotVolumeGeometry() - Generate cone mesh
// ============================================================================
//
// Creates cone geometry for spot light volumes.
//
// Cone parameters:
// - Apex at (0, 0, 0)
// - Base at Z = -1
// - Base radius = 1.0 (scaled by light range and cone angle in shader)
// - 32 segments around base
//
// Vertex format: vec4 (x, y, z, 1.0)
//
// ============================================================================

void CRenderTarget::CreateSpotVolumeGeometry()
{
	Msg("[Vulkan] CreateSpotVolumeGeometry(): Generating cone mesh");

	// ========================================================================
	// Parameters
	// ========================================================================
	const u32 NUM_SEGMENTS = 32;
	const float BASE_RADIUS = 1.0f;
	const float BASE_Z = -1.0f;

	const u32 NUM_VERTICES = 1 + NUM_SEGMENTS + 1;  // apex + base circle + base center
	const u32 NUM_INDICES = (NUM_SEGMENTS * 3) + (NUM_SEGMENTS * 3);  // side + base

	// ========================================================================
	// Generate vertices
	// ========================================================================
	xr_vector<Fvector4> vertices;
	vertices.resize(NUM_VERTICES);

	u32 vIndex = 0;

	// Apex vertex at origin
	vertices[vIndex++].set(0.0f, 0.0f, 0.0f, 1.0f);

	// Base circle vertices
	for (u32 i = 0; i < NUM_SEGMENTS; i++)
	{
		float angle = (float)i / (float)NUM_SEGMENTS * PI_MUL_2;
		float x = cosf(angle) * BASE_RADIUS;
		float y = sinf(angle) * BASE_RADIUS;
		vertices[vIndex++].set(x, y, BASE_Z, 1.0f);
	}

	// Base center vertex
	vertices[vIndex++].set(0.0f, 0.0f, BASE_Z, 1.0f);

	VERIFY(vIndex == NUM_VERTICES);

	// ========================================================================
	// Generate indices
	// ========================================================================
	xr_vector<u16> indices;
	indices.resize(NUM_INDICES);

	u32 iIndex = 0;

	// Side triangles (apex → base edge)
	u32 apexIndex = 0;
	u32 baseStart = 1;

	for (u32 i = 0; i < NUM_SEGMENTS; i++)
	{
		u32 i0 = baseStart + i;
		u32 i1 = baseStart + ((i + 1) % NUM_SEGMENTS);

		// Triangle: apex, i0, i1 (CCW winding)
		indices[iIndex++] = (u16)apexIndex;
		indices[iIndex++] = (u16)i0;
		indices[iIndex++] = (u16)i1;
	}

	// Base triangles (base center → base edge)
	u32 baseCenterIndex = baseStart + NUM_SEGMENTS;

	for (u32 i = 0; i < NUM_SEGMENTS; i++)
	{
		u32 i0 = baseStart + i;
		u32 i1 = baseStart + ((i + 1) % NUM_SEGMENTS);

		// Triangle: base center, i1, i0 (CCW winding - reversed for base facing inward)
		indices[iIndex++] = (u16)baseCenterIndex;
		indices[iIndex++] = (u16)i1;
		indices[iIndex++] = (u16)i0;
	}

	VERIFY(iIndex == NUM_INDICES);

	Msg("[Vulkan] Cone mesh: %d vertices, %d indices (%d triangles)",
	    NUM_VERTICES, NUM_INDICES, NUM_INDICES / 3);

	// ========================================================================
	// Create vertex buffer
	// ========================================================================
	VkDeviceSize vbSize = NUM_VERTICES * sizeof(Fvector4);

	VkBufferCreateInfo vbInfo = {};
	vbInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	vbInfo.size = vbSize;
	vbInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	VmaAllocationCreateInfo vbAllocInfo = {};
	vbAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	VkResult result = vmaCreateBuffer(VulkanHW.GetAllocator(), &vbInfo, &vbAllocInfo,
	                                   &m_SpotVolumeVB, &m_SpotVolumeVBAlloc, nullptr);
	if (result != VK_SUCCESS) {
		Msg("![Vulkan] Failed to create spot cone vertex buffer: %d", result);
		return;
	}

	// Upload vertex data via staging buffer
	VkBuffer stagingVB;
	VmaAllocation stagingVBAlloc;

	VkBufferCreateInfo stagingInfo = vbInfo;
	stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	VmaAllocationCreateInfo stagingAllocInfo = {};
	stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
	stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

	VmaAllocationInfo allocInfo;
	result = vmaCreateBuffer(VulkanHW.GetAllocator(), &stagingInfo, &stagingAllocInfo,
	                         &stagingVB, &stagingVBAlloc, &allocInfo);
	if (result != VK_SUCCESS) {
		Msg("![Vulkan] Failed to create staging buffer for cone VB: %d", result);
		vmaDestroyBuffer(VulkanHW.GetAllocator(), m_SpotVolumeVB, m_SpotVolumeVBAlloc);
		m_SpotVolumeVB = VK_NULL_HANDLE;
		return;
	}

	// Copy vertex data to staging buffer
	Memory.mem_copy(allocInfo.pMappedData, vertices.data(), (size_t)vbSize);

	// ========================================================================
	// Create index buffer
	// ========================================================================
	VkDeviceSize ibSize = NUM_INDICES * sizeof(u16);

	VkBufferCreateInfo ibInfo = {};
	ibInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	ibInfo.size = ibSize;
	ibInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	VmaAllocationCreateInfo ibAllocInfo = {};
	ibAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	result = vmaCreateBuffer(VulkanHW.GetAllocator(), &ibInfo, &ibAllocInfo,
	                         &m_SpotVolumeIB, &m_SpotVolumeIBAlloc, nullptr);
	if (result != VK_SUCCESS) {
		Msg("![Vulkan] Failed to create spot cone index buffer: %d", result);
		vmaDestroyBuffer(VulkanHW.GetAllocator(), m_SpotVolumeVB, m_SpotVolumeVBAlloc);
		vmaDestroyBuffer(VulkanHW.GetAllocator(), stagingVB, stagingVBAlloc);
		m_SpotVolumeVB = VK_NULL_HANDLE;
		return;
	}

	// Upload index data via staging buffer
	VkBuffer stagingIB;
	VmaAllocation stagingIBAlloc;

	stagingInfo.size = ibSize;
	result = vmaCreateBuffer(VulkanHW.GetAllocator(), &stagingInfo, &stagingAllocInfo,
	                         &stagingIB, &stagingIBAlloc, &allocInfo);
	if (result != VK_SUCCESS) {
		Msg("![Vulkan] Failed to create staging buffer for cone IB: %d", result);
		vmaDestroyBuffer(VulkanHW.GetAllocator(), m_SpotVolumeVB, m_SpotVolumeVBAlloc);
		vmaDestroyBuffer(VulkanHW.GetAllocator(), m_SpotVolumeIB, m_SpotVolumeIBAlloc);
		vmaDestroyBuffer(VulkanHW.GetAllocator(), stagingVB, stagingVBAlloc);
		m_SpotVolumeVB = VK_NULL_HANDLE;
		m_SpotVolumeIB = VK_NULL_HANDLE;
		return;
	}

	// Copy index data to staging buffer
	Memory.mem_copy(allocInfo.pMappedData, indices.data(), (size_t)ibSize);

	// Record GPU copies using a single-time command buffer
	VkCommandBuffer cmd = VulkanHW.BeginSingleTimeCommands();

	VkBufferCopy copyRegion = {};
	copyRegion.size = vbSize;
	vkCmdCopyBuffer(cmd, stagingVB, m_SpotVolumeVB, 1, &copyRegion);

	copyRegion.size = ibSize;
	vkCmdCopyBuffer(cmd, stagingIB, m_SpotVolumeIB, 1, &copyRegion);

	VulkanHW.EndSingleTimeCommands(cmd);

	// Cleanup staging buffers (safe now - GPU transfer is complete)
	vmaDestroyBuffer(VulkanHW.GetAllocator(), stagingVB, stagingVBAlloc);
	vmaDestroyBuffer(VulkanHW.GetAllocator(), stagingIB, stagingIBAlloc);

	Msg("[Vulkan] Spot cone vertex buffer created: %d bytes", (u32)vbSize);

	m_SpotVolumeIndexCount = NUM_INDICES;

	Msg("[Vulkan] Spot cone index buffer created: %d bytes, %d indices", (u32)ibSize, NUM_INDICES);
	Msg("[Vulkan] CreateSpotVolumeGeometry() complete");
}

// ============================================================================
// DestroySpotVolumeGeometry() - Cleanup cone mesh
// ============================================================================

void CRenderTarget::DestroySpotVolumeGeometry()
{
	if (m_SpotVolumeVB != VK_NULL_HANDLE) {
		vmaDestroyBuffer(VulkanHW.GetAllocator(), m_SpotVolumeVB, m_SpotVolumeVBAlloc);
		m_SpotVolumeVB = VK_NULL_HANDLE;
		m_SpotVolumeVBAlloc = VK_NULL_HANDLE;
		Msg("[Vulkan] Spot cone vertex buffer destroyed");
	}

	if (m_SpotVolumeIB != VK_NULL_HANDLE) {
		vmaDestroyBuffer(VulkanHW.GetAllocator(), m_SpotVolumeIB, m_SpotVolumeIBAlloc);
		m_SpotVolumeIB = VK_NULL_HANDLE;
		m_SpotVolumeIBAlloc = VK_NULL_HANDLE;
		m_SpotVolumeIndexCount = 0;
		Msg("[Vulkan] Spot cone index buffer destroyed");
	}
}

} // namespace VK
