// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#pragma once
#include "../../xrEngine/xr_object_list.h"

namespace VK
{

// Forward declarations
class CRender;

// ============================================================================
// Sun ray structure
// ============================================================================

namespace sun
{
	struct ray
	{
		Fvector P;  // Point
		Fvector D;  // Direction

		ray() {}
		ray(const Fvector& _P, const Fvector& _D) : P(_P), D(_D) {}
	};
}

// ============================================================================
// Sun cascade structure
// ============================================================================

struct SunCascade
{
	Fmatrix xform;                     // Shadow matrix (world → light space)
	float size;                        // Map size in world units
	float bias;                        // Depth bias
	xr_vector<sun::ray> rays;          // Frustum rays for next cascade
	bool reset_chain;                  // Reset cascade chain

	// Shadow atlas viewport offset (for cascades sharing one shadow map)
	u32 posX;                          // Viewport X offset in shadow atlas
	u32 posY;                          // Viewport Y offset in shadow atlas
	u32 viewport_size;                 // Viewport size in pixels

	SunCascade() : size(20.f), bias(0.f), reset_chain(false),
	               posX(0), posY(0), viewport_size(1024)
	{
		xform.identity();
	}
};

// ============================================================================
// Constants
// ============================================================================

const u32 LIGHT_CUBOIDSIDEPOLYS_COUNT = 4;
const u32 LIGHT_CUBOIDVERTICES_COUNT = 2 * LIGHT_CUBOIDSIDEPOLYS_COUNT;

// ============================================================================
// Fixed Convex Volume (Light Cuboid) - для cascade shadows
// ============================================================================

template <bool _debug>
class FixedConvexVolume
{
public:
	struct _poly
	{
		int points[4];
		Fplane plane;
	};

	xr_vector<sun::ray> view_frustum_rays;
	sun::ray view_ray;
	sun::ray light_ray;
	Fvector3 light_cuboid_points[LIGHT_CUBOIDVERTICES_COUNT];
	_poly light_cuboid_polys[LIGHT_CUBOIDSIDEPOLYS_COUNT];

public:
	void compute_planes();
	void compute_caster_model_fixed(xr_vector<Fplane>& dest, Fvector3& translation,
	                                float map_size, bool clip_by_view_near);

private:
	bool check_cull_plane_valid(Fplane const& plane, float& sign, float mad_factor = 0.f);
	void translate_light_model(Fvector translate);
};

// ============================================================================
// Dumb Convex Volume - для старого метода (kept for compatibility)
// ============================================================================

template <bool _debug>
class DumbConvexVolume
{
public:
	struct _poly
	{
		xr_vector<int> points;
		Fvector3 planeN;
		float planeD;
		float classify(Fvector3& p) { return planeN.dotproduct(p) + planeD; }
	};

	struct _edge
	{
		int p0, p1;
		int counter;
		_edge(int _p0, int _p1, int m) : p0(_p0), p1(_p1), counter(m)
		{
			if (p0 > p1) swap(p0, p1);
		}
		bool equal(_edge& E) { return p0 == E.p0 && p1 == E.p1; }
	};

public:
	xr_vector<Fvector3> points;
	xr_vector<_poly> polys;
	xr_vector<_edge> edges;

public:
	void compute_planes();
	void compute_caster_model(xr_vector<Fplane>& dest, Fvector3 direction);
};

// ============================================================================
// View-frustum bounds tables (defined in vk_sun_cascades.cpp)
// ============================================================================

// NDC frustum corners
extern Fvector3 corners[8];

// Frustum face index table
extern int facetable[6][4];

// ============================================================================
// Helper functions
// ============================================================================

// Transform vertex from clip space to world space
Fvector3 wform(Fmatrix& m, Fvector3 const& v);

// Line intersection in 2D
BOOL LineIntersection2D(Fvector2* result, const Fvector2* lineA, const Fvector2* lineB);

// Plane intersection (3 planes → point)
BOOL PlaneIntersection(Fvector3* intersectPt, const Fplane* p0, const Fplane* p1, const Fplane* p2);

// ============================================================================
// Frustum structure (for shadow matrix calculation)
// ============================================================================

struct Frustum
{
	Fplane camPlanes[6];
	int nVertexLUT[6];
	Fvector3 pntList[8];

	Frustum();
	Frustum(const Fmatrix* matrix);
};

// ============================================================================
// Bounding Box structure
// ============================================================================

struct BoundingBox
{
	Fvector3 minPt;
	Fvector3 maxPt;

	BoundingBox()
	{
		minPt.set(1e33f, 1e33f, 1e33f);
		maxPt.set(-1e33f, -1e33f, -1e33f);
	}

	BoundingBox(const BoundingBox& other) : minPt(other.minPt), maxPt(other.maxPt) {}

	explicit BoundingBox(const Fvector3* points, u32 n);
	explicit BoundingBox(const xr_vector<Fvector3>* points);
	explicit BoundingBox(const xr_vector<BoundingBox>* boxes);

	void Centroid(Fvector3* vec) const
	{
		vec->set(
			0.5f * (minPt.x + maxPt.x),
			0.5f * (minPt.y + maxPt.y),
			0.5f * (minPt.z + maxPt.z)
		);
	}

	void Merge(const Fvector3* vec);

	Fvector3 Point(int i) const
	{
		Fvector3 result;
		result.set(
			(i & 1) ? minPt.x : maxPt.x,
			(i & 2) ? minPt.y : maxPt.y,
			(i & 4) ? minPt.z : maxPt.z
		);
		return result;
	}
};

} // namespace VK
