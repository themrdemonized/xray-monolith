// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

#pragma once
#include <source_location>
#include <expected>
#include <format>
#include <string>
#include <string_view>

// Force-flush log to disk (X-Ray buffers Msg output)
inline void vk_flush_log()
{
    FlushLog();
}

// Structured logging with automatic source location
inline void vk_log(std::string_view msg,
    std::source_location loc = std::source_location::current())
{
    Msg("[VK %s:%u] %.*s", loc.function_name(), loc.line(),
        (int)msg.size(), msg.data());
}

inline void vk_warn(std::string_view msg,
    std::source_location loc = std::source_location::current())
{
    Msg("![VK %s:%u] %.*s", loc.function_name(), loc.line(),
        (int)msg.size(), msg.data());
    FlushLog();
}

inline void vk_error(std::string_view msg,
    std::source_location loc = std::source_location::current())
{
    Msg("!![VK %s:%u] %.*s", loc.function_name(), loc.line(),
        (int)msg.size(), msg.data());
    FlushLog();
}

// VkResult error handling with std::expected
using VkExpected = std::expected<void, VkResult>;

template<typename T>
using VkExpectedVal = std::expected<T, VkResult>;

// Check VkResult and return unexpected on failure
#define VK_EXPECT(expr) \
    do { VkResult _r = (expr); \
         if (_r != VK_SUCCESS) { \
             vk_error(std::format("{} failed: {}", #expr, (int)_r)); \
             return std::unexpected(_r); \
         } \
    } while(0)

// Per-frame UI diagnostic stats (throttled logging)
struct UIFrameStats {
    u32 deferredCmds = 0;
    u32 deferredDraws = 0;
    u32 immediateCalls = 0;
    u32 totalVerts = 0;
    u32 droppedCmds = 0;   // due to buffer overflow
    u32 bufferWraps = 0;
    u32 frameNumber = 0;

    void reset(u32 frame) { *this = {}; frameNumber = frame; }

    void log_if_active() const {
        // Log first 5 frames, then every 300th frame
        if (frameNumber < 5 || (frameNumber % 300 == 0)) {
            Msg("[VK-UI frame %u] deferred=%u (draws=%u) immediate=%u verts=%u dropped=%u wraps=%u",
                frameNumber, deferredCmds, deferredDraws, immediateCalls,
                totalVerts, droppedCmds, bufferWraps);
            FlushLog();
        }
    }
};
