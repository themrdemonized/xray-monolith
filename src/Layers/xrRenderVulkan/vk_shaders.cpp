// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#include "stdafx.h"
#include "vk_shaders.h"
#include "HW_Vulkan.h"
#include <fstream>

namespace VK
{

// SPIR-V Magic Number (0x07230203)
static constexpr u32 SPIRV_MAGIC = 0x07230203;

// Constructor
CVulkanShaderManager::CVulkanShaderManager()
{
    Msg("[Vulkan] CVulkanShaderManager::CVulkanShaderManager()");

    // Build absolute path to Vulkan shaders using Core.ApplicationPath
    // ApplicationPath = exe directory (e.g. D:\anomaly\bin\)
    // Game root = ApplicationPath + ".."
    m_BasePath = Core.ApplicationPath;
    m_BasePath += "..\\gamedata\\shaders\\";
}

// Destructor
CVulkanShaderManager::~CVulkanShaderManager()
{
    Msg("[Vulkan] CVulkanShaderManager::~CVulkanShaderManager()");
    DestroyAll();
}

// Загрузка SPIR-V шейдера
VkShaderModule CVulkanShaderManager::Load(const char* filename)
{
    if (!filename || !filename[0]) {
        Msg("![Vulkan] Load(): Empty filename");
        return VK_NULL_HANDLE;
    }

    // Проверяем кэш
    auto it = m_Modules.find(filename);
    if (it != m_Modules.end()) {
        Msg("[Vulkan] Shader '%s' already loaded (cached)", filename);
        return it->second;
    }

    Msg("[Vulkan] Loading shader: %s", filename);

    // Читаем файл
    xr_vector<char> code = ReadFile(filename);
    if (code.empty()) {
        Msg("![Vulkan] Failed to read shader file: %s", filename);
        return VK_NULL_HANDLE;
    }

    // Валидация SPIR-V
    if (!ValidateSPIRV(code)) {
        Msg("![Vulkan] Invalid SPIR-V file: %s", filename);
        return VK_NULL_HANDLE;
    }

    // Создаём shader module
    VkShaderModule module = CreateShaderModule(code);
    if (module == VK_NULL_HANDLE) {
        Msg("![Vulkan] Failed to create shader module: %s", filename);
        return VK_NULL_HANDLE;
    }

    // Кэшируем
    m_Modules[filename] = module;

    Msg("[Vulkan] Shader loaded successfully: %s (size: %zu bytes)", filename, code.size());
    return module;
}

// Получить уже загруженный module
VkShaderModule CVulkanShaderManager::Get(const char* filename)
{
    auto it = m_Modules.find(filename);
    if (it != m_Modules.end()) {
        return it->second;
    }

    Msg("![Vulkan] Shader not loaded: %s", filename);
    return VK_NULL_HANDLE;
}

// Удалить конкретный module
void CVulkanShaderManager::Destroy(const char* filename)
{
    auto it = m_Modules.find(filename);
    if (it != m_Modules.end()) {
        vkDestroyShaderModule(VulkanHW.m_Device, it->second, nullptr);
        m_Modules.erase(it);
        Msg("[Vulkan] Shader destroyed: %s", filename);
    }
}

// Удалить все modules
void CVulkanShaderManager::DestroyAll()
{
    if (m_Modules.empty()) {
        return;
    }

    Msg("[Vulkan] Destroying %u shader modules...", (u32)m_Modules.size());

    for (auto& [name, module] : m_Modules) {
        vkDestroyShaderModule(VulkanHW.m_Device, module, nullptr);
    }

    m_Modules.clear();

    Msg("[Vulkan] All shader modules destroyed");
}

// Чтение бинарного файла
xr_vector<char> CVulkanShaderManager::ReadFile(const char* filename)
{
    // Формируем полный путь
    xr_string fullPath = m_BasePath + filename;

    // Открываем файл в бинарном режиме
    std::ifstream file(fullPath.c_str(), std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        Msg("![Vulkan] Cannot open file: %s", fullPath.c_str());
        return {};
    }

    // Получаем размер файла (курсор в конце из-за ios::ate)
    size_t fileSize = (size_t)file.tellg();
    if (fileSize == 0) {
        Msg("![Vulkan] Empty file: %s", fullPath.c_str());
        file.close();
        return {};
    }

    // Allocate buffer
    xr_vector<char> buffer(fileSize);

    // Читаем с начала
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}

// Создание shader module
VkShaderModule CVulkanShaderManager::CreateShaderModule(const xr_vector<char>& code)
{
    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const u32*>(code.data());

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    VkResult result = vkCreateShaderModule(VulkanHW.m_Device, &createInfo, nullptr, &shaderModule);

    if (result != VK_SUCCESS) {
        Msg("![Vulkan] vkCreateShaderModule failed: %d", result);
        return VK_NULL_HANDLE;
    }

    return shaderModule;
}

// Валидация SPIR-V
bool CVulkanShaderManager::ValidateSPIRV(const xr_vector<char>& code)
{
    // SPIR-V должен быть кратен 4 байтам
    if (code.size() % 4 != 0) {
        Msg("![Vulkan] SPIR-V size must be multiple of 4 (got %zu)", code.size());
        return false;
    }

    // Минимальный размер SPIR-V файла (header)
    if (code.size() < 20) {
        Msg("![Vulkan] SPIR-V too small (minimum 20 bytes, got %zu)", code.size());
        return false;
    }

    // Проверяем magic number (первые 4 байта)
    u32 magic = *reinterpret_cast<const u32*>(code.data());
    if (magic != SPIRV_MAGIC) {
        Msg("![Vulkan] Invalid SPIR-V magic number: 0x%08X (expected 0x%08X)", magic, SPIRV_MAGIC);
        return false;
    }

    return true;
}

} // namespace VK

// Глобальный экземпляр
VK::CVulkanShaderManager* g_ShaderManager = nullptr;
