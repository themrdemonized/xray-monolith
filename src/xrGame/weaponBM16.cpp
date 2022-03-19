#include "stdafx.h"
#include "weaponBM16.h"
#include "entity.h"
#include "Actor.h"

CWeaponBM16::~CWeaponBM16()
{
}

void CWeaponBM16::Load(LPCSTR section)
{
	inherited::Load(section);
	m_sounds.LoadSound(section, "snd_reload_1", "sndReload1", true, m_eSoundShot);
}

void CWeaponBM16::PlayReloadSound()
{
	if (m_magazine.size() == 1)
		PlaySound("sndReload1", get_LastFP());
	else
		PlaySound("sndReload", get_LastFP());
}

void CWeaponBM16::PlayAnimShoot()
{
	switch (m_magazine.size())
	{
	case 1:
		PlayHUDMotion("anm_shot_1",TRUE, this, GetState(), 1.f, 0.f, false);
		break;
	case 2:
		PlayHUDMotion("anm_shot_2",TRUE, this, GetState(), 1.f, 0.f, false);
		break;
	}
}

void CWeaponBM16::PlayAnimShow()
{
	switch (m_magazine.size())
	{
	case 0:
		PlayHUDMotion("anm_show_0",TRUE, this, GetState(), 1.f, 0.f, false);
		break;
	case 1:
		PlayHUDMotion("anm_show_1",TRUE, this, GetState(), 1.f, 0.f, false);
		break;
	case 2:
		PlayHUDMotion("anm_show_2",TRUE, this, GetState(), 1.f, 0.f, false);
		break;
	}
}

void CWeaponBM16::PlayAnimHide()
{
	switch (m_magazine.size())
	{
	case 0:
		PlayHUDMotion("anm_hide_0",TRUE, this, GetState());
		break;
	case 1:
		PlayHUDMotion("anm_hide_1",TRUE, this, GetState());
		break;
	case 2:
		PlayHUDMotion("anm_hide_2",TRUE, this, GetState());
		break;
	}
}

bool CWeaponBM16::TryPlayAnimBore()
{
	switch (m_magazine.size())
	{
	case 0:
		if (HudAnimationExist("anm_bore_0"))
		{
			PlayHUDMotion("anm_bore_0", TRUE, this, GetState());
			return true;
		}
		break;
	case 1:
		if (HudAnimationExist("anm_bore_1"))
		{
			PlayHUDMotion("anm_bore_1", TRUE, this, GetState());
			return true;
		}
		break;
	case 2:
		if (HudAnimationExist("anm_bore_2"))
		{
			PlayHUDMotion("anm_bore_2", TRUE, this, GetState());
			return true;
		}
		break;
	}

	return false;
}

void CWeaponBM16::PlayAnimReload()
{
	bool b_both = HaveCartridgeInInventory(2);

	VERIFY(GetState()==eReload);


	if ((m_magazine.size() == 1 || !b_both) &&
		(m_set_next_ammoType_on_reload == undefined_ammo_type ||
			m_ammoType == m_set_next_ammoType_on_reload))
		PlayHUDMotion("anm_reload_1",TRUE, this, GetState());
	else
		PlayHUDMotion("anm_reload_2",TRUE, this, GetState());
}

void CWeaponBM16::PlayAnimIdleMoving()
{
	bool bAccelerated = isActorAccelerated(Actor()->MovingState(), IsZoomed());

	switch (m_magazine.size())
	{
	case 0:
		PlayHUDMotion("anm_idle_moving_0",TRUE, this, GetState(), bAccelerated ? 1.f : .75f);
		break;
	case 1:
		PlayHUDMotion("anm_idle_moving_1",TRUE, this, GetState(), bAccelerated ? 1.f : .75f);
		break;
	case 2:
		PlayHUDMotion("anm_idle_moving_2",TRUE, this, GetState(), bAccelerated ? 1.f : .75f);
		break;
	}
}

void CWeaponBM16::PlayAnimIdleSprint()
{
	switch (m_magazine.size())
	{
	case 0:
		PlayHUDMotion("anm_idle_sprint_0",TRUE, this, GetState());
		break;
	case 1:
		PlayHUDMotion("anm_idle_sprint_1",TRUE, this, GetState());
		break;
	case 2:
		PlayHUDMotion("anm_idle_sprint_2",TRUE, this, GetState());
		break;
	}
}

void CWeaponBM16::PlayAnimIdle()
{
	CActor* pActor = smart_cast<CActor*>(H_Parent());
	if (!pActor)
		return;

	if (IsZoomed())
	{
		switch (m_magazine.size())
		{
		case 0:
			{
				PlayHUDMotion("anm_idle_aim_0", TRUE, NULL, GetState());
			}
			break;
		case 1:
			{
				PlayHUDMotion("anm_idle_aim_1", TRUE, NULL, GetState());
			}
			break;
		case 2:
			{
				PlayHUDMotion("anm_idle_aim_2", TRUE, NULL, GetState());
			}
			break;
		};

		return;
	}

	CEntity::SEntityState st;
	pActor->g_State(st);
	if (pActor->AnyMove())
	{
		if (pActor->is_safemode())
		{
			switch (m_magazine.size())
			{
			case 0:
			{
				PlayHUDMotion("anm_idle_0", TRUE, NULL, GetState());
			}
			break;
			case 1:
			{
				PlayHUDMotion("anm_idle_1", TRUE, NULL, GetState());
			}
			break;
			case 2:
			{
				PlayHUDMotion("anm_idle_2", TRUE, NULL, GetState());
			}
			break;
			};

			return;
		}

		if (st.bSprint)
		{
			PlayAnimIdleSprint();
			return;
		}
		else if (!st.bCrouch)
		{
			PlayAnimIdleMoving();
			return;
		}
		else if (st.bCrouch)
		{
			switch (m_magazine.size())
			{
			case 0:
			{
				HudAnimationExist("anm_idle_moving_crouch_0")
					? PlayHUDMotion("anm_idle_moving_crouch_0", TRUE, NULL, GetState())
					: PlayHUDMotion("anm_idle_moving_0", TRUE, NULL, GetState(), .7f);
			}
			break;
			case 1:
			{
				HudAnimationExist("anm_idle_moving_crouch_1")
					? PlayHUDMotion("anm_idle_moving_crouch_1", TRUE, NULL, GetState())
					: PlayHUDMotion("anm_idle_moving_1", TRUE, NULL, GetState(), .7f);
			}
			break;
			case 2:
			{
				HudAnimationExist("anm_idle_moving_crouch_2")
					? PlayHUDMotion("anm_idle_moving_crouch_2", TRUE, NULL, GetState())
					: PlayHUDMotion("anm_idle_moving_2", TRUE, NULL, GetState(), .7f);
			}
			break;
			};

			return;
		}
	}
	
	switch (m_magazine.size())
	{
	case 0:
	{
		PlayHUDMotion("anm_idle_0", TRUE, NULL, GetState());
	}
	break;
	case 1:
	{
		PlayHUDMotion("anm_idle_1", TRUE, NULL, GetState());
	}
	break;
	case 2:
	{
		PlayHUDMotion("anm_idle_2", TRUE, NULL, GetState());
	}
	break;
	};
}
