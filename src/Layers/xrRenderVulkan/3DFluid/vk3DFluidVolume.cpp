// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

#include "stdafx.h"
#include "vk3DFluidVolume.h"
#include "vk3DFluidManager.h"

namespace VK
{

vk3DFluidVolume::vk3DFluidVolume()
{
    Type = MT_3DFLUIDVOLUME;
}

vk3DFluidVolume::~vk3DFluidVolume()
{
    Release();
}

void vk3DFluidVolume::Load(const char* name, IReader* data, u32 flags)
{
    if (!data) {
        Msg("![Vulkan] Cannot load 3D fluid volume from null data");
        return;
    }

    // Создаём fluid data
    m_FluidData = new vk3DFluidData();
    m_FluidData->Load(data);

    // Setup visibility data (bounding box для culling)
    const Fmatrix& transform = m_FluidData->GetTransform();
    u32 gridSize = m_FluidData->GetGridSize();

    // Вычисляем AABB в world space
    Fvector minPt(-0.5f, -0.5f, -0.5f);
    Fvector maxPt( 0.5f,  0.5f,  0.5f);

    // Scale by grid size
    minPt.mul(float(gridSize));
    maxPt.mul(float(gridSize));

    // Transform to world space
    Fbox bbox;
    bbox.min = minPt;
    bbox.max = maxPt;

    // Store в visibility data
    vis.box.set(bbox.min, bbox.max);
    vis.sphere.P.set(0, 0, 0);
    vis.sphere.R = bbox.getradius();

    // Добавляем в fluid manager
    g_FluidManager.AddVolume(m_FluidData);

    Msg("[Vulkan] 3D Fluid Volume loaded: %s", name);
}

void vk3DFluidVolume::Release()
{
    if (m_FluidData) {
        // Удаляем из fluid manager
        g_FluidManager.RemoveVolume(m_FluidData);

        // Уничтожаем data
        m_FluidData->Destroy();
        delete m_FluidData;
        m_FluidData = nullptr;
    }
}

void vk3DFluidVolume::Copy(vkRender_Visual* from)
{
    // Copy base class data
    vkRender_Visual::Copy(from);

    // Copy fluid-specific data
    vk3DFluidVolume* src = dynamic_cast<vk3DFluidVolume*>(from);
    if (!src || !src->m_FluidData) {
        return;
    }

    // Создаём копию fluid data
    m_FluidData = new vk3DFluidData();
    m_FluidData->Create(
        src->m_FluidData->GetGridSize(),
        src->m_FluidData->GetTransform()
    );

    // Copy settings
    // TODO: Implement settings copy if needed

    // Добавляем в manager
    g_FluidManager.AddVolume(m_FluidData);
}

void vk3DFluidVolume::Render(float LOD)
{
    if (!m_FluidData || !m_FluidData->IsEnabled()) {
        return;
    }

    // Рендеринг будет выполнен через vk3DFluidManager::RenderFluid()
    // который вызывается из main render loop
    // Здесь ничего не делаем (только update visibility)
}

} // namespace VK
