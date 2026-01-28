// ============================================================================
// vk_buffer_utils.h - Vulkan Buffer Creation Utilities
// ============================================================================
//
// Phase 2.23.1: Buffer Management Infrastructure
//
// Helper utilities for creating Vulkan buffers (vertex, index, uniform, etc.)
// using VMA (Vulkan Memory Allocator) for efficient memory management.
//
// ============================================================================

#pragma once

#include "vk_core.h"

namespace VK {

// ============================================================================
// BufferInfo - Stores buffer handle + allocation info
// ============================================================================
struct BufferInfo {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkDeviceSize size = 0;

    bool IsValid() const { return buffer != VK_NULL_HANDLE; }
};

// ============================================================================
// BufferUtils - Static helper class for buffer creation
// ============================================================================
class BufferUtils {
public:
    // ========================================================================
    // Vertex Buffer Creation
    // ========================================================================
    // Creates a vertex buffer and uploads data to GPU.
    // Uses staging buffer for efficient CPU→GPU transfer.
    //
    // Parameters:
    //   data - Pointer to vertex data (e.g., array of VertexStatic)
    //   size - Size in bytes (e.g., vertexCount * sizeof(VertexStatic))
    //
    // Returns:
    //   BufferInfo with allocated buffer handle
    //
    static BufferInfo CreateVertexBuffer(const void* data, VkDeviceSize size);

    // ========================================================================
    // Index Buffer Creation
    // ========================================================================
    // Creates an index buffer and uploads data to GPU.
    // Uses staging buffer for efficient CPU→GPU transfer.
    //
    // Parameters:
    //   data - Pointer to index data (e.g., array of u16/u32)
    //   size - Size in bytes (e.g., indexCount * sizeof(u16))
    //
    // Returns:
    //   BufferInfo with allocated buffer handle
    //
    static BufferInfo CreateIndexBuffer(const void* data, VkDeviceSize size);

    // ========================================================================
    // Uniform Buffer Creation
    // ========================================================================
    // Creates a uniform buffer for shader constants.
    // Uses HOST_VISIBLE memory for fast CPU updates.
    //
    // Parameters:
    //   size - Size in bytes (e.g., sizeof(UniformData))
    //
    // Returns:
    //   BufferInfo with allocated buffer handle
    //
    static BufferInfo CreateUniformBuffer(VkDeviceSize size);

    // ========================================================================
    // Buffer Destruction
    // ========================================================================
    // Destroys buffer and frees memory allocation.
    //
    // Parameters:
    //   buffer - BufferInfo to destroy (will be reset to null)
    //
    static void DestroyBuffer(BufferInfo& buffer);

    // ========================================================================
    // Update Buffer Data
    // ========================================================================
    // Updates buffer contents (for HOST_VISIBLE buffers only).
    //
    // Parameters:
    //   buffer - Buffer to update
    //   data - New data to upload
    //   size - Size in bytes
    //   offset - Offset in buffer (default: 0)
    //
    static void UpdateBuffer(const BufferInfo& buffer, const void* data,
                           VkDeviceSize size, VkDeviceSize offset = 0);

private:
    // ========================================================================
    // Generic Buffer Creation
    // ========================================================================
    // Internal helper for creating buffers with custom usage flags.
    //
    // Parameters:
    //   data - Initial data (can be nullptr)
    //   size - Buffer size in bytes
    //   usage - VkBufferUsageFlags (e.g., VERTEX_BUFFER_BIT)
    //   memoryUsage - VMA memory type (e.g., GPU_ONLY, CPU_TO_GPU)
    //
    // Returns:
    //   BufferInfo with allocated buffer
    //
    static BufferInfo CreateBuffer(
        const void* data,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VmaMemoryUsage memoryUsage
    );

    // ========================================================================
    // Copy Data to Buffer
    // ========================================================================
    // Uploads data to GPU buffer via staging buffer.
    //
    // Process:
    // 1. Create staging buffer (CPU_ONLY, HOST_VISIBLE)
    // 2. Copy data to staging buffer
    // 3. Record copy command (vkCmdCopyBuffer)
    // 4. Submit to transfer queue
    // 5. Wait for completion
    // 6. Destroy staging buffer
    //
    // Parameters:
    //   dstBuffer - Destination GPU buffer
    //   data - Source data
    //   size - Size in bytes
    //
    static void CopyDataToBuffer(VkBuffer dstBuffer, const void* data, VkDeviceSize size);

    // ========================================================================
    // Execute Single-Time Command
    // ========================================================================
    // Helper for executing one-off commands (e.g., buffer copy).
    //
    // Parameters:
    //   commandFunc - Lambda with command recording logic
    //
    // Example:
    //   ExecuteSingleTimeCommand([&](VkCommandBuffer cmd) {
    //       vkCmdCopyBuffer(cmd, src, dst, 1, &region);
    //   });
    //
    template<typename F>
    static void ExecuteSingleTimeCommand(F&& commandFunc);
};

} // namespace VK
