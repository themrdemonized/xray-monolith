// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

// Vulkan Wallmarks Engine implementation
// Geometry generation is ported from shared WallmarksEngine.cpp
// Rendering uses Vulkan dynamic vertex buffers instead of DX11 RCache

#include "stdafx.h"
#include "vk_WallmarksEngine.h"

#include "rvk.h"
#include "HW_Vulkan.h"
#include "vk_buffer.h"
#include "vk_texture.h"
#include "vk_Visual.h"
#include "vk_swapchain.h"
#include "vk_R_Backend.h"

#include <fstream>
#include <vector>
#include "../../xrEngine/xr_object.h"
#include "../../xrEngine/x_ray.h"
#include "../../xrEngine/GameFont.h"
#include "../../xrEngine/IGame_Level.h"

// Include CSkeletonWallmark
#define FBasicVisualH
#define dxRender_Visual vkRender_Visual
#include "../xrRender/SkeletonCustom.h"
#undef dxRender_Visual
#undef FBasicVisualH

// Console variables
extern float ps_r__WallmarkTTL;
extern float ps_r__WallmarkSHIFT;
extern float ps_r__WallmarkSHIFT_V;

// Constants
static const float wallmark_range_static = 100.f;
static const float wallmark_range_skeleton = 50.f;
static const float W_DIST_FADE = 15.f;
static const float W_DIST_FADE_SQR = W_DIST_FADE * W_DIST_FADE;
static const float I_DIST_FADE_SQR = 1.f / W_DIST_FADE_SQR;
static const int MAX_TRIS = 1024 * 16;

extern float r_ssaDISCARD;

// ============================================================================
// Slot comparison
// ============================================================================
IC bool operator==(const vkCWallmarksEngine::wm_slot* slot, const ref_shader& shader)
{
    return slot->shader == shader;
}

// ============================================================================
// Slot management
// ============================================================================
vkCWallmarksEngine::wm_slot* vkCWallmarksEngine::FindSlot(ref_shader shader)
{
    WMSlotVecIt it = std::find(marks.begin(), marks.end(), shader);
    return (it != marks.end()) ? *it : 0;
}

vkCWallmarksEngine::wm_slot* vkCWallmarksEngine::AppendSlot(ref_shader shader)
{
    marks.push_back(xr_new<wm_slot>(shader));
    return marks.back();
}

// ============================================================================
// Construction / Destruction
// ============================================================================
vkCWallmarksEngine::vkCWallmarksEngine()
#ifdef PROFILE_CRITICAL_SECTIONS
    : lock(MUTEX_PROFILE_ID(vkCWallmarksEngine))
#endif
{
    static_pool.reserve(256);
    marks.reserve(256);
}

vkCWallmarksEngine::~vkCWallmarksEngine()
{
    clear();
    DestroyVulkanResources();
}

void vkCWallmarksEngine::clear()
{
    {
        for (WMSlotVecIt p_it = marks.begin(); p_it != marks.end(); p_it++)
        {
            for (StaticWMVecIt m_it = (*p_it)->static_items.begin(); m_it != (*p_it)->static_items.end(); m_it++)
                static_wm_destroy(*m_it);
            xr_delete(*p_it);
        }
        marks.clear();
    }
    {
        for (u32 it = 0; it < static_pool.size(); it++)
            xr_delete(static_pool[it]);
        static_pool.clear();
    }
}

// ============================================================================
// Static wallmark pool
// ============================================================================
vkCWallmarksEngine::static_wallmark* vkCWallmarksEngine::static_wm_allocate()
{
    static_wallmark* W = 0;
    if (static_pool.empty())
        W = xr_new<static_wallmark>();
    else
    {
        W = static_pool.back();
        static_pool.pop_back();
    }

    W->m_fTimeStart = RDEVICE.fTimeGlobal;
    W->m_fTimeEnd = ps_r__WallmarkTTL * 15.f;
    W->verts.clear();
    return W;
}

void vkCWallmarksEngine::static_wm_destroy(vkCWallmarksEngine::static_wallmark* W)
{
    static_pool.push_back(W);
}

void vkCWallmarksEngine::static_wm_render(vkCWallmarksEngine::static_wallmark* W, FVF::LIT*& V)
{
    float a = W->TimeEnd() == -1.f ? 0.f : (RDEVICE.fTimeGlobal - W->TimeStart()) / W->TimeEnd();
    int aC = iFloor(a * 255.f);
    clamp(aC, 0, 255);
    // aC is fade progress (0=fresh, 255=expired). Invert for alpha (255=opaque, 0=transparent)
    u32 C = color_rgba(128, 128, 128, 255 - aC);
    FVF::LIT* S = &*W->verts.begin();
    FVF::LIT* E = &*W->verts.end();
    for (; S != E; S++, V++)
    {
        V->p.set(S->p);
        V->color = C;
        V->t.set(S->t);
    }
}

// ============================================================================
// Skeleton wallmark rendering
// ============================================================================
void vkCWallmarksEngine::skeleton_wm_render(intrusive_ptr<CSkeletonWallmark> wm, FVF::LIT*& V)
{
    if (!wm || !wm->Parent()) return;

    // Calculate alpha fade based on lifetime
    float a = wm->TimeEnd() == -1.f ? 0.f : (RDEVICE.fTimeGlobal - wm->TimeStart()) / wm->TimeEnd();
    int aC = iFloor(a * 255.f);
    clamp(aC, 0, 255);
    // aC is fade progress (0=fresh, 255=expired). Invert for alpha (255=opaque, 0=transparent)
    u32 C = color_rgba(128, 128, 128, 255 - aC);

    // Render wallmark through parent skeleton (CKinematics)
    // This will transform vertices by bone matrices and write to V
    FVF::LIT* w_save = V;
    try
    {
        wm->Parent()->RenderWallmark(wm, V);

        // Apply alpha to all generated vertices
        for (FVF::LIT* it = w_save; it != V; it++)
        {
            it->color = C;
        }
    }
    catch (...)
    {
        Msg("! Failed to render skeleton wallmark");
        V = w_save;  // Restore pointer on error
    }
}

// ============================================================================
// Geometry generation (CPU) - ported from shared WallmarksEngine.cpp
// ============================================================================
void vkCWallmarksEngine::RecurseTri(u32 t, Fmatrix& mView, vkCWallmarksEngine::static_wallmark& W)
{
    CDB::TRI* T = sml_collector.getT() + t;
    if (T->dummy) return;
    T->dummy = 0xffffffff;

    // Some vars
    u32* v_ids = T->verts;
    Fvector* v_data = sml_collector.getV();
    sml_poly_src.clear();
    sml_poly_src.push_back(v_data[v_ids[0]]);
    sml_poly_src.push_back(v_data[v_ids[1]]);
    sml_poly_src.push_back(v_data[v_ids[2]]);
    sml_poly_dest.clear();

    sPoly* P = sml_clipper.ClipPoly(sml_poly_src, sml_poly_dest);

    if (P)
    {
        // Create vertices and triangulate poly (tri-fan style triangulation)
        FVF::LIT V0, V1, V2;
        Fvector UV;

        mView.transform_tiny(UV, (*P)[0]);
        V0.set((*P)[0], 0, (1 + UV.x) * .5f, (1 - UV.y) * .5f);
        mView.transform_tiny(UV, (*P)[1]);
        V1.set((*P)[1], 0, (1 + UV.x) * .5f, (1 - UV.y) * .5f);

        for (u32 i = 2; i < P->size(); i++)
        {
            mView.transform_tiny(UV, (*P)[i]);
            V2.set((*P)[i], 0, (1 + UV.x) * .5f, (1 - UV.y) * .5f);
            W.verts.push_back(V0);
            W.verts.push_back(V1);
            W.verts.push_back(V2);
            V1 = V2;
        }

        // recurse through adjacent triangles
        for (u32 i = 0; i < 3; i++)
        {
            u32 adj = sml_adjacency[3 * t + i];
            if (0xffffffff == adj) continue;
            CDB::TRI* SML = sml_collector.getT() + adj;
            v_ids = SML->verts;

            Fvector test_normal;
            test_normal.mknormal(v_data[v_ids[0]], v_data[v_ids[1]], v_data[v_ids[2]]);
            float cosa = test_normal.dotproduct(sml_normal);
            if (cosa < 0.034899f) continue; // cos(88)
            RecurseTri(adj, mView, W);
        }
    }
}

void vkCWallmarksEngine::BuildMatrix(Fmatrix& mView, float invsz, const Fvector& from)
{
    // build projection
    Fmatrix mScale;
    Fvector at, up, right, y;
    at.sub(from, sml_normal);
    y.set(0, 1, 0);
    if (_abs(sml_normal.y) > .99f) y.set(1, 0, 0);
    right.crossproduct(y, sml_normal);
    up.crossproduct(sml_normal, right);
    mView.build_camera(from, at, up);
    mScale.scale(invsz, invsz, invsz);
    mView.mulA_43(mScale);
}

void vkCWallmarksEngine::AddWallmark_internal(CDB::TRI* pTri, const Fvector* pVerts,
    const Fvector& contact_point, ref_shader hShader, float sz, float ttl, float rotation)
{
    // query for polygons in bounding box and calculate adjacency
    {
        Fbox bb_query;
        Fvector bbc, bbd;
        bb_query.set(contact_point, contact_point);
        bb_query.grow(sz * 2.5f);
        bb_query.get_CD(bbc, bbd);
        xrc.box_options(CDB::OPT_FULL_TEST);
        xrc.box_query(g_pGameLevel->ObjectSpace.GetStaticModel(), bbc, bbd);
        u32 triCount = xrc.r_count();
        if (0 == triCount)
            return;

        CDB::TRI* tris = g_pGameLevel->ObjectSpace.GetStaticTris();
        sml_collector.clear();
        sml_collector.add_face_packed_D(pVerts[pTri->verts[0]], pVerts[pTri->verts[1]], pVerts[pTri->verts[2]], 0);
        for (u32 t = 0; t < triCount; t++)
        {
            CDB::TRI* T = tris + xrc.r_begin()[t].id;
            if (T == pTri) continue;
            sml_collector.add_face_packed_D(pVerts[T->verts[0]], pVerts[T->verts[1]], pVerts[T->verts[2]], 0);
        }
        sml_collector.calc_adjacency(sml_adjacency);
    }

    // calc face normal
    Fvector N;
    N.mknormal(pVerts[pTri->verts[0]], pVerts[pTri->verts[1]], pVerts[pTri->verts[2]]);
    sml_normal.set(N);

    // build 3D ortho-frustum
    Fmatrix mView, mRot;
    BuildMatrix(mView, 1 / sz, contact_point);
    mRot.rotateZ(deg2rad(rotation));
    mView.mulA_43(mRot);
    sml_clipper.CreateFromMatrix(mView, FRUSTUM_P_LRTB);

    // create wallmark
    static_wallmark* W = static_wm_allocate();
    if (ttl) W->m_fTimeEnd = ttl;
    RecurseTri(0, mView, *W);

    // calc bounding sphere
    if (W->verts.size() < 3)
    {
        static_wm_destroy(W);
        return;
    }
    else
    {
        Fbox bb;
        bb.invalidate();

        FVF::LIT* I = &*W->verts.begin();
        FVF::LIT* E = &*W->verts.end();
        for (; I != E; I++) bb.modify(I->p);
        bb.getsphere(W->bounds.P, W->bounds.R);
    }

    {
        // search if similar wallmark exists
        wm_slot* slot = FindSlot(hShader);
        if (slot)
        {
            StaticWMVecIt it = slot->static_items.begin();
            StaticWMVecIt end = slot->static_items.end();
            for (; it != end; it++)
            {
                static_wallmark* wm = *it;
                if (wm->bounds.P.similar(W->bounds.P, 0.02f))
                {
                    // replace
                    static_wm_destroy(wm);
                    *it = W;
                    return;
                }
            }
        }
        else
        {
            slot = AppendSlot(hShader);
        }

        // no similar - register new
        slot->static_items.push_back(W);
    }
}

void vkCWallmarksEngine::AddWallmark_internal(CDB::TRI* pTri, const Fvector* pVerts,
    const Fvector& contact_point, ref_shader hShader, float sz, float ttl, bool random_rotation)
{
    AddWallmark_internal(pTri, pVerts, contact_point, hShader, sz, ttl,
        random_rotation ? ::Random.randF(-20.f, 20.f) : 0.f);
}

// ============================================================================
// Public API - Add wallmarks
// ============================================================================
void vkCWallmarksEngine::AddStaticWallmark(CDB::TRI* pTri, const Fvector* pVerts,
    const Fvector& contact_point, ref_shader hShader, float sz, float ttl,
    bool ignore_opt, bool random_rotation)
{
    AddStaticWallmark(pTri, pVerts, contact_point, hShader, sz, ttl, ignore_opt,
        random_rotation ? ::Random.randF(-20.f, 20.f) : 0.f);
}

void vkCWallmarksEngine::AddStaticWallmark(CDB::TRI* pTri, const Fvector* pVerts,
    const Fvector& contact_point, ref_shader hShader, float sz, float ttl,
    bool ignore_opt, float rotation)
{
    static bool addOnce = false;
    if (!addOnce) {
        Msg("[Vulkan] Wallmarks::AddStaticWallmark() called! pos=(%.1f,%.1f,%.1f) sz=%.2f shader=%s",
            contact_point.x, contact_point.y, contact_point.z, sz,
            hShader._get() ? "valid" : "null");
        addOnce = true;
    }

    // optimization: don't allow wallmarks more than 100m from viewer/actor
    if (!ignore_opt && contact_point.distance_to_sqr(Device.vCameraPosition) > _sqr(wallmark_range_static))
        return;

    // Physics may add wallmarks in parallel with rendering
    lock.Enter();
    AddWallmark_internal(pTri, pVerts, contact_point, hShader, sz, ttl, rotation);
    lock.Leave();
}

void vkCWallmarksEngine::AddSkeletonWallmark(const Fmatrix* xf, CKinematics* obj, ref_shader& sh,
    const Fvector& start, const Fvector& dir, float size, float ttl, bool ignore_opt)
{
    if (::RImplementation.phase != CRender::PHASE_NORMAL) return;
    if (!obj || !xf || size <= EPS_L) return;
    // optimization: don't allow wallmarks more than 50m from viewer/actor
    if (!ignore_opt && xf->c.distance_to_sqr(Device.vCameraPosition) > _sqr(wallmark_range_skeleton)) return;

    lock.Enter();
    obj->AddWallmark(xf, start, dir, sh, size, ttl);
    lock.Leave();
}

void vkCWallmarksEngine::AddSkeletonWallmark(intrusive_ptr<CSkeletonWallmark> wm)
{
    if (::RImplementation.phase != CRender::PHASE_NORMAL) return;

    if (!::RImplementation.val_bHUD)
    {
        lock.Enter();
        // Search if similar wallmark exists
        wm_slot* slot = FindSlot(wm->Shader());
        if (0 == slot) slot = AppendSlot(wm->Shader());
        // No similar - register new
        slot->skeleton_items.push_back(wm);
#ifdef DEBUG
        wm->used_in_render = Device.dwFrame;
#endif
        lock.Leave();
    }
}

// ============================================================================
// Render all wallmarks (per-slot batching with individual textures)
// ============================================================================
void vkCWallmarksEngine::Render()
{
    // One-shot diagnostics
    static bool diagOnce = false;
    if (!diagOnce) {
        Msg("[Vulkan] Wallmarks::Render() called, marks.size()=%u", (u32)marks.size());
        diagOnce = true;
    }

    if (marks.empty())
        return;

    // Apply depth bias to prevent Z-fighting
    float _43 = Device.mProject._43;
    Device.mProject._43 -= ps_r__WallmarkSHIFT;

    Fmatrix mSavedView = Device.mView;
    Fvector mViewPos;
    mViewPos.mad(Device.vCameraPosition, Device.vCameraDirection, ps_r__WallmarkSHIFT_V);
    Device.mView.build_camera_dir(mViewPos, Device.vCameraDirection, Device.vCameraTop);

    Device.Statistic->RenderDUMP_WM.Begin();
    Device.Statistic->RenderDUMP_WMS_Count = 0;
    Device.Statistic->RenderDUMP_WMD_Count = 0;
    Device.Statistic->RenderDUMP_WMT_Count = 0;

    float ssaCLIP = r_ssaDISCARD / 4;

    lock.Enter();

    // Per-slot vertex collection and lifetime management
    for (WMSlotVecIt slot_it = marks.begin(); slot_it != marks.end(); slot_it++)
    {
        wm_slot* slot = *slot_it;

        // Static wallmarks: collect visible + cull expired
        for (StaticWMVecIt w_it = slot->static_items.begin(); w_it != slot->static_items.end();)
        {
            static_wallmark* W = *w_it;
            if (RImplementation.ViewBase.testSphere_dirty(W->bounds.P, W->bounds.R))
            {
                Device.Statistic->RenderDUMP_WMS_Count++;
                float dst = Device.vCameraPosition.distance_to_sqr(W->bounds.P);
                float ssa = W->bounds.R * W->bounds.R / dst;
                if (ssa >= ssaCLIP)
                {
                    Device.Statistic->RenderDUMP_WMT_Count += W->verts.size() / 3;
                }
            }

            if (W->TimeEnd() == -1.f)
            {
                w_it++;
                continue;
            }

            float w = (RDEVICE.fTimeGlobal - W->TimeStart()) / W->TimeEnd();
            if (w < 1.f)
            {
                w_it++;
            }
            else
            {
                static_wm_destroy(W);
                *w_it = slot->static_items.back();
                slot->static_items.pop_back();
            }
        }
    }

    lock.Leave();

    // Render all slots via Vulkan (per-slot texture batching)
    RenderSlots();

    // Level wallmarks
    RImplementation.r_dsgraph_render_wmarks();
    Device.Statistic->RenderDUMP_WM.End();

    // Restore projection
    Device.mView = mSavedView;
    Device.mProject._43 = _43;
}

// ============================================================================
// Vulkan Resource Management
// ============================================================================

bool vkCWallmarksEngine::InitVulkanResources()
{
    if (m_vulkanReady) return true;

    VkDevice device = VulkanHW.GetDevice();
    if (device == VK_NULL_HANDLE) return false;

    // Create descriptor set layout: 1 combined image sampler at binding 0
    {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
            Msg("![Vulkan] Wallmarks: failed to create descriptor layout");
            return false;
        }
    }

    // Create standalone descriptor pool (64 combined image samplers)
    {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = 64;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = 64;

        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
            Msg("![Vulkan] Wallmarks: failed to create descriptor pool");
            return false;
        }
    }

    // Create 1x1 white fallback texture
    m_whiteTexture = new VK::CVulkanTexture();
    u32 whitePixel = 0xFFFFFFFF;
    m_whiteTexture->CreateFromData(&whitePixel, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, sizeof(u32));

    // Allocate and bind white fallback descriptor set
    {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_descriptorSetLayout;

        if (vkAllocateDescriptorSets(device, &allocInfo, &m_whiteDescriptorSet) != VK_SUCCESS) {
            Msg("![Vulkan] Wallmarks: failed to allocate white descriptor set");
            m_whiteDescriptorSet = VK_NULL_HANDLE;
        }
    }

    if (m_whiteDescriptorSet != VK_NULL_HANDLE && m_whiteTexture->IsValid()) {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = m_whiteTexture->GetView();
        imageInfo.sampler = m_whiteTexture->GetSampler();

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = m_whiteDescriptorSet;
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
    }

    // Create dynamic vertex buffer
    if (!CreateDynamicBuffer()) {
        Msg("![Vulkan] Wallmarks: failed to create dynamic VB");
        return false;
    }

    m_vulkanReady = true;
    Msg("[Vulkan] Wallmarks: Vulkan resources initialized");
    return true;
}

void vkCWallmarksEngine::DestroyVulkanResources()
{
    VkDevice device = VulkanHW.GetDevice();

    // Destroy texture cache (descriptor sets freed when pool is destroyed)
    for (auto& pair : m_textureCache) {
        if (pair.second.texture) {
            pair.second.texture->Destroy();
            delete pair.second.texture;
        }
    }
    m_textureCache.clear();

    if (m_dynamicVB) {
        m_dynamicVB->Destroy();
        delete m_dynamicVB;
        m_dynamicVB = nullptr;
    }

    if (m_whiteTexture) {
        m_whiteTexture->Destroy();
        delete m_whiteTexture;
        m_whiteTexture = nullptr;
    }

    if (device != VK_NULL_HANDLE) {
        if (m_pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, m_pipeline, nullptr);
            m_pipeline = VK_NULL_HANDLE;
        }
        if (m_pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
            m_pipelineLayout = VK_NULL_HANDLE;
        }
        if (m_descriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
            m_descriptorPool = VK_NULL_HANDLE;
        }
        if (m_descriptorSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
            m_descriptorSetLayout = VK_NULL_HANDLE;
        }
    }

    m_whiteDescriptorSet = VK_NULL_HANDLE;
    m_vulkanReady = false;
}

bool vkCWallmarksEngine::CreateDynamicBuffer()
{
    m_dynamicVB = new VK::CVulkanBuffer();
    m_dynamicVB->Create(
        MAX_WM_VERTS * sizeof(FVF::LIT),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_HOST);

    if (!m_dynamicVB->IsValid()) {
        delete m_dynamicVB;
        m_dynamicVB = nullptr;
        return false;
    }

    if (!m_dynamicVB->Map()) {
        m_dynamicVB->Destroy();
        delete m_dynamicVB;
        m_dynamicVB = nullptr;
        return false;
    }

    return true;
}

// ============================================================================
// LoadShaderModule - Load SPIR-V binary from file
// ============================================================================
VkShaderModule vkCWallmarksEngine::LoadShaderModule(VkDevice device, const char* filename)
{
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        Msg("![Vulkan] Wallmarks: failed to open shader: %s", filename);
        return VK_NULL_HANDLE;
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = buffer.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(buffer.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        Msg("![Vulkan] Wallmarks: failed to create shader module from %s", filename);
        return VK_NULL_HANDLE;
    }

    return shaderModule;
}

// ============================================================================
// CreatePipeline - Standalone wallmark pipeline (no particle dependency)
// ============================================================================
bool vkCWallmarksEngine::CreatePipeline()
{
    if (m_pipeline != VK_NULL_HANDLE)
        return true;

    VkDevice device = VulkanHW.GetDevice();
    if (device == VK_NULL_HANDLE) return false;

    // Load wallmark-specific shaders
    string_path vertPath, fragPath;
    FS.update_path(vertPath, "$game_shaders$", "vulkan\\wallmark_dynamic_vs.spv");
    FS.update_path(fragPath, "$game_shaders$", "vulkan\\wallmark_dynamic_fs.spv");

    VkShaderModule vertModule = LoadShaderModule(device, vertPath);
    if (vertModule == VK_NULL_HANDLE) return false;

    VkShaderModule fragModule = LoadShaderModule(device, fragPath);
    if (fragModule == VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, vertModule, nullptr);
        return false;
    }

    // --- Pipeline layout: 1 descriptor set + 64-byte push constant ---
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(Fmatrix); // 64 bytes (mat4 viewProj)

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_descriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        Msg("![Vulkan] Wallmarks: failed to create pipeline layout");
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        return false;
    }

    // --- Shader stages ---
    VkPipelineShaderStageCreateInfo shaderStages[2]{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertModule;
    shaderStages[0].pName = "main";

    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragModule;
    shaderStages[1].pName = "main";

    // --- Vertex input: FVF::LIT (stride 24) ---
    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.binding = 0;
    bindingDesc.stride = sizeof(FVF::LIT); // 24 bytes
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrDescs[3]{};
    // location 0: vec3 position (offset 0)
    attrDescs[0].binding = 0;
    attrDescs[0].location = 0;
    attrDescs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrDescs[0].offset = 0;
    // location 1: vec4 color as R8G8B8A8_UNORM (offset 12)
    attrDescs[1].binding = 0;
    attrDescs[1].location = 1;
    attrDescs[1].format = VK_FORMAT_R8G8B8A8_UNORM;
    attrDescs[1].offset = 12;
    // location 2: vec2 texcoord (offset 16)
    attrDescs[2].binding = 0;
    attrDescs[2].location = 2;
    attrDescs[2].format = VK_FORMAT_R32G32_SFLOAT;
    attrDescs[2].offset = 16;

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = 3;
    vertexInputInfo.pVertexAttributeDescriptions = attrDescs;

    // --- Input assembly ---
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // --- Viewport / scissor (dynamic) ---
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    // --- Rasterizer ---
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.lineWidth = 1.0f;

    // --- Multisampling ---
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.sampleShadingEnable = VK_FALSE;

    // --- Depth stencil: test ON, write OFF, LE ---
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // --- Color blending: srcAlpha / oneMinusSrcAlpha ---
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // --- Dynamic rendering (VK_KHR_dynamic_rendering) ---
    VkFormat colorFormat = Swapchain.GetFormat();
    VkFormat depthFormat = Swapchain.m_DepthFormat;
    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &colorFormat;
    renderingInfo.depthAttachmentFormat = depthFormat;
    renderingInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    // --- Create graphics pipeline ---
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = VK_NULL_HANDLE;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline);

    // Clean up shader modules (copied into pipeline)
    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);

    if (result != VK_SUCCESS) {
        Msg("![Vulkan] Wallmarks: failed to create graphics pipeline (VkResult=%d)", result);
        return false;
    }

    Msg("[Vulkan] Wallmarks: pipeline created (depthTest=ON, depthWrite=OFF, alphaBlend)");
    return true;
}

// ============================================================================
// Texture extraction from ref_shader
// ============================================================================

// Defined in vk_shared_stubs.cpp — retrieves texture name stored by create() stub
extern shared_str vkGetShaderTextureName(Shader* pSh);

shared_str vkCWallmarksEngine::ExtractTextureName(ref_shader& shader)
{
    return vkGetShaderTextureName(shader._get());
}

// ============================================================================
// Per-slot texture cache
// ============================================================================
vkCWallmarksEngine::TextureCacheEntry* vkCWallmarksEngine::GetOrCreateSlotTexture(wm_slot* slot)
{
    shared_str texName = ExtractTextureName(slot->shader);
    if (!texName.size())
        return nullptr;

    // Look up in cache
    auto it = m_textureCache.find(texName);
    if (it != m_textureCache.end())
        return &it->second;

    // Create new entry
    VkDevice device = VulkanHW.GetDevice();
    if (device == VK_NULL_HANDLE) return nullptr;

    TextureCacheEntry entry;

    // Load DDS texture via VFS (textures may be inside .db archives)
    entry.texture = new VK::CVulkanTexture();
    string_path fn, texturePath;
    xr_sprintf(fn, sizeof(fn), "%s.dds", texName.c_str());
    FS.update_path(texturePath, "$game_textures$", fn);

    if (!entry.texture->LoadDDS(texturePath)) {
        Msg("[Vulkan] Wallmarks: texture not found '%s' (VFS path: '%s'), using fallback", texName.c_str(), texturePath);
        entry.texture->Destroy();
        delete entry.texture;
        entry.texture = nullptr;
        // Insert null entry so we don't retry
        m_textureCache[texName] = entry;
        return nullptr;
    }

    // Allocate descriptor set from wallmark pool
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_descriptorSetLayout;

    entry.descriptorSet = VK_NULL_HANDLE;
    vkAllocateDescriptorSets(device, &allocInfo, &entry.descriptorSet);
    if (entry.descriptorSet == VK_NULL_HANDLE) {
        Msg("![Vulkan] Wallmarks: failed to allocate descriptor for '%s'", texName.c_str());
        entry.texture->Destroy();
        delete entry.texture;
        entry.texture = nullptr;
        m_textureCache[texName] = entry;
        return nullptr;
    }

    // Bind texture to descriptor set
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = entry.texture->GetView();
    imageInfo.sampler = entry.texture->GetSampler();

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = entry.descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);

    Msg("[Vulkan] Wallmarks: loaded texture '%s'", texName.c_str());

    m_textureCache[texName] = entry;
    return &m_textureCache[texName];
}

// ============================================================================
// RenderSlots - Per-slot rendering with individual textures
// Two-pass approach: collect all vertices first, flush, then issue draw calls
// ============================================================================
void vkCWallmarksEngine::RenderSlots()
{
    static bool slotsOnce = false;
    if (!slotsOnce) {
        Msg("[Vulkan] Wallmarks::RenderSlots() called, marks=%u vulkanReady=%d pipeline=%p",
            (u32)marks.size(), (int)m_vulkanReady, (void*)m_pipeline);
        slotsOnce = true;
    }

    // Lazy init Vulkan resources
    if (!m_vulkanReady) {
        if (!InitVulkanResources()) {
            Msg("![Vulkan] Wallmarks::RenderSlots - InitVulkanResources FAILED");
            return;
        }
    }
    if (m_pipeline == VK_NULL_HANDLE) {
        if (!CreatePipeline()) {
            Msg("![Vulkan] Wallmarks::RenderSlots - CreatePipeline FAILED");
            return;
        }
    }
    if (!m_dynamicVB || !m_dynamicVB->IsMapped()) {
        Msg("![Vulkan] Wallmarks::RenderSlots - dynamicVB invalid (vb=%p)", (void*)m_dynamicVB);
        return;
    }

    VkCommandBuffer cmd = RCache.GetCommandBuffer();
    if (cmd == VK_NULL_HANDLE) {
        Msg("![Vulkan] Wallmarks::RenderSlots - no command buffer");
        return;
    }

    float ssaCLIP = r_ssaDISCARD / 4;

    // Per-slot draw batch info
    struct SlotBatch
    {
        wm_slot* slot;
        u32 startVertex;
        u32 vertexCount;
    };
    xr_vector<SlotBatch> batches;
    batches.reserve(marks.size());

    // ====================================================================
    // PASS 1: Collect vertices from all slots into dynamic buffer
    // ====================================================================
    u32 bufferOffset = 0;
    bool bufferFull = false;

    lock.Enter();

    for (WMSlotVecIt slot_it = marks.begin(); slot_it != marks.end(); slot_it++)
    {
        wm_slot* slot = *slot_it;
        u32 slotStart = bufferOffset;

        if (!bufferFull)
        {
            // Static wallmarks
            for (StaticWMVecIt w_it = slot->static_items.begin(); w_it != slot->static_items.end(); w_it++)
            {
                static_wallmark* W = *w_it;
                if (!RImplementation.ViewBase.testSphere_dirty(W->bounds.P, W->bounds.R))
                    continue;
                float dst = Device.vCameraPosition.distance_to_sqr(W->bounds.P);
                float ssa = W->bounds.R * W->bounds.R / dst;
                if (ssa < ssaCLIP)
                    continue;

                u32 wvCount = (u32)W->verts.size();
                if (bufferOffset + wvCount > MAX_WM_VERTS) { bufferFull = true; break; }

                FVF::LIT* dest = (FVF::LIT*)((u8*)m_dynamicVB->m_Mapped + bufferOffset * sizeof(FVF::LIT));
                static_wm_render(W, dest);
                bufferOffset += wvCount;
            }

            // Skeleton wallmarks
            for (auto& w_it : slot->skeleton_items)
            {
                intrusive_ptr<CSkeletonWallmark> W = w_it;
                if (!W) continue;

#ifdef DEBUG
                if (W->used_in_render != Device.dwFrame)
                {
                    Log("W->used_in_render", W->used_in_render);
                    Log("Device.dwFrame", Device.dwFrame);
                    VERIFY(W->used_in_render == Device.dwFrame);
                }
#endif

                Device.Statistic->RenderDUMP_WMD_Count++;

                // Check if we have space for this wallmark
                u32 wvCount = W->VCount();
                if (bufferOffset + wvCount > MAX_WM_VERTS) { bufferFull = true; break; }

                // Render skeleton wallmark (transform by bones)
                FVF::LIT* dest = (FVF::LIT*)((u8*)m_dynamicVB->m_Mapped + bufferOffset * sizeof(FVF::LIT));
                skeleton_wm_render(W, dest);
                bufferOffset += wvCount;

#ifdef DEBUG
                W->used_in_render = u32(-1);
#endif
            }
            // Skeleton wallmarks are re-submitted each frame by CKinematics;
            // must clear after collecting vertices (matches DX11 WallmarksEngine)
            slot->skeleton_items.clear();
        }

        // Record batch if this slot produced vertices
        u32 slotVertCount = bufferOffset - slotStart;
        if (slotVertCount > 0) {
            SlotBatch batch;
            batch.slot = slot;
            batch.startVertex = slotStart;
            batch.vertexCount = slotVertCount;
            batches.push_back(batch);
        }
    }

    lock.Leave();

    if (batches.empty() || bufferOffset == 0) {
        static bool emptyOnce = false;
        if (!emptyOnce) {
            Msg("[Vulkan] Wallmarks::RenderSlots - batches empty after collection (marks=%u)", (u32)marks.size());
            // Dump slot info
            for (auto* slot : marks) {
                Msg("  slot: static=%u skeleton=%u shader=%s",
                    (u32)slot->static_items.size(), (u32)slot->skeleton_items.size(),
                    slot->shader._get() ? "valid" : "null");
            }
            emptyOnce = true;
        }
        return;
    }

    static bool drawOnce = false;
    if (!drawOnce) {
        Msg("[Vulkan] Wallmarks::RenderSlots - drawing %u batches, %u verts", (u32)batches.size(), bufferOffset);
        drawOnce = true;
    }

    // ====================================================================
    // Flush buffer to GPU (all vertex data uploaded)
    // ====================================================================
    m_dynamicVB->Flush();

    // ====================================================================
    // PASS 2: Issue draw calls per-slot with slot-specific textures
    // ====================================================================

    // Bind pipeline once
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    // Push view-projection matrix (mProject already has depth bias)
    Fmatrix viewProj;
    viewProj.mul(Device.mProject, Device.mView);
    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                      0, sizeof(Fmatrix), &viewProj);

    // Set viewport and scissor (flipped Y to match G-Buffer / scene passes)
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

    // Bind vertex buffer
    VkBuffer vb = m_dynamicVB->GetHandle();
    VkDeviceSize vbOffset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &vbOffset);

    // Draw each slot batch with its texture
    for (auto& batch : batches)
    {
        // Resolve texture for this slot
        TextureCacheEntry* texEntry = GetOrCreateSlotTexture(batch.slot);
        VkDescriptorSet descSet = (texEntry && texEntry->descriptorSet != VK_NULL_HANDLE)
                                  ? texEntry->descriptorSet
                                  : m_whiteDescriptorSet;

        if (descSet != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                   m_pipelineLayout, 0, 1, &descSet, 0, nullptr);
        }

        // Draw this slot's triangles
        u32 triCount = batch.vertexCount / 3;
        vkCmdDraw(cmd, triCount * 3, 1, batch.startVertex, 0);

        RCache.stat.calls++;
        RCache.stat.verts += batch.vertexCount;
        RCache.stat.polys += triCount;
    }
}
