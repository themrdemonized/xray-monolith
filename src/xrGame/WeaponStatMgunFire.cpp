#include "stdafx.h"
#include "WeaponStatMgun.h"
#include "level.h"
#include "entity_alive.h"
#include "hudsound.h"
#include "actor.h"
#include "actorEffector.h"
#include "EffectorShot.h"
#include "Weapon.h"

#ifdef STATIONARYMGUN_NEW
#include "ai_object_location.h"
#include "ai/stalker/ai_stalker.h"
#include "CharacterPhysicsSupport.h"
#include "actor_anim_defs.h"
#include "stalker_animation_manager.h"
#include "sight_manager.h"
#include "stalker_planner.h"
#include "CameraFirstEye.h"

#include "../Include/xrRender/Kinematics.h"
#include "../xrphysics/PhysicsShell.h"
#endif

const Fvector& CWeaponStatMgun::get_CurrentFirePoint()
{
	return m_fire_pos;
}

const Fmatrix& CWeaponStatMgun::get_ParticlesXFORM()
{
	return m_fire_bone_xform;
}

void CWeaponStatMgun::FireStart()
{
	if (m_firing_disabled)
		return;

	m_dAngle.set(0.0f, 0.0f);
	inheritedShooting::FireStart();
}

void CWeaponStatMgun::FireEnd()
{
	m_dAngle.set(0.0f, 0.0f);
	inheritedShooting::FireEnd();
	StopFlameParticles();
	RemoveShotEffector();

#ifdef STATIONARYMGUN_NEW
	for (auto &B : m_barrels)
	{
		B.StopFlameParticles();
	}
#endif
}

void CWeaponStatMgun::UpdateFire()
{
	fShotTimeCounter -= Device.fTimeDelta;

	inheritedShooting::UpdateFlameParticles();
	inheritedShooting::UpdateLight();

#ifdef STATIONARYMGUN_NEW
	if (m_barrels.size())
	{
		for (auto &B : m_barrels)
		{
			B.fShotTimeCounter -= Device.fTimeDelta;
			B.UpdateFlameParticles();
			B.UpdateLight();
		}
	}
#endif

	if (m_overheat_enabled)
	{
		m_overheat_value -= m_overheat_decr_quant;
		if (m_overheat_value < 100.f)
		{
			if (p_overheat)
			{
				if (p_overheat->IsPlaying())
					p_overheat->Stop(FALSE);
				CParticlesObject::Destroy(p_overheat);
			}
			if (m_firing_disabled)
				m_firing_disabled = false;
		}
		else
		{
			if (p_overheat)
			{
				Fmatrix pos;
				pos.set(get_ParticlesXFORM());
				pos.c.set(get_CurrentFirePoint());
				p_overheat->SetXFORM(pos);
			}
		}
	}

	if (!IsWorking())
	{
		clamp(fShotTimeCounter, 0.0f, flt_max);
		clamp(m_overheat_value, 0.0f, m_overheat_threshold);
#ifdef STATIONARYMGUN_NEW
		if (m_barrels.size())
		{
			for (auto &B : m_barrels)
			{
				clamp(B.fShotTimeCounter, 0.0f, flt_max);
			}
		}
#endif
		return;
	}

	if (m_overheat_enabled)
	{
		m_overheat_value += m_overheat_time_quant;
		clamp(m_overheat_value, 0.0f, m_overheat_threshold);

		if (m_overheat_value >= 100.f)
		{
			if (!p_overheat)
			{
				p_overheat = CParticlesObject::Create(m_overheat_particles.c_str(),FALSE);
				Fmatrix pos;
				pos.set(get_ParticlesXFORM());
				pos.c.set(get_CurrentFirePoint());
				p_overheat->SetXFORM(pos);
				p_overheat->Play(false);
			}

			if (m_overheat_value >= m_overheat_threshold)
			{
				m_firing_disabled = true;
				FireEnd();
				return;
			}
		}
	}

#ifdef STATIONARYMGUN_NEW
	if (!IsUnlimitedAmmo() && (m_magazine.size() == 0))
	{
		SwitchState(eStateIdle);
		return;
	}

	if (m_barrels.size())
	{
		int barrel_shoot_num = 0;
		for (auto &B : m_barrels)
		{
			if (!IsUnlimitedAmmo() && (m_magazine.size() == 0))
			{
				SwitchState(eStateIdle);
				break;
			}
			if (B.fShotTimeCounter <= 0)
			{
				if (BarrelAllowFire(B))
				{
					OnShot(B);
					++barrel_shoot_num;
				}
				B.fShotTimeCounter += B.fOneShotTime;
			}
		}
		for (int i = 0; i < barrel_shoot_num; i++)
		{
			angle_lerp(m_dAngle.x, 0.f, 5.f, Device.fTimeDelta);
			angle_lerp(m_dAngle.y, 0.f, 5.f, Device.fTimeDelta);
		}
		return;
	}
#endif

	if (fShotTimeCounter <= 0)
	{
		OnShot();
		fShotTimeCounter += fOneShotTime;
	}
	else
	{
		angle_lerp(m_dAngle.x, 0.f, 5.f, Device.fTimeDelta);
		angle_lerp(m_dAngle.y, 0.f, 5.f, Device.fTimeDelta);
	}
}


void CWeaponStatMgun::OnShot()
{
#ifdef STATIONARYMGUN_NEW
	CGameObject *owner = (Owner()) ? Owner() : cast_game_object();
	CCartridge &l_cartridge = (m_magazine.size() > 0) ? m_magazine.back() : m_DefaultCartridge;
	FireBullet(m_fire_pos, m_fire_dir, GetFireDispersion(true), l_cartridge, owner->ID(), ID(), true, iMagazineSize - iAmmoElapsed);
	++m_iShotNum;

	m_lastCartridge = l_cartridge;
	if (!IsUnlimitedAmmo())
	{
		m_magazine.pop_back();
		--iAmmoElapsed;
		VERIFY((u32)iAmmoElapsed == m_magazine.size());
		UpdateBulletVisibility(iAmmoElapsed);
	}

	m_anim_weapon.Play(SStmAnimWeapon::eStmAnimWeapon_shot);
#else
	VERIFY(Owner());

	FireBullet(m_fire_pos, m_fire_dir, fireDispersionBase, *m_Ammo,
	           Owner()->ID(), ID(), SendHitAllowed(Owner()), ::Random.randI(0, 30));
#endif

	StartShotParticles();

	if (m_bLightShotEnabled)
		Light_Start();

	StartFlameParticles();
	StartSmokeParticles(m_fire_pos, zero_vel);

#ifdef STATIONARYMGUN_NEW
	if (m_drop_bone != BI_NONE)
	{
		OnShellDrop(Fmatrix().mul_43(XFORM(), Visual()->dcast_PKinematics()->LL_GetTransform(m_drop_bone)).c, zero_vel);
	}
#else
	OnShellDrop(m_fire_pos, zero_vel);
#endif

#ifdef STATIONARYMGUN_NEW
	m_sounds.PlaySound("sndShoot", m_fire_pos, owner, false);
#else
	bool b_hud_mode = (Level().CurrentEntity() == smart_cast<CObject*>(Owner()));

	m_sounds.PlaySound("sndShot", m_fire_pos, Owner(), b_hud_mode);
#endif

	AddShotEffector();

	m_dAngle.set(::Random.randF(-fireDispersionBase, fireDispersionBase),
	             ::Random.randF(-fireDispersionBase, fireDispersionBase));
}

#ifdef STATIONARYMGUN_NEW
void CWeaponStatMgun::OnShot(SStmBarrel &B)
{
	CGameObject *owner = (Owner()) ? Owner() : cast_game_object();
	CCartridge &l_cartridge = (m_magazine.size() > 0) ? m_magazine.back() : m_DefaultCartridge;
	FireBullet(B.m_fire_pos, B.m_fire_dir, GetFireDispersion(true), l_cartridge, owner->ID(), ID(), true, iMagazineSize - B.iAmmoElapsed);

	m_lastCartridge = l_cartridge;
	if (!IsUnlimitedAmmo())
	{
		m_magazine.pop_back();
		--iAmmoElapsed;
		VERIFY((u32)iAmmoElapsed == m_magazine.size());
		UpdateBulletVisibility(iAmmoElapsed);
		B.SetAmmoElapsed(B.iAmmoElapsed - 1);
		BarrelAmmoElapsedCorrecting();
	}

	if (B.m_bLightShotEnabled)
		B.Light_Start();

	B.StartFlameParticles();
	B.StartSmokeParticles(B.m_fire_pos, zero_vel);

	if (B.m_drop_bid != BI_NONE)
	{
		B.OnShellDrop(Fmatrix().mul_43(XFORM(), Visual()->dcast_PKinematics()->LL_GetTransform(B.m_drop_bid)).c, zero_vel);
	}

	m_sounds.PlaySound("sndShoot", B.m_fire_pos, owner, false);

	// AddShotEffector();
	m_dAngle.set(::Random.randF(-fireDispersionBase, fireDispersionBase), ::Random.randF(-fireDispersionBase, fireDispersionBase));
}

int CWeaponStatMgun::BarrelAmmoElapsed()
{
	int num = 0;
	for (auto &B : m_barrels)
	{
		num += B.iAmmoElapsed;
	}
	return num;
}

bool CWeaponStatMgun::BarrelAllowFire(SStmBarrel &B)
{
	/*
		Barrels may don't have the same RPM.
		This ensure that all barrels have their equad shares of rounds being shot. Stop when having shot enough.
		Ignore if infinite ammo or setting ammo elapsed.
	*/
	if (IsUnlimitedAmmo())
		return true;
	/* Barrel is out of ammo. */
	if (B.iAmmoElapsed > 0)
		return true;
	/* If ammo elapsed matches, stop this barrel from firing as intended. If doesn't, there is issue but let it fires. Not a big deal. */
	return (iAmmoElapsed == BarrelAmmoElapsed()) ? false : true;
}

void CWeaponStatMgun::BarrelAmmoElapsedCorrecting()
{
	/* Call after setting ammo elapsed for the gun. Correct new ammo elapsed for each barrel. */
	if (m_barrels.size() == 0)
		return;
	if (iAmmoElapsed == BarrelAmmoElapsed())
		return;
	int evn = iAmmoElapsed / m_barrels.size();
	int odd = iAmmoElapsed % m_barrels.size();
	int idx = 0;
	for (auto &B : m_barrels)
	{
		idx++;
		if (idx <= odd)
			B.iAmmoElapsed = evn + 1;
		else
			B.iAmmoElapsed = evn;
#if 0
		Msg("%s:%d evn:%d odd:%d [%d]iAmmoElapsed:%d", __FUNCTION__, __LINE__, evn, odd, idx, B.iAmmoElapsed);
#endif
	}
}
#endif

void CWeaponStatMgun::AddShotEffector()
{
	if (OwnerActor())
	{
#ifdef STATIONARYMGUN_NEW
        if (Camera() && (Camera()->tag == eCamFirst) && m_shot_effector.size() && xr_strcmp(m_shot_effector, "nil"))
		{
			CAnimatorCamEffector *E = xr_new<CAnimatorCamEffector>();
			E->SetType((ECamEffectorType)STM_SHOT_EFFECTOR);
			E->SetHudAffect(false);
			E->SetCyclic(false);
			E->Start(m_shot_effector.c_str());
			OwnerActor()->Cameras().AddCamEffector(E);
		}
#else
		CCameraShotEffector* S = smart_cast<CCameraShotEffector*>(OwnerActor()->Cameras().GetCamEffector(eCEShot));
		CameraRecoil camera_recoil;
		//( camMaxAngle,camRelaxSpeed, 0.25f, 0.01f, 0.7f )
		camera_recoil.MaxAngleVert = camMaxAngle;
		camera_recoil.RelaxSpeed = camRelaxSpeed;
		camera_recoil.MaxAngleHorz = 0.25f;
		camera_recoil.StepAngleHorz = ::Random.randF(-1.0f, 1.0f) * 0.01f;
		camera_recoil.DispersionFrac = 0.7f;

		if (!S) S = (CCameraShotEffector*)OwnerActor()->Cameras().AddCamEffector(
			xr_new<CCameraShotEffector>(camera_recoil));
		R_ASSERT(S);
		S->Initialize(camera_recoil);
		S->Shot2(0.01f);
#endif
	}
}

void CWeaponStatMgun::RemoveShotEffector()
{
	if (OwnerActor())
		OwnerActor()->Cameras().RemoveCamEffector(eCEShot);
}

#ifdef STATIONARYMGUN_NEW
float CWeaponStatMgun::GetBaseDispersion(float cartridge_k)
{
	return fireDispersionBase * cur_silencer_koef.fire_dispersion * cartridge_k;
}

float CWeaponStatMgun::GetFireDispersion(bool with_cartridge, bool for_crosshair)
{
	if (!with_cartridge)
		return GetFireDispersion(1.0f, for_crosshair);
	if (!m_magazine.empty())
		m_fCurrentCartirdgeDisp = m_magazine.back().param_s.kDisp;
	return GetFireDispersion(m_fCurrentCartirdgeDisp, for_crosshair);
}

float CWeaponStatMgun::GetFireDispersion(float cartridge_k, bool for_crosshair)
{
	float fire_disp = GetBaseDispersion(cartridge_k);
	if (OwnerActor())
	{
		fire_disp += (OwnerActor()->GetWeaponAccuracyStm() * fireDispersionOwnerScale);
	}
	else if (Owner())
	{
		fire_disp += (Owner()->cast_stalker()->GetWeaponAccuracy() * fireDispersionOwnerScale);
	}
	return fire_disp;
}

float CWeaponStatMgun::GetFireDispersionScript(bool wc, bool fc)
{
	return GetFireDispersion(wc, fc);
}

void CWeaponStatMgun::SwitchState(u16 state)
{
	switch (state)
	{
	case eStateIdle:
		switch2_Idle();
		break;
	case eStateFire:
		switch2_Fire();
		break;
	case eStateReload:
		switch2_Reload();
		break;
	}
}

void CWeaponStatMgun::switch2_Idle()
{
	m_state_index = eStateIdle;
	m_state_delay = 0;
	m_iShotNum = 0;
	FireEnd();
	m_sounds.StopSound("sndReload");
	m_sounds.StopSound("sndUnload");
	m_next_ammoType_on_reload.reset();
}

void CWeaponStatMgun::switch2_Fire()
{
	if (m_magazine.size() == 0)
	{
		Fmatrix xfm = Fmatrix().mul_43(XFORM(), Visual()->dcast_PKinematics()->LL_GetTransform(m_rotate_x_bone));
		m_sounds.PlaySound("sndEmpty", xfm.c, Owner(), false);
		return;
	}

	m_state_index = eStateFire;
	m_state_delay = 0;
	if (IsWorking())
	{
		return;
	}
	m_iShotNum = 0;
	FireStart();
	m_sounds.StopSound("sndReload");
	m_sounds.StopSound("sndUnload");
	m_next_ammoType_on_reload.reset();
}

void CWeaponStatMgun::switch2_Reload()
{
	if (iAmmoElapsed == iMagazineSize && !m_next_ammoType_on_reload.valid())
	{
		return;
	}

	if (IsReloadConsume() && (GetAmmoCount_allType() == 0))
	{
		m_next_ammoType_on_reload.reset();
		return;
	}

	if (m_next_ammoType_on_reload.valid() && (GetAmmoCount_forType(m_ammoTypes[m_next_ammoType_on_reload.get()]) == 0))
	{
		m_next_ammoType_on_reload.reset();
		return;
	}

	FireEnd();
	m_state_index = eStateReload;
	m_state_delay = m_reload_delay;

	{
		Fmatrix xfm = Fmatrix().mul_43(XFORM(), Visual()->dcast_PKinematics()->LL_GetTransform(m_rotate_x_bone));
		m_sounds.PlaySound("sndReload", Fmatrix().mul_43(XFORM(), Visual()->dcast_PKinematics()->LL_GetTransform(m_rotate_x_bone)).c, Owner(), false);
	}

	if (iAmmoElapsed == 0)
	{
		m_anim_weapon.Play(SStmAnimWeapon::eStmAnimWeapon_reload0);
	}
	else
	{
		m_anim_weapon.Play(SStmAnimWeapon::eStmAnimWeapon_reload1);
	}
}

void CWeaponStatMgun::UpdateState()
{
	switch (m_state_index)
	{
	case eStateIdle:
	case eStateFire:
		break;
	case eStateReload:
		UpdateReload();
		return;
	default:
		break;
	}
}

void CWeaponStatMgun::UpdateReload()
{
	Fmatrix xfm = Fmatrix().mul_43(XFORM(), Visual()->dcast_PKinematics()->LL_GetTransform(m_rotate_x_bone));
	m_sounds.SetPosition("sndReload", xfm.c);

	m_state_delay = m_state_delay - Device.fTimeDelta;
	if (m_state_delay < 0)
	{
		if (m_next_ammoType_on_reload.valid())
		{
			m_ammoType = m_next_ammoType_on_reload.get();
			m_next_ammoType_on_reload.reset();
		}
		ReloadMagazine();
		switch2_Idle();
	}
}

bool CWeaponStatMgun::IsReloadConsume()
{
	if (m_reload_consume_callback && strlen(m_reload_consume_callback))
	{
		if (xr_strcmp(m_reload_consume_callback, "true") == 0)
		{
			return true;
		}
		if (xr_strcmp(m_reload_consume_callback, "false") == 0)
		{
			return false;
		}
		luabind::functor<bool> lua_function;
		if (ai().script_engine().functor(m_reload_consume_callback, lua_function))
		{
			if (lua_function(lua_game_object(), this, (Owner()) ? Owner()->lua_game_object() : nullptr))
				return true;
			else
				return false;
		}
	}
	return false;
}

void CWeaponStatMgun::UpdateBulletVisibility(u16 num)
{
	if (m_bullet_bones.size() == 0)
		return;
	m_bullet_count = num;
	IKinematics *K = Visual()->dcast_PKinematics();
	for (int k = 0, n = m_bullet_bones.size(); k < n; ++k)
	{
		u16 bid = m_bullet_bones.at(k);
		BOOL visibility = FALSE;
		if (k < m_bullet_count)
		{
			visibility = TRUE;
		}
		if (K->LL_GetBoneVisible(bid) != visibility)
		{
			K->LL_SetBoneVisible(bid, visibility, FALSE);
		}
	}
}

CInventory *CWeaponStatMgun::GetInventory()
{
	return (Owner() && Owner()->cast_inventory_owner()) ? &Owner()->cast_inventory_owner()->inventory() : nullptr;
}

void CWeaponStatMgun::SetAmmoElapsed(int ammo_count)
{
	iAmmoElapsed = ammo_count;
	clamp(iAmmoElapsed, 0, iMagazineSize);
	u32 uAmmo = u32(iAmmoElapsed);
	if (uAmmo != m_magazine.size())
	{
		if (uAmmo > m_magazine.size())
		{
			CCartridge l_cartridge;
			l_cartridge.Load(m_ammoTypes[m_ammoType].c_str(), m_ammoType);
			l_cartridge.m_LocalAmmoType = m_ammoType;
			while (uAmmo > m_magazine.size())
				m_magazine.push_back(l_cartridge);
		}
		else
		{
			while (uAmmo < m_magazine.size())
				m_magazine.pop_back();
		}
	}

	UpdateBulletVisibility(iAmmoElapsed);
	BarrelAmmoElapsedCorrecting();
}

void CWeaponStatMgun::SetAmmoType(u8 type)
{
	m_ammoType = type;
	UnloadMagazine(!IsUnlimitedAmmo() && IsReloadConsume());
	m_bLockType = true;
	ReloadMagazine();
	m_bLockType = false;
	m_next_ammoType_on_reload.reset();
}

void CWeaponStatMgun::ReloadMagazine()
{
	bool is_consume_ammo = !IsUnlimitedAmmo() && IsReloadConsume();

	if (!m_bLockType)
	{
		m_pCurrentAmmo = nullptr;
	}

	if (is_consume_ammo)
	{
		if (m_ammoTypes.size() <= m_ammoType)
		{
			/* Invalid ammo type. */
			UpdateBulletVisibility(iAmmoElapsed);
			BarrelAmmoElapsedCorrecting();
			return;
		}
		LPCSTR tmp_sect_name = m_ammoTypes[m_ammoType].c_str();
		if (!tmp_sect_name)
		{
			/* Invalid ammo type. */
			UpdateBulletVisibility(iAmmoElapsed);
			BarrelAmmoElapsedCorrecting();
			return;
		}
		m_pCurrentAmmo = smart_cast<CWeaponAmmo *>(GetInventory()->GetAny(tmp_sect_name));
		if (!m_pCurrentAmmo && !m_bLockType && iAmmoElapsed == 0)
		{
			/* Try to switch to another ammo type if current ammo is out. */
			for (u8 i = 0; i < u8(m_ammoTypes.size()); ++i)
			{
				m_pCurrentAmmo = smart_cast<CWeaponAmmo *>(GetInventory()->GetAny(m_ammoTypes[i].c_str()));
				if (m_pCurrentAmmo)
				{
					m_ammoType = i;
					break;
				}
			}
		}
	}

	if (!m_pCurrentAmmo && is_consume_ammo)
	{
		/* Completely out of ammo of all types. */
		UpdateBulletVisibility(iAmmoElapsed);
		BarrelAmmoElapsedCorrecting();
		return;
	}

	if (!m_bLockType && !m_magazine.empty() && (!m_pCurrentAmmo || xr_strcmp(m_pCurrentAmmo->cNameSect(), m_magazine.back().m_ammoSect.c_str())))
	{
		UnloadMagazine(is_consume_ammo);
	}
	VERIFY((u32)iAmmoElapsed == m_magazine.size());

	if (m_DefaultCartridge.m_LocalAmmoType != m_ammoType)
	{
		m_DefaultCartridge.Load(m_ammoTypes[m_ammoType].c_str(), m_ammoType);
	}
	CCartridge l_cartridge = m_DefaultCartridge;
	while (iAmmoElapsed < iMagazineSize)
	{
		if (is_consume_ammo)
		{
			if (!m_pCurrentAmmo->Get(l_cartridge))
				break;
		}
		++iAmmoElapsed;
		l_cartridge.m_LocalAmmoType = m_ammoType;
		m_magazine.push_back(l_cartridge);
	}

	VERIFY((u32)iAmmoElapsed == m_magazine.size());

	if (m_pCurrentAmmo && (m_pCurrentAmmo->m_boxCurr == 0) && OnServer())
	{
		m_pCurrentAmmo->SetDropManual(TRUE);
	}

	if (iAmmoElapsed < iMagazineSize)
	{
		m_bLockType = true;
		ReloadMagazine();
		m_bLockType = false;
	}
	VERIFY((u32)iAmmoElapsed == m_magazine.size());

	UpdateBulletVisibility(iAmmoElapsed);
	BarrelAmmoElapsedCorrecting();
}

void CWeaponStatMgun::UnloadMagazine(bool spawn_ammo)
{
	xr_map<LPCSTR, u16> l_ammo;
	while (!m_magazine.empty())
	{
		CCartridge &l_cartridge = m_magazine.back();
		xr_map<LPCSTR, u16>::iterator l_it;
		for (l_it = l_ammo.begin(); l_ammo.end() != l_it; ++l_it)
		{
			if (!xr_strcmp(*l_cartridge.m_ammoSect, l_it->first))
			{
				++(l_it->second);
				break;
			}
		}

		if (l_it == l_ammo.end())
			l_ammo[l_cartridge.m_ammoSect.c_str()] = 1;
		m_magazine.pop_back();
		--iAmmoElapsed;
	}

	VERIFY((u32)iAmmoElapsed == m_magazine.size());

	CInventory *inv = GetInventory();
	if (spawn_ammo && inv)
	{
		xr_map<LPCSTR, u16>::iterator l_it;
		for (l_it = l_ammo.begin(); l_ammo.end() != l_it; ++l_it)
		{
			CWeaponAmmo *l_pA = smart_cast<CWeaponAmmo *>(inv->GetAny(l_it->first));
			if (l_pA)
			{
				u16 l_free = l_pA->m_boxSize - l_pA->m_boxCurr;
				l_pA->m_boxCurr = l_pA->m_boxCurr + (l_free < l_it->second ? l_free : l_it->second);
				l_it->second = l_it->second - (l_free < l_it->second ? l_free : l_it->second);
			}
			if (l_it->second)
				SpawnAmmo(l_it->second, l_it->first, Owner()->ID());
		}
	}

	UpdateBulletVisibility(iAmmoElapsed);
	BarrelAmmoElapsedCorrecting();
}

u16 CWeaponStatMgun::AddCartridge(u16 cnt)
{
	if (iAmmoElapsed >= iMagazineSize)
		return 0;
	m_pCurrentAmmo = smart_cast<CWeaponAmmo *>(GetInventory()->GetAny(m_ammoTypes[m_ammoType].c_str()));
	if (m_DefaultCartridge.m_LocalAmmoType != m_ammoType)
	{
		m_DefaultCartridge.Load(m_ammoTypes[m_ammoType].c_str(), m_ammoType);
	}
	CCartridge l_cartridge = m_DefaultCartridge;
	while (cnt)
	{
		if (IsReloadConsume() && !m_pCurrentAmmo->Get(l_cartridge))
		{
			break;
		}
		--cnt;
		++iAmmoElapsed;
		l_cartridge.m_LocalAmmoType = m_ammoType;
		m_magazine.push_back(l_cartridge);
	}
	return cnt;
}

void CWeaponStatMgun::SpawnAmmo(u32 boxCurr, LPCSTR ammoSect, u32 ParentID)
{
	if (ParentID >= 0xffff)
		return;
	if (OnClient())
		return;
	if (!m_ammoTypes.size())
		return;

	int l_type = 0;
	l_type %= m_ammoTypes.size();

	if (ammoSect == nullptr)
		ammoSect = m_ammoTypes[l_type].c_str();

	++l_type;
	l_type %= m_ammoTypes.size();

	CSE_Abstract *D = F_entity_Create(ammoSect);

	{
		CSE_ALifeItemAmmo *l_pA = smart_cast<CSE_ALifeItemAmmo *>(D);
		R_ASSERT(l_pA);
		l_pA->m_boxSize = (u16)pSettings->r_s32(ammoSect, "box_size");
		D->s_name = ammoSect;
		D->set_name_replace("");
		D->s_RP = 0xff;
		D->ID = 0xffff;
		D->ID_Parent = (u16)ParentID;
		D->ID_Phantom = 0xffff;
		D->s_flags.assign(M_SPAWN_OBJECT_LOCAL);
		D->RespawnTime = 0;
		l_pA->m_tNodeID = g_dedicated_server ? u32(-1) : ai_location().level_vertex_id();

		if (boxCurr == 0xffffffff)
			boxCurr = l_pA->m_boxSize;

		while (boxCurr)
		{
			l_pA->a_elapsed = (u16)(boxCurr > l_pA->m_boxSize ? l_pA->m_boxSize : boxCurr);
			NET_Packet P;
			D->Spawn_Write(P, TRUE);
			Level().Send(P, net_flags(TRUE));

			if (boxCurr > l_pA->m_boxSize)
				boxCurr -= l_pA->m_boxSize;
			else
				boxCurr = 0;
		}
	}
	F_entity_Destroy(D);
}

int CWeaponStatMgun::GetAmmoCount(u8 ammo_type)
{
	return (GetInventory() && (ammo_type < m_ammoTypes.size())) ? GetAmmoCount_forType(m_ammoTypes[ammo_type]) : 0;
}

int CWeaponStatMgun::GetAmmoCount_forType(shared_str const &ammo_type)
{
	CInventory *inv = GetInventory();
	int res = 0;

	TIItemContainer::iterator itb = inv->m_all.begin();
	TIItemContainer::iterator ite = inv->m_all.end();
	for (; itb != ite; ++itb)
	{
		CWeaponAmmo *pAmmo = smart_cast<CWeaponAmmo *>(*itb);
		if (pAmmo && (pAmmo->cNameSect() == ammo_type))
		{
			res += pAmmo->m_boxCurr;
		}
	}
	return res;
}

int CWeaponStatMgun::GetAmmoCount_allType()
{
	if (GetInventory())
	{
		int res = 0;
		for (u8 i = 0; i < m_ammoTypes.size(); ++i)
		{
			res += GetAmmoCount_forType(m_ammoTypes[i]);
		}
		return res;
	}
	return 0;
}

bool CWeaponStatMgun::GetBriefInfo(II_BriefInfo &info)
{
	string32 int_str;

	xr_sprintf(int_str, "%d", iAmmoElapsed);
	info.cur_ammo._set(int_str);
	info.fire_mode._set("A");
	info.grenade._set("");

	u32 at_size = m_ammoTypes.size();
	if (IsUnlimitedAmmo() || at_size == 0)
	{
		info.fmj_ammo._set("--");
		info.ap_ammo._set("--");
		info.third_ammo._set("--");
	}
	else
	{
		info.fmj_ammo._set("");
		info.ap_ammo._set("");
		info.third_ammo._set("");

		if (at_size >= 1)
		{
			xr_sprintf(int_str, "%d", GetAmmoCount(0));
			info.fmj_ammo._set(int_str);
		}
		if (at_size >= 2)
		{
			xr_sprintf(int_str, "%d", GetAmmoCount(1));
			info.ap_ammo._set(int_str);
		}
		if (at_size >= 3)
		{
			xr_sprintf(int_str, "%d", GetAmmoCount(2));
			info.third_ammo._set(int_str);
		}
	}

	if (iAmmoElapsed != 0 && m_magazine.size() != 0)
	{
		LPCSTR ammo_type = m_ammoTypes[m_magazine.back().m_LocalAmmoType].c_str();
		info.name = CStringTable().translate(pSettings->r_string(ammo_type, "inv_name_short"));
		info.icon = ammo_type;
	}
	else
	{
		LPCSTR ammo_type = m_ammoTypes[m_ammoType].c_str();
		info.name = CStringTable().translate(pSettings->r_string(ammo_type, "inv_name_short"));
		info.icon = ammo_type;
	}
	return true;
}

void CWeaponStatMgun::SetNextAmmoTypeOnReload(u8 ammo_type)
{
	if (ammo_type == SStmNextAmmoTypeOnReload::undefined)
	{
		m_next_ammoType_on_reload.reset();
		return;
	}
	m_next_ammoType_on_reload.set(ammo_type % m_ammoTypes.size());
}
#endif