/////////////////////////////////////////////////////
// 
// Common class for gravity anomalies: Vortex and Whirlgig
// When object gets caught it will be pulled towards the
// center + additional tele_height, then ripped apart.
// Zone recharges after blowout
// (charges from 0 to m_fMaxPower in m_dwPeriod time)
// 
/////////////////////////////////////////////////////
#pragma once

#include "gravizone.h"
#include "telewhirlwind.h"
#include "PhysicsShellHolder.h"
#include "script_export_space.h"
#include "PHDestroyable.h"

class CMincer :
	public CBaseGraviZone,
	public CPHDestroyableNotificator
{
private:
	typedef CBaseGraviZone inherited;

	CTeleWhirlwind	m_oTelekinesis;
	shared_str		m_sParticlesSkeleton;
	float			m_fActorBlowoutRadiusPercent;

public:
	virtual CTelekinesis& Telekinesis() { return m_oTelekinesis; }

public:
	CMincer();
	virtual ~CMincer();

	virtual void OnStateSwitch(EZoneState new_state);
	virtual bool feel_touch_contact(CObject* O);
	virtual void feel_touch_new(CObject* O);
	virtual void Load(LPCSTR section);
	virtual bool BlowoutState();

	virtual void AffectPullDead(CPhysicsShellHolder* GO, const Fvector& throw_in_dir, float dist)
	{
	}

	virtual void AffectPullAlife(CEntityAlive* EA, const Fvector& throw_in_dir, float dist);
	virtual void AffectThrow(SZoneObjectInfo* O, CPhysicsShellHolder* GO, const Fvector& throw_in_dir, float dist);
	virtual void ThrowInCenter(Fvector& C);
	virtual BOOL net_Spawn(CSE_Abstract* DC);
	virtual void net_Destroy();
	virtual void Center(Fvector& C) const;
	virtual void NotificateDestroy(CPHDestroyableNotificate* dn);
	virtual float BlowoutRadiusPercent(CPhysicsShellHolder* GO);

DECLARE_SCRIPT_REGISTER_FUNCTION
};

add_to_type_list(CMincer)
#undef script_type_list
#define script_type_list save_type_list(CMincer)
