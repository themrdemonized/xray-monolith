#pragma once
// Near-plane push-out actor camera collision: the legacy technique (box + contact push-out, see
// ActorCameraCollision.cpp) with its defects fixed. Selected with g_actor_camera_collision_mode 1
// (see ActorCameraCollisionCommon.h).
//
// Fixes over the legacy implementation:
//  - the box is exactly the volume between the eye and the near plane (+ margin); the legacy box
//    was 1.5x taller, 2x deeper, shifted down and reached ~0.3 m behind the eye
//  - the box is sized from the unzoomed FOV, so aiming down sights does not resize it
//  - no NPC cylinder and no collision with character controllers (stalkers no longer shove the view)
//  - inventory items and light physics objects are ignored
//  - push-out stops when it stalls instead of always burning up to 100 solver steps
//  - the "pose unchanged" early-out compares desired poses (legacy compared against the pushed
//    result, so it never triggered while touching geometry)
//  - the push is limited to the box's own size and eased out instead of popping
//  - lean: searches towards the requested side, keeps the last free angle (legacy kept the first
//    colliding one) and uses march + bisection instead of up to ~125 tests in PI/1000 steps

#include "ActorCameraCollisionCommon.h"

class CCameraBase;
class CPhysicsShell;
class IPhysicsShellHolder;

// Mode 1 settings
extern XRPHYSICS_API float g_cam_pushout_margin; // clearance added around the near-plane box, m
extern XRPHYSICS_API float g_cam_pushout_recover_speed; // 1/s, how fast a push-out eases back
extern XRPHYSICS_API float g_cam_pushout_min_mass; // dynamic objects lighter than this are ignored, kg

extern XRPHYSICS_API CPhysicsShell* actor_camera_pushout_shell;

// Resolves camera.vPosition by pushing a near-plane box out of geometry.
XRPHYSICS_API void collide_camera_pushout(CCameraBase& camera, float viewport_near, float fov_deg, float dt,
                                        IPhysicsShellHolder* actor);

// Moves the near-plane box (rotation from 'orientation' i/j/k = right/up/direction) along 'path' (eye
// positions) and returns the largest t at which it does not overlap geometry.
XRPHYSICS_API float sweep_camera_pushout_box(camera_path_fn* path, const void* ctx, float path_length,
                                           const Fmatrix& orientation, float fov_deg, float viewport_near,
                                           IPhysicsShellHolder* actor);

// Destroys the shell. owner == NULL destroys unconditionally, otherwise only if it belongs to owner.
XRPHYSICS_API void destroy_actor_camera_pushout_shell(IPhysicsShellHolder* owner);
