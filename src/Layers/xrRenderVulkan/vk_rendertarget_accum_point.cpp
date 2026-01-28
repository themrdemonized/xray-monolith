#include "stdafx.h"
#include "vk_rendertarget.h"
#include "rvk.h"
#include "../xrRender/light.h"  // For light class definition
#include "vk_R_Backend.h"
#include "HW_Vulkan.h"
#include "vk_shaders.h"
#include "vk_pipeline.h"

namespace VK
{

// ============================================================================
// accum_point() - Point light accumulation with shadow cube
// ============================================================================
//
// Полная реализация point light с omnidirectional shadow mapping.
//
// Процесс:
// 1. Shadow cube уже отрендерена в rt_smap_cube (6 faces)
// 2. Render sphere geometry для light volume
// 3. Sample shadow cube в fragment shader
// 4. Вычисляем lighting с distance attenuation
// 5. Accumulate в rt_Accumulator
//
// Stencil masking (Carmack's reverse):
// - Pass 1 (backfaces):  Mark lit pixels в stencil
// - Pass 2 (frontfaces): Clear stencil для внутренних pixels
// - Pass 3 (backfaces):  Render lighting только где stencil == lightID
//
// ============================================================================

void CRenderTarget::accum_point(light* L)
{
	Msg("[Vulkan] accum_point() - Point light accumulation");

	if (!L) {
		Msg("![Vulkan] accum_point: NULL light pointer");
		return;
	}

	VkCommandBuffer cmd = RCache.GetCommandBuffer();

	// ========================================================================
	// Step 1: Phase accumulator (ensure rt_Accumulator is ready)
	// ========================================================================
	// Accumulator должен быть ready для additive blending
	// phase_accumulator() вызывается once per frame, здесь не нужен

	// ========================================================================
	// Step 2: Setup light transform (sphere scaled by light range)
	// ========================================================================
	Fmatrix lightXForm;
	lightXForm.identity();
	lightXForm.scale(L->range, L->range, L->range);  // Scale sphere to light range
	lightXForm.translate_over(L->position);          // Move to light position

	RCache.set_xform_world(lightXForm);
	RCache.set_xform_view(Device.mView);
	RCache.set_xform_project(Device.mProject);

	// ========================================================================
	// Step 3: Stencil Masking (Carmack's Reverse)
	// ========================================================================
	// TODO: Implement stencil masking для optimization
	// For now: skip stencil, render fullscreen (simplified approach)
	//
	// Production implementation:
	// Pass 1 (backfaces):  Set stencil = lightID where zfail
	// Pass 2 (frontfaces): Clear stencil = 1 where zfail
	// Pass 3 (backfaces):  Render lighting where stencil == lightID

	// ========================================================================
	// Step 4: Begin rendering to accumulator
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
	// Step 5: Setup viewport and scissor
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

	// TODO: Scissor rect optimization (enable_scissor)

	// ========================================================================
	// Step 6: Load point light shaders
	// ========================================================================
	VkShaderModule vertShader = g_ShaderManager->Load("accum_point.vert.spv");
	VkShaderModule fragShader = g_ShaderManager->Load("accum_point.frag.spv");

	if (vertShader == VK_NULL_HANDLE || fragShader == VK_NULL_HANDLE) {
		Msg("![Vulkan] Failed to load point light shaders");
		vkCmdEndRendering(cmd);
		return;
	}

	// ========================================================================
	// Step 7: Create pipeline (with additive blending)
	// ========================================================================
	PipelineConfig config = {};
	config.vertShader = vertShader;
	config.fragShader = fragShader;
	config.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	config.cullMode = VK_CULL_MODE_NONE;  // TODO: CULL_BACK for sphere volume
	config.depthTest = false;  // No depth testing for lighting accumulation
	config.depthWrite = false;
	config.blendEnable = true;  // Additive blending!
	config.colorAttachmentCount = 1;
	config.colorFormats[0] = VK_FORMAT_R16G16B16A16_SFLOAT;  // rt_Accumulator

	VkPipeline pipeline = g_PipelineManager->GetOrCreate(config);
	if (pipeline == VK_NULL_HANDLE) {
		Msg("![Vulkan] Failed to create point light pipeline");
		vkCmdEndRendering(cmd);
		return;
	}

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	// ========================================================================
	// Step 8: Bind descriptor sets (G-Buffer + Point light data)
	// ========================================================================
	// Get or create descriptor sets
	if (m_GBufferDescSet == VK_NULL_HANDLE) {
		m_GBufferDescSet = CreateGBufferDescriptorSet();
	}
	if (m_PointDescSet == VK_NULL_HANDLE) {
		m_PointDescSet = CreatePointDescriptorSet();
	}

	// Update descriptor sets
	UpdateGBufferDescriptorSet(m_GBufferDescSet);
	UpdatePointDescriptorSet(m_PointDescSet, L);

	// Bind descriptor sets
	VkPipelineLayout layout = g_PipelineManager->GetLayout();

	VkDescriptorSet sets[4] = {
		VK_NULL_HANDLE,      // Set 0: PerFrame (not used)
		m_GBufferDescSet,    // Set 1: G-Buffer
		VK_NULL_HANDLE,      // Set 2: PerObject (not used)
		m_PointDescSet       // Set 3: Point light + shadow cube
	};

	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
	                        1, 1, &sets[1], 0, nullptr);  // Set 1
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
	                        3, 1, &sets[3], 0, nullptr);  // Set 3

	Msg("[Vulkan] Descriptor sets bound: G-Buffer + Point light");

	// ========================================================================
	// Step 9: Draw sphere volume (or fullscreen quad for now)
	// ========================================================================
	// TODO Phase 2.16.4: Bind sphere geometry and draw
	// For now: draw fullscreen quad (simplified)

	// Bind point volume geometry
	VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(cmd, 0, 1, &m_PointVolumeVB, &offset);
	vkCmdBindIndexBuffer(cmd, m_PointVolumeIB, 0, VK_INDEX_TYPE_UINT32);

	// Draw sphere
	vkCmdDrawIndexed(cmd, m_PointVolumeIndexCount, 1, 0, 0, 0);

	Msg("[Vulkan] Point light sphere drawn: %d indices", m_PointVolumeIndexCount);

	// ========================================================================
	// Step 10: End rendering
	// ========================================================================
	vkCmdEndRendering(cmd);

	// ========================================================================
	// Step 11: Increment light marker (for next light)
	// ========================================================================
	increment_light_marker();

	Msg("[Vulkan] accum_point() complete");
}

// ============================================================================
// setup_cubemap_matrices() - Setup 6 view matrices для shadow cube rendering
// ============================================================================
//
// Создаём 6 view matrices для рендеринга cubemap:
// Face 0: +X (right)
// Face 1: -X (left)
// Face 2: +Y (up)
// Face 3: -Y (down)
// Face 4: +Z (forward)
// Face 5: -Z (back)
//
// ============================================================================

void CRenderTarget::setup_cubemap_matrices(light* L, Fmatrix face_matrices[6])
{
	Msg("[Vulkan] setup_cubemap_matrices()");

	// ========================================================================
	// Light position в world space
	// ========================================================================
	Fvector lightPos = L->position;

	// ========================================================================
	// Projection matrix: 90 degree FOV для cubemap
	// ========================================================================
	// Cubemap требует 90 degree FOV чтобы каждая грань покрывала ровно 1/4 сферы
	Fmatrix proj;
	proj.build_projection(
		deg2rad(90.0f),  // FOV = 90 degrees (pi/2)
		1.0f,            // aspect = 1.0 (square faces)
		0.1f,            // near plane
		L->range         // far plane = light range
	);

	// ========================================================================
	// View matrices для 6 граней cubemap
	// ========================================================================
	// Стандартный порядок граней в cubemap:
	// 0: +X (right)
	// 1: -X (left)
	// 2: +Y (up)
	// 3: -Y (down)
	// 4: +Z (forward)
	// 5: -Z (back)

	// Face 0: +X (look right)
	{
		Fvector target, up;
		target.set(lightPos.x + 1.0f, lightPos.y, lightPos.z);  // Look right
		up.set(0.0f, -1.0f, 0.0f);                              // Up = -Y (flipped for RH coords)

		Fmatrix view;
		view.build_camera(lightPos, target, up);
		face_matrices[0].mul(proj, view);
	}

	// Face 1: -X (look left)
	{
		Fvector target, up;
		target.set(lightPos.x - 1.0f, lightPos.y, lightPos.z);  // Look left
		up.set(0.0f, -1.0f, 0.0f);                              // Up = -Y

		Fmatrix view;
		view.build_camera(lightPos, target, up);
		face_matrices[1].mul(proj, view);
	}

	// Face 2: +Y (look up)
	{
		Fvector target, up;
		target.set(lightPos.x, lightPos.y + 1.0f, lightPos.z);  // Look up
		up.set(0.0f, 0.0f, 1.0f);                               // Up = +Z

		Fmatrix view;
		view.build_camera(lightPos, target, up);
		face_matrices[2].mul(proj, view);
	}

	// Face 3: -Y (look down)
	{
		Fvector target, up;
		target.set(lightPos.x, lightPos.y - 1.0f, lightPos.z);  // Look down
		up.set(0.0f, 0.0f, -1.0f);                              // Up = -Z

		Fmatrix view;
		view.build_camera(lightPos, target, up);
		face_matrices[3].mul(proj, view);
	}

	// Face 4: +Z (look forward)
	{
		Fvector target, up;
		target.set(lightPos.x, lightPos.y, lightPos.z + 1.0f);  // Look forward
		up.set(0.0f, -1.0f, 0.0f);                              // Up = -Y

		Fmatrix view;
		view.build_camera(lightPos, target, up);
		face_matrices[4].mul(proj, view);
	}

	// Face 5: -Z (look back)
	{
		Fvector target, up;
		target.set(lightPos.x, lightPos.y, lightPos.z - 1.0f);  // Look back
		up.set(0.0f, -1.0f, 0.0f);                              // Up = -Y

		Fmatrix view;
		view.build_camera(lightPos, target, up);
		face_matrices[5].mul(proj, view);
	}

	Msg("[Vulkan] Cubemap matrices setup complete for light at (%.2f, %.2f, %.2f), range=%.2f",
	    lightPos.x, lightPos.y, lightPos.z, L->range);
}

// ============================================================================
// render_smap_cube_face() - Render one cubemap face для shadow cube
// ============================================================================
//
// Рендерит depth-only geometry в одну грань shadow cubemap.
//
// ============================================================================

void CRenderTarget::render_smap_cube_face(light* L, u32 face_index, const Fmatrix& face_matrix)
{
	const char* faceNames[6] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
	Msg("[Vulkan] render_smap_cube_face(face %d: %s)", face_index, faceNames[face_index]);

	// ========================================================================
	// Step 1: Get command buffer
	// ========================================================================
	VkCommandBuffer cmd = RCache.GetCommandBuffer();

	// ========================================================================
	// Step 2: Begin render pass для cubemap face
	// ========================================================================
	// Render to specific face view (depth-only)
	VkRenderingAttachmentInfo depthAttachment = {};
	depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	depthAttachment.imageView = rt_smap_cube.m_FaceViews[face_index];  // Specific face!
	depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;              // Clear depth
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depthAttachment.clearValue.depthStencil.depth = 1.0f;              // Clear to far plane

	VkRenderingInfo renderingInfo = {};
	renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	renderingInfo.renderArea.offset = {0, 0};
	renderingInfo.renderArea.extent = {rt_smap_cube.m_Width, rt_smap_cube.m_Height}; // 512x512
	renderingInfo.layerCount = 1;
	renderingInfo.colorAttachmentCount = 0;  // Depth-only, no color attachments
	renderingInfo.pDepthAttachment = &depthAttachment;

	vkCmdBeginRendering(cmd, &renderingInfo);

	// ========================================================================
	// Step 3: Setup viewport and scissor (512x512)
	// ========================================================================
	VkViewport viewport = {};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float)rt_smap_cube.m_Width;
	viewport.height = (float)rt_smap_cube.m_Height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor = {};
	scissor.offset = {0, 0};
	scissor.extent = {rt_smap_cube.m_Width, rt_smap_cube.m_Height};
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	// ========================================================================
	// Step 4: Render shadow caster geometry
	// ========================================================================
	// Call CRender::render_shadow_geometry_cubemap() which handles:
	// - Shadow pipeline binding
	// - Push constants (face view-projection matrix)
	// - Level visuals rendering with distance culling

	RImplementation.render_shadow_geometry_cubemap(
		face_index,
		face_matrix,     // Face view-projection matrix
		L->position,     // Light position (for distance culling)
		L->range         // Light range (for distance culling)
	);

	// ========================================================================
	// Step 5: End render pass
	// ========================================================================
	vkCmdEndRendering(cmd);

	Msg("[Vulkan] Cubemap face %s rendered", faceNames[face_index]);
}

// ============================================================================
// phase_smap_point() - Render all 6 cubemap faces для point light shadows
// ============================================================================
//
// Главный метод для рендеринга point light shadow cube.
//
// ============================================================================

void CRenderTarget::phase_smap_point(light* L)
{
	Msg("[Vulkan] phase_smap_point() - Rendering shadow cube for point light");

	if (!L) {
		Msg("![Vulkan] phase_smap_point: NULL light pointer");
		return;
	}

	// ========================================================================
	// Step 1: Setup cubemap view matrices (6 faces)
	// ========================================================================
	Fmatrix face_matrices[6];
	setup_cubemap_matrices(L, face_matrices);

	// ========================================================================
	// Step 2: Transition shadow cube to DEPTH_ATTACHMENT layout
	// ========================================================================
	VkCommandBuffer cmd = RCache.GetCommandBuffer();
	rt_smap_cube.TransitionLayout(cmd,
	                              VK_IMAGE_LAYOUT_UNDEFINED,
	                              VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

	// ========================================================================
	// Step 3: Render all 6 cubemap faces
	// ========================================================================
	for (u32 face = 0; face < 6; face++)
	{
		// Render this face with its view-projection matrix
		render_smap_cube_face(L, face, face_matrices[face]);
	}

	// ========================================================================
	// Step 4: Transition shadow cube to SHADER_READ_ONLY layout
	// ========================================================================
	// После рендеринга всех граней, переводим cubemap в режим sampling
	// для использования в accum_point()
	rt_smap_cube.TransitionLayout(cmd,
	                              VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
	                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	Msg("[Vulkan] phase_smap_point() complete - 6 faces rendered");

	// ========================================================================
	// Performance Notes:
	// ========================================================================
	// Текущая реализация: 6 render passes per point light
	//
	// Optimizations (TODO для production):
	// 1. Geometry shader approach:
	//    - Single render pass
	//    - Geometry shader outputs to gl_Layer (cubemap face)
	//    - 6x faster но требует geometry shader support
	//
	// 2. Multiview extension (VK_KHR_multiview):
	//    - Single render pass
	//    - Hardware renders to multiple views simultaneously
	//    - Most efficient на modern GPU
	//
	// 3. Frustum culling:
	//    - Cull objects outside cubemap face frustum
	//    - Significant performance gain для large scenes
	//
	// 4. Distance culling:
	//    - Skip objects beyond light range
	//    - Simple distance check: length(objPos - lightPos) > range
	//
	// 5. LOD selection:
	//    - Use lower LOD for cubemap shadows
	//    - Less triangles = faster rendering
}

} // namespace VK
