// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#pragma once
#include "vk_core.h"
#include <unordered_map>
#include <string>

namespace VK
{

/**
 * Vulkan Shader Manager
 *
 * Управляет загрузкой и кэшированием SPIR-V шейдерных модулей.
 *
 * Использование:
 *   VkShaderModule vs = ShaderManager.Load("shaders/test.vert.spv");
 *   VkShaderModule fs = ShaderManager.Load("shaders/test.frag.spv");
 *
 * Особенности:
 * - Автоматическое кэширование (повторный Load() вернёт кэшированный module)
 * - Загрузка из gamedata/shaders/ (путь относительно рабочей директории)
 * - Проверка существования файлов
 * - Валидация SPIR-V magic number
 */
class CVulkanShaderManager
{
public:
    CVulkanShaderManager();
    ~CVulkanShaderManager();

    /**
     * Загрузить SPIR-V шейдер из файла
     * @param filename Путь к .spv файлу (например "test.vert.spv")
     * @return VkShaderModule или VK_NULL_HANDLE при ошибке
     *
     * При повторном вызове возвращает кэшированный module.
     */
    VkShaderModule Load(const char* filename);

    /**
     * Получить уже загруженный module (без загрузки)
     * @param filename Путь к .spv файлу
     * @return VkShaderModule или VK_NULL_HANDLE если не загружен
     */
    VkShaderModule Get(const char* filename);

    /**
     * Удалить конкретный shader module
     * @param filename Путь к .spv файлу
     */
    void Destroy(const char* filename);

    /**
     * Удалить все загруженные shader modules
     * Вызывается при shutdown
     */
    void DestroyAll();

    /**
     * Статистика загруженных шейдеров
     */
    u32 GetLoadedCount() const { return (u32)m_Modules.size(); }

private:
    /**
     * Прочитать бинарный файл в память
     * @param filename Путь к файлу
     * @return Массив байт или пустой вектор при ошибке
     */
    xr_vector<char> ReadFile(const char* filename);

    /**
     * Создать VkShaderModule из SPIR-V bytecode
     * @param code Массив байт SPIR-V
     * @return VkShaderModule или VK_NULL_HANDLE при ошибке
     */
    VkShaderModule CreateShaderModule(const xr_vector<char>& code);

    /**
     * Проверить SPIR-V magic number (0x07230203)
     */
    bool ValidateSPIRV(const xr_vector<char>& code);

private:
    // Кэш загруженных modules: filename -> VkShaderModule
    xr_map<xr_string, VkShaderModule> m_Modules;

    // Базовый путь к шейдерам (gamedata/shaders/)
    xr_string m_BasePath;
};

} // namespace VK

// Глобальный экземпляр
extern VK::CVulkanShaderManager* g_ShaderManager;
