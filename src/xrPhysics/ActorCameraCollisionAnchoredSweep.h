#pragma once
// Anchored sweep actor camera collision (alternative to ActorCameraCollision.cpp).
//
// Principle: the eye normally lives on the axis of the actor's movement capsule, which the
// character controller already keeps out of geometry, so it needs no collision at all.
// Collision only matters when something moves the eye *away* from that axis (lean, IK shift).
// In that case a view-rotation-invariant sphere is swept from the capsule axis ("anchor")
// towards the desired eye, and the eye is only ever retracted back along that path.
//
// Selected with g_actor_camera_collision_mode 2 (see ActorCameraCollisionCommon.h).

#include "ActorCameraCollisionCommon.h"

class CCameraBase;
class CPhysicsShell;
class IPhysicsShellHolder;

// Mode 2 settings
extern XRPHYSICS_API float g_cam_anchored_sweep_margin; // extra clearance added to the near-plane sphere, m
extern XRPHYSICS_API float g_cam_anchored_sweep_recover_speed; // 1/s, how fast a retracted eye eases back out
extern XRPHYSICS_API float g_cam_anchored_sweep_min_mass; // dynamic objects lighter than this are ignored, kg

extern XRPHYSICS_API CPhysicsShell* actor_camera_probe_shell;

// Distance from the eye to a near-plane corner is viewport_near * factor.
XRPHYSICS_API float camera_near_plane_corner_factor(float fov_deg);

// True if a sphere at 'center' overlaps blocking geometry (static world + heavy dynamic objects;
// characters, inventory items and passable materials are ignored).
XRPHYSICS_API bool test_camera_sphere(const Fvector& center, float radius, IPhysicsShellHolder* actor);

// Sweeps a sphere along 'path' (approximate length 'path_length') and returns the largest free t.
XRPHYSICS_API float sweep_camera_sphere(camera_path_fn* path, const void* ctx, float path_length, float radius,
                                        IPhysicsShellHolder* actor);

// Resolves camera.vPosition against the world by sweeping from 'anchor' (capsule axis at eye height).
XRPHYSICS_API void collide_camera_anchored_sweep(CCameraBase& camera, const Fvector& anchor, float radius, float dt,
                                                 IPhysicsShellHolder* actor);

// Destroys the probe shell. owner == NULL destroys unconditionally, otherwise only if it belongs to owner.
XRPHYSICS_API void destroy_actor_camera_probe_shell(IPhysicsShellHolder* owner);
