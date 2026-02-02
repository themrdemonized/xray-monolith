// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#include "stdafx.h"
#include "vk_DetailManager.h"
#include "vk_R_Backend.h"
#include "rvk.h"
#include "../xrRender/DetailFormat.h"
#include "../../xrEngine/IGame_Persistent.h"
#include "../../xrEngine/Environment.h"

namespace VK
{

// ============================================================================
// Render - Main rendering entry point
// ============================================================================
void CDetailManager::Render()
{
    if (!m_bCreated)
        return;

    if (objects.empty())
        return;

    // Ensure visibility is up to date
    MT_SYNC();

    // Update wind animation
    UpdateWindAnimation();

    // Setup view-projection matrix
    Fmatrix mVP;
    mVP.mul(RDEVICE.mProject, RDEVICE.mView);
    m_Constants.mViewProj = mVP;

    // Setup constants (scale, aniso, ambient)
    m_Constants.vConsts.set(
        1.0f,                                   // scale_x
        1.0f,                                   // scale_y
        g_pGamePersistent->Environment().CurrentEnv->sun_dir.y,  // l_aniso (sun angle)
        0.2f                                    // l_ambient
    );

    // Get current command buffer
    VkCommandBuffer cmd = RCache.GetCommandBuffer();
    if (cmd == VK_NULL_HANDLE)
        return;

    // Bind pipeline
    if (m_Pipeline == VK_NULL_HANDLE)
    {
        Msg("![Vulkan] Details: Pipeline not created");
        return;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline);

    // Push constants
    VkPipelineLayout layout = VK::g_PipelineManager->GetLayout();
    vkCmdPushConstants(cmd, layout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(m_Constants), &m_Constants);

    // Render each object type
    for (u32 obj_id = 0; obj_id < objects.size(); obj_id++)
    {
        VK::CDetail* obj = objects[obj_id];
        if (!obj || !obj->m_VertexBuffer || !obj->m_IndexBuffer)
            continue;

        // Render each animation type separately (different shader permutations)
        for (int anim = 0; anim < 3; anim++)
        {
            if (m_visibles[anim].size() <= obj_id)
                continue;

            xr_vector<SlotItemVec*>& vis_vec = m_visibles[anim][obj_id];
            if (vis_vec.empty())
                continue;

            // Build instance buffer for this (object, animation) combo
            m_InstanceBuffer.BeginUpdate();

            for (u32 i = 0; i < vis_vec.size(); i++)
            {
                SlotItemVec* items = vis_vec[i];
                if (!items)
                    continue;

                for (SlotItemVecIt it = items->begin(); it != items->end(); ++it)
                {
                    SlotItem* Item = *it;

                    if (Item->alpha < 0.01f)
                        continue;

                    // Build transform matrix
                    Fmatrix mTranslation;
                    mTranslation.translate(Item->position);
                    Fmatrix transform;
                    transform.mul_43(Item->mRotY, mTranslation);

                    // Add instance
                    m_InstanceBuffer.AddInstance(
                        transform,
                        Item->c_sun,
                        Item->c_hemi,
                        Item->scale_calculated
                    );
                }
            }

            u32 instance_count = m_InstanceBuffer.EndUpdate();
            if (instance_count == 0)
                continue;

            // Bind base mesh vertex buffer
            VkBuffer vb = obj->m_VertexBuffer->GetHandle();
            VkDeviceSize vb_offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &vb_offset);

            // Bind instance buffer
            VkBuffer ib_inst = m_InstanceBuffer.GetBuffer()->GetHandle();
            VkDeviceSize ib_offset = 0;
            vkCmdBindVertexBuffers(cmd, 1, 1, &ib_inst, &ib_offset);

            // Bind index buffer
            VkBuffer ib = obj->m_IndexBuffer->GetHandle();
            vkCmdBindIndexBuffer(cmd, ib, 0, VK_INDEX_TYPE_UINT16);

            // Bind texture descriptor set (if texture exists)
            if (obj->m_Texture)
            {
                // TODO: Bind texture descriptor set when texture system is ready
                // For now skip - will use default white texture
            }

            // Draw indexed instanced
            vkCmdDrawIndexed(cmd, obj->m_IndexCount, instance_count, 0, 0, 0);
        }
    }
}

} // namespace VK
