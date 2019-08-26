#include "stdafx.h"
#include "weaponrevolver.h"
#include "ParticlesObject.h"
#include "actor.h"

CWeaponRevolver::CWeaponRevolver()
{
	m_eSoundClose = ESoundTypes(SOUND_TYPE_WEAPON_RECHARGING);
	SetPending(FALSE);
}

CWeaponRevolver::~CWeaponRevolver(void)
{
}

void CWeaponRevolver::Load(LPCSTR section)
{
	inherited::Load(section);

	m_sounds.LoadSound(section, "snd_close", "sndClose", false, m_eSoundClose);
}

void CWeaponRevolver::PlayAnimReload()
{
	VERIFY(GetState()==eReload);
	if (iAmmoElapsed == 1)
	{
		PlayHUDMotion("anm_reload_1", TRUE, this, GetState());
	}
	else if (iAmmoElapsed == 2)
	{
		PlayHUDMotion("anm_reload_2", TRUE, this, GetState());
	}
	else if (iAmmoElapsed == 3)
	{
		PlayHUDMotion("anm_reload_3", TRUE, this, GetState());
	}
	else if (iAmmoElapsed == 4)
	{
		PlayHUDMotion("anm_reload_4", TRUE, this, GetState());
	}
	else if (iAmmoElapsed == 5)
	{
		PlayHUDMotion("anm_reload_5", TRUE, this, GetState());
	}
	else
	{
		PlayHUDMotion("anm_reload", TRUE, this, GetState());
	}
}

void CWeaponRevolver::UpdateSounds()
{
	inherited::UpdateSounds();
	m_sounds.SetPosition("sndClose", get_LastFP());
}
