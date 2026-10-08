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
	virtual void Reload();
	virtual bool Action(u16 cmd, u32 flags);

protected:
	virtual void ReloadMagazine();
	virtual void state_Fire(float dt);
	virtual void OnAnimationEnd(u32 state);
	virtual void OnStateSwitch(u32 S, u32 oldState);
	void switch2_StartReload();
	void switch2_AddCartridge();
	void switch2_EndReload();
	bool HaveCartridgeInInventory(u8 cnt);
	u8 AddCartridge(u8 cnt);
	void SyncRockets();
	float fAiOneShotTime;
	xr_vector<shared_str> m_pendingRockets;
	xr_vector<u16> m_strippedRockets;
	bool m_bSyncRockets = false;
	u32 m_rocketlessRounds = 0;
	shared_str m_syncedTopAmmo;
	bool m_bReloadFromEmpty = false;

DECLARE_SCRIPT_REGISTER_FUNCTION
};
