#include "stdafx.h"
#include "WeaponMagazined.h"
#include "WeaponMagazinedWGrenade.h"

void CWeaponMagazined::switch2_StartAim()
{
	VERIFY(GetState() == eAimStart);

	if(iAmmoElapsed == 0 && HudAnimationExist("anm_idle_aim_start_empty"))
		PlayHUDMotion("anm_idle_aim_start_empty", TRUE, this, GetState());
	else
		PlayHUDMotion("anm_idle_aim_start", TRUE, this, GetState());
}

void CWeaponMagazined::switch2_EndAim()
{
	VERIFY(GetState() == eAimEnd);

	if (iAmmoElapsed == 0 && HudAnimationExist("anm_idle_aim_end_empty"))
		PlayHUDMotion("anm_idle_aim_end_empty", TRUE, this, GetState());
	else
		PlayHUDMotion("anm_idle_aim_end", TRUE, this, GetState());
}

void CWeaponMagazinedWGrenade::switch2_StartAim()
{
	VERIFY(GetState() == eAimStart);

	if (IsGrenadeLauncherAttached())
	{
		if (m_bGrenadeMode)
		{
			if (iAmmoElapsed == 0 && HudAnimationExist("anm_idle_aim_start_g_empty"))
				PlayHUDMotion("anm_idle_aim_start_g_empty", TRUE, this, GetState());
			else
				PlayHUDMotion("anm_idle_aim_start_g", TRUE, this, GetState());
		}
		else
		{
			if (iAmmoElapsed == 0 && HudAnimationExist("anm_idle_aim_start_w_gl_empty"))
				PlayHUDMotion("anm_idle_aim_start_w_gl_empty", TRUE, this, GetState());
			else
				PlayHUDMotion("anm_idle_aim_start_w_gl", TRUE, this, GetState());
		}
	}
	else
		inherited::switch2_StartAim();


	
}

void CWeaponMagazinedWGrenade::switch2_EndAim()
{
	VERIFY(GetState() == eAimEnd);

	if (IsGrenadeLauncherAttached())
	{
		if (m_bGrenadeMode)
		{
			if (iAmmoElapsed == 0 && HudAnimationExist("anm_idle_aim_end_g_empty"))
				PlayHUDMotion("anm_idle_aim_end_g_empty", TRUE, this, GetState());
			else
				PlayHUDMotion("anm_idle_aim_end_g", TRUE, this, GetState());
		}
		else
		{
			if (iAmmoElapsed == 0 && HudAnimationExist("anm_idle_aim_end_w_gl_empty"))
				PlayHUDMotion("anm_idle_aim_end_w_gl_empty", TRUE, this, GetState());
			else
				PlayHUDMotion("anm_idle_aim_end_w_gl", TRUE, this, GetState());
		}
	}
	else
		inherited::switch2_EndAim();

}