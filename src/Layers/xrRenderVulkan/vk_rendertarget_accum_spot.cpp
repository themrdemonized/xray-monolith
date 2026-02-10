// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

#include "stdafx.h"
#include "vk_rendertarget.h"
#include "HW_Vulkan.h"
#include "vk_shaders.h"
#include "vk_pipeline.h"
#include "vk_descriptors.h"
#include "vk_R_Backend.h"
#include "rvk.h"

// Light structure
#include "../xrRender/light.h"

namespace VK
{

// ============================================================================
// Phase 2.17.2: Spot Shadow Rendering
// ============================================================================

/**
 * phase_smap_spot() - Render 2D shadow map для spot light
 *
 * Spot lights use 2D shadow maps (как directional), но с атласингом.
 * Multiple spot lights can share rt_smap_depth через viewport atlasing.
 *
 * Process:
 * 1. Allocate atlas slot (if not already allocated)
 * 2. Setup view-projection matrix (perspective, FOV = cone angle)
 * 3. Begin render pass with viewport offset (atlasing)
 * 4. Render shadow geometry with frustum culling
 * 5. End render pass
 *
 * Atlas Strategy:
 * - rt_smap_depth (2048x2048) divided into slots
 * - Simple grid: 4x 1024x1024
 * - Store viewport (posX, posY, size) in light->X.S
 */
void CRenderTarget::phase_smap_spot(light* L)
{
    if (!L) {
        Msg("![Vulkan] phase_smap_spot: NULL light pointer");
        return;
    }

    // phase_smap_spot: Rendering shadow map

    // ========================================================================
    // 1. Allocate atlas slot (if not already allocated)
    // ========================================================================
    if (!AllocateShadowAtlasSlot(L)) {
        Msg("![Vulkan] phase_smap_spot: Failed to allocate shadow atlas slot");
        return;
    }

    // ========================================================================
    // 2. Setup view-projection matrix
    // ========================================================================

    // View matrix: Look from light position along light direction
    Fmatrix view;
    Fvector up;
    up.set(0.0f, 1.0f, 0.0f);  // Default up vector

    // If direction is nearly vertical, use different up vector
    if (fabsf(L->direction.y) > 0.99f) {
        up.set(1.0f, 0.0f, 0.0f);
    }

    Fvector target;
    target.add(L->position, L->direction);
    view.build_camera(L->position, target, up);

    // Projection matrix: Perspective with FOV = cone angle * 2
    Fmatrix proj;
    float fov = L->cone * 2.0f;  // cone = half-angle, so FOV = full angle
    proj.build_projection(
        fov,        // Field of view (radians)
        1.0f,       // Aspect ratio (square viewport)
        0.1f,       // Near plane
        L->range    // Far plane
    );

    // Combined matrix
    Fmatrix viewProj;
    viewProj.mul(proj, view);

    // Store matrices in light structure (for shader usage later)
    L->X.S.view = view;
    L->X.S.project = proj;
    L->X.S.combine = viewProj;

    // View-projection matrix calculated

    // ========================================================================
    // 3. Transition rt_smap_depth to DEPTH_ATTACHMENT (if needed)
    // ========================================================================
    VkCommandBuffer cmd = RCache.GetCommandBuffer();

    // Note: We only transition on first use per frame
    // For now, assume it's already in correct layout or we transition once per frame
    // TODO: Track layout state properly

    // ========================================================================
    // 4. Begin render pass (depth-only, with viewport offset for atlasing)
    // ========================================================================

    VkRenderingAttachmentInfo depthAttachment = {};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = rt_smap_depth.m_ImageView;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;  // Clear this slot
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil.depth = 1.0f;
    depthAttachment.clearValue.depthStencil.stencil = 0;

    VkRenderingInfo renderInfo = {};
    renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderInfo.renderArea.offset.x = (int32_t)L->X.S.posX;
    renderInfo.renderArea.offset.y = (int32_t)L->X.S.posY;
    renderInfo.renderArea.extent.width = L->X.S.size;
    renderInfo.renderArea.extent.height = L->X.S.size;
    renderInfo.layerCount = 1;
    renderInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(cmd, &renderInfo);

    // Render pass begun

    // ========================================================================
    // 5. Set viewport and scissor (atlasing)
    // ========================================================================

    VkViewport viewport = {};
    viewport.x = (float)L->X.S.posX;
    viewport.y = (float)L->X.S.posY;
    viewport.width = (float)L->X.S.size;
    viewport.height = (float)L->X.S.size;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset.x = (int32_t)L->X.S.posX;
    scissor.offset.y = (int32_t)L->X.S.posY;
    scissor.extent.width = L->X.S.size;
    scissor.extent.height = L->X.S.size;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // ========================================================================
    // 6. Render shadow geometry
    // ========================================================================

    // Call CRender to render shadow casters with frustum culling
    extern ::CRender RImplementation;
    RImplementation.render_shadow_geometry(viewProj, L->position, L->range);

    // ========================================================================
    // 7. End render pass
    // ========================================================================

    vkCmdEndRendering(cmd);

    // Shadow map rendering complete

    // Note: Layout transition to SHADER_READ_ONLY will be done once per frame
    // after all spot shadow maps are rendered
}

// ============================================================================
// Phase 2.17.5: Spot Light Accumulation
// ============================================================================

/**
 * accum_spot() - Accumulate spot light в rt_Accumulator
 *
 * Render cone volume scaled by light range и rotated by direction.
 * Apply distance + cone angle attenuation.
 * Sample 2D shadow map с projective texgen.
 *
 * Process:
 * 1. Setup cone transform (scale by range, rotate by direction)
 * 2. Stencil masking (Carmack's reverse) - optional
 * 3. Begin rendering to rt_Accumulator (additive blend)
 * 4. Load spot light shaders (accum_spot.vert/frag)
 * 5. Create pipeline (additive blending)
 * 6. Bind descriptor sets (G-Buffer + spot light data + shadow map)
 * 7. Push constants (cone transform, light params)
 * 8. Draw cone volume
 * 9. Increment light marker
 */
void CRenderTarget::accum_spot(light* L)
{
    if (!L) return;

    // Early-out: check shaders before any Vulkan commands
    static bool s_shaders_checked = false;
    static bool s_shaders_available = false;
    if (!s_shaders_checked) {
        s_shaders_checked = true;
        VkShaderModule vs = g_ShaderManager->Load("accum_spot.vert.spv");
        VkShaderModule fs = g_ShaderManager->Load("accum_spot.frag.spv");
        s_shaders_available = (vs != VK_NULL_HANDLE && fs != VK_NULL_HANDLE);
        if (!s_shaders_available) Msg("![Vulkan] Spot light shaders not available - skipping all spot lights");
    }
    if (!s_shaders_available) return;

    VkCommandBuffer cmd = RCache.GetCommandBuffer();

    // ========================================================================
    // Step 1: Setup cone transform (scaled, rotated, translated)
    // ========================================================================
    // Cone geometry:
    // - Apex at origin (0, 0, 0)
    // - Base at Z = -1, radius = 1.0
    // - Oriented along -Z axis
    //
    // Transform:
    // 1. Scale base radius by: tan(cone_angle) * range
    // 2. Scale length by: range
    // 3. Rotate to align with light direction
    // 4. Translate to light position

    float coneAngle = L->cone;  // Half-angle (radians)
    float coneRadius = tanf(coneAngle) * L->range;

    // Build cone transform matrix
    Fmatrix coneXForm;
    coneXForm.identity();

    // Scale: X/Y by cone radius, Z by range
    coneXForm.scale(coneRadius, coneRadius, L->range);

    // Rotate: align -Z axis with light direction
    // Build rotation matrix from light direction
    Fvector zAxis;
    zAxis.set(L->direction).normalize().invert();  // Light direction = -Z for cone

    Fvector yAxis;
    yAxis.set(0, 1, 0);  // Default up vector

    // Handle vertical direction (avoid gimbal lock)
    if (fabsf(zAxis.y) > 0.99f) {
        yAxis.set(1, 0, 0);
    }

    // Build orthonormal basis
    Fvector xAxis;
    xAxis.crossproduct(yAxis, zAxis).normalize();
    yAxis.crossproduct(zAxis, xAxis).normalize();

    Fmatrix rotation;
    rotation.identity();
    rotation.i = xAxis;
    rotation.j = yAxis;
    rotation.k = zAxis;

    // Combine rotation and scale
    Fmatrix scaleRot;
    scaleRot.mul(rotation, coneXForm);

    // Translate to light position
    coneXForm = scaleRot;
    coneXForm.translate_over(L->position);

    RCache.set_xform_world(coneXForm);
    RCache.set_xform_view(Device.mView);
    RCache.set_xform_project(Device.mProject);

    // ========================================================================
    // Step 2: Calculate MVP matrix for push constants
    // ========================================================================
    Fmatrix mvp;
    mvp.mul(Device.mProject, Device.mView);
    mvp.mulA_43(coneXForm);

    // ========================================================================
    // Step 3: Calculate shadow matrix (world → shadow UV)
    // ========================================================================
    // Shadow matrix converts from eye-space to shadow map UV space:
    // 1. Eye-space → World-space (inverse view)
    // 2. World-space → Light clip space (light VP)
    // 3. Clip space → UV space (bias + scale)

    Fmatrix shadowMatrix;

    // Get light view-projection from shadow rendering phase
    Fmatrix lightVP = L->X.S.combine;  // Calculated in phase_smap_spot()

    // Inverse view matrix (eye-space → world-space)
    Fmatrix invView;
    invView.invert(Device.mView);

    // Combined: eye-space → world → light clip
    Fmatrix eyeToLightClip;
    eyeToLightClip.mul(lightVP, invView);

    // Bias matrix: clip [-1..1] → UV [0..1]
    // Also apply atlas offset (posX, posY, size)
    float atlasOffsetX = (float)L->X.S.posX / (float)m_ShadowMapSize;
    float atlasOffsetY = (float)L->X.S.posY / (float)m_ShadowMapSize;
    float atlasScale = (float)L->X.S.size / (float)m_ShadowMapSize;

    Fmatrix biasMat;
    biasMat.identity();
    biasMat.m[0][0] = atlasScale * 0.5f;
    biasMat.m[1][1] = atlasScale * 0.5f;
    biasMat.m[2][2] = 1.0f;
    biasMat.m[3][0] = atlasOffsetX + atlasScale * 0.5f;
    biasMat.m[3][1] = atlasOffsetY + atlasScale * 0.5f;
    biasMat.m[3][2] = 0.0f;

    shadowMatrix.mul(biasMat, eyeToLightClip);

    // ========================================================================
    // Step 4: Transform light position/direction to eye-space
    // ========================================================================
    Fvector4 lightPosEye;
    Device.mView.transform(lightPosEye, L->position);
    lightPosEye.w = 1.0f / (L->range * L->range);  // Attenuation factor

    Fvector lightDirEye;
    Device.mView.transform_dir(lightDirEye, L->direction);
    lightDirEye.normalize();

    Fvector4 lightDirEye4;
    lightDirEye4.set(lightDirEye.x, lightDirEye.y, lightDirEye.z, cosf(coneAngle));  // .w = cos(inner_cone)

    // ========================================================================
    // Step 5: Prepare cone parameters
    // ========================================================================
    // Cone attenuation uses inner and outer cone angles
    // For now: use same angle for both (no falloff)
    // TODO: Add outer cone angle (e.g., inner * 1.2)

    float outerConeAngle = coneAngle * 1.2f;  // Outer cone slightly larger
    float cosOuterCone = cosf(outerConeAngle);
    float cosInnerCone = cosf(coneAngle);

    Fvector4 coneParams;
    coneParams.x = cosOuterCone;
    coneParams.y = 1.0f / (cosInnerCone - cosOuterCone);  // Falloff factor
    coneParams.z = 0.0f;  // Unused
    coneParams.w = 0.0f;  // Unused

    // ========================================================================
    // Step 6: Begin rendering to accumulator
    // ========================================================================
    VkRenderingAttachmentInfo colorAttachment = {};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = rt_Accumulator.m_ImageView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;  // Preserve existing lighting!
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo renderingInfo = {};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = {m_Width, m_Height};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;

    vkCmdBeginRendering(cmd, &renderingInfo);

    // ========================================================================
    // Step 7: Setup viewport and scissor
    // ========================================================================
    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)m_Width;
    viewport.height = (float)m_Height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = {m_Width, m_Height};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // ========================================================================
    // Step 8: Load spot light shaders
    // ========================================================================
    VkShaderModule vertShader = g_ShaderManager->Load("accum_spot.vert.spv");
    VkShaderModule fragShader = g_ShaderManager->Load("accum_spot.frag.spv");

    if (vertShader == VK_NULL_HANDLE || fragShader == VK_NULL_HANDLE) {
        Msg("![Vulkan] Failed to load spot light shaders");
        vkCmdEndRendering(cmd);
        return;
    }

    // ========================================================================
    // Step 9: Create pipeline (with additive blending)
    // ========================================================================
    PipelineConfig config = {};
    config.vertShader = vertShader;
    config.fragShader = fragShader;
    config.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    config.cullMode = VK_CULL_MODE_NONE;  // TODO: CULL_BACK for cone volume
    config.depthTest = false;  // No depth testing for lighting accumulation
    config.depthWrite = false;
    config.blendEnable = true;  // Additive blending!
    config.colorAttachmentCount = 1;
    config.colorFormats[0] = VK_FORMAT_R16G16B16A16_SFLOAT;  // rt_Accumulator

    VkPipeline pipeline = g_PipelineManager->GetOrCreate(config);
    if (pipeline == VK_NULL_HANDLE) {
        Msg("![Vulkan] Failed to create spot light pipeline");
        vkCmdEndRendering(cmd);
        return;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    // ========================================================================
    // Step 10: Bind descriptor sets (G-Buffer + Spot light data)
    // ========================================================================
    // Get or create descriptor sets
    if (m_GBufferDescSet == VK_NULL_HANDLE) {
        m_GBufferDescSet = CreateGBufferDescriptorSet();
    }
    if (m_SpotDescSet == VK_NULL_HANDLE) {
        m_SpotDescSet = CreateSpotDescriptorSet();
    }

    // Update descriptor sets
    UpdateGBufferDescriptorSet(m_GBufferDescSet);
    UpdateSpotDescriptorSet(m_SpotDescSet, L);

    // Bind descriptor sets
    VkPipelineLayout layout = g_PipelineManager->GetLayout();

    VkDescriptorSet sets[4] = {
        VK_NULL_HANDLE,      // Set 0: PerFrame (not used)
        m_GBufferDescSet,    // Set 1: G-Buffer
        VK_NULL_HANDLE,      // Set 2: PerObject (not used)
        m_SpotDescSet        // Set 3: Spot light + shadow map
    };

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
                            1, 1, &sets[1], 0, nullptr);  // Set 1
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
                            3, 1, &sets[3], 0, nullptr);  // Set 3

    // Descriptor sets bound: G-Buffer + Spot light

    // ========================================================================
    // Step 11: Push constants (spot light data)
    // ========================================================================
    // Push constants layout (192 bytes total):
    // - mat4 u_MVP               (offset 0, 64 bytes)
    // - vec4 u_Position          (offset 64, 16 bytes) - eye-space, .w = 1/(range²)
    // - vec4 u_Direction         (offset 80, 16 bytes) - eye-space, .w = cos(inner)
    // - vec4 u_Color             (offset 96, 16 bytes) - RGB + specular
    // - mat4 u_ShadowMatrix      (offset 112, 64 bytes)
    // - vec4 u_ConeParams        (offset 176, 16 bytes) - .x = cos(outer), .y = falloff

    struct SpotLightPushConstants {
        Fmatrix u_MVP;            // 64 bytes
        Fvector4 u_Position;      // 16 bytes
        Fvector4 u_Direction;     // 16 bytes
        Fvector4 u_Color;         // 16 bytes
        Fmatrix u_ShadowMatrix;   // 64 bytes
        Fvector4 u_ConeParams;    // 16 bytes
    } pushData;

    pushData.u_MVP = mvp;
    pushData.u_Position = lightPosEye;
    pushData.u_Direction = lightDirEye4;
    pushData.u_Color.set(L->color.r, L->color.g, L->color.b, 1.0f);  // .w = specular intensity
    pushData.u_ShadowMatrix = shadowMatrix;
    pushData.u_ConeParams = coneParams;

    vkCmdPushConstants(cmd, layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(SpotLightPushConstants), &pushData);

    // Push constants updated

    // ========================================================================
    // Step 12: Draw cone volume
    // ========================================================================
    // Bind cone geometry
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_SpotVolumeVB, &offset);
    vkCmdBindIndexBuffer(cmd, m_SpotVolumeIB, 0, VK_INDEX_TYPE_UINT16);

    // Draw cone
    vkCmdDrawIndexed(cmd, m_SpotVolumeIndexCount, 1, 0, 0, 0);

    // Spot light cone drawn

    // ========================================================================
    // Step 13: End rendering
    // ========================================================================
    vkCmdEndRendering(cmd);

    // ========================================================================
    // Step 14: Increment light marker (for next light)
    // ========================================================================
    increment_light_marker();

    // accum_spot() complete
}

// ============================================================================
// Shadow Atlas Management (Phase 2.17.1 / 2.17.2)
// ============================================================================

/**
 * InitShadowAtlas() - Initialize shadow atlas grid
 *
 * Creates atlas layout для rt_smap_depth (2048x2048):
 * - 4 slots of 1024x1024 (для больших/близких spot lights)
 *
 * Atlas Layout:
 * ┌─────────┬─────────┐
 * │ Slot 0  │ Slot 1  │ 1024x1024
 * │  (0,0)  │(1024,0) │
 * ├─────────┼─────────┤
 * │ Slot 2  │ Slot 3  │
 * │(0,1024) │(1024,   │
 * └─────────┴─────────┘
 *
 * Future: Can subdivide into 16x 512x512 or 64x 256x256 for smaller lights
 */
void CRenderTarget::InitShadowAtlas()
{
    Msg("[Vulkan] Initializing shadow atlas (2048x2048 → 4x 1024x1024 slots)...");

    m_SpotShadowAtlas.clear();

    // Strategy: 4 large slots (1024x1024)
    // Можно позже добавить меньшие слоты (512, 256) по необходимости
    const u32 slotSize = 1024;

    for (u32 y = 0; y < 2; y++) {
        for (u32 x = 0; x < 2; x++) {
            ShadowAtlasSlot slot;
            slot.posX = x * slotSize;
            slot.posY = y * slotSize;
            slot.size = slotSize;
            slot.occupied = false;
            slot.owner = nullptr;

            m_SpotShadowAtlas.push_back(slot);

            Msg("[Vulkan]   Slot %d: pos(%d,%d), size=%d",
                (u32)m_SpotShadowAtlas.size() - 1, slot.posX, slot.posY, slot.size);
        }
    }

    Msg("[Vulkan] Shadow atlas initialized: %d slots", (u32)m_SpotShadowAtlas.size());
}

/**
 * AllocateShadowAtlasSlot() - Find free slot in shadow atlas
 *
 * Returns: true если slot allocated, false если atlas full
 */
bool CRenderTarget::AllocateShadowAtlasSlot(light* L)
{
    if (!L) {
        Msg("![Vulkan] AllocateShadowAtlasSlot: NULL light pointer");
        return false;
    }

    // Check if already allocated
    if (L->X.S.size > 0) {
        // Already has a slot
        // Already allocated
        return true;
    }

    // Find free slot
    for (u32 i = 0; i < m_SpotShadowAtlas.size(); i++) {
        ShadowAtlasSlot& slot = m_SpotShadowAtlas[i];

        if (!slot.occupied) {
            // Found free slot!
            slot.occupied = true;
            slot.owner = L;

            // Store in light structure
            L->X.S.posX = slot.posX;
            L->X.S.posY = slot.posY;
            L->X.S.size = slot.size;

            // Allocated slot

            return true;
        }
    }

    // Atlas full!
    Msg("![Vulkan] AllocateShadowAtlasSlot: Shadow atlas FULL! Cannot allocate slot.");
    return false;
}

/**
 * FreeShadowAtlasSlot() - Free atlas slot
 */
void CRenderTarget::FreeShadowAtlasSlot(light* L)
{
    if (!L) {
        Msg("![Vulkan] FreeShadowAtlasSlot: NULL light pointer");
        return;
    }

    // Check if light has a slot
    if (L->X.S.size == 0) {
        // No slot allocated
        return;
    }

    // Find slot that matches light's viewport
    for (u32 i = 0; i < m_SpotShadowAtlas.size(); i++) {
        ShadowAtlasSlot& slot = m_SpotShadowAtlas[i];

        if (slot.occupied && slot.owner == L) {
            // Found the slot!
            slot.occupied = false;
            slot.owner = nullptr;

            Msg("[Vulkan] FreeShadowAtlasSlot: Freed slot %d (pos=%d,%d, size=%d)",
                i, slot.posX, slot.posY, slot.size);

            // Clear light's slot data
            L->X.S.posX = 0;
            L->X.S.posY = 0;
            L->X.S.size = 0;

            return;
        }
    }

    Msg("![Vulkan] FreeShadowAtlasSlot: Could not find slot for light");
}

// Note: CreateSpotDescriptorSet() and UpdateSpotDescriptorSet() implementations
// are in vk_rendertarget_descriptors.cpp

} // namespace VK
