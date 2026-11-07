#pragma once

#include "alife_space.h"

class CObject;
class CPhysicsShellHolder;
class IPhysicsShellHolder;

// Vehicle Handling Overhaul: true when obj is a car that ignores direct pushes (impulses, telekinesis,
// gravity changes) from pusher. pusher NULL stands for anomaly or telekinesis sources without an owner.
// Always false for non-cars and for cars without the overhaul.
bool vehicle_blocks_push(CPhysicsShellHolder* obj, CObject* pusher);

// Push share for effects that can be scaled and move anything alike (gravi anomaly pull, burer wave, giant stomp),
// scaled like a fixed impulse by vehicle mass (1 at 1250 kg, at most 1.5). 1 for anything that is not a car with
// the overhaul.
float vehicle_push_scale(CPhysicsShellHolder* obj, CObject* pusher);

// Whirlwind anomalies (CMincer) move loose objects with telekinesis, which cars refuse below a full push.
// These give such cars a scaled drag toward the centre while the anomaly works (rel_power 0..1 by distance)
// and a kick out of it when it bursts. They do nothing for non-cars and for cars that telekinesis can take.
void vehicle_anomaly_pull(CPhysicsShellHolder* obj, CObject* zone, const Fvector& dir, float rel_power);
void vehicle_anomaly_throw(CPhysicsShellHolder* obj, CObject* zone, const Fvector& dir, float power);

// ms a zone recharges at least after it hit obj with hit_type: a car with the overhaul drains an electric
// anomaly (shock_recharge), so the driver can restart and get away before the next discharge. 0 otherwise.
int vehicle_zone_recharge(CObject* obj, ALife::EHitType hit_type);

// Limits a car to one static-geometry impact sound per hit instead of one per contact point and physics step.
bool vehicle_collide_sound(IPhysicsShellHolder* obj);
