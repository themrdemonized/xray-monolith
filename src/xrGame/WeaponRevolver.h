#pragma once
#include "weaponcustompistol.h"

class CWeaponRevolver :
	public CWeaponCustomPistol
{
	typedef CWeaponCustomPistol inherited;
public:
	CWeaponRevolver();
	virtual ~CWeaponRevolver();

	virtual void Load(LPCSTR section);
	virtual void PlayAnimReload();
	virtual void UpdateSounds();

protected:
	virtual bool AllowFireWhileWorking() { return true; }
	ESoundTypes m_eSoundClose;
};
