// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

#include "stdafx.h"
#include "vk3DFluidGrid.h"
#include "../HW_Vulkan.h"

namespace VK
{

// Vertex format для fluid grid
struct FluidVertex
{
    float x, y, z;
    float u, v, w;  // 3D texture coordinates
};

vk3DFluidGrid::vk3DFluidGrid()
{
}

vk3DFluidGrid::~vk3DFluidGrid()
{
    Destroy();
}

void vk3DFluidGrid::Create(u32 gridSize)
{
    if (gridSize == 0 || (gridSize & (gridSize - 1)) != 0) {
        Msg("![Vulkan] Grid size must be power of 2: %d", gridSize);
        return;
    }

    m_GridSize = gridSize;

    GenerateSliceGeometry();
    GenerateFullscreenQuad();
    GenerateBoundaryQuads();
    GenerateBoundingBox();

    Msg("[Vulkan] 3D Fluid Grid created: %dx%dx%d", gridSize, gridSize, gridSize);
}

void vk3DFluidGrid::GenerateSliceGeometry()
{
    // Генерируем Z-slice quads
    // Каждый quad - это один Z-слой в 3D текстуре
    // Для 128³ grid - это 128 quads

    u32 sliceCount = m_GridSize;
    m_SliceVertexCount = sliceCount * 6;  // 6 vertices per quad (2 triangles)

    xr_vector<FluidVertex> vertices(m_SliceVertexCount);

    for (u32 z = 0; z < sliceCount; z++)
    {
        float zCoord = (float)z / (float)sliceCount;  // 0.0 to 1.0
        u32 baseIdx = z * 6;

        // Triangle 1: (0,0) -> (1,0) -> (1,1)
        vertices[baseIdx + 0] = { -1.0f, -1.0f, 0.0f,  0.0f, 0.0f, zCoord };
        vertices[baseIdx + 1] = {  1.0f, -1.0f, 0.0f,  1.0f, 0.0f, zCoord };
        vertices[baseIdx + 2] = {  1.0f,  1.0f, 0.0f,  1.0f, 1.0f, zCoord };

        // Triangle 2: (0,0) -> (1,1) -> (0,1)
        vertices[baseIdx + 3] = { -1.0f, -1.0f, 0.0f,  0.0f, 0.0f, zCoord };
        vertices[baseIdx + 4] = {  1.0f,  1.0f, 0.0f,  1.0f, 1.0f, zCoord };
        vertices[baseIdx + 5] = { -1.0f,  1.0f, 0.0f,  0.0f, 1.0f, zCoord };
    }

    // Создаём vertex buffer
    VkDeviceSize bufferSize = sizeof(FluidVertex) * vertices.size();

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    VK_CHECK(vmaCreateBuffer(VulkanHW.m_Allocator, &bufferInfo, &allocInfo,
                             &m_SliceVB, &m_SliceAlloc, nullptr));

    // Upload vertices
    void* data;
    VK_CHECK(vmaMapMemory(VulkanHW.m_Allocator, m_SliceAlloc, &data));
    memcpy(data, vertices.data(), bufferSize);
    vmaUnmapMemory(VulkanHW.m_Allocator, m_SliceAlloc);

    Msg("[Vulkan] Fluid slice geometry created: %d quads", sliceCount);
}

void vk3DFluidGrid::GenerateFullscreenQuad()
{
    // Fullscreen quad для raycasting
    FluidVertex vertices[6] = {
        // Triangle 1
        { -1.0f, -1.0f, 0.0f,  0.0f, 0.0f, 0.0f },
        {  1.0f, -1.0f, 0.0f,  1.0f, 0.0f, 0.0f },
        {  1.0f,  1.0f, 0.0f,  1.0f, 1.0f, 0.0f },
        // Triangle 2
        { -1.0f, -1.0f, 0.0f,  0.0f, 0.0f, 0.0f },
        {  1.0f,  1.0f, 0.0f,  1.0f, 1.0f, 0.0f },
        { -1.0f,  1.0f, 0.0f,  0.0f, 1.0f, 0.0f }
    };

    VkDeviceSize bufferSize = sizeof(vertices);

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    VK_CHECK(vmaCreateBuffer(VulkanHW.m_Allocator, &bufferInfo, &allocInfo,
                             &m_FullscreenVB, &m_FullscreenAlloc, nullptr));

    // Upload vertices
    void* data;
    VK_CHECK(vmaMapMemory(VulkanHW.m_Allocator, m_FullscreenAlloc, &data));
    memcpy(data, vertices, bufferSize);
    vmaUnmapMemory(VulkanHW.m_Allocator, m_FullscreenAlloc);
}

void vk3DFluidGrid::GenerateBoundaryQuads()
{
    // Boundary quads для граничных условий
    // 6 граней куба (для Neumann/Dirichlet boundary conditions)

    xr_vector<FluidVertex> vertices;
    vertices.reserve(36);  // 6 faces × 6 vertices

    // Для простоты пока создадим пустой буфер
    // Реальная геометрия boundary будет добавлена при необходимости
    m_BoundaryVertexCount = 0;
}

void vk3DFluidGrid::GenerateBoundingBox()
{
    // Unit cube [0,1]³ для ray entry/exit point рендеринга
    // 36 vertices (12 triangles, 6 faces)

    FluidVertex vertices[36] = {
        // Front face (+Z)
        { 0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f },
        { 1.0f, 0.0f, 1.0f,  1.0f, 0.0f, 1.0f },
        { 1.0f, 1.0f, 1.0f,  1.0f, 1.0f, 1.0f },
        { 0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f },
        { 1.0f, 1.0f, 1.0f,  1.0f, 1.0f, 1.0f },
        { 0.0f, 1.0f, 1.0f,  0.0f, 1.0f, 1.0f },

        // Back face (-Z)
        { 1.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f,  0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f,  0.0f, 1.0f, 0.0f },
        { 1.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f,  0.0f, 1.0f, 0.0f },
        { 1.0f, 1.0f, 0.0f,  1.0f, 1.0f, 0.0f },

        // Right face (+X)
        { 1.0f, 0.0f, 1.0f,  1.0f, 0.0f, 1.0f },
        { 1.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f },
        { 1.0f, 1.0f, 0.0f,  1.0f, 1.0f, 0.0f },
        { 1.0f, 0.0f, 1.0f,  1.0f, 0.0f, 1.0f },
        { 1.0f, 1.0f, 0.0f,  1.0f, 1.0f, 0.0f },
        { 1.0f, 1.0f, 1.0f,  1.0f, 1.0f, 1.0f },

        // Left face (-X)
        { 0.0f, 0.0f, 0.0f,  0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f },
        { 0.0f, 1.0f, 1.0f,  0.0f, 1.0f, 1.0f },
        { 0.0f, 0.0f, 0.0f,  0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 1.0f,  0.0f, 1.0f, 1.0f },
        { 0.0f, 1.0f, 0.0f,  0.0f, 1.0f, 0.0f },

        // Top face (+Y)
        { 0.0f, 1.0f, 1.0f,  0.0f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 1.0f,  1.0f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 0.0f,  1.0f, 1.0f, 0.0f },
        { 0.0f, 1.0f, 1.0f,  0.0f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 0.0f,  1.0f, 1.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f,  0.0f, 1.0f, 0.0f },

        // Bottom face (-Y)
        { 0.0f, 0.0f, 0.0f,  0.0f, 0.0f, 0.0f },
        { 1.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f },
        { 1.0f, 0.0f, 1.0f,  1.0f, 0.0f, 1.0f },
        { 0.0f, 0.0f, 0.0f,  0.0f, 0.0f, 0.0f },
        { 1.0f, 0.0f, 1.0f,  1.0f, 0.0f, 1.0f },
        { 0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f }
    };

    VkDeviceSize bufferSize = sizeof(vertices);

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    VK_CHECK(vmaCreateBuffer(VulkanHW.m_Allocator, &bufferInfo, &allocInfo,
                             &m_BBoxVB, &m_BBoxAlloc, nullptr));

    // Upload vertices
    void* data;
    VK_CHECK(vmaMapMemory(VulkanHW.m_Allocator, m_BBoxAlloc, &data));
    memcpy(data, vertices, bufferSize);
    vmaUnmapMemory(VulkanHW.m_Allocator, m_BBoxAlloc);

    Msg("[Vulkan] Fluid bounding box created (36 vertices)");
}

void vk3DFluidGrid::Destroy()
{
    if (m_SliceVB != VK_NULL_HANDLE) {
        vmaDestroyBuffer(VulkanHW.m_Allocator, m_SliceVB, m_SliceAlloc);
        m_SliceVB = VK_NULL_HANDLE;
        m_SliceAlloc = VK_NULL_HANDLE;
    }

    if (m_FullscreenVB != VK_NULL_HANDLE) {
        vmaDestroyBuffer(VulkanHW.m_Allocator, m_FullscreenVB, m_FullscreenAlloc);
        m_FullscreenVB = VK_NULL_HANDLE;
        m_FullscreenAlloc = VK_NULL_HANDLE;
    }

    if (m_BoundaryVB != VK_NULL_HANDLE) {
        vmaDestroyBuffer(VulkanHW.m_Allocator, m_BoundaryVB, m_BoundaryAlloc);
        m_BoundaryVB = VK_NULL_HANDLE;
        m_BoundaryAlloc = VK_NULL_HANDLE;
    }

    if (m_BBoxVB != VK_NULL_HANDLE) {
        vmaDestroyBuffer(VulkanHW.m_Allocator, m_BBoxVB, m_BBoxAlloc);
        m_BBoxVB = VK_NULL_HANDLE;
        m_BBoxAlloc = VK_NULL_HANDLE;
    }

    m_GridSize = 0;
    m_SliceVertexCount = 0;
    m_BoundaryVertexCount = 0;
}

void vk3DFluidGrid::RenderSlices(VkCommandBuffer cmd)
{
    if (m_SliceVB == VK_NULL_HANDLE) {
        return;
    }

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_SliceVB, &offset);
    vkCmdDraw(cmd, m_SliceVertexCount, 1, 0, 0);
}

void vk3DFluidGrid::RenderFullscreen(VkCommandBuffer cmd)
{
    if (m_FullscreenVB == VK_NULL_HANDLE) {
        return;
    }

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_FullscreenVB, &offset);
    vkCmdDraw(cmd, 6, 1, 0, 0);
}

void vk3DFluidGrid::RenderBoundary(VkCommandBuffer cmd)
{
    if (m_BoundaryVB == VK_NULL_HANDLE || m_BoundaryVertexCount == 0) {
        return;
    }

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_BoundaryVB, &offset);
    vkCmdDraw(cmd, m_BoundaryVertexCount, 1, 0, 0);
}

void vk3DFluidGrid::RenderBoundingBox(VkCommandBuffer cmd)
{
    if (m_BBoxVB == VK_NULL_HANDLE) {
        return;
    }

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_BBoxVB, &offset);
    vkCmdDraw(cmd, 36, 1, 0, 0);  // 36 vertices (unit cube)
}

} // namespace VK
