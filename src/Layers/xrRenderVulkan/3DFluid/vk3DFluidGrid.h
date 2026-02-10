// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

#pragma once
#include "../vk_core.h"

namespace VK
{

/**
 * 3D Fluid Grid Geometry
 *
 * Генерирует геометрию для рендеринга в 3D текстуры:
 * - Z-slice quads для записи в отдельные срезы 3D текстуры
 * - Fullscreen quad для raycasting
 * - Boundary quads для граничных условий
 *
 * Используется для GPU симуляции жидкостей (Navier-Stokes).
 */
class vk3DFluidGrid
{
public:
    vk3DFluidGrid();
    ~vk3DFluidGrid();

    /**
     * Создать геометрию для grid
     * @param gridSize Размер grid (64, 128, 256)
     */
    void Create(u32 gridSize);

    /**
     * Уничтожить геометрию
     */
    void Destroy();

    /**
     * Рендерить Z-slice quads
     * Каждый quad рендерится в отдельный слой 3D текстуры
     */
    void RenderSlices(VkCommandBuffer cmd);

    /**
     * Рендерить fullscreen quad для raycasting
     */
    void RenderFullscreen(VkCommandBuffer cmd);

    /**
     * Рендерить boundary quads (граничные условия)
     */
    void RenderBoundary(VkCommandBuffer cmd);

    /**
     * Рендерить unit cube bounding box (для ray entry/exit points)
     * Cube в [0,1]³ пространстве, transform применяется в vertex shader
     */
    void RenderBoundingBox(VkCommandBuffer cmd);

    // Accessors
    u32 GetGridSize() const { return m_GridSize; }
    u32 GetSliceCount() const { return m_GridSize; }

private:
    /**
     * Генерация vertex buffer для Z-slice quads
     */
    void GenerateSliceGeometry();

    /**
     * Генерация vertex buffer для fullscreen quad
     */
    void GenerateFullscreenQuad();

    /**
     * Генерация vertex buffer для boundary quads
     */
    void GenerateBoundaryQuads();

private:
    u32 m_GridSize = 0;

    // Vertex buffers
    VkBuffer m_SliceVB = VK_NULL_HANDLE;
    VkBuffer m_FullscreenVB = VK_NULL_HANDLE;
    VkBuffer m_BoundaryVB = VK_NULL_HANDLE;
    VkBuffer m_BBoxVB = VK_NULL_HANDLE;  // Bounding box

    VmaAllocation m_SliceAlloc = VK_NULL_HANDLE;
    VmaAllocation m_FullscreenAlloc = VK_NULL_HANDLE;
    VmaAllocation m_BoundaryAlloc = VK_NULL_HANDLE;
    VmaAllocation m_BBoxAlloc = VK_NULL_HANDLE;

    u32 m_SliceVertexCount = 0;
    u32 m_BoundaryVertexCount = 0;

    /**
     * Генерация bounding box geometry
     */
    void GenerateBoundingBox();
};

} // namespace VK
