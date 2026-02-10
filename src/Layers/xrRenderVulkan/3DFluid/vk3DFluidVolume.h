// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

#pragma once
#include "../vk_Visual.h"
#include "vk3DFluidData.h"

namespace VK
{

/**
 * 3D Fluid Volume Visual
 *
 * Visual класс для fluid volumes (MT_3DFLUIDVOLUME type).
 * Интегрируется в scene graph через FHierrarhyVisual::children.
 *
 * Содержит:
 * - vk3DFluidData (состояние volume)
 * - Bounding box для culling
 * - Transform матрица
 */
class vk3DFluidVolume : public vkRender_Visual
{
public:
    vk3DFluidVolume();
    virtual ~vk3DFluidVolume();

    // ========================================================================
    // vkRender_Visual interface
    // ========================================================================

    /**
     * Загрузить volume из level.fog_vol
     */
    virtual void Load(const char* name, IReader* data, u32 flags) override;

    /**
     * Уничтожить GPU ресурсы
     */
    virtual void Release() override;

    /**
     * Copy от другого volume
     */
    virtual void Copy(vkRender_Visual* from) override;

    /**
     * Render volume (вызывает raycasting renderer)
     */
    virtual void Render(float LOD) override;

    // ========================================================================
    // Accessors
    // ========================================================================

    vk3DFluidData* GetData() { return m_FluidData; }
    const vk3DFluidData* GetData() const { return m_FluidData; }

private:
    vk3DFluidData* m_FluidData = nullptr;
};

} // namespace VK
