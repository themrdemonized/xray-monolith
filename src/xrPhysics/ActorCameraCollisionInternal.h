#pragma once
// xrPhysics-internal helpers shared by the alternative actor camera collision modes
// (ActorCameraCollisionPushOut.cpp - mode 1, ActorCameraCollisionAnchoredSweep.cpp - mode 2).
// Not exported; xrGame only needs ActorCameraCollisionCommon.h and the per-mode headers.

#include "ActorCameraCollisionCommon.h"

struct dContact;
struct SGameMtl;
class IPhysicsShellHolder;

// Half extents of the near plane at distance viewport_near. Same convention as tviewport_size():
// fov is vertical, fASPECT is height / width.
void camera_near_plane_half_extents(float fov_deg, float viewport_near, float& half_w, float& half_h);

// Contact filter shared by the camera collision volumes. Returns true if the contact must not block
// the camera: passable materials, character controllers (actor, NPCs, mutants - handled by movement
// collision), the actor itself, inventory items and dynamic objects lighter than min_mass.
bool camera_contact_ignored(bool bo1, const dContact& c, SGameMtl* material_1, SGameMtl* material_2,
                            IPhysicsShellHolder* actor, float min_mass);

// Overlap test at path parameter t, used by camera_sweep().
typedef bool camera_overlap_fn(float t, void* ctx);

// Marches t from 0 to 1 in steps of at most 'spacing' (in path length units), then bisects between the
// last free and the first blocked sample. t = 0 is assumed free. Returns the largest free t (1 if the
// whole path is free).
float camera_sweep(float path_length, float spacing, camera_overlap_fn* overlaps, void* ctx);
