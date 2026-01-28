// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#include "stdafx.h"
#include "vk_sun_cascades.h"
#include "vk_rendertarget.h"

namespace VK
{

// ============================================================================
// Constants
// ============================================================================

const float tweak_COP_initial_offs = 1200.f;
const float tweak_ortho_xform_initial_offs = 1000.f;
const float tweak_guaranteed_range = 20.f;

// ============================================================================
// View-frustum bounds tables
// ============================================================================

// D3D uses [0..1] range for Z
// External linkage - visible from rvk_sun.cpp
Fvector3 corners[8] = {
	{-1, -1, 0}, {-1, -1, +1},
	{-1, +1, +1}, {-1, +1, 0},
	{+1, +1, +1}, {+1, +1, 0},
	{+1, -1, +1}, {+1, -1, 0}
};

int facetable[6][4] = {
	{6, 7, 5, 4}, {1, 0, 7, 6},
	{1, 2, 3, 0}, {3, 2, 4, 5},
	// near and far planes
	{0, 3, 5, 7}, {1, 6, 4, 2},
};

// ============================================================================
// Plane/vector helpers
// ============================================================================

#define DW_AS_FLT(DW) (*(FLOAT*)&(DW))
#define FLT_AS_DW(F) (*(DWORD*)&(F))
#define FLT_SIGN(F) ((FLT_AS_DW(F) & 0x80000000L))
#define ALMOST_ZERO(F) ((FLT_AS_DW(F) & 0x7f800000L)==0)
#define IS_SPECIAL(F) ((FLT_AS_DW(F) & 0x7f800000L)==0x7f800000L)

const float _eps = 0.000001f;

// ============================================================================
// Helper functions implementation
// ============================================================================

Fvector3 wform(Fmatrix& m, Fvector3 const& v)
{
	Fvector4 r;
	r.x = v.x * m._11 + v.y * m._21 + v.z * m._31 + m._41;
	r.y = v.x * m._12 + v.y * m._22 + v.z * m._32 + m._42;
	r.z = v.x * m._13 + v.y * m._23 + v.z * m._33 + m._43;
	r.w = v.x * m._14 + v.y * m._24 + v.z * m._34 + m._44;

	if (r.w > 0.f)
	{
		float invW = 1.0f / r.w;
		Fvector3 result;
		result.set(r.x * invW, r.y * invW, r.z * invW);
		return result;
	}
	Fvector3 result;
	result.set(r.x, r.y, r.z);
	return result;
}

BOOL LineIntersection2D(Fvector2* result, const Fvector2* lineA, const Fvector2* lineB)
{
	float x[2] = {lineA[0].x, lineB[0].x};
	float y[2] = {lineA[0].y, lineB[0].y};
	float dx[2] = {lineA[1].x, lineB[1].x};
	float dy[2] = {lineA[1].y, lineB[1].y};

	float x_diff = x[0] - x[1];
	float y_diff = y[0] - y[1];

	float s = (x_diff - (dx[1] / dy[1]) * y_diff) / ((dx[1] * dy[0] / dy[1]) - dx[0]);

	result->x = lineA[0].x + s * lineA[1].x;
	result->y = lineA[0].y + s * lineA[1].y;
	return TRUE;
}

BOOL PlaneIntersection(Fvector3* intersectPt, const Fplane* p0, const Fplane* p1, const Fplane* p2)
{
	Fvector3 n0, n1, n2;
	n0.set(p0->n.x, p0->n.y, p0->n.z);
	n1.set(p1->n.x, p1->n.y, p1->n.z);
	n2.set(p2->n.x, p2->n.y, p2->n.z);

	Fvector3 n1_n2, n2_n0, n0_n1;
	n1_n2.crossproduct(n1, n2);
	n2_n0.crossproduct(n2, n0);
	n0_n1.crossproduct(n0, n1);

	float cosTheta = n0.dotproduct(n1_n2);

	if (ALMOST_ZERO(cosTheta) || IS_SPECIAL(cosTheta))
		return FALSE;

	float secTheta = 1.f / cosTheta;

	n1_n2.mul(p0->d);
	n2_n0.mul(p1->d);
	n0_n1.mul(p2->d);

	intersectPt->x = -(n1_n2.x + n2_n0.x + n0_n1.x) * secTheta;
	intersectPt->y = -(n1_n2.y + n2_n0.y + n0_n1.y) * secTheta;
	intersectPt->z = -(n1_n2.z + n2_n0.z + n0_n1.z) * secTheta;

	return TRUE;
}

// ============================================================================
// Frustum implementation
// ============================================================================

Frustum::Frustum()
{
	for (int i = 0; i < 6; i++) {
		camPlanes[i].n.set(0.f, 0.f, 0.f);
		camPlanes[i].d = 0.f;
	}
}

Frustum::Frustum(const Fmatrix* matrix)
{
	// Build view frustum from matrix
	Fvector4 column4, column1, column2, column3;
	column4.set(matrix->_14, matrix->_24, matrix->_34, matrix->_44);
	column1.set(matrix->_11, matrix->_21, matrix->_31, matrix->_41);
	column2.set(matrix->_12, matrix->_22, matrix->_32, matrix->_42);
	column3.set(matrix->_13, matrix->_23, matrix->_33, matrix->_43);

	Fvector4 planes[6];
	planes[0].sub(column4, column1);  // left
	planes[1].add(column4, column1);  // right
	planes[2].sub(column4, column2);  // bottom
	planes[3].add(column4, column2);  // top
	planes[4].sub(column4, column3);  // near
	planes[5].add(column4, column3);  // far

	// Normalize planes
	for (int p = 0; p < 6; p++)
	{
		float dot = planes[p].x * planes[p].x + planes[p].y * planes[p].y + planes[p].z * planes[p].z;
		dot = 1.f / _sqrt(dot);
		planes[p].mul(dot);
	}

	for (int p = 0; p < 6; p++) {
		camPlanes[p].n.set(planes[p].x, planes[p].y, planes[p].z);
		camPlanes[p].d = planes[p].w;
	}

	// Build vertex LUT
	for (int i = 0; i < 6; i++)
		nVertexLUT[i] = ((planes[i].x < 0.f) ? 1 : 0) | ((planes[i].y < 0.f) ? 2 : 0) | ((planes[i].z < 0.f) ? 4 : 0);

	// Compute frustum corner points
	for (int i = 0; i < 8; i++)
	{
		const Fplane& p0 = (i & 1) ? camPlanes[4] : camPlanes[5];
		const Fplane& p1 = (i & 2) ? camPlanes[3] : camPlanes[2];
		const Fplane& p2 = (i & 4) ? camPlanes[0] : camPlanes[1];
		PlaneIntersection(&pntList[i], &p0, &p1, &p2);
	}
}

// ============================================================================
// BoundingBox implementation
// ============================================================================

BoundingBox::BoundingBox(const Fvector3* points, u32 n)
{
	minPt.set(1e33f, 1e33f, 1e33f);
	maxPt.set(-1e33f, -1e33f, -1e33f);
	for (u32 i = 0; i < n; i++)
		Merge(&points[i]);
}

BoundingBox::BoundingBox(const xr_vector<Fvector3>* points)
{
	minPt.set(1e33f, 1e33f, 1e33f);
	maxPt.set(-1e33f, -1e33f, -1e33f);
	for (u32 i = 0; i < points->size(); i++)
		Merge(&(*points)[i]);
}

BoundingBox::BoundingBox(const xr_vector<BoundingBox>* boxes)
{
	minPt.set(1e33f, 1e33f, 1e33f);
	maxPt.set(-1e33f, -1e33f, -1e33f);
	for (u32 i = 0; i < boxes->size(); i++)
	{
		Merge(&(*boxes)[i].maxPt);
		Merge(&(*boxes)[i].minPt);
	}
}

void BoundingBox::Merge(const Fvector3* vec)
{
	minPt.x = _min(minPt.x, vec->x);
	minPt.y = _min(minPt.y, vec->y);
	minPt.z = _min(minPt.z, vec->z);
	maxPt.x = _max(maxPt.x, vec->x);
	maxPt.y = _max(maxPt.y, vec->y);
	maxPt.z = _max(maxPt.z, vec->z);
}

// ============================================================================
// FixedConvexVolume implementation
// ============================================================================

template <bool _debug>
void FixedConvexVolume<_debug>::compute_planes()
{
	for (u32 it = 0; it < LIGHT_CUBOIDSIDEPOLYS_COUNT; it++)
	{
		_poly& P = light_cuboid_polys[it];
		P.plane.build(light_cuboid_points[P.points[0]],
		              light_cuboid_points[P.points[2]],
		              light_cuboid_points[P.points[1]]);

		// Verify (only in debug)
		if (_debug)
		{
			Fvector& p0 = light_cuboid_points[P.points[0]];
			Fvector& p1 = light_cuboid_points[P.points[1]];
			Fvector& p2 = light_cuboid_points[P.points[2]];
			Fvector& p3 = light_cuboid_points[P.points[3]];
			Fplane p012; p012.build(p0, p1, p2);
			Fplane p123; p123.build(p1, p2, p3);
			Fplane p230; p230.build(p2, p3, p0);
			Fplane p301; p301.build(p3, p0, p1);
			VERIFY(p012.n.similar(p123.n) && p012.n.similar(p230.n) && p012.n.similar(p301.n));
		}
	}
}

template <bool _debug>
void FixedConvexVolume<_debug>::compute_caster_model_fixed(
	xr_vector<Fplane>& dest, Fvector3& translation, float map_size, bool clip_by_view_near)
{
	translation.set(0.f, 0.f, 0.f);

	if (fis_zero(1 - abs(view_ray.D.dotproduct(light_ray.D)), EPS_S))
		return;

	// Compute planes for each polygon
	compute_planes();

	for (u32 i = 0; i < LIGHT_CUBOIDSIDEPOLYS_COUNT; i++)
		VERIFY(light_cuboid_polys[i].plane.classify(light_ray.P) > 0);

	int align_planes[2];
	int align_planes_count = 0;

	// Find one or two planes that align to view frustum from behind
	for (u32 i = 0; i < LIGHT_CUBOIDSIDEPOLYS_COUNT; i++)
	{
		float tmp_dot = view_ray.D.dotproduct(light_cuboid_polys[i].plane.n);
		if (tmp_dot <= EPS_L)
			continue;

		align_planes[align_planes_count] = i;
		++align_planes_count;

		if (align_planes_count == 2)
			break;
	}

	Fvector align_vector;
	align_vector.set(0.f, 0.f, 0.f);

	// Align ray points to the align planes
	for (int p = 0; p < align_planes_count; ++p)
	{
		float min_dist = 10000;
		for (u32 i = 0; i < view_frustum_rays.size(); ++i)
		{
			float tmp_dist = 0;
			Fvector tmp_point = view_frustum_rays[i].P;
			tmp_dist = light_cuboid_polys[align_planes[p]].plane.classify(tmp_point);
			min_dist = _min(tmp_dist, min_dist);
		}

		Fvector shift = light_cuboid_polys[align_planes[p]].plane.n;
		shift.mul(min_dist);
		align_vector.add(shift);
	}

	translation.add(align_vector);
	light_ray.P.add(align_vector);

	align_vector.set(0.f, 0.f, 0.f);

	// Check if view edges intersect, and push planes
	for (int p = 0; p < align_planes_count; ++p)
	{
		float max_mag = 0;
		for (u32 i = 0; i < view_frustum_rays.size(); ++i)
		{
			float plane_dot_ray = view_frustum_rays[i].D.dotproduct(light_cuboid_polys[align_planes[p]].plane.n);
			if (plane_dot_ray < 0)
			{
				Fvector per_plane_view;
				per_plane_view.crossproduct(light_cuboid_polys[align_planes[p]].plane.n, view_ray.D);
				Fvector per_view_to_plane;
				per_view_to_plane.crossproduct(per_plane_view, view_ray.D);

				float tmp_mag = -plane_dot_ray / view_frustum_rays[i].D.dotproduct(per_view_to_plane);
				max_mag = (max_mag < tmp_mag) ? tmp_mag : max_mag;
			}
		}

		if (fis_zero(max_mag))
			continue;

		VERIFY(max_mag <= 1.f);

		float dist = -light_cuboid_polys[align_planes[p]].plane.n.dotproduct(translation);
		align_vector.mad(light_cuboid_polys[align_planes[p]].plane.n, dist * max_mag);
	}

	translation.add(align_vector);
	light_ray.P.add(align_vector);
	translate_light_model(translation);

	// Compute culling planes by rays as edges
	for (u32 i = 0; i < view_frustum_rays.size(); ++i)
	{
		Fvector tmp_vector;
		tmp_vector.crossproduct(view_frustum_rays[i].D, light_ray.D);

		if (fis_zero(tmp_vector.square_magnitude(), EPS))
			continue;

		Fplane tmp_plane;
		tmp_plane.build(view_frustum_rays[i].P, tmp_vector);

		float sign = 0;
		if (check_cull_plane_valid(tmp_plane, sign, 5))
		{
			tmp_plane.n.mul(-sign);
			tmp_plane.d *= -sign;
			dest.push_back(tmp_plane);
		}
	}

	// Compute culling planes by ray points pairs as edges
	if (clip_by_view_near && abs(view_ray.D.dotproduct(light_ray.D)) < 0.8)
	{
		Fvector perp_light_view, perp_light_to_view;
		perp_light_view.crossproduct(view_ray.D, light_ray.D);
		perp_light_to_view.crossproduct(perp_light_view, light_ray.D);

		Fplane plane;
		plane.build(view_ray.P, perp_light_to_view);

		float max_dist = -1000;
		for (u32 i = 0; i < view_frustum_rays.size(); ++i)
			max_dist = _max(plane.classify(view_frustum_rays[i].P), max_dist);

		for (u32 i = 0; i < view_frustum_rays.size(); ++i)
		{
			Fvector P = view_frustum_rays[i].P;
			P.mad(view_frustum_rays[i].D, 5);

			if (plane.classify(P) > max_dist)
			{
				max_dist = 0.f;
				break;
			}
		}

		if (max_dist > -1000)
		{
			plane.d += max_dist;
			dest.push_back(plane);
		}
	}

	// Add light cuboid planes (inverted)
	for (u32 i = 0; i < LIGHT_CUBOIDSIDEPOLYS_COUNT; i++)
	{
		dest.push_back(light_cuboid_polys[i].plane);
		dest.back().n.mul(-1);
		dest.back().d *= -1;
		VERIFY(light_cuboid_polys[i].plane.classify(light_ray.P) > 0);
	}

	// Compute ray intersection with light model (for next cascade)
	for (u32 i = 0; i < view_frustum_rays.size(); ++i)
	{
		float min_dist = 2 * map_size;
		for (int p = 0; p < 4; ++p)
		{
			float dist;
			if ((light_cuboid_polys[p].plane.n.dotproduct(view_frustum_rays[i].D)) > -0.1)
				dist = map_size;
			else
				light_cuboid_polys[p].plane.intersectRayDist(view_frustum_rays[i].P, view_frustum_rays[i].D, dist);

			if (dist > EPS_L && dist < min_dist)
				min_dist = dist;
		}

		view_frustum_rays[i].P.mad(view_frustum_rays[i].D, min_dist);
	}
}

template <bool _debug>
bool FixedConvexVolume<_debug>::check_cull_plane_valid(Fplane const& plane, float& sign, float mad_factor)
{
	bool valid = false;
	bool oriented = false;
	float orient = 0;

	for (u32 j = 0; j < view_frustum_rays.size(); ++j)
	{
		Fvector tmp_pt = view_frustum_rays[j].P;
		tmp_pt.mad(view_frustum_rays[j].D, mad_factor);
		float tmp_dist = plane.classify(tmp_pt);

		if (fis_zero(tmp_dist, EPS_L))
			continue;

		if (!oriented)
		{
			orient = tmp_dist > 0.f ? 1.f : -1.f;
			valid = true;
			oriented = true;
			continue;
		}

		if (tmp_dist < 0 && orient < 0 || tmp_dist > 0 && orient > 0)
			continue;

		valid = false;
		break;
	}

	sign = orient;
	return valid;
}

template <bool _debug>
void FixedConvexVolume<_debug>::translate_light_model(Fvector translate)
{
	for (int i = 0; i < LIGHT_CUBOIDSIDEPOLYS_COUNT; ++i)
		light_cuboid_polys[i].plane.d -= translate.dotproduct(light_cuboid_polys[i].plane.n);
}

// Explicit template instantiations
template class FixedConvexVolume<true>;
template class FixedConvexVolume<false>;

// ============================================================================
// DumbConvexVolume implementation (simplified)
// ============================================================================

template <bool _debug>
void DumbConvexVolume<_debug>::compute_planes()
{
	// Implementation similar to original but simplified for now
	// Will be fully implemented if needed
}

template <bool _debug>
void DumbConvexVolume<_debug>::compute_caster_model(xr_vector<Fplane>& dest, Fvector3 direction)
{
	// Implementation similar to original but simplified for now
	// Will be fully implemented if needed
}

// Explicit template instantiations
template class DumbConvexVolume<true>;
template class DumbConvexVolume<false>;

} // namespace VK
