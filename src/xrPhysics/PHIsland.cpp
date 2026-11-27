#include "stdafx.h"
#include "PHIsland.h"
#include "physics.h"
#include "ph_valid_ode.h"
#include "PhysicsShell.h"

void ph_island_debug(CPhysicsShell* shell, int& exact, int& nj, int& nb)
{
	CPHIsland* island = shell ? shell->PIsland() : NULL;
	island = island ? island->DActiveIsland() : NULL;
	if (!island) return;
	exact = island->dbg_exact;
	nj = island->dbg_nj;
	nb = island->dbg_nb;
}

void CPHIsland::Step(dReal step)
{
	if (!m_flags.is_active()) return;
	//dWorldStepFast1	(DWorld(),	fixed_step,	phIterations/*+Random.randI(0,phIterationCycle)*/);
	dbg_nj = nj;
	dbg_nb = nb;
	dbg_exact = 0;
	if ((m_flags.is_exact_integration_forced() && nj < max_joint_allowed_for_forced_exact_integration)
		|| (m_flags.is_exact_integration_prefeared() && nj < max_joint_allowed_for_exeact_integration))
	{
		dbg_exact = 1;
		dWorldStep(DWorld(), fixed_step);
	}
	else
		dWorldQuickStep(DWorld(), fixed_step);
	//dWorldStep(DWorld(),fixed_step);
}

void CPHIsland::Enable()
{
	if (!m_flags.is_active()) return;

	for (dxBody* body = DWorld()->firstbody; body; body = (dxBody *)body->next)body->flags &= ~dxBodyDisabled;
}

void CPHIsland::Repair()
{
	if (!m_flags.is_active()) return;
	dBodyID body;
	for (body = firstbody; body; body = (dxBody *)body->next)
	{
		if (!dV_valid(dBodyGetAngularVel(body)))
			dBodySetAngularVel(body, 0.f, 0.f, 0.f);
		if (!dV_valid(dBodyGetLinearVel(body)))
			dBodySetLinearVel(body, 0.f, 0.f, 0.f);
		if (!dV_valid(dBodyGetPosition(body)))
			dBodySetPosition(body, 0.f, 0.f, 0.f);
		if (!dQ_valid(dBodyGetQuaternion(body)))
		{
			dQuaternion q = {1.f, 0.f, 0.f, 0.f}; //dQSetIdentity(q);
			dBodySetQuaternion(body, q);
		}
	}
}
