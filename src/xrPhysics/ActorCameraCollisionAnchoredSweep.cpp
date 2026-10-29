#include "stdafx.h"

#include "ActorCameraCollisionAnchoredSweep.h"

#include "../xrEngine/CameraBase.h"
#include "../xrEngine/gamemtllib.h"

#include "phworld.h"
#include "PHShell.h"
#include "Geometry.h"
#include "iphysicsshellholder.h"
#include "ExtendedGeom.h"

#if defined(DEBUG) || defined(DEBUG_DRAW)
#	include "debug_output.h"
extern BOOL dbg_draw_camera_collision; // ActorCameraCollision.cpp
#endif

// ---------------------------------------------------------------------------------------------
// Settings (console: see xrGame/console_commands.cpp)
// ---------------------------------------------------------------------------------------------
// Mode switch for all implementations (declared in ActorCameraCollisionCommon.h).
XRPHYSICS_API int g_actor_camera_collision_mode = eActorCamCollisionLegacy;

XRPHYSICS_API float g_cam_anchored_sweep_margin = 0.02f;
XRPHYSICS_API float g_cam_anchored_sweep_recover_speed = 12.f;
XRPHYSICS_API float g_cam_anchored_sweep_min_mass = 20.f;

XRPHYSICS_API CPhysicsShell* actor_camera_probe_shell = NULL;

// Contacts shallower than this are treated as touching, not overlapping (avoids flicker on contact).
static const float probe_depth_eps = 0.002f;
// Binary-search refinement steps after the first blocked sample (~1/128 of the path).
static const int probe_refine_iterations = 7;
// Upper bound on marching samples per sweep.
static const int probe_max_samples = 32;
static const float probe_shell_mass = 1.f;

// ---------------------------------------------------------------------------------------------
// Probe shell: a single disabled sphere body that is teleported around and only used for
// overlap queries. It never steps and never creates contact joints, so it cannot push anything.
// ---------------------------------------------------------------------------------------------
static bool s_probe_hit = false;
static IPhysicsShellHolder* s_probe_actor = NULL;

static void probe_contact_callback(bool& do_collide, bool bo1, dContact& c, SGameMtl* material_1,
                                   SGameMtl* material_2)
{
	if (!do_collide)
		return; // already rejected (e.g. passable material handled by the engine)
	do_collide = false; // probe only - never generate a contact joint

	SGameMtl* other_material = bo1 ? material_2 : material_1;
	if (other_material && other_material->Flags.test(SGameMtl::flPassable))
		return;

	dxGeomUserData* other_data = retrieveGeomUserData(bo1 ? c.geom.g2 : c.geom.g1);
	if (other_data)
	{
		// Character controllers (actor, NPCs, mutants) are handled by movement collision.
		if (other_data->ph_object && other_data->ph_object->CastType() == CPHObject::tpCharacter)
			return;

		IPhysicsShellHolder* other = other_data->ph_ref_object;
		if (other)
		{
			if (other == s_probe_actor)
				return;
			if (other->IsInventoryItem())
				return;
			CPhysicsShell* other_shell = other->ObjectPPhysicsShell();
			if (other_shell && other_shell->getMass() < g_cam_anchored_sweep_min_mass)
				return;
		}
	}

	if (c.geom.depth > probe_depth_eps)
		s_probe_hit = true;
}

static CPhysicsShell* create_probe_shell(IPhysicsShellHolder* actor)
{
	VERIFY(actor);
	CPhysicsShell* shell = P_create_Shell();
	CPhysicsElement* element = P_create_Element();
	R_ASSERT(element);

	Fsphere sphere;
	sphere.P.set(0.f, 0.f, 0.f);
	sphere.R = 0.2f;
	element->add_Sphere(sphere);
	shell->add_Element(element);
	shell->setMass(probe_shell_mass);
	shell->set_PhysicsRefObject(actor);
	shell->Activate(actor->ObjectXFORM(), 0, actor->ObjectXFORM(), true);

	shell->set_ApplyByGravity(false);
	shell->set_ObjectContactCallback(probe_contact_callback);
	shell->set_ContactCallback(0);

	element->set_local_mass_center(Fvector().set(0.f, 0.f, 0.f));
	VERIFY(element->numberOfGeoms());
	element->geometry(0)->set_local_form_bt(Fidentity);

	shell->DisableCollision();
	shell->Disable();
	return shell;
}

static void update_probe_shell(IPhysicsShellHolder* actor)
{
	if (actor_camera_probe_shell &&
		actor_camera_probe_shell->get_ElementByStoreOrder(0)->PhysicsRefObject() != actor)
		destroy_physics_shell(actor_camera_probe_shell);

	if (!actor_camera_probe_shell)
		actor_camera_probe_shell = create_probe_shell(actor);
}

void destroy_actor_camera_probe_shell(IPhysicsShellHolder* owner)
{
	if (!actor_camera_probe_shell)
		return;
	if (owner && actor_camera_probe_shell->get_ElementByStoreOrder(0)->PhysicsRefObject() != owner)
		return;
	destroy_physics_shell(actor_camera_probe_shell);
}

// RAII scope for a batch of probes: disables actor movement collision once, sets the radius.
class CProbeScope
{
public:
	CProbeScope(IPhysicsShellHolder* actor, float radius)
		: m_actor(actor)
	{
		VERIFY(ph_world);
		VERIFY(!ph_world->Processing());
		VERIFY(actor);
		update_probe_shell(actor);
		m_shell = actor_camera_probe_shell;
		m_element = m_shell->get_ElementByStoreOrder(0);
		CSphereGeom* geom = smart_cast<CSphereGeom*>(m_element->geometry(0));
		VERIFY(geom);
		geom->set_radius(_max(radius, 0.01f));
		s_probe_actor = actor;
		m_actor->MovementCollisionEnable(false);
	}

	~CProbeScope()
	{
		m_actor->MovementCollisionEnable(true);
		s_probe_actor = NULL;
	}

	bool overlaps(const Fvector& center)
	{
		VERIFY(_valid(center));
		Fmatrix xform = Fidentity;
		xform.c.set(center);
		m_element->SetTransform(xform, mh_clear);

		s_probe_hit = false;
		m_shell->EnableCollision();
		m_shell->CollideAll();
		m_shell->DisableCollision();
		m_shell->Disable();
		return s_probe_hit;
	}

private:
	IPhysicsShellHolder* m_actor;
	CPhysicsShell* m_shell;
	CPhysicsElement* m_element;
};

// ---------------------------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------------------------
float camera_near_plane_corner_factor(float fov_deg)
{
	// Same convention as tviewport_size(): fov is vertical, fASPECT is height / width.
	const float half_h = tanf(deg2rad(fov_deg) * 0.5f);
	float aspect = inl_ph_world().Device().fASPECT;
	if (aspect < EPS)
		aspect = 1.f;
	const float half_w = half_h / aspect;
	return _sqrt(1.f + half_h * half_h + half_w * half_w);
}

bool test_camera_sphere(const Fvector& center, float radius, IPhysicsShellHolder* actor)
{
	CProbeScope probe(actor, radius);
	return probe.overlaps(center);
}

float sweep_camera_sphere(camera_path_fn* path, const void* ctx, float path_length, float radius,
                          IPhysicsShellHolder* actor)
{
	VERIFY(path);
	if (path_length < EPS_L)
		return 1.f;

	CProbeScope probe(actor, radius);

	// Conservative march: with a sample spacing below the sphere radius, the sphere centre always
	// lands within one radius in front of any surface it would cross, so thin walls are not skipped.
	const float spacing = _max(radius * 0.75f, 0.02f);
	int samples = iCeil(path_length / spacing);
	clamp(samples, 1, probe_max_samples);

	float free_t = 0.f; // t = 0 is the anchor, assumed free
	float blocked_t = -1.f;
	Fvector p;
	for (int i = 1; i <= samples; ++i)
	{
		const float t = float(i) / float(samples);
		path(t, p, ctx);
		if (probe.overlaps(p))
		{
			blocked_t = t;
			break;
		}
		free_t = t;
	}

	if (blocked_t < 0.f)
		return 1.f;

	for (int i = 0; i < probe_refine_iterations; ++i)
	{
		const float mid = 0.5f * (free_t + blocked_t);
		path(mid, p, ctx);
		if (probe.overlaps(p))
			blocked_t = mid;
		else
			free_t = mid;
	}
	return free_t;
}

// ---------------------------------------------------------------------------------------------
// Per-frame camera resolve
// ---------------------------------------------------------------------------------------------
struct SSegmentPath
{
	Fvector from;
	Fvector delta;
};

static void segment_path(float t, Fvector& out, const void* ctx)
{
	const SSegmentPath& s = *static_cast<const SSegmentPath*>(ctx);
	out.mad(s.from, s.delta, t);
}

// Smoothing state (one local player camera).
static IPhysicsShellHolder* s_smooth_actor = NULL;
static u32 s_smooth_frame = 0;
static float s_allowed_dist = 0.f; // smoothed free distance from the anchor
static float s_prev_desired_dist = 0.f;

void collide_camera_anchored_sweep(CCameraBase& camera, const Fvector& anchor, float radius, float dt,
                                 IPhysicsShellHolder* actor)
{
	VERIFY(actor);
	const Fvector desired = camera.vPosition;
	SSegmentPath seg;
	seg.from.set(anchor);
	seg.delta.sub(desired, anchor);
	const float desired_dist = seg.delta.magnitude();

	// Reset smoothing when the camera owner changes or the solver was not running last frame
	// (mode toggled, level loaded, cutscene...).
	const u32 frame = inl_ph_world().Device().dwFrame;
	const bool fresh = (s_smooth_actor != actor) || (frame > s_smooth_frame + 1) || (frame < s_smooth_frame);
	s_smooth_actor = actor;
	s_smooth_frame = frame;

	float allowed_dist = desired_dist;
	if (desired_dist > EPS_L)
		allowed_dist = desired_dist * sweep_camera_sphere(segment_path, &seg, desired_dist, radius, actor);

	// Retract instantly (never clip), recover gradually (never pop). While unrestricted the eye simply
	// follows the desired offset so lean / IK motion is not delayed.
	const bool was_unrestricted = s_allowed_dist >= s_prev_desired_dist - EPS_L;
	if (fresh || allowed_dist <= s_allowed_dist || was_unrestricted)
		s_allowed_dist = allowed_dist;
	else
		s_allowed_dist += (allowed_dist - s_allowed_dist) * (1.f - expf(-g_cam_anchored_sweep_recover_speed * dt));
	s_prev_desired_dist = desired_dist;

	const float use_dist = _min(s_allowed_dist, desired_dist);
	if (desired_dist > EPS_L && use_dist < desired_dist)
		camera.vPosition.mad(anchor, seg.delta, use_dist / desired_dist);

#if defined(DEBUG) || defined(DEBUG_DRAW)
	if (dbg_draw_camera_collision)
	{
		debug_output().DBG_DrawLine(anchor, desired, D3DCOLOR_XRGB(255, 0, 0));
		debug_output().DBG_DrawPoint(camera.vPosition, radius, D3DCOLOR_XRGB(0, 255, 0));
	}
#endif
}
