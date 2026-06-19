#include "stdafx.h"
#include "WeaponSSRS.h"

extern BOOL g_launcher_dynamic_range_zoom;
#include "entity.h"
#include "explosiveRocket.h"
#include "level.h"
#include "../xrphysics/MathUtils.h"
#include "actor.h"
#include "GrenadeLauncher.h"
#include "WeaponMagazined.h"
#include "pch_script.h"
#include "ParticlesObject.h"
#include "Scope.h"
#include "Silencer.h"
#include "inventory.h"
#include "InventoryOwner.h"
#include "xrserver_objects_alife_items.h"
#include "ActorEffector.h"
#include "EffectorZoomInertion.h"
#include "xr_level_controller.h"
#include "UIGameCustom.h"
#include "object_broker.h"
#include "string_table.h"
#include "MPPlayersBag.h"
#include "ui/UIXmlInit.h"
#include "ui/UIStatic.h"
#include "game_object_space.h"
#include "script_callback_ex.h"
#include "script_game_object.h"
#include "player_hud.h"
#include "HudSound.h"
#include "WeaponSSRS.h"

#include "ai/stalker/ai_stalker.h"

#ifdef DEBUG
#	include "phdebug.h"
#endif

CWeaponSSRS::~CWeaponSSRS()
{
}

BOOL CWeaponSSRS::net_Spawn(CSE_Abstract* DC)
{
	BOOL l_res = inheritedWM::net_Spawn(DC);
	if (!l_res) return l_res;

	if (iAmmoElapsed && !getCurrentRocket())
	{
		shared_str grenade_name = m_ammoTypes[0];
		shared_str fake_grenade_name = pSettings->r_string(grenade_name, "fake_grenade_name");

		if (fake_grenade_name.size())
		{
			int k = iAmmoElapsed;
			while (k)
			{
				k--;
				inheritedRL::SpawnRocket(*fake_grenade_name, this);
			}
		}
	}

	return l_res;
};

void CWeaponSSRS::Load(LPCSTR section)
{
	inheritedRL::Load(section);
	inheritedWM::Load(section);
	fAiOneShotTime = READ_IF_EXISTS(pSettings, r_float, section, "ai_rpm", fOneShotTime);
}

void CWeaponSSRS::FireStart()
{
	//Check if actor is AI
	if (smart_cast<CAI_Stalker*>(H_Parent()))
	{
		fOneShotTime = 60.f / fAiOneShotTime;
	}

	inheritedWM::FireStart();
}

void CWeaponSSRS::OnEvent(NET_Packet& P, u16 type)
{
	inheritedWM::OnEvent(P, type);

	u16 id;
	switch (type)
	{
	case GE_OWNERSHIP_TAKE:
		{
			P.r_u16(id);
			inheritedRL::AttachRocket(id, this);
		}
		break;
	case GE_OWNERSHIP_REJECT:
	case GE_LAUNCH_ROCKET:
		{
			bool bLaunch = (type == GE_LAUNCH_ROCKET);
			P.r_u16(id);
			inheritedRL::DetachRocket(id, bLaunch);
		}
		break;
	}
}

#ifdef CROCKETLAUNCHER_CHANGE
void CWeaponSSRS::UnloadRocket()
{
	while (getRocketCount() > 0)
	{
		Msg("%s:%d [%d]-[%s]", __FUNCTION__, __LINE__, getRocketCount(), getCurrentRocket()->cNameSect_str());
		NET_Packet P;
		u_EventGen(P, GE_OWNERSHIP_REJECT, ID());
		P.w_u16(u16(getCurrentRocket()->ID()));
		u_EventSend(P);
        dropCurrentRocket();
	}
}
#endif

void CWeaponSSRS::ReloadMagazine()
{
	m_needReload = false;
	m_BriefInfo_CalcFrame = 0;

	//устранить осечку при перезарядке
	if (IsMisfire())
	{
		bMisfire = false;
		if (bClearJamOnly)
		{
			bClearJamOnly = false;
			return;
		}
	}

	if (!m_bLockType)
	{
		m_pCurrentAmmo = NULL;
	}

	if (!m_pInventory) return;

	if (m_set_next_ammoType_on_reload != undefined_ammo_type)
	{
		m_ammoType = m_set_next_ammoType_on_reload;
		m_set_next_ammoType_on_reload = undefined_ammo_type;
	}

	UnloadRocket();

	if (!unlimited_ammo())
	{
		if (m_ammoTypes.size() <= m_ammoType)
			return;

		LPCSTR tmp_sect_name = m_ammoTypes[m_ammoType].c_str();

		if (!tmp_sect_name)
			return;

		//попытаться найти в инвентаре патроны текущего типа
		m_pCurrentAmmo = smart_cast<CWeaponAmmo*>(m_pInventory->GetAny(tmp_sect_name));

		if (!m_pCurrentAmmo && !m_bLockType && iAmmoElapsed == 0)
		{
			shared_str fake_grenade_name = pSettings->r_string(m_ammoTypes[m_ammoType].c_str(), "fake_grenade_name");
			for (u8 i = 0; i < u8(m_ammoTypes.size()); ++i)
			{
				//проверить патроны всех подходящих типов
				m_pCurrentAmmo = smart_cast<CWeaponAmmo*>(m_pInventory->GetAny(m_ammoTypes[i].c_str()));

				if (m_pCurrentAmmo)
				{
					m_ammoType = i;
					break;
				}
			}
		}
	}

	//нет патронов для перезарядки
	if (!m_pCurrentAmmo && !unlimited_ammo()) return;

	//разрядить магазин, если загружаем патронами другого типа
	if (!m_bLockType && !m_magazine.empty() &&
		(!m_pCurrentAmmo || xr_strcmp(m_pCurrentAmmo->cNameSect(),
			*m_magazine.back().m_ammoSect)))
		UnloadMagazine(!unlimited_ammo());

	VERIFY((u32)iAmmoElapsed == m_magazine.size());

	if (m_DefaultCartridge.m_LocalAmmoType != m_ammoType)
		m_DefaultCartridge.Load(m_ammoTypes[m_ammoType].c_str(), m_ammoType, m_APk);
	CCartridge l_cartridge = m_DefaultCartridge;

	shared_str fake_grenade_name = pSettings->r_string(m_ammoTypes[m_ammoType].c_str(), "fake_grenade_name");
	while (iAmmoElapsed < iMagazineSize)
	{
		if (!unlimited_ammo())
		{
			if (!m_pCurrentAmmo->Get(l_cartridge)) break;
		}
		++iAmmoElapsed;
		l_cartridge.m_LocalAmmoType = m_ammoType;
		m_magazine.push_back(l_cartridge);
		inheritedRL::SpawnRocket(*fake_grenade_name, this);
	}

	VERIFY((u32)iAmmoElapsed == m_magazine.size());

	//выкинуть коробку патронов, если она пустая
	if (m_pCurrentAmmo && !m_pCurrentAmmo->m_boxCurr && OnServer())
		m_pCurrentAmmo->SetDropManual(TRUE);

	if (iMagazineSize > iAmmoElapsed)
	{
		m_bLockType = true;
		ReloadMagazine();
		m_bLockType = false;
	}

	VERIFY((u32)iAmmoElapsed == m_magazine.size());
}

void CWeaponSSRS::state_Fire(float dt)
{
	if (iAmmoElapsed > 0)
	{
		VERIFY(fOneShotTime > 0.f);
		VERIFY(fAiOneShotTime > 0.f);

		if (!H_Parent()) return;

		if (smart_cast<CMPPlayersBag*>(H_Parent()) != NULL)
		{
			Msg("! WARNING: state_Fire of object [%d][%s] while parent is CMPPlayerBag...", ID(), cNameSect().c_str());
			return;
		}

		CInventoryOwner* io = smart_cast<CInventoryOwner*>(H_Parent());
		if (NULL == io->inventory().ActiveItem())
		{
			Log("current_state", GetState());
			Log("next_state", GetNextState());
			Log("item_sect", cNameSect().c_str());
			Log("H_Parent", H_Parent()->cNameSect().c_str());
			StopShooting();
			return;
			//Alundaio: This is not supposed to happen but it does. GSC was aware but why no return here? Known to cause crash on game load if npc immediatly enters combat.
		}
		Fvector p1, d;
		p1.set(get_LastFP());
		d.set(get_LastFD());
		CEntity* E = smart_cast<CEntity*>(H_Parent());

		if (!E->g_stateFire())
			StopShooting();

		if (m_iShotNum == 0)
		{
			m_vStartPos = p1;
			m_vStartDir = d;
		}

		VERIFY(!m_magazine.empty());

		while (!m_magazine.empty() && fShotTimeCounter < 0 && (IsWorking() || m_bFireSingleShot) && (m_iQueueSize < 0 ||
			m_iShotNum < m_iQueueSize))
		{
			m_bFireSingleShot = false;

			//Alundaio: Use fModeShotTime instead of fOneShotTime if current fire mode is 2-shot burst
			//Alundaio: Cycle down RPM after two shots; used for Abakan/AN-94
			// demonized: Add support for arbitrary shot burst at rpm_mode_2
			if (GetCurrentFireMode() >= 2 || cycleDownCheck())
			{
				fShotTimeCounter = fModeShotTime;
			}
			else
				fShotTimeCounter = fOneShotTime;
			//Alundaio: END

#ifdef CROCKETLAUNCHER_CHANGE
			LPCSTR ammo_name = m_ammoTypes[m_ammoType].c_str();
			float launch_speed = READ_IF_EXISTS(pSettings, r_float, ammo_name, "ammo_grenade_vel", CRocketLauncher::m_fLaunchSpeed);
#endif
			if (E)
			{
				CInventoryOwner* io = smart_cast<CInventoryOwner*>(H_Parent());
				if (NULL == io->inventory().ActiveItem())
				{
					Log("current_state", GetState());
					Log("next_state", GetNextState());
					Log("item_sect", cNameSect().c_str());
					Log("H_Parent", H_Parent()->cNameSect().c_str());
				}
				E->g_fireParams(this, p1, d);
			}

			Fmatrix launch_matrix;
			launch_matrix.identity();
			launch_matrix.k.set(d);
			Fvector::generate_orthonormal_basis(launch_matrix.k, launch_matrix.j, launch_matrix.i);
			launch_matrix.c.set(p1);

			if (IsGameTypeSingle() && IsZoomed() && smart_cast<CActor*>(H_Parent()) && g_launcher_dynamic_range_zoom)
			{
				H_Parent()->setEnabled(FALSE);
				setEnabled(FALSE);

				collide::rq_result RQ;
				BOOL HasPick = Level().ObjectSpace.RayPick(p1, d, 300.0f, collide::rqtStatic, RQ, this);

				setEnabled(TRUE);
				H_Parent()->setEnabled(TRUE);

				if (HasPick)
				{
					Fvector Transference;
					Transference.mul(d, RQ.range);
					Fvector res[2];

#ifdef CROCKETLAUNCHER_CHANGE
					u8 canfire0 = TransferenceAndThrowVelToThrowDir(Transference, launch_speed, EffectiveGravity(), res);
#else
					u8 canfire0 = TransferenceAndThrowVelToThrowDir(Transference, CRocketLauncher::m_fLaunchSpeed,
						EffectiveGravity(), res);
#endif
					if (canfire0 != 0)
					{
						d = res[0];
					};
				}
			};
			d.normalize();
#ifdef CROCKETLAUNCHER_CHANGE
			d.mul(launch_speed);
#else
			d.mul(m_fLaunchSpeed);
#endif
			VERIFY2(_valid(launch_matrix), "CWeaponSSRS::state_Fire. Invalid launch_matrix");
			inheritedRL::LaunchRocket(launch_matrix, d, zero_vel);
			CExplosiveRocket* pGrenade = smart_cast<CExplosiveRocket*>(getCurrentRocket());
			VERIFY(pGrenade);
			pGrenade->SetInitiator(H_Parent()->ID());
			if (OnServer())
			{
				NET_Packet P;
				u_EventGen(P, GE_LAUNCH_ROCKET, ID());
				P.w_u16(u16(getCurrentRocket()->ID()));
				u_EventSend(P);
			}
			dropCurrentRocket();

			++m_iShotNum;

			CheckForMisfire();
			OnShot();

			if (m_iShotNum > m_iBaseDispersionedBulletsCount)
				FireTrace(p1, d);
			else
				FireTrace(m_vStartPos, m_vStartDir);

			if (bMisfire)
			{
				CGameObject* object = smart_cast<CGameObject*>(H_Parent());
				if (object)
					object->callback(GameObject::eOnWeaponJammed)(object->lua_game_object(), this->lua_game_object());
				StopShooting();
				return;
			}
		}

		if (m_iShotNum == m_iQueueSize)
			m_bStopedAfterQueueFired = true;

		UpdateSounds();
	}

	if (fShotTimeCounter < 0)
	{
		if (iAmmoElapsed == 0)
			OnMagazineEmpty();

		StopShooting();
	}
	else
	{
		fShotTimeCounter -= dt;
	}
}