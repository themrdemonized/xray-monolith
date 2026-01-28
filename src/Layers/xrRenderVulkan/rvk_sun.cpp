// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#include "stdafx.h"
#include "rvk.h"
#include "vk_rendertarget.h"
#include "../xrRender/light.h"  // For light class definition
#include "../../xrEngine/igame_persistent.h"

// ============================================================================
// Constants
// ============================================================================

extern float OLES_SUN_LIMIT_27_01_07;  // From engine (default 100.f)
extern Fvector3 ps_ssfx_shadow_cascades;  // Cascade sizes from settings

// ============================================================================
// init_sun_cascades() - Initialize cascade parameters
// ============================================================================

void CRender::init_sun_cascades()
{
	Msg("[Vulkan] Initializing sun cascades...");

	u32 cascade_count = 3;
	m_sun_cascades.resize(cascade_count);

	float fBias = -0.0000025f;

	// Cascade 0: NEAR (0..20m by default)
	m_sun_cascades[0].reset_chain = true;
	m_sun_cascades[0].size = 20.f;  // ps_ssfx_shadow_cascades.x
	m_sun_cascades[0].bias = m_sun_cascades[0].size * fBias;

	// Cascade 1: MIDDLE (20..40m by default)
	m_sun_cascades[1].size = 40.f;  // ps_ssfx_shadow_cascades.y
	m_sun_cascades[1].bias = m_sun_cascades[1].size * fBias;

	// Cascade 2: FAR (40..160m by default)
	m_sun_cascades[2].size = 160.f;  // ps_ssfx_shadow_cascades.z
	m_sun_cascades[2].bias = m_sun_cascades[2].size * fBias;

	Msg("[Vulkan] Sun cascades initialized:");
	Msg("[Vulkan]   - Cascade 0 (NEAR):   size=%.1f, bias=%.7f", m_sun_cascades[0].size, m_sun_cascades[0].bias);
	Msg("[Vulkan]   - Cascade 1 (MIDDLE): size=%.1f, bias=%.7f", m_sun_cascades[1].size, m_sun_cascades[1].bias);
	Msg("[Vulkan]   - Cascade 2 (FAR):    size=%.1f, bias=%.7f", m_sun_cascades[2].size, m_sun_cascades[2].bias);
}

// ============================================================================
// render_sun_cascades() - Render all cascades in sequence
// ============================================================================

void CRender::render_sun_cascades()
{
	// TODO: Check if sunshafts need rendering (affects last cascade reset_chain)
	// bool b_need_to_render_sunshafts = RImplementation.Target->need_to_render_sunshafts();

	for (u32 i = 0; i < m_sun_cascades.size(); ++i)
		render_sun_cascade(i);
}

// ============================================================================
// render_sun_cascade() - Full cascade rendering with shadow matrix calculation
// ============================================================================

void CRender::render_sun_cascade(u32 cascade_ind)
{
	Msg("[Vulkan] render_sun_cascade(%d)", cascade_ind);

	// Get sun light
	light* sun = (light*)Lights.sun_adapted._get();
	if (!sun)
	{
		Msg("![Vulkan] No sun light available");
		return;
	}

	// ========================================================================
	// Step 1: Calculate projection matrix for this cascade
	// ========================================================================

	Fmatrix m_LightViewProj;
	Fvector3 L_dir, L_up, L_right, L_pos;

	// Light direction (world space)
	L_dir.set(sun->direction);
	L_dir.normalize();

	// Build light coordinate system
	L_up.set(0, 1, 0);
	if (_abs(L_up.dotproduct(L_dir)) > 0.99f)
		L_up.set(0, 0, 1);
	L_right.crossproduct(L_up, L_dir);
	L_right.normalize();
	L_up.crossproduct(L_dir, L_right);
	L_up.normalize();

	// Virtual light position (far away from camera)
	float tweak_COP_initial_offs = 1200.f;
	L_pos.mad(Device.vCameraPosition, L_dir, -tweak_COP_initial_offs);

	// ========================================================================
	// Step 2: Build light matrices (view + projection)
	// ========================================================================

	Fmatrix mdir_View, mdir_Project;
	mdir_View.build_camera_dir(L_pos, L_dir, L_up);

	// Calculate extended projection matrix for frustum calculation
	Fmatrix ex_project, ex_full, ex_full_inverse;
	{
		float OLES_SUN_LIMIT = 100.f;  // Default sun distance limit
		float _far_ = _min(OLES_SUN_LIMIT, g_pGamePersistent->Environment().CurrentEnv->far_plane);
		ex_project.build_projection(deg2rad(Device.fFOV), Device.fASPECT, VIEWPORT_NEAR, _far_);
		ex_full.mul(ex_project, Device.mView);
		ex_full_inverse.invert(ex_full);
	}

	// ========================================================================
	// Step 3: Build light cuboid (frustum volume for this cascade)
	// ========================================================================

	typedef VK::FixedConvexVolume<false> t_cuboid;
	t_cuboid light_cuboid;

	// Initialize frustum rays for this cascade
	if (cascade_ind == 0 || m_sun_cascades[cascade_ind].reset_chain)
	{
		// First cascade or reset - compute rays from view frustum
		Fvector3 near_p, edge_vec;
		for (int p = 0; p < 4; p++)
		{
			Fvector3 asd = VK::wform(ex_full_inverse, VK::corners[VK::facetable[4][p]]);
			near_p.set(asd);

			asd = VK::wform(ex_full_inverse, VK::corners[VK::facetable[5][p]]);
			edge_vec.sub(asd, near_p);
			edge_vec.normalize();

			light_cuboid.view_frustum_rays.push_back(VK::sun::ray(near_p, edge_vec));
		}
	}
	else
	{
		// Use rays from previous cascade
		light_cuboid.view_frustum_rays = m_sun_cascades[cascade_ind].rays;
	}

	light_cuboid.view_ray.P = Device.vCameraPosition;
	light_cuboid.view_ray.D = Device.vCameraDirection;
	light_cuboid.light_ray.P = L_pos;
	light_cuboid.light_ray.D = L_dir;

	// ========================================================================
	// Step 4: Build orthographic projection for shadow map
	// ========================================================================

	// Calculate distance from camera to light plane
	Fplane light_top_plane;
	light_top_plane.build_unit_normal(L_pos, L_dir);
	float dist = light_top_plane.classify(Device.vCameraPosition);

	float map_size = m_sun_cascades[cascade_ind].size;
	mdir_Project.build_projection_ortho(map_size, map_size, 0.1f, dist + map_size);

	// ========================================================================
	// Step 5: Build light cuboid polys (box around view frustum)
	// ========================================================================

	// Define light cuboid geometry (8 points forming a box)
	for (int p = 0; p < 4; ++p)
	{
		int asd = VK::facetable[4][p];
		light_cuboid.light_cuboid_points[p] = light_cuboid.view_frustum_rays[asd].P;
		light_cuboid.light_cuboid_points[p + 4].mad(
			light_cuboid.view_frustum_rays[asd].P,
			light_cuboid.view_frustum_rays[asd].D,
			map_size
		);
	}

	// Define light cuboid polygons (4 sides)
	int planes_indices[4][4] = {
		{0, 4, 6, 2},
		{1, 3, 7, 5},
		{0, 1, 5, 4},
		{3, 2, 6, 7}
	};

	for (int plane = 0; plane < 4; ++plane)
		for (int pt = 0; pt < 4; ++pt)
		{
			int asd = planes_indices[plane][pt];
			light_cuboid.light_cuboid_polys[plane].points[pt] = asd;
		}

	// ========================================================================
	// Step 6: Compute shadow caster model and alignment
	// ========================================================================

	xr_vector<Fplane> cull_planes;
	Fvector3 lightXZshift;

	light_cuboid.compute_caster_model_fixed(
		cull_planes,
		lightXZshift,
		m_sun_cascades[cascade_ind].size,
		m_sun_cascades[cascade_ind].reset_chain
	);

	// Save rays for next cascade
	if (cascade_ind < m_sun_cascades.size() - 1)
		m_sun_cascades[cascade_ind + 1].rays = light_cuboid.view_frustum_rays;

	// ========================================================================
	// Step 7: Build final shadow matrix with alignment
	// ========================================================================

	Fmatrix cull_xform;
	cull_xform.mul(mdir_Project, mdir_View);

	// Align to texel grid (stable shadows)
	{
		// Transform alignment shift to light space
		Fmatrix proj_to_light;
		proj_to_light.invert(cull_xform);

		Fvector cam_pos_light = Device.vCameraPosition;
		proj_to_light.transform(cam_pos_light);

		// Compute texel size
		float texel_size = map_size / float(o.smapsize);

		// Snap to texel grid
		float cam_x_snapped = floorf(cam_pos_light.x / texel_size) * texel_size;
		float cam_z_snapped = floorf(cam_pos_light.z / texel_size) * texel_size;

		// Calculate offset
		Fvector diff;
		diff.x = cam_x_snapped - cam_pos_light.x;
		diff.y = 0;
		diff.z = cam_z_snapped - cam_pos_light.z;

		// Apply offset
		Fmatrix adjust;
		adjust.translate(diff);
		cull_xform.mulB_44(adjust);
	}

	// Store final shadow matrix
	m_sun_cascades[cascade_ind].xform = cull_xform;

	Msg("[Vulkan] Cascade %d shadow matrix computed (size=%.1f)", cascade_ind, map_size);

	// ========================================================================
	// Step 8: Render shadow map (Phase 2.15.2)
	// ========================================================================

	// Call render target to actually render the shadow map depth pass
	// phase_smap_direct will use the matrix we just calculated from m_sun_cascades
	if (RTarget) {
		RTarget->phase_smap_direct(sun, cascade_ind);
	}
}

// ============================================================================
// render_sun() - Main sun rendering entry point (legacy method)
// ============================================================================

void CRender::render_sun()
{
	Msg("[Vulkan] render_sun() - using cascade method");
	render_sun_cascades();
}
