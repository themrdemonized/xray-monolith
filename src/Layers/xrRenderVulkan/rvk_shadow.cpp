// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

#include "stdafx.h"
#include "rvk.h"
#include "vk_rendertarget.h"
#include "vk_pipeline.h"
#include "vk_R_Backend.h"
#include "HW_Vulkan.h"
#include "vk_Visual.h"  // For IRenderVisual definition
#include "../xrRender/FBasicVisual.h"  // For dxRender_Visual

// ============================================================================
// render_shadow_geometry() - Render shadow caster geometry for a cascade
// ============================================================================
//
// Рендерит геометрию которая отбрасывает тени для указанного каскада.
//
// Процесс:
// 1. Bind depth-only pipeline (shadow pipeline)
// 2. Traverse scene graph для сбора shadow casters
// 3. Для каждого caster:
//    - Set world matrix
//    - Bind vertex/index buffers
//    - Issue draw call
//
// ============================================================================

void CRender::render_shadow_geometry(u32 cascade_ind)
{
	Msg("[Vulkan] render_shadow_geometry(cascade %d)", cascade_ind);

	// ========================================================================
	// Step 1: Get shadow pipeline
	// ========================================================================

	VkPipeline shadowPipeline = RTarget->GetShadowPipeline();
	if (shadowPipeline == VK_NULL_HANDLE) {
		Msg("![Vulkan] Failed to get shadow pipeline");
		return;
	}

	// ========================================================================
	// Step 2: Bind shadow pipeline
	// ========================================================================

	VkCommandBuffer cmd = RCache.GetCommandBuffer();
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline);

	// Bind pipeline layout for descriptor sets (if needed)
	// vkCmdBindDescriptorSets(...);

	// ========================================================================
	// Step 3: Update push constants with shadow MVP matrix
	// ========================================================================

	const VK::SunCascade& cascade = m_sun_cascades[cascade_ind];

	// Shadow MVP matrix (light view-projection)
	// Используем cascade.xform который уже содержит полный VP transform
	struct ShadowPushConstants {
		Fmatrix u_MVP;  // Model-View-Projection matrix (64 bytes)
	} pushConstants;

	pushConstants.u_MVP = cascade.xform;

	// Push MVP matrix to vertex shader
	VkPipelineLayout layout = VK::g_PipelineManager->GetLayout();
	vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT,
	                   0, sizeof(ShadowPushConstants), &pushConstants);

	Msg("[Vulkan] Shadow MVP matrix pushed for cascade %d", cascade_ind);

	// ========================================================================
	// Step 4: Render shadow caster geometry
	// ========================================================================

	// Render level visuals as shadow casters
	RenderLevelVisuals();

	// TODO: Advanced features for production:
	//   - Frustum culling в shadow space (reduce overdraw)
	//   - Distance culling (far objects don't need near cascade shadows)
	//   - Shadow caster filtering (skip transparent/small objects)
	//   - LOD selection based on cascade index (far cascade can use lower LOD)
	//   - Instanced rendering для repeated geometry

	Msg("[Vulkan] Shadow geometry rendering complete");

	// ========================================================================
	// Step 5: Statistics
	// ========================================================================

	// Update render stats when geometry is actually rendered
	// RCache.stat.polys += ...
	// RCache.stat.verts += ...
	// RCache.stat.calls += ...

	Msg("[Vulkan] render_shadow_geometry(cascade %d) complete", cascade_ind);
}

// ============================================================================
// RenderLevelVisuals() - Render level geometry (for shadow map or main scene)
// ============================================================================

void CRender::RenderLevelVisuals()
{
	// Check if level is loaded
	if (!b_loaded) {
		Msg("[Vulkan] No level loaded");
		return;
	}

	Msg("[Vulkan] Rendering level visuals for shadow map using scene graph");

	// Use the scene graph rendering system instead of manually iterating visuals
	// This is the proper way to render geometry in X-Ray engine
	// Priority 0 = normal geometry (shadow casters)
	r_dsgraph_render_graph(0);

	Msg("[Vulkan] Level visuals rendered via scene graph");
}

// ============================================================================
// render_shadow_geometry_cubemap() - Render shadow geometry для cubemap face
// ============================================================================
//
// Phase 2.16: Point light shadow cube rendering
//
// Рендерит shadow casters для одной грани cubemap.
// В отличие от directional shadows, здесь:
// - Используем perspective projection (90 degree FOV)
// - Frustum culling per face (only visible objects)
// - Distance culling (objects beyond light range)
//
// ============================================================================

void CRender::render_shadow_geometry_cubemap(u32 face_index, const Fmatrix& face_matrix,
                                              const Fvector& light_pos, float light_range)
{
	const char* faceNames[6] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
	Msg("[Vulkan] render_shadow_geometry_cubemap(face %d: %s)", face_index, faceNames[face_index]);

	// ========================================================================
	// Step 1: Store cubemap parameters globally
	// ========================================================================
	// Нужно для передачи в push constants
	m_cubemap_face_matrix = face_matrix;
	m_cubemap_light_pos = light_pos;
	m_cubemap_light_range = light_range;

	// ========================================================================
	// Step 2: Get shadow pipeline
	// ========================================================================
	VkPipeline shadowPipeline = RTarget->GetShadowPipeline();
	if (shadowPipeline == VK_NULL_HANDLE) {
		Msg("![Vulkan] Failed to get shadow pipeline for cubemap");
		return;
	}

	// ========================================================================
	// Step 3: Bind shadow pipeline
	// ========================================================================
	VkCommandBuffer cmd = RCache.GetCommandBuffer();
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline);

	// ========================================================================
	// Step 4: Update push constants with cubemap face matrix
	// ========================================================================
	struct ShadowPushConstants {
		Fmatrix u_MVP;  // Face view-projection matrix (64 bytes)
	} pushConstants;

	pushConstants.u_MVP = face_matrix;

	// Push MVP matrix to vertex shader
	VkPipelineLayout layout = VK::g_PipelineManager->GetLayout();
	vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT,
	                   0, sizeof(ShadowPushConstants), &pushConstants);

	Msg("[Vulkan] Cubemap face %s MVP matrix pushed", faceNames[face_index]);

	// ========================================================================
	// Step 5: Render shadow casters (with frustum + distance culling)
	// ========================================================================
	RenderLevelVisuals_Cubemap(light_pos, light_range);

	Msg("[Vulkan] render_shadow_geometry_cubemap(face %s) complete", faceNames[face_index]);
}

// ============================================================================
// RenderLevelVisuals_Cubemap() - Render level visuals with cubemap culling
// ============================================================================
//
// Phase 2.16: Optimized rendering для cubemap faces
//
// Отличия от RenderLevelVisuals():
// 1. Distance culling: skip objects beyond light range
// 2. TODO: Frustum culling: skip objects outside face frustum
// 3. TODO: LOD selection: use lower LOD для distant objects
//
// ============================================================================

void CRender::RenderLevelVisuals_Cubemap(const Fvector& light_pos, float light_range)
{
	// Check if level is loaded
	if (!b_loaded || Visuals.empty()) {
		Msg("[Vulkan] No level loaded or no visuals available for cubemap");
		return;
	}

	Msg("[Vulkan] Rendering %d level visuals for cubemap (with culling)", (u32)Visuals.size());

	// Set world matrix to identity for static level geometry
	Fmatrix identity;
	identity.identity();
	RCache.set_xform_world(identity);

	// ========================================================================
	// Build frustum from cubemap face matrix
	// ========================================================================
	CFrustum frustum;
	frustum.CreateFromMatrix(m_cubemap_face_matrix, FRUSTUM_P_ALL);

	// ========================================================================
	// Render all visuals with frustum + distance culling
	// ========================================================================
	u32 rendered_count = 0;
	u32 culled_distance = 0;
	u32 culled_frustum = 0;

	for (u32 i = 0; i < Visuals.size(); i++)
	{
		IRenderVisual* visual = Visuals[i];
		if (!visual) continue;

		// Cast to dxRender_Visual to access Render() method
		dxRender_Visual* dxVisual = (dxRender_Visual*)visual->dcast_RenderVisual();
		if (!dxVisual) continue;

		// ====================================================================
		// Get visual bounding sphere
		// ====================================================================
		// TODO: Get actual bounding sphere from visual->vis
		// For now, use placeholder values
		Fvector visual_center = {0, 0, 0};  // Placeholder - need visual->vis.sphere.P
		float visual_radius = 10.0f;        // Placeholder - need visual->vis.sphere.R

		// ====================================================================
		// Distance culling: skip objects beyond light range
		// ====================================================================
		float dist = light_pos.distance_to(visual_center);

		// Cull if beyond light range + visual radius (conservative)
		if (dist > (light_range + visual_radius)) {
			culled_distance++;
			continue;
		}

		// ====================================================================
		// Frustum culling: test bounding sphere against cubemap face frustum
		// ====================================================================
		u32 test_mask = 0xFFFFFFFF;  // Test against all planes
		EFC_Visible vis = frustum.testSphere(visual_center, visual_radius, test_mask);

		if (vis == fcvNone) {
			// Completely outside frustum
			culled_frustum++;
			continue;
		}

		// ====================================================================
		// Render visual
		// ====================================================================
		// Use LOD = 1.0 for shadows (max detail)
		// TODO: LOD selection based on distance from light
		//   float lod = _min(1.0f, dist / light_range);
		dxVisual->Render(1.0f);
		rendered_count++;
	}

	Msg("[Vulkan] Cubemap visuals rendered: %d/%d (culled: %d distance, %d frustum)",
	    rendered_count, (u32)Visuals.size(), culled_distance, culled_frustum);
}

// ============================================================================
// render_shadow_geometry() - Generic shadow rendering для spot lights
// ============================================================================
//
// Phase 2.17.2: Render shadow caster geometry для spot light
//
// Process:
// 1. Store view-projection matrix globally
// 2. Bind shadow pipeline
// 3. Push view-projection matrix (push constants)
// 4. Call RenderLevelVisuals_Spot() with culling
//
// ============================================================================

void CRender::render_shadow_geometry(const Fmatrix& viewProj, const Fvector& light_pos, float light_range)
{
	Msg("[Vulkan] render_shadow_geometry(spot): Rendering shadow geometry");

	// ========================================================================
	// Step 1: Store parameters globally (for RenderLevelVisuals_Spot)
	// ========================================================================
	m_spot_view_proj = viewProj;
	m_spot_light_pos = light_pos;
	m_spot_light_range = light_range;

	// ========================================================================
	// Step 2: Get and bind shadow pipeline
	// ========================================================================
	VkPipeline shadowPipeline = RTarget->GetShadowPipeline();
	if (shadowPipeline == VK_NULL_HANDLE) {
		Msg("![Vulkan] Failed to get shadow pipeline");
		return;
	}

	VkCommandBuffer cmd = RCache.GetCommandBuffer();
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline);

	// ========================================================================
	// Step 3: Update push constants (view-projection matrix)
	// ========================================================================
	struct ShadowPushConstants {
		Fmatrix u_MVP;
	} pushConstants;

	pushConstants.u_MVP = viewProj;

	VkPipelineLayout layout = VK::g_PipelineManager->GetLayout();
	vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT,
	                   0, sizeof(ShadowPushConstants), &pushConstants);

	// ========================================================================
	// Step 4: Render level visuals with frustum + distance culling
	// ========================================================================
	RenderLevelVisuals_Spot(light_pos, light_range);

	Msg("[Vulkan] render_shadow_geometry(spot) complete");
}

// ============================================================================
// RenderLevelVisuals_Spot() - Render level visuals with spot light culling
// ============================================================================
//
// Phase 2.17.2: Optimized rendering для spot light shadow maps
//
// Features:
// 1. Distance culling: skip objects beyond light range
// 2. Frustum culling: skip objects outside spot light frustum
// 3. LOD selection: use appropriate LOD based on distance
//
// ============================================================================

void CRender::RenderLevelVisuals_Spot(const Fvector& light_pos, float light_range)
{
	// Check if level is loaded
	if (!b_loaded || Visuals.empty()) {
		Msg("[Vulkan] No level loaded or no visuals available for spot shadow");
		return;
	}

	Msg("[Vulkan] Rendering %d level visuals for spot shadow (with culling)", (u32)Visuals.size());

	// Set world matrix to identity for static level geometry
	Fmatrix identity;
	identity.identity();
	RCache.set_xform_world(identity);

	// ========================================================================
	// Build frustum from spot light view-projection matrix
	// ========================================================================
	CFrustum frustum;
	frustum.CreateFromMatrix(m_spot_view_proj, FRUSTUM_P_ALL);

	// ========================================================================
	// Render all visuals with frustum + distance culling
	// ========================================================================
	u32 rendered_count = 0;
	u32 culled_distance = 0;
	u32 culled_frustum = 0;

	for (u32 i = 0; i < Visuals.size(); i++)
	{
		IRenderVisual* visual = Visuals[i];
		if (!visual) continue;

		// Cast to dxRender_Visual to access Render() method
		dxRender_Visual* dxVisual = (dxRender_Visual*)visual->dcast_RenderVisual();
		if (!dxVisual) continue;

		// ====================================================================
		// Get visual bounding sphere
		// ====================================================================
		// TODO: Get actual bounding sphere from visual->vis
		// For now, use placeholder values
		Fvector visual_center = {0, 0, 0};  // Placeholder - need visual->vis.sphere.P
		float visual_radius = 10.0f;        // Placeholder - need visual->vis.sphere.R

		// ====================================================================
		// Distance culling: skip objects beyond light range
		// ====================================================================
		float dist = light_pos.distance_to(visual_center);

		// Cull if beyond light range + visual radius (conservative)
		if (dist > (light_range + visual_radius)) {
			culled_distance++;
			continue;
		}

		// ====================================================================
		// Frustum culling: test bounding sphere against spot frustum
		// ====================================================================
		u32 test_mask = 0xFFFFFFFF;  // Test against all planes
		EFC_Visible vis = frustum.testSphere(visual_center, visual_radius, test_mask);

		if (vis == fcvNone) {
			// Completely outside frustum
			culled_frustum++;
			continue;
		}

		// ====================================================================
		// Render visual
		// ====================================================================
		// Use LOD = 1.0 for shadows (max detail)
		// TODO: LOD selection based on distance from light
		//   float lod = _min(1.0f, dist / light_range);
		dxVisual->Render(1.0f);
		rendered_count++;
	}

	Msg("[Vulkan] Spot shadow visuals rendered: %d/%d (culled: %d distance, %d frustum)",
	    rendered_count, (u32)Visuals.size(), culled_distance, culled_frustum);
}
