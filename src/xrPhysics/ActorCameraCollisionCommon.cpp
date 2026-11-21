#include "stdafx.h"

#include "ActorCameraCollisionInternal.h"

#include "../xrEngine/CameraBase.h"
#include "../xrEngine/gamemtllib.h"

#include "phworld.h"
#include "PHShell.h"
#include "Geometry.h"
#include "iphysicsshellholder.h"
#include "ExtendedGeom.h"

// Mode switch for all implementations (declared in ActorCameraCollisionCommon.h).
XRPHYSICS_API int g_actor_camera_collision_mode = eActorCamCollisionLegacy;

// Sweep limits.
static const float camera_sweep_min_spacing = 0.02f;
static const int camera_sweep_max_samples = 32;
// Bisection steps after the first blocked sample (~1/128 of the sample interval).
static const int camera_sweep_refine_iterations = 7;

// ---------------------------------------------------------------------------------------------
// Near plane
// ---------------------------------------------------------------------------------------------
void camera_near_plane_half_extents(float fov_deg, float viewport_near, float& half_w, float& half_h)
{
	half_h = viewport_near * tanf(deg2rad(fov_deg) * 0.5f);
	float aspect = inl_ph_world().Device().fASPECT;
	if (aspect < EPS)
		aspect = 1.f;
	half_w = half_h / aspect;
}

float camera_near_plane_corner_factor(float fov_deg)
{
	float half_w, half_h;
	camera_near_plane_half_extents(fov_deg, 1.f, half_w, half_h);
	return _sqrt(1.f + half_h * half_h + half_w * half_w);
}

// ---------------------------------------------------------------------------------------------
// Contact filter
// ---------------------------------------------------------------------------------------------
bool camera_contact_ignored(bool bo1, const dContact& c, SGameMtl* material_1, SGameMtl* material_2,
                            IPhysicsShellHolder* actor, float min_mass)
{
	SGameMtl* other_material = bo1 ? material_2 : material_1;
	if (other_material && other_material->Flags.test(SGameMtl::flPassable))
		return true;

	dxGeomUserData* other_data = retrieveGeomUserData(bo1 ? c.geom.g2 : c.geom.g1);
	if (!other_data)
		return false; // static geometry

	if (other_data->ph_object && other_data->ph_object->CastType() == CPHObject::tpCharacter)
		return true;

	IPhysicsShellHolder* other = other_data->ph_ref_object;
	if (!other)
		return false;
	if (other == actor || other->IsInventoryItem())
		return true;
	CPhysicsShell* other_shell = other->ObjectPPhysicsShell();
	return other_shell && other_shell->getMass() < min_mass;
}

// ---------------------------------------------------------------------------------------------
// Sweep
// ---------------------------------------------------------------------------------------------
float camera_sweep(float path_length, float spacing, camera_overlap_fn* overlaps, void* ctx)
{
	VERIFY(overlaps);
	if (path_length < EPS_L)
		return 1.f;

	int samples = iCeil(path_length / _max(spacing, camera_sweep_min_spacing));
	clamp(samples, 1, camera_sweep_max_samples);

	float free_t = 0.f;
	float blocked_t = -1.f;
	for (int i = 1; i <= samples; ++i)
	{
		const float t = float(i) / float(samples);
		if (overlaps(t, ctx))
		{
			blocked_t = t;
			break;
		}
		free_t = t;
	}

	if (blocked_t < 0.f)
		return 1.f;

	for (int i = 0; i < camera_sweep_refine_iterations; ++i)
	{
		const float mid = 0.5f * (free_t + blocked_t);
		if (overlaps(mid, ctx))
			blocked_t = mid;
		else
			free_t = mid;
	}
	return free_t;
}
