# План полной реализации Scene Graph для Vulkan 3D Renderer

## Текущая проблема
- UI работает ✅
- 3D рендеринг — черный экран ❌
- Причина: `r_dsgraph_render_graph()` и `Calculate()` — пустые заглушки

---

## Архитектура: Pipeline → Material → Items

### Почему не копируем DX11 иерархию
DX11: `VS → GS → PS → Constants → States → Textures → Items` (7 уровней)

Vulkan: `Pipeline → Material → Items` (3 уровня)
- **Pipeline** = VS + PS + States (одна смена = дорого)
- **Material** = DescriptorSet с текстурами (смена = средне)
- **Items** = draw calls (смена = дёшево)

---

## Структуры данных

### Файл: `vk_dsgraph_types.h` (НОВЫЙ)

```cpp
#pragma once
#include "../../xrCore/fixedmap.h"
#include "vk_material.h"

namespace VK_dsgraph
{
    // Статический элемент (уровневая геометрия)
    struct _NormalItem
    {
        float ssa;                      // Screen-Space Area
        vkRender_Visual* pVisual;
    };

    // Динамический элемент (объекты с матрицей)
    struct _MatrixItem
    {
        float ssa;
        IRenderable* pObject;
        vkRender_Visual* pVisual;
        Fmatrix Matrix;
        Fmatrix PrevMatrix;             // Для motion vectors
    };

    // Уровень 3: Items
    struct mapNormalItems : public xr_vector<_NormalItem>
    {
        float ssa = 0.f;
    };

    struct mapMatrixItems : public xr_vector<_MatrixItem>
    {
        float ssa = 0.f;
    };

    // Уровень 2: Material → Items
    struct mapMaterial : public FixedMAP<VK::CMaterial*, mapNormalItems, render_allocator>
    {
        float ssa = 0.f;
    };

    struct mapMatrixMaterial : public FixedMAP<VK::CMaterial*, mapMatrixItems, render_allocator>
    {
        float ssa = 0.f;
    };

    // Уровень 1: Pipeline → Material
    struct mapPipeline : public FixedMAP<VkPipeline, mapMaterial, render_allocator>
    {
    };

    struct mapMatrixPipeline : public FixedMAP<VkPipeline, mapMatrixMaterial, render_allocator>
    {
    };

    // Sorted queues (back-to-front для прозрачных)
    typedef FixedMAP<float, _MatrixItem, render_allocator> mapSorted_T;
    typedef FixedMAP<float, _MatrixItem, render_allocator> mapHUD_T;
}
```

---

## Реализация по файлам

### 1. `rvk.h` — добавить члены класса

```cpp
// После строки 166 добавить:
private:
    // ========================================================================
    // Scene Graph Render Queues
    // ========================================================================
    // Normal (static) passes - [priority][pass]
    VK_dsgraph::mapPipeline mapNormalPasses[2][SHADER_PASSES_MAX];

    // Matrix (dynamic) passes - [priority][pass]
    VK_dsgraph::mapMatrixPipeline mapMatrixPasses[2][SHADER_PASSES_MAX];

    // Sorted queues
    VK_dsgraph::mapSorted_T mapSorted;
    VK_dsgraph::mapHUD_T mapHUD;

    // Visibility
    CFrustum ViewBase;
    CFrustum* View;

    // Default pipeline for G-Buffer (cached)
    VkPipeline m_DefaultGBufferPipeline = VK_NULL_HANDLE;

public:
    // Scene graph methods
    void r_dsgraph_insert_static(vkRender_Visual* pVisual);
    void r_dsgraph_insert_dynamic(vkRender_Visual* pVisual, Fvector& Center);
    void add_Static(vkRender_Visual* pVisual, u32 planes);
    void add_leafs_Static(vkRender_Visual* pVisual);
    void add_Dynamic(vkRender_Visual* pVisual, u32 planes);
    void add_leafs_Dynamic(vkRender_Visual* pVisual);
```

---

### 2. `vk_dsgraph_build.cpp` (НОВЫЙ) — visibility и вставка

```cpp
#include "stdafx.h"
#include "rvk.h"
#include "vk_dsgraph_types.h"
#include "vk_Visual.h"
#include "vk_material.h"
#include "vk_pipeline.h"

// SSA calculation
ICF float CalcSSA(float& distSQ, const Fvector& C, vkRender_Visual* V)
{
    float R = V->vis.sphere.R;
    distSQ = Device.vCameraPosition.distance_to_sqr(C) + EPS;
    return R / distSQ;
}

// ============================================================================
// r_dsgraph_insert_static — вставка в Pipeline→Material→Items
// ============================================================================
void CRender::r_dsgraph_insert_static(vkRender_Visual* pVisual)
{
    // Deduplicate by marker
    if (pVisual->vis.marker == marker) return;
    pVisual->vis.marker = marker;

    // Calculate SSA
    float distSQ;
    float SSA = CalcSSA(distSQ, pVisual->vis.sphere.P, pVisual);
    if (SSA <= r_ssaDISCARD) return;

    // Get pipeline (or default)
    VkPipeline pipeline = m_DefaultGBufferPipeline;
    // TODO: pipeline = pVisual->GetPipeline();

    // Get material (or default)
    VK::CMaterial* material = g_MaterialManager ?
        g_MaterialManager->GetDefaultMaterial() : nullptr;
    // TODO: material = pVisual->GetMaterial();

    // Priority 0, Pass 0
    u32 priority = 0;
    u32 pass = 0;

    // Insert: Pipeline → Material → Item
    auto& pipelineMap = mapNormalPasses[priority][pass];
    auto* pipelineNode = pipelineMap.insert_IfNotFind(pipeline);
    auto* materialNode = pipelineNode->val.insert_IfNotFind(material);

    VK_dsgraph::_NormalItem item = {SSA, pVisual};
    materialNode->val.push_back(item);

    // Update SSA chain
    if (SSA > materialNode->val.ssa) materialNode->val.ssa = SSA;
    if (SSA > pipelineNode->val.ssa) pipelineNode->val.ssa = SSA;
}

// ============================================================================
// add_Static — с frustum culling
// ============================================================================
void CRender::add_Static(vkRender_Visual* pVisual, u32 planes)
{
    // Frustum test
    EFC_Visible VIS = View->testSphere(pVisual->vis.sphere.P,
                                        pVisual->vis.sphere.R, planes);
    if (VIS == fcvNone) return;

    // HOM test (optional)
    // if (HOM && !HOM->visible(pVisual->vis)) return;

    // Handle by type
    switch (pVisual->Type)
    {
    case MT_HIERRARHY:
        {
            vkFHierrarhyVisual* pV = (vkFHierrarhyVisual*)pVisual;
            for (auto child : pV->children)
            {
                if (VIS == fcvPartial)
                    add_Static((vkRender_Visual*)child, planes);
                else
                    add_leafs_Static((vkRender_Visual*)child);
            }
        }
        break;

    case MT_LOD:
        // TODO: LOD selection
        break;

    default:
        r_dsgraph_insert_static(pVisual);
        break;
    }
}

// ============================================================================
// add_leafs_Static — без дополнительного culling
// ============================================================================
void CRender::add_leafs_Static(vkRender_Visual* pVisual)
{
    switch (pVisual->Type)
    {
    case MT_HIERRARHY:
        {
            vkFHierrarhyVisual* pV = (vkFHierrarhyVisual*)pVisual;
            for (auto child : pV->children)
                add_leafs_Static((vkRender_Visual*)child);
        }
        break;

    default:
        r_dsgraph_insert_static(pVisual);
        break;
    }
}

// ============================================================================
// Calculate — главная точка входа
// ============================================================================
void CRender::Calculate()
{
    if (!b_loaded) return;

    // Clear all queues
    for (int p = 0; p < 2; p++)
    {
        for (int pass = 0; pass < SHADER_PASSES_MAX; pass++)
        {
            mapNormalPasses[p][pass].clear();
            mapMatrixPasses[p][pass].clear();
        }
    }
    mapSorted.clear();
    mapHUD.clear();

    // Increment marker
    marker++;

    // Build frustum from camera
    ViewBase.CreateFromMatrix(Device.mFullTransform, FRUSTUM_P_LRTB | FRUSTUM_P_FAR);
    View = &ViewBase;

    // Ensure default pipeline exists
    if (m_DefaultGBufferPipeline == VK_NULL_HANDLE && RTarget)
    {
        m_DefaultGBufferPipeline = RTarget->GetGBufferPipeline();
    }

    // Add level visuals
    for (auto visual : Visuals)
    {
        if (!visual) continue;
        add_Static(static_cast<vkRender_Visual*>(visual), 0xFFFFFFFF);
    }

    // TODO: Portal traversal
    // TODO: Dynamic objects from spatial DB
}
```

---

### 3. `vk_dsgraph_render.cpp` (НОВЫЙ) — рендеринг очередей

```cpp
#include "stdafx.h"
#include "rvk.h"
#include "vk_dsgraph_types.h"
#include "vk_Visual.h"
#include "vk_material.h"
#include "vk_pipeline.h"
#include "vk_R_Backend.h"

// Sort comparators
IC bool cmp_ssa_greater(const VK_dsgraph::_NormalItem& a,
                        const VK_dsgraph::_NormalItem& b)
{
    return a.ssa > b.ssa;  // Front-to-back
}

// ============================================================================
// r_dsgraph_render_graph — рендеринг с минимизацией state changes
// ============================================================================
void R_dsgraph_structure::r_dsgraph_render_graph(u32 _priority, bool _clear)
{
    CRender& RI = RImplementation;
    VkCommandBuffer cmd = RCache.GetCommandBuffer();
    if (cmd == VK_NULL_HANDLE) return;

    VkPipelineLayout layout = VK::g_PipelineManager->GetLayout();

    // World = identity for static geometry
    RCache.set_xform_world(Fidentity);

    // ====================================================================
    // NORMAL (Static) Geometry
    // ====================================================================
    for (u32 iPass = 0; iPass < SHADER_PASSES_MAX; ++iPass)
    {
        auto& pipelineMap = RI.mapNormalPasses[_priority][iPass];

        // Iterate pipelines (expensive state change)
        for (auto pNode = pipelineMap.begin(); pNode != pipelineMap.end(); ++pNode)
        {
            VkPipeline pipeline = pNode->key;
            if (pipeline != VK_NULL_HANDLE)
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

            // Iterate materials (medium state change)
            auto& materialMap = pNode->val;
            for (auto mNode = materialMap.begin(); mNode != materialMap.end(); ++mNode)
            {
                VK::CMaterial* material = mNode->key;
                if (material)
                    material->Bind(cmd);

                // Sort items by SSA and render
                auto& items = mNode->val;
                std::sort(items.begin(), items.end(), cmp_ssa_greater);

                for (auto& item : items)
                {
                    // Push constants
                    struct {
                        Fmatrix Model, View, Projection;
                    } pc;
                    pc.Model = Fidentity;
                    pc.View = RCache.xforms.get_V();
                    pc.Projection = RCache.xforms.get_P();

                    vkCmdPushConstants(cmd, layout,
                        VK_SHADER_STAGE_VERTEX_BIT,
                        0, sizeof(pc), &pc);

                    // Render
                    item.pVisual->Render(1.0f);
                }

                if (_clear) items.clear();
            }
            if (_clear) materialMap.clear();
        }
        if (_clear) pipelineMap.clear();
    }

    // ====================================================================
    // MATRIX (Dynamic) Geometry — аналогично, но с world matrix
    // ====================================================================
    // TODO: Implement when dynamic objects are needed
}
```

---

### 4. Обновить `vk_shared_stubs.cpp`

Удалить/закомментировать заглушку `r_dsgraph_render_graph()` — теперь реализация в `vk_dsgraph_render.cpp`.

---

### 5. Обновить `vk_rendertarget_phase_gbuffer.cpp`

Строка ~316, заменить:
```cpp
RImplementation.RenderLevelVisuals();
```

На:
```cpp
// Build render queues with visibility culling
RImplementation.Calculate();

// Render opaque geometry (priority 0)
RImplementation.r_dsgraph_render_graph(0, true);
```

---

### 6. Обновить CMakeLists / vcxproj

Добавить новые файлы:
- `vk_dsgraph_types.h`
- `vk_dsgraph_build.cpp`
- `vk_dsgraph_render.cpp`

---

## Порядок реализации

1. ✅ Создать `vk_dsgraph_types.h` со структурами
2. ✅ Добавить члены в `rvk.h`
3. ✅ Создать `vk_dsgraph_build.cpp` (Calculate, add_Static, insert)
4. ✅ Создать `vk_dsgraph_render.cpp` (r_dsgraph_render_graph)
5. ✅ Убрать заглушку из `vk_shared_stubs.cpp`
6. ✅ Обновить `phase_gbuffer()` для вызова scene graph
7. ✅ Собрать и тестировать

---

## Тестирование

### Логи (ожидаемые):
```
[Vulkan] Calculate(): 1234 items in mapNormalPasses[0][0]
[Vulkan] r_dsgraph_render_graph(0): rendering 1234 items
```

### RenderDoc:
- G-Buffer targets должны заполняться
- Draw calls должны быть видны
- Pipeline binds минимизированы

### Визуально:
- Геометрия уровня видна
- Frustum culling работает (объекты за камерой не рендерятся)
