#include "stdafx.h"

#include "ActorCameraCollisionPushOut.h"

#include "../xrEngine/CameraBase.h"
#include "../xrEngine/gamemtllib.h"

#include "phworld.h"
#include "phcollidevalidator.h"
#include "PHShell.h"
#include "Geometry.h"
#include "matrix_utils.h"
#include "iphysicsshellholder.h"
#include "ExtendedGeom.h"

#if defined(DEBUG) || defined(DEBUG_DRAW)
#	include "debug_output.h"
extern BOOL dbg_draw_camera_collision; // ActorCameraCollision.cpp
#endif

extern dJointGroupID ContactGroup;

XRPHYSICS_API CPhysicsShell* actor_camera_pushout_shell = NULL;

// Settings (console: see xrGame/console_commands.cpp)
XRPHYSICS_API float g_cam_pushout_margin = 0.02f;
XRPHYSICS_API float g_cam_pushout_recover_speed = 12.f;
XRPHYSICS_API float g_cam_pushout_min_mass = 20.f;

// Same body parameters as the legacy camera shell.
static const float pushout_camera_mass = 10.f;
static const float pushout_camera_mass_size = 10000000.f;
static const float pushout_soft_cfm = 0.01f;

// Push-out solver limits. The legacy solver always ran until no contact remained (up to 100 steps).
static const u16 pushout_max_steps = 50;
static const float pushout_stall_distance = 0.0002f; // box moved less than this in a step -> wedged, stop

// Re-solve at least this often even if the desired pose is unchanged (moving doors, vehicles...).
static const u32 pushout_cache_max_age_frames = 10;

// Lean sweep.
static const int pushout_refine_iterations = 7;
static const int pushout_max_samples = 32;

// ---------------------------------------------------------------------------------------------
// Contact handling
// ---------------------------------------------------------------------------------------------
static bool s_collided = false;
static bool s_step = false;
static IPhysicsShellHolder* s_actor = NULL;

static bool ignore_contact(bool bo1, dContact& c, SGameMtl* material_1, SGameMtl* material_2)
{
	SGameMtl* other_material = bo1 ? material_2 : material_1;
	if (other_material && other_material->Flags.test(SGameMtl::flPassable))
		return true;

	dxGeomUserData* other_data = retrieveGeomUserData(bo1 ? c.geom.g2 : c.geom.g1);
	if (!other_data)
		return false; // static geometry

	// Character controllers (actor, NPCs, mutants) are handled by movement collision.
	if (other_data->ph_object && other_data->ph_object->CastType() == CPHObject::tpCharacter)
		return true;

	IPhysicsShellHolder* other = other_data->ph_ref_object;
	if (!other)
		return false;
	if (other == s_actor || other->IsInventoryItem())
		return true;
	CPhysicsShell* other_shell = other->ObjectPPhysicsShell();
	return other_shell && other_shell->getMass() < g_cam_pushout_min_mass;
}

static void pushout_collide_callback(bool& do_collide, bool bo1, dContact& c, SGameMtl* material_1,
                                   SGameMtl* material_2)
{
	if (!do_collide)
		return;
	do_collide = false; // contacts are created here, only for the camera body
	if (ignore_contact(bo1, c, material_1, material_2))
		return;

	if (c.geom.depth > g_cam_pushout_margin * 0.5f)
		s_collided = true;

	if (!s_step)
		return;

	c.surface.soft_cfm = pushout_soft_cfm;
	c.surface.mu = 0;
	dJointID contact_joint = dJointCreateContactSpecial(0, ContactGroup, &c);
	dxGeomUserData* my_data = retrieveGeomUserData(bo1 ? c.geom.g1 : c.geom.g2);
	VERIFY(my_data);
	CPHObject* obj = (CPHObject*)my_data->callback_data;
	VERIFY(obj);
#if defined(DEBUG) || defined(DEBUG_DRAW)
	if (dbg_draw_camera_collision)
		debug_output().DBG_DrawContact(c);
#endif
	obj->Island().DActiveIsland()->ConnectJoint(contact_joint);
	if (bo1)
		dJointAttach(contact_joint, dGeomGetBody(c.geom.g1), 0);
	else
		dJointAttach(contact_joint, 0, dGeomGetBody(c.geom.g2));
}

// ---------------------------------------------------------------------------------------------
// Shell (box only - no NPC cylinder)
// ---------------------------------------------------------------------------------------------
static CPhysicsShell* create_pushout_shell(IPhysicsShellHolder* actor)
{
	VERIFY(actor);
	CPhysicsShell* shell = P_build_SimpleShell(actor, pushout_camera_mass, true);
	CPhysicsElement* root = shell->get_ElementByStoreOrder(0);
	VERIFY(root);

	shell->set_ApplyByGravity(false);
	shell->set_ObjectContactCallback(pushout_collide_callback);
	shell->set_ContactCallback(0);
	shell->set_CallbackData(smart_cast<CPHObject*>(shell));

	dMass m;
	dMassSetSphere(&m, 1, pushout_camera_mass_size);
	dMassAdjust(&m, pushout_camera_mass);
	shell->setEquelInertiaForEls(m);

	root->set_local_mass_center(Fvector().set(0.f, 0.f, 0.f));
	VERIFY(root->numberOfGeoms());
	root->geometry(0)->set_local_form_bt(Fidentity);

	shell->DisableCollision();
	shell->Disable();
	return shell;
}

static void update_pushout_shell(IPhysicsShellHolder* actor)
{
	if (actor_camera_pushout_shell &&
		actor_camera_pushout_shell->get_ElementByStoreOrder(0)->PhysicsRefObject() != actor)
		destroy_physics_shell(actor_camera_pushout_shell);

	if (!actor_camera_pushout_shell)
		actor_camera_pushout_shell = create_pushout_shell(actor);
}

void destroy_actor_camera_pushout_shell(IPhysicsShellHolder* owner)
{
	if (!actor_camera_pushout_shell)
		return;
	if (owner && actor_camera_pushout_shell->get_ElementByStoreOrder(0)->PhysicsRefObject() != owner)
		return;
	destroy_physics_shell(actor_camera_pushout_shell);
}

// ---------------------------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------------------------

// Box covering exactly the space between the eye and the near plane, plus margin on every side.
// Same FOV / aspect convention as tviewport_size(): fov is vertical, fASPECT is height / width.
static void near_plane_box(Fvector& half_size, Fmatrix& xform, const Fvector& eye, const Fmatrix& orientation,
                           float fov_deg, float viewport_near)
{
	const float half_h = viewport_near * tanf(deg2rad(fov_deg) * 0.5f);
	float aspect = inl_ph_world().Device().fASPECT;
	if (aspect < EPS)
		aspect = 1.f;
	const float half_w = half_h / aspect;
	const float margin = g_cam_pushout_margin;
	half_size.set(half_w + margin, half_h + margin, viewport_near * 0.5f + margin);

	xform.identity();
	xform.i.set(orientation.i);
	xform.j.set(orientation.j);
	xform.k.set(orientation.k);
	xform.c.mad(eye, orientation.k, viewport_near * 0.5f);
}

// RAII scope for work with the shell: disables actor movement collision, sets the box size.
class CPushOutScope
{
public:
	CPushOutScope(IPhysicsShellHolder* actor, const Fvector& half_size)
		: m_actor(actor)
	{
		VERIFY(ph_world);
		VERIFY(!ph_world->Processing());
		VERIFY(actor);
		update_pushout_shell(actor);
		m_shell = actor_camera_pushout_shell;
		m_root = m_shell->get_ElementByStoreOrder(0);
		CBoxGeom* box = smart_cast<CBoxGeom*>(m_root->geometry(0));
		VERIFY(box);
		box->set_size(half_size);
		s_actor = actor;
		s_step = false;
		m_actor->MovementCollisionEnable(false);
	}

	~CPushOutScope()
	{
		s_step = false;
		m_actor->MovementCollisionEnable(true);
		s_actor = NULL;
	}

	bool overlaps(const Fmatrix& xform)
	{
		VERIFY(_valid(xform));
		m_root->SetTransform(xform, mh_clear);
		s_collided = false;
		m_shell->EnableCollision();
		m_shell->CollideAll();
		m_shell->DisableCollision();
		m_shell->Disable();
		return s_collided;
	}

	// Returns how far the box centre had to move to get out of geometry.
	Fvector push_out(const Fmatrix& xform)
	{
		Fvector push = {0.f, 0.f, 0.f};
		m_root->SetTransform(xform, mh_clear);
		s_collided = false;
		m_shell->EnableCollision();
		m_shell->CollideAll();

		if (s_collided)
		{
			s_step = true;
			const Fquaternion rotation = Fquaternion().set(xform);
			Fvector prev = xform.c;
			for (u16 i = 0; i < pushout_max_steps; ++i)
			{
				m_shell->set_LinearVel(Fvector().set(0.f, 0.f, 0.f));
				m_shell->set_AngularVel(Fvector().set(0.f, 0.f, 0.f));
				m_root->setQuaternion(rotation);
				s_collided = false;
				m_shell->PureStep();

				Fvector pos;
				m_root->GetGlobalPositionDynamic(&pos);
				if (!s_collided || pos.distance_to(prev) < pushout_stall_distance)
					break;
				prev = pos;
			}
			s_step = false;

			Fvector pos;
			m_root->GetGlobalPositionDynamic(&pos);
			push.sub(pos, xform.c);
		}

		m_shell->DisableCollision();
		m_shell->Disable();
		return push;
	}

private:
	IPhysicsShellHolder* m_actor;
	CPhysicsShell* m_shell;
	CPhysicsElement* m_root;
};

// ---------------------------------------------------------------------------------------------
// Per-frame camera resolve
// ---------------------------------------------------------------------------------------------
static IPhysicsShellHolder* s_state_actor = NULL;
static u32 s_state_frame = 0;
static Fmatrix s_cache_xform = Fidentity; // desired box pose of the last solve
static Fvector s_cache_half = {0.f, 0.f, 0.f};
static Fvector s_cache_push = {0.f, 0.f, 0.f};
static u32 s_cache_frame = 0;
static Fvector s_push = {0.f, 0.f, 0.f}; // smoothed push applied to the camera

static bool same_pose(const Fmatrix& a, const Fmatrix& b)
{
	return a.c.similar(b.c, 0.0005f) && a.i.similar(b.i, 0.0005f) && a.j.similar(b.j, 0.0005f) &&
		a.k.similar(b.k, 0.0005f);
}

void collide_camera_pushout(CCameraBase& camera, float viewport_near, float fov_deg, float dt,
                          IPhysicsShellHolder* actor)
{
	VERIFY(actor);
	Fmatrix orientation = Fidentity;
	orientation.i.set(camera.Right());
	orientation.j.set(camera.Up());
	orientation.k.set(camera.Direction());

	Fvector half_size;
	Fmatrix xform;
	near_plane_box(half_size, xform, camera.vPosition, orientation, fov_deg, viewport_near);

	// Reset state when the owner changes or this mode was not running last frame.
	const u32 frame = inl_ph_world().Device().dwFrame;
	const bool fresh = (s_state_actor != actor) || (frame > s_state_frame + 1) || (frame < s_state_frame);
	s_state_actor = actor;
	s_state_frame = frame;

	Fvector push;
	const bool cache_hit = !fresh && same_pose(s_cache_xform, xform) && s_cache_half.similar(half_size, 0.0005f) &&
		frame - s_cache_frame < pushout_cache_max_age_frames;
	if (cache_hit)
		push = s_cache_push;
	else
	{
		CPushOutScope scope(actor, half_size);
		push = scope.push_out(xform);
		s_cache_xform.set(xform);
		s_cache_half.set(half_size);
		s_cache_push.set(push);
		s_cache_frame = frame;
	}

	// The box never legitimately needs to move further than its own size; a larger push means the
	// solver got wedged or picked the far side of thin geometry.
	const float max_push = half_size.magnitude();
	const float push_len = push.magnitude();
	if (push_len > max_push)
		push.mul(max_push / push_len);

	// Increase the correction instantly (never clip), decrease it gradually (never pop).
	if (fresh || push.magnitude() >= s_push.magnitude())
		s_push.set(push);
	else
		s_push.lerp(s_push, push, 1.f - expf(-g_cam_pushout_recover_speed * dt));

	camera.vPosition.add(s_push);

#if defined(DEBUG) || defined(DEBUG_DRAW)
	// Drawn every frame from the solver's inputs/outputs (not from the shell, which only holds valid
	// data on cache misses and may have been moved by lean probes since).
	// Red: requested near-plane box. Green: box at the applied (capped + smoothed) push.
	// Contacts are still drawn from the solver itself, so they only appear on frames that re-solve.
	if (dbg_draw_camera_collision)
	{
		debug_output().DBG_DrawOBB(xform, half_size, D3DCOLOR_XRGB(255, 0, 0));
		Fmatrix resolved = xform;
		resolved.c.add(s_push);
		debug_output().DBG_DrawOBB(resolved, half_size, D3DCOLOR_XRGB(0, 255, 0));
		debug_output().DBG_DrawMatrix(Fmatrix().translate(resolved.c), 0.1f);
	}
#endif
}

// ---------------------------------------------------------------------------------------------
// Lean
// ---------------------------------------------------------------------------------------------
float sweep_camera_pushout_box(camera_path_fn* path, const void* ctx, float path_length, const Fmatrix& orientation,
                             float fov_deg, float viewport_near, IPhysicsShellHolder* actor)
{
	VERIFY(path);
	if (path_length < EPS_L)
		return 1.f;

	Fvector half_size;
	Fmatrix xform;
	Fvector eye;
	path(0.f, eye, ctx);
	near_plane_box(half_size, xform, eye, orientation, fov_deg, viewport_near);

	CPushOutScope scope(actor, half_size);

	// Sample spacing below the box's smallest extent so thin geometry cannot be stepped over.
	const float smallest = _min(half_size.x, _min(half_size.y, half_size.z));
	const float spacing = _max(smallest * 1.5f, 0.02f);
	int samples = iCeil(path_length / spacing);
	clamp(samples, 1, pushout_max_samples);

	float free_t = 0.f; // upright pose
	float blocked_t = -1.f;
	for (int i = 1; i <= samples; ++i)
	{
		const float t = float(i) / float(samples);
		path(t, eye, ctx);
		near_plane_box(half_size, xform, eye, orientation, fov_deg, viewport_near);
		if (scope.overlaps(xform))
		{
			blocked_t = t;
			break;
		}
		free_t = t;
	}

	if (blocked_t < 0.f)
		return 1.f;

	for (int i = 0; i < pushout_refine_iterations; ++i)
	{
		const float mid = 0.5f * (free_t + blocked_t);
		path(mid, eye, ctx);
		near_plane_box(half_size, xform, eye, orientation, fov_deg, viewport_near);
		if (scope.overlaps(xform))
			blocked_t = mid;
		else
			free_t = mid;
	}
	return free_t;
}
