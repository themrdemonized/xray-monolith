// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#include "stdafx.h"
#include "HW_Vulkan.h"
#include "vk_geometry.h"
#include "vk_lighting.h"
#include <vector>
#include <set>

// Use VK namespace globals
using VK::g_VulkanGeometry;
using VK::g_VulkanLighting;

// VULKAN_DIAG: Static init diagnostics
static void VulkanDiagWriteHW(const char* msg) {
	HANDLE h = CreateFileA("D:\\anomaly\\appdata\\logs\\vulkan_diag.txt",
		FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, NULL);
	if (h != INVALID_HANDLE_VALUE) {
		DWORD written;
		WriteFile(h, msg, (DWORD)strlen(msg), &written, NULL);
		WriteFile(h, "\r\n", 2, &written, NULL);
		FlushFileBuffers(h);
		CloseHandle(h);
	}
}

// Глобальный экземпляр Vulkan HW (переименован чтобы избежать конфликта с R4)
static struct DiagHW1 { DiagHW1() { VulkanDiagWriteHW("[DIAG] HW_Vulkan.cpp: before VulkanHW"); } } g_diagHW1;
CVulkanHW VulkanHW;
static struct DiagHW2 { DiagHW2() { VulkanDiagWriteHW("[DIAG] HW_Vulkan.cpp: after VulkanHW"); } } g_diagHW2;

CVulkanHW::CVulkanHW()
{
    // DON'T use Msg() in global constructors - logging system isn't initialized yet!
    // Msg("[Vulkan] CVulkanHW constructor");
}

CVulkanHW::~CVulkanHW()
{
    // Msg("[Vulkan] CVulkanHW destructor");
}

// Поиск queue families (graphics + present)
bool CVulkanHW::FindQueueFamilies()
{
    u32 queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &queueFamilyCount, queueFamilies.data());

    // Ищем graphics queue и present queue
    m_GraphicsFamily = UINT32_MAX;
    m_PresentFamily = UINT32_MAX;

    for (u32 i = 0; i < queueFamilyCount; i++) {
        // Graphics queue
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            m_GraphicsFamily = i;
        }

        // Present queue
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(m_PhysicalDevice, i, m_Surface, &presentSupport);
        if (presentSupport) {
            m_PresentFamily = i;
        }

        // Если нашли обе - выходим
        if (m_GraphicsFamily != UINT32_MAX && m_PresentFamily != UINT32_MAX) {
            break;
        }
    }

    if (m_GraphicsFamily == UINT32_MAX) {
        Msg("!Failed to find graphics queue family");
        return false;
    }

    if (m_PresentFamily == UINT32_MAX) {
        Msg("!Failed to find present queue family");
        return false;
    }

    Msg("[Vulkan] Graphics queue family: %u", m_GraphicsFamily);
    if (m_GraphicsFamily == m_PresentFamily) {
        Msg("[Vulkan] Present queue family: %u (same as graphics)", m_PresentFamily);
    } else {
        Msg("[Vulkan] Present queue family: %u", m_PresentFamily);
    }

    return true;
}

// Создание logical device
bool CVulkanHW::CreateLogicalDevice()
{
    // Queue create infos
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<u32> uniqueQueueFamilies = { m_GraphicsFamily, m_PresentFamily };

    float queuePriority = 1.0f;
    for (u32 queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo = {};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    // Vulkan 1.3 features
    VkPhysicalDeviceVulkan13Features features13 = {};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;
    features13.maintenance4 = VK_TRUE;

    VkPhysicalDeviceVulkan12Features features12 = {};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.bufferDeviceAddress = VK_TRUE;
    features12.pNext = &features13;

    // Device features
    VkPhysicalDeviceFeatures2 deviceFeatures = {};
    deviceFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    deviceFeatures.features.samplerAnisotropy = VK_TRUE;
    deviceFeatures.features.fillModeNonSolid = VK_TRUE;
    deviceFeatures.features.wideLines = VK_TRUE;
    deviceFeatures.pNext = &features12;

    // Device create info
    VkDeviceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &deviceFeatures;
    createInfo.queueCreateInfoCount = static_cast<u32>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.enabledExtensionCount = g_DeviceExtensionCount;
    createInfo.ppEnabledExtensionNames = g_DeviceExtensions;

    #ifdef DEBUG
    createInfo.enabledLayerCount = g_ValidationLayerCount;
    createInfo.ppEnabledLayerNames = g_ValidationLayers;
    #else
    createInfo.enabledLayerCount = 0;
    #endif

    // Создаём device
    VkResult result = vkCreateDevice(m_PhysicalDevice, &createInfo, nullptr, &m_Device);
    if (result != VK_SUCCESS) {
        Msg("!Failed to create logical device. Error: %d", result);
        return false;
    }

    Msg("[Vulkan] Logical device created");

    // Получаем queue handles
    vkGetDeviceQueue(m_Device, m_GraphicsFamily, 0, &m_GraphicsQueue);
    vkGetDeviceQueue(m_Device, m_PresentFamily, 0, &m_PresentQueue);

    Msg("[Vulkan] Queue handles obtained");

    return true;
}

// Создание VMA
bool CVulkanHW::CreateVMA()
{
    // Явно указываем Vulkan функции для VMA
    VmaVulkanFunctions vulkanFunctions = {};
    vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    vulkanFunctions.vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties;
    vulkanFunctions.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
    vulkanFunctions.vkAllocateMemory = vkAllocateMemory;
    vulkanFunctions.vkFreeMemory = vkFreeMemory;
    vulkanFunctions.vkMapMemory = vkMapMemory;
    vulkanFunctions.vkUnmapMemory = vkUnmapMemory;
    vulkanFunctions.vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges;
    vulkanFunctions.vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges;
    vulkanFunctions.vkBindBufferMemory = vkBindBufferMemory;
    vulkanFunctions.vkBindImageMemory = vkBindImageMemory;
    vulkanFunctions.vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements;
    vulkanFunctions.vkGetImageMemoryRequirements = vkGetImageMemoryRequirements;
    vulkanFunctions.vkCreateBuffer = vkCreateBuffer;
    vulkanFunctions.vkDestroyBuffer = vkDestroyBuffer;
    vulkanFunctions.vkCreateImage = vkCreateImage;
    vulkanFunctions.vkDestroyImage = vkDestroyImage;
    vulkanFunctions.vkCmdCopyBuffer = vkCmdCopyBuffer;

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.flags = 0;
    allocatorInfo.physicalDevice = m_PhysicalDevice;
    allocatorInfo.device = m_Device;
    allocatorInfo.instance = m_Instance;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorInfo.pVulkanFunctions = &vulkanFunctions;

    VkResult result = vmaCreateAllocator(&allocatorInfo, &m_Allocator);

    if (result != VK_SUCCESS) {
        return false;
    }

    return true;
}

// Helper struct for finding unique video modes
struct _uniq_mode_vulkan
{
    LPCSTR _val;
    _uniq_mode_vulkan(LPCSTR v) : _val(v) {}
    bool operator()(LPCSTR _other) { return !_stricmp(_val, _other); }
};

// Free vid_mode_token list (called on shutdown)
static void free_vid_mode_list_vulkan()
{
    if (vid_mode_token == NULL) return;

    for (int i = 0; vid_mode_token[i].name; i++)
    {
        xr_free(vid_mode_token[i].name);
    }
    xr_free(vid_mode_token);
    vid_mode_token = NULL;
    Msg("[Vulkan] Video mode list freed");
}

// Fill vid_mode_token with available display modes
static void fill_vid_mode_list_vulkan()
{
    if (vid_mode_token != NULL) return;

    xr_vector<LPCSTR> _tmp;
    DEVMODEW dm;
    ZeroMemory(&dm, sizeof(dm));
    dm.dmSize = sizeof(dm);

    // Enumerate all display modes
    for (DWORD iModeNum = 0; EnumDisplaySettingsW(NULL, iModeNum, &dm); iModeNum++)
    {
        // Filter out small resolutions and non-32bit modes
        if (dm.dmPelsWidth < 800)
            continue;
        if (dm.dmBitsPerPel < 32)
            continue;

        string32 str;
        xr_sprintf(str, sizeof(str), "%dx%d", dm.dmPelsWidth, dm.dmPelsHeight);

        // Check if this mode is already in the list
        if (_tmp.end() != std::find_if(_tmp.begin(), _tmp.end(), _uniq_mode_vulkan(str)))
            continue;

        _tmp.push_back(NULL);
        _tmp.back() = xr_strdup(str);
    }

    // Sort by resolution (width first, then height)
    std::sort(_tmp.begin(), _tmp.end(), [](LPCSTR a, LPCSTR b) {
        int wa, ha, wb, hb;
        sscanf(a, "%dx%d", &wa, &ha);
        sscanf(b, "%dx%d", &wb, &hb);
        if (wa != wb) return wa < wb;
        return ha < hb;
    });

    u32 _cnt = _tmp.size() + 1;
    vid_mode_token = xr_alloc<xr_token>(_cnt);
    vid_mode_token[_cnt - 1].id = -1;
    vid_mode_token[_cnt - 1].name = NULL;

    Msg("[Vulkan] Available video modes[%d]:", _tmp.size());
    for (u32 i = 0; i < _tmp.size(); ++i)
    {
        vid_mode_token[i].id = i;
        vid_mode_token[i].name = _tmp[i];
        Msg("  [%d] %s", i, _tmp[i]);
    }
}

// Главный метод создания device (аналог DX11)
void CVulkanHW::CreateDevice(HWND hWnd)
{
    m_hWnd = hWnd;

    // 1. Создаём instance
    if (!VK_CreateInstance(&m_Instance)) {
        FATAL("Failed to create Vulkan instance");
    }

#ifdef DEBUG
    // 2. Debug messenger
    VK_SetupDebugMessenger(m_Instance, &m_DebugMessenger);
#endif

    // 3. Создаём surface
    VkWin32SurfaceCreateInfoKHR surfaceInfo = {};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surfaceInfo.hinstance = GetModuleHandle(nullptr);
    surfaceInfo.hwnd = hWnd;
    VK_CHECK(vkCreateWin32SurfaceKHR(m_Instance, &surfaceInfo, nullptr, &m_Surface));

    // 4. Выбираем physical device
    m_PhysicalDevice = VK_SelectPhysicalDevice(m_Instance, &Caps);
    if (m_PhysicalDevice == VK_NULL_HANDLE) {
        FATAL("Failed to select physical device");
    }

    // Проверяем минимальные требования
    if (!Caps.CheckMinimumRequirements()) {
        FATAL("GPU doesn't meet minimum requirements for Vulkan 1.3");
    }

    // 5. Находим queue families
    if (!FindQueueFamilies()) {
        FATAL("Failed to find queue families");
    }

    // 6. Создаём logical device
    if (!CreateLogicalDevice()) {
        FATAL("Failed to create logical device");
    }

    // 7. Создаём VMA
    if (!CreateVMA()) {
        FATAL("Failed to create VMA allocator");
    }

    // 8. Создаём command pool для transfer operations
    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = m_GraphicsFamily;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;  // Short-lived commands
    VK_CHECK(vkCreateCommandPool(m_Device, &poolInfo, nullptr, &m_TransferCommandPool));

    Msg("[Vulkan] Transfer command pool created");

    // 9. Создаём геометрию для deferred rendering
    g_VulkanGeometry = xr_new<VK::CVulkanGeometry>();
    g_VulkanGeometry->Create();

    // 10. Создаём lighting manager
    g_VulkanLighting = xr_new<VK::CVulkanLighting>();
    g_VulkanLighting->Create();

    // 11. Fill video mode list for options menu
    fill_vid_mode_list_vulkan();
}

// Уничтожение device
void CVulkanHW::DestroyDevice()
{
    if (m_Device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_Device);
    }

    // Free video mode list
    free_vid_mode_list_vulkan();

    // Уничтожаем lighting manager
    if (g_VulkanLighting) {
        xr_delete(g_VulkanLighting);
        g_VulkanLighting = nullptr;
    }

    // Уничтожаем геометрию
    if (g_VulkanGeometry) {
        xr_delete(g_VulkanGeometry);
        g_VulkanGeometry = nullptr;
    }

    // Уничтожаем command pool
    if (m_TransferCommandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_Device, m_TransferCommandPool, nullptr);
        m_TransferCommandPool = VK_NULL_HANDLE;
        Msg("[Vulkan] Transfer command pool destroyed");
    }

    // Уничтожаем VMA
    if (m_Allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(m_Allocator);
        m_Allocator = VK_NULL_HANDLE;
        Msg("[Vulkan] VMA allocator destroyed");
    }

    // Уничтожаем device
    if (m_Device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_Device, nullptr);
        m_Device = VK_NULL_HANDLE;
        Msg("[Vulkan] Logical device destroyed");
    }

    // Уничтожаем surface
    if (m_Surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
        m_Surface = VK_NULL_HANDLE;
        Msg("[Vulkan] Surface destroyed");
    }

#ifdef DEBUG
    // Уничтожаем debug messenger
    if (m_DebugMessenger != VK_NULL_HANDLE) {
        VK_DestroyDebugMessenger(m_Instance, m_DebugMessenger);
        m_DebugMessenger = VK_NULL_HANDLE;
    }
#endif

    // Уничтожаем instance
    if (m_Instance != VK_NULL_HANDLE) {
        VK_DestroyInstance(m_Instance);
        m_Instance = VK_NULL_HANDLE;
    }

    Msg("[Vulkan] Device destruction complete");
}

// Reset device (при resize окна и т.д.)
void CVulkanHW::Reset(HWND hWnd)
{
    Msg("[Vulkan] Reset device");

    // Ждём завершения всех операций
    if (m_Device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_Device);
    }

    // TODO: Пересоздать swapchain и render targets
}

// App activation
void CVulkanHW::OnAppActivate()
{
    Msg("[Vulkan] App activated");
}

// App deactivation
void CVulkanHW::OnAppDeactivate()
{
    Msg("[Vulkan] App deactivated");
}

// ============================================================================
// Single-time command helpers (for buffer uploads, layout transitions, etc.)
// ============================================================================

VkCommandBuffer CVulkanHW::BeginSingleTimeCommands()
{
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_TransferCommandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    VK_CHECK(vkAllocateCommandBuffers(m_Device, &allocInfo, &commandBuffer));

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

    return commandBuffer;
}

void CVulkanHW::EndSingleTimeCommands(VkCommandBuffer commandBuffer)
{
    VK_CHECK(vkEndCommandBuffer(commandBuffer));

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    VK_CHECK(vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(m_GraphicsQueue));

    vkFreeCommandBuffers(m_Device, m_TransferCommandPool, 1, &commandBuffer);
}
