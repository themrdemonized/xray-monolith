// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#include "stdafx.h"
#include "vk3DFluidData.h"
#include "vk3DFluidEmitters.h"
#include "vk3DFluidObstacles.h"

namespace VK
{

vk3DFluidData::vk3DFluidData()
{
    // Default settings
    m_Settings.TimeStep = 0.016f;  // ~60fps
    m_Settings.Decay = 0.995f;
    m_Settings.Viscosity = 0.001f;
    m_Settings.Buoyancy = 1.0f;
    m_Settings.Weight = -0.05f;
    m_Settings.VorticityStrength = 0.3f;
    m_Settings.JacobiIterations = 40;
    m_Settings.AmbientTemperature = 0.0f;
    m_Settings.EnableVorticity = true;
    m_Settings.EnableBFECC = false;  // BFECC медленнее, по умолчанию выключено

    // Default smoke color (белый)
    m_Settings.SmokeColor[0] = 1.0f;
    m_Settings.SmokeColor[1] = 1.0f;
    m_Settings.SmokeColor[2] = 1.0f;
    m_Settings.SmokeColor[3] = 1.0f;

    // Default fire color (оранжево-красный)
    m_Settings.FireColor[0] = 1.0f;
    m_Settings.FireColor[1] = 0.4f;
    m_Settings.FireColor[2] = 0.1f;
    m_Settings.FireColor[3] = 1.0f;

    m_WorldTransform.identity();
}

vk3DFluidData::~vk3DFluidData()
{
    Destroy();
}

void vk3DFluidData::Create(u32 gridSize, const Fmatrix& worldTransform)
{
    m_GridSize = gridSize;
    m_WorldTransform = worldTransform;

    // Создаём render targets
    // Формат: R16G16B16A16_SFLOAT для точной симуляции

    m_RT_Velocity = new CRT();
    m_RT_Velocity->Create3D(
        VK_FORMAT_R16G16B16A16_SFLOAT,
        gridSize, gridSize, gridSize,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
    );

    m_RT_Pressure = new CRT();
    m_RT_Pressure->Create3D(
        VK_FORMAT_R16G16B16A16_SFLOAT,  // 4 компонента для удобства (используем только R)
        gridSize, gridSize, gridSize,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
    );

    m_RT_Color = new CRT();
    m_RT_Color->Create3D(
        VK_FORMAT_R16G16B16A16_SFLOAT,
        gridSize, gridSize, gridSize,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
    );

    // Создаём emitters и obstacles
    m_Emitters = new vk3DFluidEmitters();
    m_Obstacles = new vk3DFluidObstacles();

    Msg("[Vulkan] 3D Fluid Data created: %dx%dx%d", gridSize, gridSize, gridSize);
}

void vk3DFluidData::Load(IReader* fs)
{
    if (!fs) {
        Msg("![Vulkan] Cannot load fluid data from null reader");
        return;
    }

    // Читаем версию файла
    u16 version = fs->r_u16();
    if (version != 3) {
        Msg("![Vulkan] Unsupported fog_vol version: %d (expected 3)", version);
        return;
    }

    // Читаем grid size
    u32 gridSize = fs->r_u32();

    // Читаем transform
    Fmatrix transform;
    fs->r(&transform, sizeof(Fmatrix));

    // Создаём volume
    Create(gridSize, transform);

    // Читаем settings
    m_Settings.TimeStep = fs->r_float();
    m_Settings.Decay = fs->r_float();
    m_Settings.Viscosity = fs->r_float();
    m_Settings.Buoyancy = fs->r_float();
    m_Settings.Weight = fs->r_float();
    m_Settings.VorticityStrength = fs->r_float();
    m_Settings.JacobiIterations = fs->r_u32();
    m_Settings.AmbientTemperature = fs->r_float();

    // Читаем цвета
    fs->r(m_Settings.SmokeColor, sizeof(m_Settings.SmokeColor));
    fs->r(m_Settings.FireColor, sizeof(m_Settings.FireColor));

    // Читаем флаги
    m_Settings.EnableVorticity = fs->r_u8() != 0;
    m_Settings.EnableBFECC = fs->r_u8() != 0;

    // Читаем emitters
    u32 emitterCount = fs->r_u32();
    if (m_Emitters && emitterCount > 0) {
        // TODO: Load emitters (будет реализовано в vk3DFluidEmitters)
    }

    // Читаем obstacles
    u32 obstacleCount = fs->r_u32();
    if (m_Obstacles && obstacleCount > 0) {
        // TODO: Load obstacles (будет реализовано в vk3DFluidObstacles)
    }

    Msg("[Vulkan] 3D Fluid Data loaded: %dx%dx%d, %d emitters, %d obstacles",
        gridSize, gridSize, gridSize, emitterCount, obstacleCount);
}

void vk3DFluidData::Update(float dt)
{
    if (!m_bEnabled) {
        return;
    }

    // Update будет вызывать симуляцию через vk3DFluidManager
    // Здесь только обновление settings если нужно
}

void vk3DFluidData::Destroy()
{
    // Уничтожаем emitters и obstacles
    if (m_Emitters) {
        delete m_Emitters;
        m_Emitters = nullptr;
    }

    if (m_Obstacles) {
        delete m_Obstacles;
        m_Obstacles = nullptr;
    }

    // Уничтожаем render targets
    if (m_RT_Velocity) {
        m_RT_Velocity->Destroy();
        delete m_RT_Velocity;
        m_RT_Velocity = nullptr;
    }

    if (m_RT_Pressure) {
        m_RT_Pressure->Destroy();
        delete m_RT_Pressure;
        m_RT_Pressure = nullptr;
    }

    if (m_RT_Color) {
        m_RT_Color->Destroy();
        delete m_RT_Color;
        m_RT_Color = nullptr;
    }

    m_GridSize = 0;
    m_bEnabled = false;
}

void vk3DFluidData::SwapVelocityRT(CRT* externalRT)
{
    // Note: на самом деле проще использовать copy вместо swap
    // так как shared RT используется всеми volumes.
    // Для production версии нужно либо:
    // 1. VkCmdCopyImage из externalRT → m_RT_Velocity
    // 2. Или использовать triple buffering схему

    // Пока оставляем как stub - swap логика требует рефакторинга архитектуры
}

void vk3DFluidData::SwapColorRT(CRT* externalRT)
{
    // См. комментарий в SwapVelocityRT()
}

} // namespace VK
