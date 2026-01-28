#include "stdafx.h"
#include "vk_core.h"
#include "HWCaps_Vulkan.h"
#include <vector>

#ifdef DEBUG
// Debug messenger callback
static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
    // Определяем уровень серьезности
    const char* severity = "INFO";
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        severity = "ERROR";
    } else if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        severity = "WARNING";
    }

    // Выводим сообщение
    Msg("[Vulkan %s] %s", severity, pCallbackData->pMessage);

    // Возвращаем false чтобы не прерывать выполнение
    return VK_FALSE;
}
#endif

bool VK_CreateInstance(VkInstance* outInstance)
{
    VERIFY(outInstance);

    // Application info
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "X-Ray Engine";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "X-Ray";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    // Instance create info
    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = g_InstanceExtensionCount;
    createInfo.ppEnabledExtensionNames = g_InstanceExtensions;

    #ifdef DEBUG
    createInfo.enabledLayerCount = g_ValidationLayerCount;
    createInfo.ppEnabledLayerNames = g_ValidationLayers;

    // Debug messenger для создания instance
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {};
    debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debugCreateInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugCreateInfo.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debugCreateInfo.pfnUserCallback = DebugCallback;

    createInfo.pNext = &debugCreateInfo;
    #else
    createInfo.enabledLayerCount = 0;
    createInfo.pNext = nullptr;
    #endif

    // Создаём instance
    VkResult result = vkCreateInstance(&createInfo, nullptr, outInstance);

    if (result != VK_SUCCESS) {
        Msg("!Failed to create Vulkan instance. Error code: %d", result);
        return false;
    }

    Msg("[Vulkan] Instance created successfully");
    return true;
}

void VK_DestroyInstance(VkInstance instance)
{
    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
        Msg("[Vulkan] Instance destroyed");
    }
}

#ifdef DEBUG
VkResult VK_SetupDebugMessenger(VkInstance instance, VkDebugUtilsMessengerEXT* outMessenger)
{
    VERIFY(outMessenger);

    VkDebugUtilsMessengerCreateInfoEXT createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = DebugCallback;

    // Загружаем функцию создания debug messenger
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance, "vkCreateDebugUtilsMessengerEXT");

    if (func != nullptr) {
        VkResult result = func(instance, &createInfo, nullptr, outMessenger);
        if (result == VK_SUCCESS) {
            Msg("[Vulkan] Debug messenger created");
        }
        return result;
    } else {
        Msg("!Failed to load vkCreateDebugUtilsMessengerEXT");
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

void VK_DestroyDebugMessenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger)
{
    if (messenger != VK_NULL_HANDLE) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            instance, "vkDestroyDebugUtilsMessengerEXT");

        if (func != nullptr) {
            func(instance, messenger, nullptr);
            Msg("[Vulkan] Debug messenger destroyed");
        }
    }
}
#endif

// Проверка поддержки расширений устройства
bool VK_CheckDeviceExtensionSupport(VkPhysicalDevice device)
{
    u32 extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    // Проверяем все требуемые расширения
    for (u32 i = 0; i < g_DeviceExtensionCount; i++) {
        bool found = false;
        for (const auto& extension : availableExtensions) {
            if (strcmp(g_DeviceExtensions[i], extension.extensionName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }

    return true;
}

// Выбор физического устройства
VkPhysicalDevice VK_SelectPhysicalDevice(VkInstance instance, VulkanCaps* outCaps)
{
    VERIFY(outCaps);

    u32 deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

    if (deviceCount == 0) {
        Msg("!Failed to find GPUs with Vulkan support");
        return VK_NULL_HANDLE;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    // Система оценки устройств
    struct DeviceScore {
        VkPhysicalDevice device;
        int score;
        VkPhysicalDeviceProperties props;
        VkPhysicalDeviceVulkan13Features features13;
    };
    std::vector<DeviceScore> scores;

    for (auto device : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);

        // Проверка версии API
        if (props.apiVersion < VK_API_VERSION_1_3) {
            continue;
        }

        // Проверка расширений
        if (!VK_CheckDeviceExtensionSupport(device)) {
            continue;
        }

        // Проверка Vulkan 1.3 features
        VkPhysicalDeviceVulkan13Features features13 = {};
        features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

        VkPhysicalDeviceFeatures2 features2 = {};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &features13;

        vkGetPhysicalDeviceFeatures2(device, &features2);

        // Проверяем обязательные features
        if (!features13.dynamicRendering || !features13.synchronization2) {
            continue;
        }

        // Подсчет очков
        int score = 0;

        // Discrete GPU +1000
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            score += 1000;
        }

        // Vendor preference
        switch (props.vendorID) {
        case 0x10DE:  // NVIDIA
            score += 100;
            Msg("[Vulkan] Found NVIDIA: %s", props.deviceName);
            break;
        case 0x1002:  // AMD
            score += 90;
            Msg("[Vulkan] Found AMD: %s", props.deviceName);
            break;
        case 0x8086:  // Intel
            score += 50;
            Msg("[Vulkan] Found Intel: %s", props.deviceName);
            break;
        default:
            Msg("[Vulkan] Found GPU: %s (Vendor: 0x%X)", props.deviceName, props.vendorID);
            break;
        }

        // VRAM bonus
        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(device, &memProps);
        VkDeviceSize totalVRAM = 0;
        for (u32 i = 0; i < memProps.memoryHeapCount; i++) {
            if (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
                totalVRAM += memProps.memoryHeaps[i].size;
            }
        }
        score += static_cast<int>(totalVRAM / (1024ULL * 1024 * 1024));  // +1 per GB

        scores.push_back({ device, score, props, features13 });
    }

    if (scores.empty()) {
        Msg("!No suitable Vulkan GPU found");
        return VK_NULL_HANDLE;
    }

    // Сортировка по score
    std::sort(scores.begin(), scores.end(),
              [](const auto& a, const auto& b) { return a.score > b.score; });

    // Выбираем лучшее устройство
    DeviceScore& best = scores[0];

    // Заполняем caps
    xr_strcpy(outCaps->deviceName, best.props.deviceName);
    outCaps->vendorID = best.props.vendorID;
    outCaps->deviceID = best.props.deviceID;
    outCaps->driverVersion = best.props.driverVersion;
    outCaps->apiVersion = best.props.apiVersion;

    outCaps->dynamicRendering = best.features13.dynamicRendering;
    outCaps->synchronization2 = best.features13.synchronization2;
    outCaps->maintenance4 = best.features13.maintenance4;

    outCaps->maxColorAttachments = best.props.limits.maxColorAttachments;
    outCaps->maxSamplerAnisotropy = best.props.limits.maxSamplerAnisotropy;
    outCaps->msaaSamples = best.props.limits.framebufferColorSampleCounts;

    // Подсчитываем VRAM
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(best.device, &memProps);
    for (u32 i = 0; i < memProps.memoryHeapCount; i++) {
        if (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            outCaps->totalDeviceMemory += memProps.memoryHeaps[i].size;
        }
    }

    Msg("[Vulkan] Selected: %s (score: %d)", best.props.deviceName, best.score);

    return best.device;
}
