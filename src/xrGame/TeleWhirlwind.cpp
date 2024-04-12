#include "stdafx.h"
#include "telewhirlwind.h"
#include "../xrphysics/PhysicsShell.h"
#include "PhysicsShellHolder.h"
#include "level.h"
#include "hit.h"
#include "phdestroyable.h"
#include "xrmessages.h"
#include "../Include/xrRender/Kinematics.h"
#include "../Include/xrRender/KinematicsAnimated.h"
#include "entity_alive.h"

CTeleWhirlwind::CTeleWhirlwind()
{
	m_fTeleTime = 2900.f;
	m_iTelekinesisType = 0;
	m_bHeightFixed = true;
	m_iOwnerID = 0;
	m_vCenter.set(0.f, 0.f, 0.f);
	m_fKeepRadius = 1.f;
	m_fThrowPower = 100.f;
	m_bSpawnSkeleton = true;
}

CTelekineticObject* CTeleWhirlwind::activate(CPhysicsShellHolder* obj, float strength, float height, u32 max_time_keep,
                                             bool rot)
{
	if (inherited::activate(obj, strength, height, max_time_keep, rot))
	{
		CTelekineticObject* o = objects.back();
		if (smart_cast<CTeleWhirlwindObject*>(o)) 
		{
			smart_cast<CTeleWhirlwindObject*>(o)->set_throw_power(m_fThrowPower);
		}
		else if (smart_cast<CTeleTrampolinObject*>(o))
		{
			smart_cast<CTeleTrampolinObject*>(o)->set_throw_power(m_fThrowPower);
		}
		return o;
	}
	else
		return 0;
}

void CTeleWhirlwind::Load(LPCSTR section)
{
	if (pSettings->line_exist(section, "tele_type"))
	{
		m_iTelekinesisType = pSettings->r_u16(section, "tele_type");
	}

	if (pSettings->line_exist(section, "tele_height_fixed"))
	{
		m_bHeightFixed = pSettings->r_bool(section, "tele_height_fixed");
	}

	if (pSettings->line_exist(section, "tele_time"))
	{
		m_fTeleTime = pSettings->r_u32(section, "tele_time");
	}

	if (pSettings->line_exist(section, "tele_rotate_speed"))
	{
		m_fTeleRotateSpeed = pSettings->r_u32(section, "tele_rotate_speed");
	}

	if (pSettings->line_exist(section, "spawn_skeleton"))
	{
		m_bSpawnSkeleton = pSettings->r_bool(section, "spawn_skeleton");
	}

	m_pTearingSound.create(pSettings->r_string(section, "body_tearing_sound"), st_Effect, sg_SourceType);
	m_sTearingParticles = shared_str(pSettings->r_string(section, "tearing_particles"));
	m_fThrowPower = pSettings->r_float(section, "throw_out_impulse");

}

void CTeleWhirlwind::ClearImpacts()
{
	m_vSavedImpacts.clear();
}

void CTeleWhirlwind::clear()
{
	inherited::clear();
}

void CTeleWhirlwind::AddImpact(const Fvector& dir, float val)
{
	Fvector force, point;
	force.set(dir);
	force.mul(val);
	point.set(0.f, 0.f, 0.f);
	m_vSavedImpacts.push_back(SPHImpact(force, point, 0));
}

void CTeleWhirlwind::DrawOutImpact(Fvector& dir, float& val)
{
	VERIFY2(m_vSavedImpacts.size(), "NO IMPACTS ADDED!");
	if (0 == m_vSavedImpacts.size()) return;
	dir.set(m_vSavedImpacts[0].force);
	val = dir.magnitude();
	m_vSavedImpacts.erase(m_vSavedImpacts.begin()); //swartz
}

static bool RemovePred(CTelekineticObject* tele_object)
{
	return (!tele_object->get_object() ||tele_object->get_object()->getDestroy());
}

void CTeleWhirlwind::clear_notrelevant()
{
	//убрать все объеты со старыми параметрами
	objects.erase(
		std::remove_if(
			objects.begin(),
			objects.end(),
			&RemovePred
		),
		objects.end()
	);
}

CTeleWhirlwindObject::CTeleWhirlwindObject()
{
	m_bDestroyable = false;
	m_pTelekinesis = NULL;
	m_fThrowPower = 0.f;
}


bool CTeleWhirlwindObject::init(CTelekinesis* tele, CPhysicsShellHolder* obj, float s, float h, u32 ttk, bool rot)
{
	bool result = inherited::init(tele, obj, s, h, ttk, rot);
	m_pTelekinesis = static_cast<CTeleWhirlwind*>(tele);
	m_fThrowPower = strength;
	if (m_pTelekinesis->is_active_object(obj))
	{
		return false;
	}
	if (obj->PPhysicsShell())
	{
		obj->PPhysicsShell()->SetAirResistance(0.f, 0.f);
		obj->m_pPhysicsShell->set_ApplyByGravity(TRUE);
	}

	if (object->ph_destroyable() && object->ph_destroyable()->CanDestroy())
		m_bDestroyable = true;
	else
		m_bDestroyable = false;

	return result;
}

void CTeleWhirlwindObject::raise_update()
{

}

void CTeleWhirlwindObject::release()
{
	if (!object || object->getDestroy() || !object->m_pPhysicsShell || !object->m_pPhysicsShell->isActive()) return;


	Fvector dir_inv;
	dir_inv.sub(object->Position(), m_pTelekinesis->GetCenter());
	float magnitude = dir_inv.magnitude();

	// включить гравитацию 
	object->m_pPhysicsShell->set_ApplyByGravity(TRUE);

	float impulse = 0.f;
	if (magnitude > 0.2f)
	{
		dir_inv.mul(1.f / magnitude);
		impulse = m_fThrowPower / magnitude / magnitude;
	}
	else
	{
		dir_inv.random_dir();
		impulse = m_fThrowPower * 10.f;
	}

	bool b_destroyed = false;
	if (magnitude < 2.f * object->Radius())
	{
		b_destroyed = destroy_object(dir_inv, impulse);
	}


	if (!b_destroyed)
		object->m_pPhysicsShell->applyImpulse(dir_inv, impulse);
	switch_state(TS_None);
}

bool CTeleWhirlwindObject::destroy_object(const Fvector dir, float val)
{
	CPHDestroyable* D = object->ph_destroyable();
	if (D && D->CanDestroy())
	{
		D->PhysicallyRemoveSelf();

		if (m_pTelekinesis->GetSpawnSkeleton())
		{
			D->Destroy(m_pTelekinesis->GetOwnerID());
			if (IsGameTypeSingle())
			{
				for (auto& object : D->m_destroyed_obj_visual_names)
				{
					m_pTelekinesis->AddImpact(dir, val * 10.f);
				}
			};
		}

		CEntityAlive* pEntity = smart_cast<CEntityAlive*>(object);
		if (pEntity)
		{
			CParticlesObject* pParticles = CParticlesObject::Create(
				m_pTelekinesis->GetTearingParticles(), true, true);
			
			Fmatrix xform, m;
			Fvector angles;
			
			xform.identity();
			xform.k.normalize(Fvector().set(0, 1, 0));
			Fvector::generate_orthonormal_basis(xform.k, xform.j, xform.i);
			xform.getHPB(angles);

			m.setHPB(angles.x, angles.y, angles.z);
			m.c = pEntity->Position();

			pParticles->UpdateParent(m, zero_vel);
			pParticles->Play(false);

			m_pTelekinesis->PlayTearingSound(object);
		}
		return true;
	}
	return false;
}

void CTeleWhirlwindObject::raise(float step)
{
	CPhysicsShell* p = get_object()->PPhysicsShell();

	if (!p || !p->isActive())
		return;
	else
	{
	p->SetAirResistance(0.f, 0.f);
	p->set_ApplyByGravity(TRUE);
	}
	u16 element_number = p->get_ElementsNumber();
	Fvector center = m_pTelekinesis->GetCenter();
	CPhysicsElement* maxE = p->get_ElementByStoreOrder(0);
	for (u16 element = 0; element < element_number; ++element)
	{
		float k = strength; //600.f;
		float predict_v_eps = 0.1f;
		float mag_eps = .01f;

		CPhysicsElement* E = p->get_ElementByStoreOrder(element);
		if (maxE->getMass() < E->getMass()) maxE = E;
		if (!E->isActive()) continue;
		Fvector pos = E->mass_Center();

		Fvector diff;
		diff.sub(center, pos);
		float mag = _sqrt(diff.x * diff.x + diff.z * diff.z);
		Fvector lc;
		lc.set(center);
		if (mag > 1.f)
		{
			lc.y /= mag;
		}
		diff.sub(lc, pos);
		mag = diff.magnitude();
		float accel = k / mag / mag / mag; //*E->getMass()
		Fvector dir;
		if (mag < mag_eps)
		{
			accel = 0.f;
			//Fvector zer;zer.set(0,0,0);
			//E->set_LinearVel(zer);
			dir.random_dir();
		}
		else
		{
			dir.set(diff);
			dir.mul(1.f / mag);
		}
		Fvector vel;
		E->get_LinearVel(vel);
		float delta_v = accel * fixed_step;
		Fvector delta_vel;
		delta_vel.set(dir);
		delta_vel.mul(delta_v);
		Fvector predict_vel;
		predict_vel.add(vel, delta_vel);
		Fvector delta_pos;
		delta_pos.set(predict_vel);
		delta_pos.mul(fixed_step);
		Fvector predict_pos;
		predict_pos.add(pos, delta_pos);

		Fvector predict_diff;
		predict_diff.sub(lc, predict_pos);
		float predict_mag = predict_diff.magnitude();
		float predict_v = predict_vel.magnitude();

		Fvector force;
		force.set(dir);
		if (predict_mag > mag && predict_vel.dotproduct(dir) > 0.f && predict_v > predict_v_eps)
		{
			Fvector motion_dir;
			motion_dir.set(predict_vel);
			motion_dir.mul(1.f / predict_v);
			float needed_d = diff.dotproduct(motion_dir);
			Fvector needed_diff;
			needed_diff.set(motion_dir);
			needed_diff.mul(needed_d);
			Fvector nearest_p;
			nearest_p.add(pos, needed_diff); //
			Fvector needed_vel;
			needed_vel.set(needed_diff);
			needed_vel.mul(1.f / fixed_step);
			force.sub(needed_vel, vel);
			force.mul(E->getMass() / fixed_step);
		}
		else
		{
			force.mul(accel * E->getMass());
		}


		E->applyForce(force.x, force.y + get_object()->EffectiveGravity() * E->getMass(), force.z);
	}
	Fvector dist;
	dist.sub(center, maxE->mass_Center());
	if (dist.magnitude() < m_pTelekinesis->GetKeepRadius() && m_bDestroyable)
	{
		if (m_pTelekinesis->GetHeightFixed())
		{
			p->setTorque(Fvector().set(0, 0, 0));
			p->setForce(Fvector().set(0, 0, 0));
			p->set_LinearVel(Fvector().set(0, 0, 0));
			p->set_AngularVel(Fvector().set(0, 0, 0));
		}
		switch_state(TS_Keep);
	}
}


void CTeleWhirlwindObject::keep()
{
	CPhysicsShell* p = get_object()->PPhysicsShell();
	if (!p || !p->isActive())
		return;
	else
	{
		p->SetAirResistance(0.f, 0.f);
		p->set_ApplyByGravity(FALSE);
	}

	u16 element_number = p->get_ElementsNumber();
	Fvector center = m_pTelekinesis->GetCenter();

	CPhysicsElement* maxE = p->get_ElementByStoreOrder(0);
	for (u16 element = 0; element < element_number; ++element)
	{
		CPhysicsElement* E = p->get_ElementByStoreOrder(element);
		if (maxE->getMass() < E->getMass())maxE = E;
		Fvector dir;
		dir.sub(center, E->mass_Center());
		dir.normalize_safe();
		Fvector vel;
		E->get_LinearVel(vel);
		float force = dir.dotproduct(vel) * E->getMass() / 2.f;
		if (force < 0.f)
		{
			dir.mul(force);
		}
	}

	maxE->setTorque(Fvector().set(0, 500.f, 0));

	Fvector dist;
	dist.sub(center, maxE->mass_Center());
	if (dist.magnitude() > m_pTelekinesis->GetKeepRadius() * 1.5f)
	{
		if (m_pTelekinesis->GetHeightFixed())
		{
			p->setTorque(Fvector().set(0, 0, 0));
			p->setForce(Fvector().set(0, 0, 0));
			p->set_LinearVel(Fvector().set(0, 0, 0));
			p->set_AngularVel(Fvector().set(0, 0, 0));
		}
		p->set_ApplyByGravity(TRUE);
		switch_state(TS_Raise);
	}
}

void CTeleWhirlwindObject::fire(const Fvector& target)
{
	//inherited::fire(target);
}

void CTeleWhirlwindObject::fire(const Fvector& target, float power)
{
	//inherited:: fire(target,power);
}

void CTeleWhirlwindObject::set_throw_power(float throw_pow)
{
	m_fThrowPower = throw_pow;
}

void CTeleWhirlwindObject::switch_state(ETelekineticState new_state)
{
	inherited::switch_state(new_state);
}

bool CTeleWhirlwindObject::can_activate(CPhysicsShellHolder* obj)
{
	return (obj != NULL);
}


// ================================================================================================

static float clampF(float x, float a, float b)
{
	return x < a ? a : (x > b ? b : x);
}

CTeleTrampolinObject::CTeleTrampolinObject()
{
	m_bDestroyable = false;
	m_pTelekinesis = NULL;
	m_fThrowPower = 0.f;
}

bool CTeleTrampolinObject::init(CTelekinesis* tele, CPhysicsShellHolder* obj, float s, float h, u32 ttk, bool rot)
{
	bool result = inherited::init(tele, obj, s, h, ttk, rot);
	m_pTelekinesis = static_cast<CTeleWhirlwind*>(tele);
	m_fThrowPower = strength;
	if (m_pTelekinesis->is_active_object(obj))
	{
		return false;
	}
	if (obj->PPhysicsShell())
	{
		obj->PPhysicsShell()->SetAirResistance(0.f, 0.f);
		obj->m_pPhysicsShell->set_ApplyByGravity(TRUE);
	}

	if (object->ph_destroyable() && object->ph_destroyable()->CanDestroy())
		m_bDestroyable = true;
	else
		m_bDestroyable = false;

	return result;
}

void CTeleTrampolinObject::set_throw_power(float throw_pow)
{
	m_fThrowPower = throw_pow;
}

bool CTeleTrampolinObject::can_activate(CPhysicsShellHolder* obj)
{
	return (obj != NULL);
}

void CTeleTrampolinObject::raise(float step)
{
	CPhysicsShell* p = object->PPhysicsShell();

	if (!p || !p->isActive())
		return;

	p->SetAirResistance(0.f, 0.f);
	p->set_ApplyByGravity(TRUE);

	CPhysicsElement* maxE = p->get_ElementByStoreOrder(0);

	for (u16 element = 0; element < p->get_ElementsNumber(); ++element)
	{
		CPhysicsElement* E = p->get_ElementByStoreOrder(element);
		if (!E->isActive()) continue;
		if (maxE->getMass() < E->getMass()) maxE = E;
	}

	Fvector pos = maxE->mass_Center();
	Fvector center = m_pTelekinesis->GetCenter();

	float currentHeight = pos.y;
	float peakHeight = target_height;

	float diff = peakHeight - currentHeight;

	u32 rotateSpeed = m_pTelekinesis->GetTeleRotateSpeed();
	float timeRaisePassed = Device.dwTimeGlobal - time_raise_started;
	float angularVelocity = clampF(timeRaisePassed * rotateSpeed, 0.f, rotateSpeed);

	maxE->set_AngularVel(Fvector().set(0, angularVelocity, 0));

	if (timeRaisePassed <= m_pTelekinesis->GetTeleTime())
	{

		if (diff <= 0)
		{
			maxE->set_LinearVel(Fvector().set(0.f, 0.f, 0.f));
		}
		else
		{
			maxE->set_LinearVel(Fvector().set(0.f, diff, 0.f));
		}
	}
	else
	{
	
		//IRenderVisual* v = object->Visual();
		//IKinematics* k = v->dcast_PKinematics();

		Fvector randomDir;
		randomDir.random_dir();
		randomDir.y = 1.0f;

		maxE->applyForce(randomDir, 10000000.f);
	}
}

void CTeleTrampolinObject::raise_update()
{
}

void CTeleTrampolinObject::keep()
{
}

void CTeleTrampolinObject::release()
{
	if (!object || object->getDestroy() || !object->m_pPhysicsShell || !object->m_pPhysicsShell->isActive()) return;
	switch_state(TS_None);
}

void CTeleTrampolinObject::fire(const Fvector& target)
{
}

void CTeleTrampolinObject::fire(const Fvector& target, float power)
{
}

void CTeleTrampolinObject::switch_state(ETelekineticState new_state)
{
	inherited::switch_state(new_state);
}

bool CTeleTrampolinObject::destroy_object(const Fvector dir, float val)
{
	CPHDestroyable* D = object->ph_destroyable();
	if (D && D->CanDestroy())
	{
		D->PhysicallyRemoveSelf();
		D->Destroy(m_pTelekinesis->GetOwnerID());

		if (IsGameTypeSingle())
		{
			for (auto& object : D->m_destroyed_obj_visual_names)
			{
				m_pTelekinesis->AddImpact(dir, val * 10.f);
			}
		};

		CEntityAlive* pEntity = smart_cast<CEntityAlive*>(object);
		if (pEntity)
		{
			CParticlesObject* pParticles = CParticlesObject::Create(
				m_pTelekinesis->GetTearingParticles(), true, true);

			Fmatrix xform, m;
			Fvector angles;

			xform.identity();
			xform.k.normalize(Fvector().set(0, 1, 0));
			Fvector::generate_orthonormal_basis(xform.k, xform.j, xform.i);
			xform.getHPB(angles);

			m.setHPB(angles.x, angles.y, angles.z);
			m.c = pEntity->Position();

			pParticles->UpdateParent(m, zero_vel);
			pParticles->Play(false);

			m_pTelekinesis->PlayTearingSound(object);
		}
		return true;
	}
	return false;
}
