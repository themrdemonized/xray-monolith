// Actor_Weapon.cpp:	 для работы с оружием
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "actor.h"
#include "actoreffector.h"
#include "Missile.h"
#include "inventory.h"
#include "weapon.h"
#include "map_manager.h"
#include "level.h"
#include "CharacterPhysicsSupport.h"
#include "EffectorShot.h"
#include "WeaponMagazined.h"
#include "Grenade.h"
#include "game_base_space.h"
#include "Artefact.h"
#include "../xrEngine/CameraBase.h"
#include "player_hud.h"
#include "HUDManager.h"
#include "WeaponKnife.h"

static const float VEL_MAX = 10.f;
static const float VEL_A_MAX = 10.f;

#define GetWeaponParam(pWeapon, func_name, def_value)	((pWeapon) ? (pWeapon->func_name) : def_value)

//возвращает текуший разброс стрельбы (в радианах)с учетом движения
float CActor::GetWeaponAccuracy() const
{
	CWeapon* W = smart_cast<CWeapon*>(inventory().ActiveItem());

	if (IsZoomAimingMode() && W && !GetWeaponParam(W, IsRotatingToZoom(), false))
	{
		return m_fDispAim;
	}
	float dispersion = m_fDispBase * GetWeaponParam(W, Get_PDM_Base(), 1.0f) *
		GetWeaponParam(W, Get_Silencer_PDM_Base(), 1.0f) * GetWeaponParam(W, Get_Scope_PDM_Base(), 1.0f) *
		GetWeaponParam(W, Get_Launcher_PDM_Base(), 1.0f);


	CEntity::SEntityState state;
	if (g_State(state))
	{
		//fAVelocity = angle velocity
		dispersion *= (1.0f + (state.fAVelocity / VEL_A_MAX) * m_fDispVelFactor * GetWeaponParam(W, Get_PDM_Vel_F(), 1.0f) * GetWeaponParam(W, Get_Silencer_PDM_Vel(), 1.0f) * GetWeaponParam(W, Get_Scope_PDM_Vel(), 1.0f) * GetWeaponParam(W, Get_Launcher_PDM_Vel(), 1.0f));

		//fVelocity = linear velocity
		dispersion *= (1.0f + (state.fVelocity / VEL_MAX) * m_fDispVelFactor * GetWeaponParam(W, Get_PDM_Vel_F(), 1.0f) * GetWeaponParam(W, Get_Silencer_PDM_Vel(), 1.0f) * GetWeaponParam(W, Get_Scope_PDM_Vel(), 1.0f) * GetWeaponParam(W, Get_Launcher_PDM_Vel(), 1.0f));

		bool bAccelerated = isActorAccelerated(mstate_real, IsZoomAimingMode());
		if (bAccelerated || !state.bCrouch)
		{
			dispersion *= (1.0f + m_fDispAccelFactor * GetWeaponParam(W, Get_PDM_Accel_F(), 1.0f) * GetWeaponParam(W, Get_Silencer_PDM_Accel(), 1.0f) * GetWeaponParam(W, Get_Scope_PDM_Accel(), 1.0f) * GetWeaponParam(W, Get_Launcher_PDM_Accel(), 1.0f));
		}

		if (state.bCrouch)
		{
			dispersion *= (1.0f + m_fDispCrouchFactor * GetWeaponParam(W, Get_PDM_Crouch(), 1.0f));
			if (!bAccelerated)
			{
				dispersion *= (1.0f + m_fDispCrouchNoAccelFactor * GetWeaponParam(W, Get_PDM_Crouch_NA(), 1.0f));
			}
		}
	}

	if (W && W->m_lastCartridge.param_s.buckShot > 1)
		dispersion *= GetWeaponParam(W, Get_PDM_BuckShot(), 1.0f);

	return dispersion;
}


void CActor::g_fireParams(const CHudItem* pHudItem, Fvector& fire_pos, Fvector& fire_dir)
{
	fire_pos = Cameras().Position();
	fire_dir = Cameras().Direction();

	CWeapon* pWeapon = smart_cast<CWeapon*>(inventory().ActiveItem());
	const CMissile* pMissile = smart_cast<const CMissile*>(pHudItem);
	if (pMissile)
	{
		Fvector offset;
		XFORM().transform_dir(offset, pMissile->throw_point_offset());
		fire_pos.add(offset);
	}
	else if (pWeapon && pWeapon->HudItemData() && !smart_cast<CWeaponKnife*>(pWeapon))
	{
		const Fmatrix& fire_mat = pWeapon->get_ParticlesXFORM();

		if (cam_freelook != eflDisabled)
		{
			Fvector dir;
			float pitch = fire_mat.k.getP();
			dir.setHP(-angle_normalize_signed(old_torso_yaw), pitch > 0.f ? ((pWeapon->GetState() == CWeapon::eFire || cam_freelook == eflDisabling) ? pitch : pitch * .6f) : pitch * .8f);
			fire_dir = dir;
		}
		else if ((psActorFlags.test(AF_FIREPOS) || (mstate_real & mcAnyMove)) && (pWeapon->GetZRotatingFactor() != 1.f /*|| dist < 1.f*/))
		{
			fire_dir = fire_mat.k;
		}
	}
}

void CActor::g_WeaponBones(int& L, int& R1, int& R2)
{
	R1 = m_r_hand;
	R2 = m_r_finger2;
	L = m_l_finger1;
}

BOOL CActor::g_State(SEntityState& state) const
{
	state.bJump = !!(mstate_real & mcJump);
	state.bCrouch = !!(mstate_real & mcCrouch);
	state.bFall = !!(mstate_real & mcFall);
	state.bSprint = !!(mstate_real & mcSprint);
	state.fVelocity = character_physics_support()->movement()->GetVelocityActual();
	state.fAVelocity = fCurAVelocity;
	return TRUE;
}

void CActor::SetCantRunState(bool bDisable)
{
	if (g_Alive() && this == Level().CurrentControlEntity())
	{
		NET_Packet P;
		u_EventGen(P, GEG_PLAYER_DISABLE_SPRINT, ID());
		P.w_s8(bDisable ? 1 : -1);
		u_EventSend(P);
	};
}

void CActor::SetWeaponHideState(u16 State, bool bSet)
{
	if (g_Alive() && this == Level().CurrentControlEntity())
	{
		NET_Packet P;
		u_EventGen(P, GEG_PLAYER_WEAPON_HIDE_STATE, ID());
		P.w_u16(State);
		P.w_u8(u8(bSet));
		u_EventSend(P);
	};
}

static u16 BestWeaponSlots [] = {
	INV_SLOT_3, // 2
	INV_SLOT_2, // 1
	GRENADE_SLOT, // 3
	KNIFE_SLOT, // 0
};

void CActor::SelectBestWeapon(CObject* O)
{}

#define ENEMY_HIT_SPOT	"mp_hit_sector_location"
BOOL g_bShowHitSectors = TRUE;

void CActor::HitSector(CObject* who, CObject* weapon)
{
	if (!g_bShowHitSectors) return;
	if (!g_Alive()) return;

	bool bShowHitSector = true;

	CEntityAlive* pEntityAlive = smart_cast<CEntityAlive*>(who);

	if (!pEntityAlive || this == who) bShowHitSector = false;

	if (weapon)
	{
		CWeapon* pWeapon = smart_cast<CWeapon*>(weapon);
		if (pWeapon)
		{
			if (pWeapon->IsSilencerAttached())
			{
				bShowHitSector = false;
			}
		}
	}

	if (!bShowHitSector) return;
	Level().MapManager().AddMapLocation(ENEMY_HIT_SPOT, who->ID());
}

void CActor::on_weapon_shot_start(CWeapon* weapon)
{
	CameraRecoil const& camera_recoil = (IsZoomAimingMode()) ? weapon->zoom_cam_recoil : weapon->cam_recoil;

	CCameraShotEffector* effector = smart_cast<CCameraShotEffector*>(Cameras().GetCamEffector(eCEShot));
	if (!effector)
	{
		effector = (CCameraShotEffector*)Cameras().AddCamEffector(xr_new<CCameraShotEffector>(camera_recoil));
	}
	else
	{
		if (effector->m_WeaponID != weapon->ID())
		{
			effector->Initialize(camera_recoil);
		}
		else {
			effector->UpdateCameraRecoil(camera_recoil);
		}
	}

	effector->m_WeaponID = weapon->ID();
	R_ASSERT(effector);

	effector->SetRndSeed(GetShotRndSeed());
	effector->SetActor(this);
	effector->Shot(weapon);
}

void CActor::on_weapon_shot_update()
{
	CCameraShotEffector* effector = smart_cast<CCameraShotEffector*>(Cameras().GetCamEffector(eCEShot));
	if (effector)
	{
		update_camera(effector);
	}
}

void CActor::on_weapon_shot_remove(CWeapon* weapon)
{
	Cameras().RemoveCamEffector(eCEShot);
}

void CActor::on_weapon_shot_stop()
{
	CCameraShotEffector* effector = smart_cast<CCameraShotEffector*>(Cameras().GetCamEffector(eCEShot));
	if (effector && effector->IsActive())
	{
		effector->StopShoting();
	}
}

void CActor::on_weapon_hide(CWeapon* weapon)
{
	CCameraShotEffector* effector = smart_cast<CCameraShotEffector*>(Cameras().GetCamEffector(eCEShot));
	if (effector && effector->IsActive())
		effector->Reset();
}

Fvector CActor::weapon_recoil_delta_angle()
{
	CCameraShotEffector* effector = smart_cast<CCameraShotEffector*>(Cameras().GetCamEffector(eCEShot));
	Fvector result = {0.f, 0.f, 0.f};

	if (effector)
		effector->GetDeltaAngle(result);

	return (result);
}

Fvector CActor::weapon_recoil_last_delta()
{
	CCameraShotEffector* effector = smart_cast<CCameraShotEffector*>(Cameras().GetCamEffector(eCEShot));
	Fvector result = {0.f, 0.f, 0.f};

	if (effector)
		effector->GetLastDelta(result);

	return (result);
}

//////////////////////////////////////////////////////////////////////////

void CActor::SpawnAmmoForWeapon(CInventoryItem* pIItem)
{
	if (OnClient()) return;
	if (!pIItem) return;

	CWeaponMagazined* pWM = smart_cast<CWeaponMagazined*>(pIItem);
	if (!pWM || !pWM->AutoSpawnAmmo()) return;

	pWM->SpawnAmmo(0xffffffff, NULL, ID());
};

void CActor::RemoveAmmoForWeapon(CInventoryItem* pIItem)
{
	if (OnClient()) return;
	if (!pIItem) return;

	CWeaponMagazined* pWM = smart_cast<CWeaponMagazined*>(pIItem);
	if (!pWM || !pWM->AutoSpawnAmmo()) return;

	CWeaponAmmo* pAmmo = smart_cast<CWeaponAmmo*>(inventory().GetAny(pWM->m_ammoTypes[0].c_str()));
	if (!pAmmo) return;

	pAmmo->DestroyObject();
};
