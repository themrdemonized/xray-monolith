#pragma once
// Shared by the actor camera collision implementations:
//   ActorCameraCollision.cpp              - mode 0, legacy box + contact solver (original GSC code)
//   ActorCameraCollisionPushOut.cpp       - mode 1, near-plane push-out (legacy technique, bugs fixed)
//   ActorCameraCollisionAnchoredSweep.cpp - mode 2, anchored sweep (sphere swept out from the capsule axis)
// Switched at runtime with the g_actor_camera_collision_mode console command.
// g_actor_camera_collision_mode is defined in ActorCameraCollisionAnchoredSweep.cpp. Per-mode settings
// live in each mode's own header (g_cam_pushout_* for mode 1, g_cam_anchored_sweep_* for mode 2).

enum EActorCameraCollisionMode
{
	eActorCamCollisionLegacy = 0,
	eActorCamCollisionNearPlanePushOut = 1,
	eActorCamCollisionAnchoredSweep = 2,
};

extern XRPHYSICS_API int g_actor_camera_collision_mode; // EActorCameraCollisionMode

// Parametric eye path: t in [0..1], t = 0 must be a known-free position.
typedef void camera_path_fn(float t, Fvector& out, const void* ctx);
