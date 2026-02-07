// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

// ============================================================================
// vk_ParticleEffect.cpp - Particle effect rendering implementation
// ============================================================================
//
// Implements billboard-based particle rendering using PAPI particle data.
// Ported from DX11 ParticleEffect.cpp with Vulkan adaptations:
// - 6 verts per particle (triangle list, no index buffer)
// - FVF::LIT vertex format (24 bytes, same layout as VkParticleVertex)
// - Viewport Y-flip for Vulkan coordinate system
//
// ============================================================================

#include "stdafx.h"
#include "vk_ParticleEffect.h"
#include "vk_ParticlePipeline.h"
#include "vk_ParticleDescriptors.h"
#include "vk_R_Backend.h"       // RCache
#include "rvk.h"                // RImplementation
#include "HW_Vulkan.h"
#include "vk_swapchain.h"
#include "vk_texture.h"
#include <array>

using namespace PAPI;
using namespace PS;

// ============================================================================
// Birth/Death callbacks (same as DX11)
// ============================================================================
static void OnEffectParticleBirth_vk(void* owner, u32, PAPI::Particle& m, u32)
{
    vkCParticleEffect* PE = static_cast<vkCParticleEffect*>(owner);
    if (!PE) return;
    PS::CPEDef* PED = PE->GetDefinition();
    if (PED)
    {
        if (PED->m_Flags.is(CPEDef::dfRandomFrame))
            m.frame = (u16)iFloor(Random.randI(PED->m_Frame.m_iFrameCount) * 255.f);
        if (PED->m_Flags.is(CPEDef::dfAnimated) && PED->m_Flags.is(CPEDef::dfRandomPlayback) && Random.randI(2))
            m.flags.set(Particle::ANIMATE_CCW, TRUE);
    }
}

static void OnEffectParticleDead_vk(void*, u32, PAPI::Particle&, u32)
{
}

// ============================================================================
// VkParticleVertex implementation (kept for pipeline vertex description)
// ============================================================================

VkVertexInputBindingDescription VkParticleVertex::GetBindingDescription()
{
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(VkParticleVertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return bindingDescription;
}

std::array<VkVertexInputAttributeDescription, 3> VkParticleVertex::GetAttributeDescriptions()
{
    std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};

    // Position
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(VkParticleVertex, pos);

    // Color (D3DCOLOR = ARGB packed as u32 → BGRA bytes on little-endian)
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_B8G8R8A8_UNORM;
    attributeDescriptions[1].offset = offsetof(VkParticleVertex, color);

    // UV
    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(VkParticleVertex, uv);

    return attributeDescriptions;
}

// ============================================================================
// FillSprite6 - Generate 6 vertices (2 triangles) for one billboard
// ============================================================================
// Adapted from DX11 FillSprite_fpu which generates 4 verts (quad strip).
// We output 6 verts as triangle list: (d,a,c) and (a,b,c).
IC void FillSprite6(FVF::LIT*& pv, const Fvector& T, const Fvector& R,
                    const Fvector& pos, const Fvector2& lt, const Fvector2& rb,
                    float r1, float r2, u32 clr, float angle)
{
    float sa = _sin(angle);
    float ca = _cos(angle);

    Fvector Vr, Vt;
    Vr.x = T.x * r1 * sa + R.x * r1 * ca;
    Vr.y = T.y * r1 * sa + R.y * r1 * ca;
    Vr.z = T.z * r1 * sa + R.z * r1 * ca;

    Vt.x = T.x * r2 * ca - R.x * r2 * sa;
    Vt.y = T.y * r2 * ca - R.y * r2 * sa;
    Vt.z = T.z * r2 * ca - R.z * r2 * sa;

    Fvector a, b, c, d;
    a.sub(Vt, Vr);
    b.add(Vt, Vr);
    c.invert(a);
    d.invert(b);

    // DX11 quad order: d(lt.x,rb.y), a(lt.x,lt.y), c(rb.x,rb.y), b(rb.x,lt.y)
    // Triangle 1: d, a, c
    pv->set(d.x + pos.x, d.y + pos.y, d.z + pos.z, clr, lt.x, rb.y); pv++;
    pv->set(a.x + pos.x, a.y + pos.y, a.z + pos.z, clr, lt.x, lt.y); pv++;
    pv->set(c.x + pos.x, c.y + pos.y, c.z + pos.z, clr, rb.x, rb.y); pv++;
    // Triangle 2: a, b, c
    pv->set(a.x + pos.x, a.y + pos.y, a.z + pos.z, clr, lt.x, lt.y); pv++;
    pv->set(b.x + pos.x, b.y + pos.y, b.z + pos.z, clr, rb.x, lt.y); pv++;
    pv->set(c.x + pos.x, c.y + pos.y, c.z + pos.z, clr, rb.x, rb.y); pv++;
}

// Overload for align-to-path: takes pos + direction, computes R from cross product
IC void FillSprite6(FVF::LIT*& pv, const Fvector& pos, const Fvector& dir,
                    const Fvector2& lt, const Fvector2& rb,
                    float r1, float r2, u32 clr, float angle)
{
    const Fvector& T = dir;
    Fvector R;
    R.crossproduct(T, RDEVICE.vCameraDirection).normalize_safe();
    FillSprite6(pv, T, R, pos, lt, rb, r1, r2, clr, angle);
}

// ============================================================================
// vkCParticleEffect - Constructor
// ============================================================================
vkCParticleEffect::vkCParticleEffect()
    : m_Def(nullptr),
      m_HandleEffect(-1),
      m_HandleActionList(-1),
      m_dynamicVB(nullptr),
      m_maxParticles(10000),
      m_fElapsedLimit(0.f),
      m_MemDT(0),
      m_CollisionCallback(nullptr),
      m_DestroyCallback(nullptr)
{
    Type = MT_PARTICLE_EFFECT;
    m_XFORM.identity();
    m_InitialPosition.set(0, 0, 0);

    // Create PAPI effect and action list
    m_HandleEffect = ParticleManager()->CreateEffect(1);
    VERIFY(m_HandleEffect >= 0);
    m_HandleActionList = ParticleManager()->CreateActionList();
    VERIFY(m_HandleActionList >= 0);

    // Initialize visibility data
    vis.box.set(Fvector{-10, -10, -10}, Fvector{10, 10, 10});
    vis.sphere.P.set(0, 0, 0);
    vis.sphere.R = 10.f;
}

// ============================================================================
// vkCParticleEffect - Destructor
// ============================================================================
vkCParticleEffect::~vkCParticleEffect()
{
    OnDeviceDestroy();
    ParticleManager()->DestroyEffect(m_HandleEffect);
    ParticleManager()->DestroyActionList(m_HandleActionList);
    m_HandleEffect = -1;
    m_HandleActionList = -1;
}

// ============================================================================
// Compile - Initialize from definition
// ============================================================================
BOOL vkCParticleEffect::Compile(PS::CPEDef* def)
{
    m_Def = def;
    if (!m_Def) {
        Msg("![Vulkan] Cannot compile particle effect: definition is nullptr");
        return FALSE;
    }

    m_maxParticles = (def->m_MaxParticles > 0) ? def->m_MaxParticles : 1000;

    // Load actions from definition into PAPI
    IReader F(m_Def->m_Actions.pointer(), m_Def->m_Actions.size());
    ParticleManager()->LoadActions(m_HandleActionList, F);
    ParticleManager()->SetMaxParticles(m_HandleEffect, m_Def->m_MaxParticles);
    ParticleManager()->SetCallback(m_HandleEffect,
        OnEffectParticleBirth_vk, OnEffectParticleDead_vk, this, 0);

    // Time limit
    if (m_Def->m_Flags.is(CPEDef::dfTimeLimit))
        m_fElapsedLimit = m_Def->m_fTimeLimit;

    return TRUE;
}

// ============================================================================
// Play - Start playing the effect
// ============================================================================
void vkCParticleEffect::Play()
{
    m_RT_Flags.set(flRT_DeferredStop, FALSE);
    m_RT_Flags.set(flRT_Playing, TRUE);
    ParticleManager()->PlayEffect(m_HandleEffect, m_HandleActionList);
}

// ============================================================================
// Stop - Stop playing the effect
// ============================================================================
void vkCParticleEffect::Stop(BOOL bDeferredStop)
{
    ParticleManager()->StopEffect(m_HandleEffect, m_HandleActionList, bDeferredStop);
    if (bDeferredStop) {
        m_RT_Flags.set(flRT_DeferredStop, TRUE);
    } else {
        m_RT_Flags.set(flRT_Playing, FALSE);
    }
}

// ============================================================================
// OnFrame - Update with elapsed time (PAPI step-based, ported from DX11)
// ============================================================================
void vkCParticleEffect::OnFrame(u32 frame_dt)
{
    if (m_Def && m_RT_Flags.is(flRT_Playing))
    {
        m_MemDT += frame_dt;

        int StepCount = 0;
        u32 uDT_STEP = m_Def->GetUStep();
        float fDT_STEP = m_Def->GetFStep();
        if (m_MemDT >= (s32)uDT_STEP)
        {
            StepCount = m_MemDT / uDT_STEP;
            m_MemDT = m_MemDT % uDT_STEP;
            clamp(StepCount, 0, 3);
        }

        for (; StepCount; StepCount--)
        {
            if (m_Def->m_Flags.is(CPEDef::dfTimeLimit))
            {
                if (!m_RT_Flags.is(flRT_DeferredStop))
                {
                    m_fElapsedLimit -= fDT_STEP;
                    if (m_fElapsedLimit < 0.f)
                    {
                        m_fElapsedLimit = m_Def->m_fTimeLimit;
                        Stop(true);
                        break;
                    }
                }
            }
            ParticleManager()->Update(m_HandleEffect, m_HandleActionList, fDT_STEP);

            PAPI::Particle* particles;
            u32 p_cnt;
            ParticleManager()->GetParticles(m_HandleEffect, particles, p_cnt);

            // Execute frame animation and collision
            if (m_Def->m_Flags.is(CPEDef::dfFramed | CPEDef::dfAnimated))
                m_Def->ExecuteAnimate(particles, p_cnt, fDT_STEP);
            if (m_Def->m_Flags.is(CPEDef::dfCollision))
                m_Def->ExecuteCollision(particles, p_cnt, fDT_STEP, nullptr, m_CollisionCallback);

            // Update bounding box
            if (p_cnt)
            {
                vis.box.invalidate();
                float p_size = 0.f;
                for (u32 i = 0; i < p_cnt; i++)
                {
                    Particle& m = particles[i];
                    vis.box.modify((Fvector&)m.pos);
                    if (m.size.x > p_size) p_size = m.size.x;
                    if (m.size.y > p_size) p_size = m.size.y;
                    if (m.size.z > p_size) p_size = m.size.z;
                }
                vis.box.grow(p_size);
                vis.box.getsphere(vis.sphere.P, vis.sphere.R);
            }
            if (m_RT_Flags.is(flRT_DeferredStop) && (0 == p_cnt))
            {
                m_RT_Flags.set(flRT_Playing | flRT_DeferredStop, FALSE);
                break;
            }
        }
    }
    else
    {
        vis.box.set(m_InitialPosition, m_InitialPosition);
        vis.box.grow(EPS_L);
        vis.box.getsphere(vis.sphere.P, vis.sphere.R);
    }
}

// ============================================================================
// UpdateParent - Set world transform (ported from DX11)
// ============================================================================
void vkCParticleEffect::UpdateParent(const Fmatrix& m, const Fvector& velocity, BOOL bXFORM)
{
    static u32 s_upDiag = 0;
    if (s_upDiag < 10) {
        s_upDiag++;
        Msg("[PE-UPDATEPARENT] pos=(%.1f,%.1f,%.1f) bXFORM=%d handle=%d actionList=%d",
            m.c.x, m.c.y, m.c.z, (int)bXFORM, m_HandleEffect, m_HandleActionList);
    }
    m_RT_Flags.set(flRT_XFORM, bXFORM);
    if (bXFORM) {
        m_XFORM.set(m);
    } else {
        m_InitialPosition = m.c;
        ParticleManager()->Transform(m_HandleActionList, m, velocity);
    }
}

// ============================================================================
// SetBirthDeadCB
// ============================================================================
void vkCParticleEffect::SetBirthDeadCB(PAPI::OnBirthParticleCB bc, PAPI::OnDeadParticleCB dc, void* owner, u32 p)
{
    ParticleManager()->SetCallback(m_HandleEffect, bc, dc, owner, p);
}

// ============================================================================
// Render - Main rendering method
// ============================================================================
void vkCParticleEffect::Render(float LOD)
{
    // Get PAPI particle data
    PAPI::Particle* particles;
    u32 p_cnt;
    ParticleManager()->GetParticles(m_HandleEffect, particles, p_cnt);

    if (p_cnt == 0) return;
    if (!m_Def || !m_Def->m_Flags.is(CPEDef::dfSprite)) return;

    // Lazy-create pipeline on first render
    if (!IsPipelineReady()) {
        if (!CreatePipeline()) {
            return;
        }
    }

    // Lazy-load texture
    if (!m_textureLoaded) {
        LoadParticleTexture();
        m_textureLoaded = true;
    }

    // Ensure dynamic VB exists
    if (!m_dynamicVB) {
        OnDeviceCreate();
        if (!m_dynamicVB) return;
    }

    // Clamp to max
    if (p_cnt > m_maxParticles)
        p_cnt = m_maxParticles;

    // Allocate temporary vertex buffer for particle quads
    u32 vertexCount = p_cnt * 6;  // 6 vertices per particle (2 triangles)
    FVF::LIT* pv_start = (FVF::LIT*)_alloca(vertexCount * sizeof(FVF::LIT));
    FVF::LIT* pv = pv_start;

    // Generate billboard quads from real PAPI data
    GenerateBillboardQuads(pv_start, p_cnt, particles);

    // HUD mode handling
    Fmatrix FTold;
    bool bHudMode = !!GetHudMode();
    if (bHudMode)
    {
        FTold = Device.mFullTransform;
        Device.mFullTransform = Device.mFullTransformHud;
        RCache.set_xform_project(Device.mProjectHud);
        RImplementation.rmNear();
    }

    // Upload to dynamic buffer (FVF::LIT and VkParticleVertex have identical layout)
    UpdateDynamicBuffer(pv_start, vertexCount);

    // Get current command buffer
    VkCommandBuffer cmd = RCache.GetCommandBuffer();
    if (cmd == VK_NULL_HANDLE) {
        if (bHudMode) {
            RImplementation.rmNormal();
            Device.mFullTransform = FTold;
            RCache.set_xform_project(Device.mProject);
        }
        return;
    }

    // Skip draw if no texture bound (null descriptor set)
    if (m_descriptorSet == VK_NULL_HANDLE) {
        if (bHudMode) {
            RImplementation.rmNormal();
            Device.mFullTransform = FTold;
            RCache.set_xform_project(Device.mProject);
        }
        return;
    }

    // Bind all resources and draw
    BindResources(cmd);
    vkCmdDraw(cmd, vertexCount, 1, 0, 0);

    // Restore projection if HUD mode
    if (bHudMode)
    {
        RImplementation.rmNormal();
        Device.mFullTransform = FTold;
        RCache.set_xform_project(Device.mProject);
    }
}

// ============================================================================
// GetTimeLimit
// ============================================================================
float vkCParticleEffect::GetTimeLimit()
{
    if (!m_Def) return -1.f;
    return m_Def->m_Flags.is(CPEDef::dfTimeLimit) ? m_Def->m_fTimeLimit : -1.f;
}

// ============================================================================
// Name - Get effect name
// ============================================================================
const shared_str vkCParticleEffect::Name()
{
    if (!m_Def) return shared_str("");
    return m_Def->m_Name;
}

// ============================================================================
// ParticlesCount - Get current particle count from PAPI
// ============================================================================
u32 vkCParticleEffect::ParticlesCount()
{
    return ParticleManager()->GetParticlesCount(m_HandleEffect);
}

// ============================================================================
// OnDeviceCreate - Initialize Vulkan resources
// ============================================================================
void vkCParticleEffect::OnDeviceCreate()
{
    if (!m_Def) return;

    // Create dynamic vertex buffer for particles
    // Each particle = 6 vertices (2 triangles), using FVF::LIT (24 bytes)
    u32 maxVertices = m_maxParticles * 6;
    u32 bufferSize = maxVertices * sizeof(FVF::LIT);

    if (bufferSize > 0 && !m_dynamicVB) {
        m_dynamicVB = xr_new<VK::CVulkanBuffer>();

        m_dynamicVB->Create(
            bufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU
        );

        if (!m_dynamicVB->IsValid()) {
            Msg("![Vulkan] Failed to create particle vertex buffer");
            xr_delete(m_dynamicVB);
        }
    }
}

// ============================================================================
// OnDeviceDestroy - Device destruction
// ============================================================================
void vkCParticleEffect::OnDeviceDestroy()
{
    DestroyPipeline();
    DestroyDescriptorSet();
    if (m_texture) {
        m_texture->Destroy();
        xr_delete(m_texture);
    }
    m_textureLoaded = false;
    if (m_dynamicVB) {
        xr_delete(m_dynamicVB);
    }
}

// ============================================================================
// CreatePipeline - Create particle rendering pipeline on-demand
// ============================================================================
bool vkCParticleEffect::CreatePipeline()
{
    if (m_pipeline != VK_NULL_HANDLE) return true;

    VkDevice device = VulkanHW.GetDevice();
    if (device == VK_NULL_HANDLE) return false;

    // Create descriptor set layout for particle texture
    if (m_descriptorSetLayout == VK_NULL_HANDLE) {
        if (!vkParticleCustom::CreateDescriptorSetLayout(device, m_descriptorSetLayout)) {
            Msg("![Vulkan] Failed to create particle descriptor set layout");
            return false;
        }
    }

    // Configure pipeline - must match forward phase render target
    ParticlePipelineConfig config;
    config.colorFormat = Swapchain.GetFormat();
    config.depthFormat = Swapchain.m_DepthFormat;
    config.depthTest = true;     // Read depth to avoid rendering behind geometry
    config.depthWrite = false;   // Don't write depth (transparent)
    config.cullMode = VK_CULL_MODE_NONE;
    config.srcBlend = VK_BLEND_FACTOR_SRC_ALPHA;
    config.dstBlend = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    config.blendOp = VK_BLEND_OP_ADD;

    vkParticlePipeline* pipelineHelper = xr_new<vkParticlePipeline>();
    if (!pipelineHelper->Create(device, config, m_descriptorSetLayout)) {
        Msg("![Vulkan] Failed to create particle pipeline");
        xr_delete(pipelineHelper);
        return false;
    }

    m_pipeline = pipelineHelper->pipeline;
    m_pipelineLayout = pipelineHelper->pipelineLayout;

    Msg("[Vulkan] Particle pipeline created successfully");
    return true;
}

// ============================================================================
// LoadParticleTexture - Load texture from particle definition
// ============================================================================
bool vkCParticleEffect::LoadParticleTexture()
{
    if (!m_Def || !m_Def->m_TextureName.size()) return false;

    VkDevice device = VulkanHW.GetDevice();
    if (device == VK_NULL_HANDLE) return false;

    // Ensure descriptor pool is initialized
    if (!vkParticleDescriptorPool::GetPool()) {
        vkParticleDescriptorPool::Initialize(device);
    }

    // Ensure descriptor set layout exists
    if (m_descriptorSetLayout == VK_NULL_HANDLE) {
        vkParticleCustom::CreateDescriptorSetLayout(device, m_descriptorSetLayout);
    }

    // Load DDS texture via VFS
    m_texture = xr_new<VK::CVulkanTexture>();
    string_path fn, texturePath;
    xr_sprintf(fn, sizeof(fn), "%s.dds", m_Def->m_TextureName.c_str());
    FS.update_path(texturePath, "$game_textures$", fn);

    if (!m_texture->LoadDDS(texturePath)) {
        Msg("![Vulkan] Particle texture not found: '%s'", texturePath);
        m_texture->Destroy();
        xr_delete(m_texture);
        return false;
    }

    // Allocate descriptor set
    m_descriptorSet = vkParticleDescriptorPool::Allocate(device, m_descriptorSetLayout);
    if (m_descriptorSet == VK_NULL_HANDLE) {
        Msg("![Vulkan] Failed to allocate particle descriptor set for '%s'", m_Def->m_TextureName.c_str());
        m_texture->Destroy();
        xr_delete(m_texture);
        return false;
    }

    // Update descriptor set with texture binding
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = m_texture->GetView();
    imageInfo.sampler = m_texture->GetSampler();

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);

    Msg("[Vulkan] Particle texture loaded: '%s'", m_Def->m_TextureName.c_str());
    return true;
}

// ============================================================================
// UpdateDynamicBuffer - Update vertex buffer with particle data
// ============================================================================
void vkCParticleEffect::UpdateDynamicBuffer(FVF::LIT* data, u32 vertexCount)
{
    if (!m_dynamicVB || vertexCount == 0) return;

    u32 requiredSize = vertexCount * sizeof(FVF::LIT);
    if (requiredSize > m_dynamicVB->GetSize()) return;

    void* mappedData = m_dynamicVB->Map();
    if (mappedData) {
        memcpy(mappedData, data, requiredSize);
        m_dynamicVB->Unmap();
    }
}

// ============================================================================
// BindResources - Bind resources to command buffer
// ============================================================================
void vkCParticleEffect::BindResources(VkCommandBuffer cmd)
{
    if (m_pipeline == VK_NULL_HANDLE) return;

    // Bind pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    // Bind vertex buffer
    if (m_dynamicVB) {
        VkBuffer vertexBuffers[] = { m_dynamicVB->GetHandle() };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    }

    // Bind descriptor set (texture)
    if (m_descriptorSet != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(
            cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
            0, 1, &m_descriptorSet, 0, nullptr);
    }

    // Set push constants (view-projection matrix)
    // Vertex positions are already in world space:
    // - non-XFORM: PAPI stores world-space positions via Transform()
    // - XFORM: GenerateBillboardQuads() transforms by m_XFORM on CPU
    Fmatrix viewProj;
    viewProj.mul(Device.mProject, Device.mView);
    vkCmdPushConstants(
        cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
        0, sizeof(Fmatrix), &viewProj);

    // Set viewport with Y-flip for Vulkan
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = (float)Device.dwHeight;
    viewport.width = (float)Device.dwWidth;
    viewport.height = -(float)Device.dwHeight;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {Device.dwWidth, Device.dwHeight};
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

// ============================================================================
// GenerateBillboardQuads - Generate billboard geometry from PAPI particles
// ============================================================================
// Ported from DX11 ParticleRenderStream() with 6-vert triangle list output.
void vkCParticleEffect::GenerateBillboardQuads(FVF::LIT* pv, u32 p_cnt,
                                                PAPI::Particle* particles)
{
    if (!pv || p_cnt == 0 || !particles) return;

    for (u32 i = 0; i < p_cnt; i++)
    {
        PAPI::Particle& m = particles[i];
        Fvector2 lt, rb;
        lt.set(0.f, 0.f);
        rb.set(1.f, 1.f);

        // Frame animation
        if (m_Def->m_Flags.is(CPEDef::dfFramed))
            m_Def->m_Frame.CalculateTC(iFloor(float(m.frame) / 255.f), lt, rb);

        float r_x = m.size.x * 0.5f;
        float r_y = m.size.y * 0.5f;
        float speed = 0.f;
        bool speed_calculated = false;

        // Velocity scaling
        if (m_Def->m_Flags.is(CPEDef::dfVelocityScale))
        {
            speed = m.vel.magnitude();
            speed_calculated = true;
            r_x += speed * m_Def->m_VelocityScale.x;
            r_y += speed * m_Def->m_VelocityScale.y;
        }

        u32 clr = color_rgba_f(m.colorR, m.colorG, m.colorB, m.colorA);

        if (m_Def->m_Flags.is(CPEDef::dfAlignToPath))
        {
            if (!speed_calculated)
                speed = m.vel.magnitude();

            if ((speed < EPS_S) && m_Def->m_Flags.is(CPEDef::dfWorldAlign))
            {
                // World-aligned (stationary particles)
                Fmatrix M;
                M.setXYZ(m_Def->m_APDefaultRotation);
                if (m_RT_Flags.is(flRT_XFORM))
                {
                    Fvector p;
                    m_XFORM.transform_tiny(p, m.pos);
                    M.mulA_43(m_XFORM);
                    FillSprite6(pv, M.k, M.i, p, lt, rb, r_x, r_y, clr, m.rot.x);
                }
                else
                {
                    FillSprite6(pv, M.k, M.i, m.pos, lt, rb, r_x, r_y, clr, m.rot.x);
                }
            }
            else if ((speed >= EPS_S) && m_Def->m_Flags.is(CPEDef::dfFaceAlign))
            {
                // Face-aligned (oriented along velocity)
                Fmatrix M;
                M.identity();
                M.k.div(m.vel, speed);
                M.j.set(0, 1, 0);
                if (_abs(M.j.dotproduct(M.k)) > .99f)
                    M.j.set(0, 0, 1);
                M.i.crossproduct(M.j, M.k); M.i.normalize();
                M.j.crossproduct(M.k, M.i); M.j.normalize();
                if (m_RT_Flags.is(flRT_XFORM))
                {
                    Fvector p;
                    m_XFORM.transform_tiny(p, m.pos);
                    M.mulA_43(m_XFORM);
                    FillSprite6(pv, M.j, M.i, p, lt, rb, r_x, r_y, clr, m.rot.x);
                }
                else
                {
                    FillSprite6(pv, M.j, M.i, m.pos, lt, rb, r_x, r_y, clr, m.rot.x);
                }
            }
            else
            {
                // Align to path (direction-based billboard)
                Fvector dir;
                if (speed >= EPS_S)
                    dir.div(m.vel, speed);
                else
                    dir.setHP(-m_Def->m_APDefaultRotation.y, -m_Def->m_APDefaultRotation.x);
                if (m_RT_Flags.is(flRT_XFORM))
                {
                    Fvector p, d;
                    m_XFORM.transform_tiny(p, m.pos);
                    m_XFORM.transform_dir(d, dir);
                    FillSprite6(pv, p, d, lt, rb, r_x, r_y, clr, m.rot.x);
                }
                else
                {
                    FillSprite6(pv, m.pos, dir, lt, rb, r_x, r_y, clr, m.rot.x);
                }
            }
        }
        else
        {
            // Default: camera-facing billboard
            if (m_RT_Flags.is(flRT_XFORM))
            {
                Fvector p;
                m_XFORM.transform_tiny(p, m.pos);
                FillSprite6(pv, RDEVICE.vCameraTop, RDEVICE.vCameraRight, p,
                            lt, rb, r_x, r_y, clr, m.rot.x);
            }
            else
            {
                FillSprite6(pv, RDEVICE.vCameraTop, RDEVICE.vCameraRight, m.pos,
                            lt, rb, r_x, r_y, clr, m.rot.x);
            }
        }
    }
}
