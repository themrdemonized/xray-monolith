// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

#pragma once
#include "vk_core.h"

// Command buffer management (triple buffering)
class CVulkanCommandManager
{
public:
    static constexpr u32 FRAMES_IN_FLIGHT = 3;

private:
    VkCommandPool   m_CommandPools[FRAMES_IN_FLIGHT];
    VkCommandBuffer m_CommandBuffers[FRAMES_IN_FLIGHT];
    u32             m_CurrentFrame = 0;

    // Dedicated pool+buffer for one-shot immediate operations (uploads, layout transitions)
    // This is separate from the per-frame render command buffers to avoid conflicts.
    VkCommandPool   m_ImmediatePool = VK_NULL_HANDLE;
    VkCommandBuffer m_ImmediateCmd  = VK_NULL_HANDLE;
    VkFence         m_ImmediateFence = VK_NULL_HANDLE;

public:
    void Create();
    void Destroy();

    // Per-frame render command buffer (used by render loop only)
    VkCommandBuffer Begin();
    bool End(VkCommandBuffer cmd);
    bool Submit(VkCommandBuffer cmd, VkSemaphore waitSemaphore, VkSemaphore signalSemaphore, VkFence fence);

    // One-shot immediate command buffer (safe to call during rendering)
    VkCommandBuffer BeginImmediate();
    void            EndAndSubmitImmediate(VkCommandBuffer cmd);

    void NextFrame() { m_CurrentFrame = (m_CurrentFrame + 1) % FRAMES_IN_FLIGHT; }
    u32 GetCurrentFrame() const { return m_CurrentFrame; }

    VkCommandBuffer GetCurrentCommandBuffer() const { return m_CommandBuffers[m_CurrentFrame]; }
    VkCommandPool   GetCurrentPool() const { return m_CommandPools[m_CurrentFrame]; }
    VkCommandPool   GetPool(u32 index) const { return m_CommandPools[index]; }
    void            ResetFrameCounter() { m_CurrentFrame = 0; }
};

// Глобальный экземпляр
extern CVulkanCommandManager CommandManager;
