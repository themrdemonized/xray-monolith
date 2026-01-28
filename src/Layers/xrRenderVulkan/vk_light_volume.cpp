#include "stdafx.h"
#include "vk_rendertarget.h"
#include "HW_Vulkan.h"
#include "vk_buffer.h"

namespace VK
{

// ============================================================================
// CreatePointVolumeGeometry() - Create sphere geometry for point light volumes
// ============================================================================
//
// Phase 2.16.4: Light Volume Geometry
//
// Создаёт ICO sphere (icosahedron-based sphere) для рендеринга light volumes.
// Sphere используется для stencil masking и light accumulation.
//
// Sphere properties:
// - Center at origin (0, 0, 0)
// - Radius = 1.0 (scaled by light range в shader)
// - ~92 vertices, ~180 triangles (sufficient tesselation)
//
// ============================================================================

void CRenderTarget::CreatePointVolumeGeometry()
{
	Msg("[Vulkan] Creating point light volume geometry (ICO sphere)...");

	// ========================================================================
	// ICO Sphere Generation
	// ========================================================================
	// Icosahedron base vertices (12 vertices)
	const float t = (1.0f + sqrtf(5.0f)) / 2.0f;  // Golden ratio

	Fvector icoVertices[12] = {
		{-1,  t,  0}, { 1,  t,  0}, {-1, -t,  0}, { 1, -t,  0},
		{ 0, -1,  t}, { 0,  1,  t}, { 0, -1, -t}, { 0,  1, -t},
		{ t,  0, -1}, { t,  0,  1}, {-t,  0, -1}, {-t,  0,  1}
	};

	// Normalize vertices to unit sphere
	for (int i = 0; i < 12; i++) {
		float len = sqrtf(icoVertices[i].x * icoVertices[i].x +
		                  icoVertices[i].y * icoVertices[i].y +
		                  icoVertices[i].z * icoVertices[i].z);
		icoVertices[i].x /= len;
		icoVertices[i].y /= len;
		icoVertices[i].z /= len;
	}

	// Icosahedron faces (20 triangles)
	u32 icoIndices[60] = {
		// 5 faces around point 0
		0, 11, 5,   0, 5, 1,   0, 1, 7,   0, 7, 10,   0, 10, 11,
		// 5 adjacent faces
		1, 5, 9,   5, 11, 4,   11, 10, 2,   10, 7, 6,   7, 1, 8,
		// 5 faces around point 3
		3, 9, 4,   3, 4, 2,   3, 2, 6,   3, 6, 8,   3, 8, 9,
		// 5 adjacent faces
		4, 9, 5,   2, 4, 11,   6, 2, 10,   8, 6, 7,   9, 8, 1
	};

	// ========================================================================
	// Subdivision (для smoother sphere)
	// ========================================================================
	// TODO: Implement subdivision для higher tesselation
	// For now: use icosahedron directly (20 triangles)
	// Production: subdivide once or twice (80 or 320 triangles)

	// ========================================================================
	// Create vertex buffer
	// ========================================================================
	u32 vertexCount = 12;
	u32 vertexSize = sizeof(Fvector) * vertexCount;

	VkBufferCreateInfo vbInfo = {};
	vbInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	vbInfo.size = vertexSize;
	vbInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	vbInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo allocInfo = {};
	allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
	allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

	VkBuffer vb = VK_NULL_HANDLE;
	VmaAllocation vbAlloc = VK_NULL_HANDLE;

	VK_CHECK(vmaCreateBuffer(VulkanHW.m_Allocator, &vbInfo, &allocInfo,
	                         &vb, &vbAlloc, nullptr));

	// Upload vertex data
	void* data = nullptr;
	vmaMapMemory(VulkanHW.m_Allocator, vbAlloc, &data);
	memcpy(data, icoVertices, vertexSize);
	vmaUnmapMemory(VulkanHW.m_Allocator, vbAlloc);

	// Store vertex buffer
	m_PointVolumeVB = vb;
	m_PointVolumeVBAlloc = vbAlloc;

	Msg("[Vulkan]   Point volume VB created: %d vertices, %d bytes", vertexCount, vertexSize);

	// ========================================================================
	// Create index buffer
	// ========================================================================
	u32 indexCount = 60;  // 20 triangles * 3 indices
	u32 indexSize = sizeof(u32) * indexCount;

	VkBufferCreateInfo ibInfo = {};
	ibInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	ibInfo.size = indexSize;
	ibInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	ibInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VkBuffer ib = VK_NULL_HANDLE;
	VmaAllocation ibAlloc = VK_NULL_HANDLE;

	VK_CHECK(vmaCreateBuffer(VulkanHW.m_Allocator, &ibInfo, &allocInfo,
	                         &ib, &ibAlloc, nullptr));

	// Upload index data
	vmaMapMemory(VulkanHW.m_Allocator, ibAlloc, &data);
	memcpy(data, icoIndices, indexSize);
	vmaUnmapMemory(VulkanHW.m_Allocator, ibAlloc);

	// Store index buffer
	m_PointVolumeIB = ib;
	m_PointVolumeIBAlloc = ibAlloc;
	m_PointVolumeIndexCount = indexCount;

	Msg("[Vulkan]   Point volume IB created: %d indices, %d bytes", indexCount, indexSize);

	// ========================================================================
	// Summary
	// ========================================================================
	Msg("[Vulkan] Point light volume geometry created:");
	Msg("[Vulkan]   - Vertices: %d", vertexCount);
	Msg("[Vulkan]   - Triangles: %d", indexCount / 3);
	Msg("[Vulkan]   - VRAM: %.2f KB", (vertexSize + indexSize) / 1024.0f);
}

// ============================================================================
// DestroyPointVolumeGeometry() - Cleanup
// ============================================================================

void CRenderTarget::DestroyPointVolumeGeometry()
{
	if (m_PointVolumeVB != VK_NULL_HANDLE) {
		vmaDestroyBuffer(VulkanHW.m_Allocator, m_PointVolumeVB, m_PointVolumeVBAlloc);
		m_PointVolumeVB = VK_NULL_HANDLE;
		m_PointVolumeVBAlloc = VK_NULL_HANDLE;
	}

	if (m_PointVolumeIB != VK_NULL_HANDLE) {
		vmaDestroyBuffer(VulkanHW.m_Allocator, m_PointVolumeIB, m_PointVolumeIBAlloc);
		m_PointVolumeIB = VK_NULL_HANDLE;
		m_PointVolumeIBAlloc = VK_NULL_HANDLE;
	}

	Msg("[Vulkan] Point light volume geometry destroyed");
}

} // namespace VK
