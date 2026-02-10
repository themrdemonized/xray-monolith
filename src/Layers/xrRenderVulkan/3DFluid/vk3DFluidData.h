// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

#pragma once
#include "../vk_core.h"
#include "../SH_RT_Vulkan.h"

namespace VK
{

// Forward declarations
class vk3DFluidEmitters;
class vk3DFluidObstacles;

/**
 * 3D Fluid Volume Settings
 */
struct FluidSettings
{
    float TimeStep;           // Временной шаг симуляции (0.016 для 60fps)
    float Decay;              // Затухание density (0.0-1.0)
    float Viscosity;          // Вязкость (обычно ~0.001)
    float Buoyancy;           // Плавучесть (для дыма вверх)
    float Weight;             // Вес (для огня вниз)
    float VorticityStrength;  // Сила вихрей (обычно ~0.3)
    u32   JacobiIterations;   // Количество итераций Jacobi solver (обычно 30-50)
    float AmbientTemperature; // Окружающая температура
    float SmokeColor[4];      // RGBA цвет дыма
    float FireColor[4];       // RGBA цвет огня
    bool  EnableVorticity;    // Включить vorticity confinement
    bool  EnableBFECC;        // Включить BFECC advection (более точная, но медленнее)
};

/**
 * 3D Fluid Volume Data
 *
 * Состояние одного fluid volume:
 * - 3 приватных RT (velocity, pressure, color/density)
 * - Transform матрица (position, rotation, scale)
 * - Settings (decay, buoyancy, etc.)
 * - Emitters (источники дыма)
 * - Obstacles (статичная геометрия)
 */
class vk3DFluidData
{
public:
    vk3DFluidData();
    ~vk3DFluidData();

    /**
     * Создать volume
     * @param gridSize Размер grid (64, 128, 256)
     * @param worldTransform Transform матрица в мировых координатах
     */
    void Create(u32 gridSize, const Fmatrix& worldTransform);

    /**
     * Загрузить из файла level.fog_vol
     * @param fs IReader для файла
     */
    void Load(IReader* fs);

    /**
     * Уничтожить volume
     */
    void Destroy();

    /**
     * Update симуляции (вызывается каждый frame)
     * @param dt Delta time
     */
    void Update(float dt);

    // Accessors
    CRT* GetVelocityRT() const { return m_RT_Velocity; }
    CRT* GetPressureRT() const { return m_RT_Pressure; }
    CRT* GetColorRT() const { return m_RT_Color; }

    const Fmatrix& GetTransform() const { return m_WorldTransform; }
    const FluidSettings& GetSettings() const { return m_Settings; }
    u32 GetGridSize() const { return m_GridSize; }

    vk3DFluidEmitters* GetEmitters() { return m_Emitters; }
    vk3DFluidObstacles* GetObstacles() { return m_Obstacles; }

    bool IsEnabled() const { return m_bEnabled; }
    void SetEnabled(bool enabled) { m_bEnabled = enabled; }

    /**
     * Swap velocity RT (для ping-pong между volume->velocity и shared velocity1)
     * @param externalRT External RT to swap with
     */
    void SwapVelocityRT(CRT* externalRT);

    /**
     * Swap color RT (для ping-pong)
     * @param externalRT External RT to swap with
     */
    void SwapColorRT(CRT* externalRT);

private:
    u32 m_GridSize = 0;
    bool m_bEnabled = true;

    // Transform в мировых координатах
    Fmatrix m_WorldTransform;

    // Settings
    FluidSettings m_Settings;

    // Private render targets (для этого volume)
    CRT* m_RT_Velocity = nullptr;  // RGB = velocity vector
    CRT* m_RT_Pressure = nullptr;  // R = pressure scalar
    CRT* m_RT_Color = nullptr;     // RGBA = density + color

    // Emitters и obstacles
    vk3DFluidEmitters*  m_Emitters = nullptr;
    vk3DFluidObstacles* m_Obstacles = nullptr;
};

} // namespace VK
