#pragma once
#include "vk_core.h"

// Synchronization для одного frame
struct FrameSync
{
    VkSemaphore imageAvailable;  // Signaled когда swapchain image acquired
    VkSemaphore renderFinished;  // Signaled когда rendering complete
    VkFence     inFlightFence;   // CPU-GPU sync
};

// Synchronization management (triple buffering)
class CVulkanSync
{
public:
    static constexpr u32 FRAMES_IN_FLIGHT = 3;

private:
    FrameSync m_FrameSync[FRAMES_IN_FLIGHT];

public:
    void Create();
    void Destroy();

    FrameSync& GetCurrentFrame(u32 frameIndex) { return m_FrameSync[frameIndex]; }

    void WaitForFence(u32 frameIndex);
    void ResetFence(u32 frameIndex);
};

// Глобальный экземпляр
extern CVulkanSync Sync;
