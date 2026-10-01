#pragma once

#include "rocketlauncher.h"
#include "WeaponMagazined.h"
#include "script_export_space.h"
#include "WeaponSSRS.h"

class CWeaponSSRS : public CRocketLauncher,
                    public CWeaponMagazined
{
	typedef CRocketLauncher inheritedRL;
	typedef CWeaponMagazined inheritedWM;

public:
	virtual ~CWeaponSSRS();
	virtual BOOL net_Spawn(CSE_Abstract* DC);
	virtual void Load(LPCSTR section);
	virtual void UpdateCL();
	virtual void OnEvent(NET_Packet& P, u16 type);

protected:
	virtual void ReloadMagazine();
	virtual void state_Fire(float dt);
	void SyncRockets();
	float fAiOneShotTime;
	xr_vector<shared_str> m_pendingRockets;
	xr_vector<u16> m_strippedRockets;
	bool m_bSyncRockets = false;
	u32 m_rocketlessRounds = 0;
	shared_str m_syncedTopAmmo;

DECLARE_SCRIPT_REGISTER_FUNCTION
};
