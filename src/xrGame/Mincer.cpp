#include "stdafx.h"
#include "alife_space.h"
#include "hit.h"
#include "PHDestroyable.h"
#include "mincer.h"
#include "xrmessages.h"
#include "level.h"
#include "CustomZone.h"
#include "entity_alive.h"
#include "PHDestroyableNotificate.h"
#include "actor.h"

CMincer::CMincer(void)
{
	m_fActorBlowoutRadiusPercent = 0.5f;
}

CMincer::~CMincer(void)
{
}

void CMincer::OnStateSwitch(EZoneState new_state)
{
	if (m_eZoneState != eZoneStateBlowout && new_state == eZoneStateBlowout)
	{
		OBJECT_INFO_VEC_IT it;
		for (it = m_ObjectInfoMap.begin(); m_ObjectInfoMap.end() != it; ++it)
		{
			CPhysicsShellHolder* GO = smart_cast<CPhysicsShellHolder *>((*it).object);
			if (GO) Telekinesis().activate(GO, m_fThrowInImpulse, m_fTeleHeight, 100000);
		}
	}

	if (m_eZoneState == eZoneStateBlowout && new_state != eZoneStateBlowout)
	{
		Telekinesis().clear_deactivate();
	}
	inherited::OnStateSwitch(new_state);
}

void CMincer::Load(LPCSTR section)
{
	inherited::Load(section);
	m_oTelekinesis.Load(section);
	if (pSettings->line_exist(section, "torn_particles"))
	{
		m_sParticlesSkeleton = pSettings->r_string(section, "torn_particles");
	}
	m_fActorBlowoutRadiusPercent = pSettings->r_float(section, "actor_blowout_radius_percent");
}

BOOL CMincer::net_Spawn(CSE_Abstract* DC)
{
	BOOL result = inherited::net_Spawn(DC);
	Fvector C;
	Center(C);
	C.y += m_fTeleHeight;
	m_oTelekinesis.SetCenter(C);
	m_oTelekinesis.SetOwnerID(this->ID());
	return result;
}

void CMincer::net_Destroy()
{
	inherited::net_Destroy();
	m_oTelekinesis.ClearImpacts();
}

void CMincer::feel_touch_new(CObject* O)
{
	inherited::feel_touch_new(O);
	if (m_eZoneState == eZoneStateBlowout && (m_dwBlowoutExplosionTime > (u32)m_iStateTime))
	{
		CPhysicsShellHolder* GO = smart_cast<CPhysicsShellHolder *>(O);
		Telekinesis().activate(GO, m_fThrowInImpulse, m_fTeleHeight, 100000);
	}
}

bool CMincer::feel_touch_contact(CObject* O)
{
	return inherited::feel_touch_contact(O) && smart_cast<CPhysicsShellHolder *>(O);
}

void CMincer::AffectThrow(SZoneObjectInfo* O, CPhysicsShellHolder* GO, const Fvector& throw_in_dir, float dist)
{
	inherited::AffectThrow(O, GO, throw_in_dir, dist);
}

bool CMincer::BlowoutState()
{
	bool ret = inherited::BlowoutState();
	if (m_dwBlowoutExplosionTime < (u32)m_iPreviousStateTime ||
		m_dwBlowoutExplosionTime >= (u32)m_iStateTime)
		return ret;
	Telekinesis().deactivate();
	return ret;
}

void CMincer::ThrowInCenter(Fvector& C)
{
	C.set(m_oTelekinesis.GetCenter());
	C.y = Position().y;
}


void CMincer::Center(Fvector& C) const
{
	C.set(Position());
}

/* This is called on object destruction when spawned
 * "destroyed" replacement object: mostly skeleton models (ph_skeleton_object)
 */
void CMincer::NotificateDestroy(CPHDestroyableNotificate* dn)
{
	CPhysicsShellHolder* obj = dn->PPhysicsShellHolder();
	CParticlesPlayer* PP = smart_cast<CParticlesPlayer*>(obj);

	if (PP && *m_sParticlesSkeleton)
	{
		PP->StartParticles(m_sParticlesSkeleton, Fvector().set(0, 1, 0), ID());
	}

	Fvector dir;
	Fvector position_in_bone_space { 0.0f, 0.0f, 0.0f };
	Fvector throw_in_dir{ 1.0f, 0.0f, 1.0f };

	float power = 0.0f;
	float impulse;

	m_oTelekinesis.DrawOutImpact(dir, impulse);

	CreateHit(obj->ID(), ID(), throw_in_dir, power, 0, position_in_bone_space, impulse, ALife::eHitTypeExplosion);
}

void CMincer::AffectPullAlife(CEntityAlive* EA, const Fvector& throw_in_dir, float dist)
{
	float power = Power(dist, Radius());
	if (!smart_cast<CActor*>(EA))
	{
		Fvector pos_in_bone_space;
		pos_in_bone_space.set(0, 0, 0);
		CreateHit(EA->ID(), ID(), throw_in_dir, power, 0, pos_in_bone_space, 0.0f, m_eHitTypeBlowout);
	}
	inherited::AffectPullAlife(EA, throw_in_dir, dist);
}

float CMincer::BlowoutRadiusPercent(CPhysicsShellHolder* GO)
{
	return (!smart_cast<CActor*>(GO) ? m_fBlowoutRadiusPercent : m_fActorBlowoutRadiusPercent);
}
