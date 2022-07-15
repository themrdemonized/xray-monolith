#include "stdafx.h"
#include "customdevice.h"
#include "inventory.h"
#include "actor.h"
#include "player_hud.h"
#include "weapon.h"
#include "Missile.h"

CCustomDevice::CCustomDevice()
{
	m_bFastAnimMode = false;
	m_bNeedActivation = false;
	m_ui = nullptr;
	m_bWorking = false;
	m_bZoomed = false;
	m_fZoomfactor = 0.f;
	m_bOldZoom = false;
}

CCustomDevice::~CCustomDevice()
{
	TurnDeviceInternal(false);
	if (m_ui)
		xr_delete(m_ui);
}

bool CCustomDevice::CheckCompatibilityInt(CHudItem* itm, u16* slot_to_activate)
{
	if (itm == nullptr)
		return true;

	CInventoryItem& iitm = itm->item();
	u32 slot = iitm.BaseSlot();
	bool bres = (slot == INV_SLOT_2 || slot == KNIFE_SLOT || slot == BOLT_SLOT); // || iitm.AllowDevice();
	if (!bres && slot_to_activate)
	{
		*slot_to_activate = NO_ACTIVE_SLOT;
		if (m_pInventory->ItemFromSlot(BOLT_SLOT))
			*slot_to_activate = BOLT_SLOT;

		if (m_pInventory->ItemFromSlot(KNIFE_SLOT))
			*slot_to_activate = KNIFE_SLOT;

		if (m_pInventory->ItemFromSlot(INV_SLOT_3) && m_pInventory->ItemFromSlot(INV_SLOT_3)->BaseSlot() != INV_SLOT_3)
			*slot_to_activate = INV_SLOT_3;

		if (m_pInventory->ItemFromSlot(INV_SLOT_2) && m_pInventory->ItemFromSlot(INV_SLOT_2)->BaseSlot() != INV_SLOT_3)
			*slot_to_activate = INV_SLOT_2;

		if (*slot_to_activate != NO_ACTIVE_SLOT)
			bres = true;
	}

	if (itm->GetState() != CHUDState::eShowing)
		bres = bres && !itm->IsPending();

	if (bres)
	{
		CWeapon* W = smart_cast<CWeapon*>(itm);
		if (W)
			bres = bres &&
			(W->GetState() != CHUDState::eBore) &&
			(W->GetState() != CWeapon::eReload) &&
			(W->GetState() != CWeapon::eSwitch) && 
			(m_bCanBeZoomed || !W->IsZoomed());
	}
	return bres;
}

bool CCustomDevice::CheckCompatibility(CHudItem* itm)
{
	if (!inherited::CheckCompatibility(itm))
		return false;

	if (!CheckCompatibilityInt(itm, NULL))
	{
		HideDevice(true);
		return false;
	}
	return true;
}

void CCustomDevice::HideDevice(bool bFastMode)
{
	if (GetState() != eHidden)
		ToggleDevice(bFastMode);
}

void CCustomDevice::ShowDevice(bool bFastMode)
{
	if (GetState() == eHidden)
		ToggleDevice(bFastMode);
}

void CCustomDevice::ForceHide()
{
	g_player_hud->detach_item(this);
}

void CCustomDevice::ToggleDevice(bool bFastMode)
{
	m_bNeedActivation = false;
	m_bFastAnimMode = bFastMode;

	if (GetState() == eHidden)
	{
		PIItem iitem = m_pInventory->ActiveItem();
		CHudItem* itm = (iitem) ? iitem->cast_hud_item() : NULL;
		u16 slot_to_activate = NO_ACTIVE_SLOT;

		if (CheckCompatibilityInt(itm, &slot_to_activate))
		{
			if (slot_to_activate != NO_ACTIVE_SLOT)
			{
				m_pInventory->Activate(slot_to_activate);
				m_bNeedActivation = true;
			}
			else
				SwitchState(eShowing);
		}
	}
	else
		SwitchState(eHiding);
}

void CCustomDevice::OnStateSwitch(u32 S, u32 oldState)
{
	inherited::OnStateSwitch(S, oldState);

	switch (S)
	{
	case eShowing:
	{
		g_player_hud->attach_item(this);

		if (!IsUsingCondition() || (IsUsingCondition() && GetCondition() >= m_fLowestBatteryCharge))
			TurnDeviceInternal(true);

		m_sounds.PlaySound("sndShow", Fvector().set(0, 0, 0), this, true, false);

		attachable_hud_item* i0 = g_player_hud->attached_item(0);
		if (m_bCanBeZoomed && i0)
		{
			CWeapon* wpn = smart_cast<CWeapon*>(i0->m_parent_hud_item);
			if (wpn && wpn->IsZoomed() && wpn->GetZRotatingFactor() > .5f)
				m_bZoomed = true;
		}

		if (m_bZoomed)
			m_fZoomfactor = .51f;

		m_bZoomed && HudAnimationExist("anm_zoom_show")
			? PlayHUDMotion("anm_zoom_show", FALSE, this, GetState(), 1.f, 0.f, false) 
			: PlayHUDMotion(m_bFastAnimMode ? "anm_show_fast" : "anm_show", FALSE, this, GetState(), 1.f, 0.f, false);
		SetPending(TRUE);
	}
	break;
	case eHiding:
	{
		if (oldState != eHiding)
		{
			m_sounds.PlaySound("sndHide", Fvector().set(0, 0, 0), this, true, false);

			m_fZoomfactor > .5f && (oldState == eIdleZoom || oldState == eIdleZoomIn || oldState == eIdleZoomOut || oldState == eShowing) && HudAnimationExist(m_bFastAnimMode ? "anm_zoom_hide_fast" : "anm_zoom_hide")
				? PlayHUDMotion(m_bFastAnimMode ? "anm_zoom_hide_fast" : "anm_zoom_hide", TRUE, this, GetState()) 
				: PlayHUDMotion(m_bFastAnimMode ? "anm_hide_fast" : "anm_hide", TRUE, this, GetState());
			SetPending(TRUE);

			m_bZoomed = false;
		}
	}
	break;
	case eIdle:
	{
		m_bZoomed = false;
		PlayAnimIdle();
		SetPending(FALSE);
	}
	break;
	case eIdleZoom:
	{
		m_bZoomed = true;
		PlayHUDMotion("anm_idle_zoom", TRUE, this, GetState());
		SetPending(FALSE);
	}
	break;
	case eIdleZoomIn:
	{
		m_bZoomed = true;

		HudAnimationExist(m_bOldZoom ? "anm_zoom_in" : "anm_zoom")
			? PlayHUDMotion(m_bOldZoom ? "anm_zoom_in" : "anm_zoom", TRUE, this, GetState())
			: SwitchState(eIdleZoom);
		SetPending(FALSE);
	}
	break;
	case eIdleZoomOut:
	{
		m_bZoomed = false;
		HudAnimationExist(m_bOldZoom ? "anm_zoom_out" : "anm_zoom")
			? PlayHUDMotion(m_bOldZoom ? "anm_zoom_out" : "anm_zoom", TRUE, this, GetState())
			: SwitchState(eIdle);
		SetPending(FALSE);
	}
	break;
	case eIdleThrowStart:
	{
		if (HudAnimationExist("anm_throw_start"))
		{
			PlayHUDMotion("anm_throw_start", TRUE, this, GetState());
			SetPending(TRUE);
		}
		else 
			SwitchState(eIdleThrow);
	}
	break;
	case eIdleThrow:
	{
		PlayHUDMotion("anm_throw", TRUE, this, GetState());
		SetPending(FALSE);
	}
	break;
	case eIdleThrowEnd:
	{
		if (HudAnimationExist("anm_throw_end"))
		{
			PlayHUDMotion("anm_throw_end", TRUE, this, GetState());
			SetPending(TRUE);
		}
		else
			SwitchState(eIdle);
	}
	break;
	}
}

void CCustomDevice::OnAnimationEnd(u32 state)
{
	inherited::OnAnimationEnd(state);
	switch (state)
	{
	case eShowing:
	{
		if (m_bCanBeZoomed)
		{
			attachable_hud_item* i0 = g_player_hud->attached_item(0);
			if (i0)
			{
				CWeapon* wpn = smart_cast<CWeapon*>(i0->m_parent_hud_item);
				if (wpn && wpn->IsZoomed())
				{
					SwitchState(eIdleZoom);
					return;
				}
			}
		}
		SwitchState(eIdle);
	}
	break;
	case eHiding:
	{
		SwitchState(eHidden);
		TurnDeviceInternal(false);
		g_player_hud->detach_item(this);
		m_fZoomfactor = 0.f;
	}
	break;
	case eIdleZoomIn:
	{
		SwitchState(eIdleZoom);
	}
	break;
	case eIdleZoomOut:
	{
		SwitchState(eIdle);
	}
	break;
	case eIdleThrowStart:
	{
		SwitchState(eIdleThrow);
	}
	break;
	case eIdleThrowEnd:
	{
		SwitchState(eIdle);
	}
	break;
	}
}

void CCustomDevice::UpdateXForm()
{
	CInventoryItem::UpdateXForm();
}

void CCustomDevice::OnActiveItem()
{
	return;
}

void CCustomDevice::OnHiddenItem()
{
}

BOOL CCustomDevice::net_Spawn(CSE_Abstract* DC)
{
	TurnDeviceInternal(false);
	return (inherited::net_Spawn(DC));
}

void CCustomDevice::Load(LPCSTR section)
{
	inherited::Load(section);

	m_sounds.LoadSound(section, "snd_draw", "sndShow");
	m_sounds.LoadSound(section, "snd_holster", "sndHide");

	LPCSTR hud_sect = pSettings->r_string(section, "hud");

	m_bCanBeZoomed = pSettings->line_exist(hud_sect, "anm_idle_zoom");
	m_bThrowAnm = pSettings->line_exist(hud_sect, "anm_throw");
	m_bOldZoom = pSettings->line_exist(hud_sect, "anm_zoom_in") && pSettings->line_exist(hud_sect, "anm_zoom_out");
}

void CCustomDevice::shedule_Update(u32 dt)
{
	inherited::shedule_Update(dt);

	if (!IsWorking()) return;

	Position().set(H_Parent()->Position());
}

bool CCustomDevice::IsWorking()
{
	return m_bWorking && H_Parent() && H_Parent() == Level().CurrentViewEntity();
}

void CCustomDevice::UpdateVisibility()
{
	bool bClimb = ((Actor()->MovingState() & mcClimb) != 0);

	//check visibility
	attachable_hud_item* i0 = g_player_hud->attached_item(0);
	if (i0 && HudItemData())
	{
		if (bClimb)
		{
			HideDevice(true);
			m_bNeedActivation = true;
		}
		else
		{
			CInventoryItem* itm = g_actor->inventory().ActiveItem();
			CWeapon* wpn = smart_cast<CWeapon*>(itm);
			CMissile* msl = smart_cast<CMissile*>(itm);
			if (msl && m_bThrowAnm)
			{
				u32 state = msl->GetState();
				if ((state == CMissile::eThrowStart || state == CMissile::eReady) && GetState() == eIdle)
					SwitchState(eIdleThrowStart);
				else if (state == CMissile::eThrow && GetState() == eIdleThrow)
					SwitchState(eIdleThrowEnd);
				else if (state == CMissile::eHiding && (GetState() == eIdleThrowStart || GetState() == eIdleThrow))
					SwitchState(eIdle);
			}
			else
			{
				if (GetState() == eIdleThrowStart || GetState() == eIdleThrow)
					SwitchState(eIdle);

				if (wpn)
				{
					u32 state = wpn->GetState();
					if ((wpn->IsZoomed() && !m_bCanBeZoomed) || state == CWeapon::eReload || state == CWeapon::eSwitch)
					{
						if (GetState() != eHiding)
							HideDevice(true);
						m_bNeedActivation = true;
						return;
					}
					else if (wpn->IsZoomed())
					{
						if ((GetState() == eIdle || GetState() == eIdleZoomOut || GetState() == eShowing) && !m_current_motion.equal("anm_zoom_show"))
							SwitchState(eIdleZoomIn);
					}
					else if (GetState() == eIdleZoom || GetState() == eIdleZoomIn)
					{
						SwitchState(eIdleZoomOut);
					}
					else if (m_bNeedActivation && !bClimb && CheckCompatibilityInt(i0->m_parent_hud_item, 0))
					{ 
						ShowDevice(true);
					}
				}
			}
		}
	}
	else
	{
		if (bClimb)
		{
			if (HudItemData())
			{
				if (GetState() != eHiding)
					HideDevice(true);
				m_bNeedActivation = true;
			}
		}
		else
		{
			if (m_bNeedActivation && (!i0 || (i0 && CheckCompatibilityInt(i0->m_parent_hud_item, 0))))
				ShowDevice(true);
		}

		if (GetState() == eIdleZoom)
			SwitchState(eIdleZoomOut);
		else if (GetState() == eIdleZoomIn)
			SwitchState(eIdle);
	}

	// Sync zoom in/out anim to zoomfactor :)
	if (!m_bOldZoom && (GetState() == eIdleZoomIn || GetState() == eIdleZoomOut))
	{
		if (m_fZoomfactor > 0.f && m_fZoomfactor < 1.f)
			g_player_hud->set_part_cycle_time(1, m_fZoomfactor);
		else if (m_fZoomfactor == 0)
		{
			PlayHUDMotion("anm_idle", FALSE, this, eIdle); // Necessary to fix small jump caused by motion mixing
			SwitchState(eIdle);
		}
	}
}

void CCustomDevice::UpdateWork()
{
	if (m_ui)
		m_ui->update();
}

void CCustomDevice::TurnDeviceInternal(bool b)
{
	m_bWorking = b;
	
	if (ParentIsActor())
	{
		if (m_bWorking)
			g_pGamePersistent->pda_shader_data.pda_display_factor = 1.f;
		else
		{
			g_pGamePersistent->pda_shader_data.pda_display_factor = 0.f;
			ResetUI();
		}
	}
}

void CCustomDevice::UpdateCL()
{
	inherited::UpdateCL();

	if (H_Parent() != Level().CurrentEntity()) return;

	UpdateVisibility();

	if (IsUsingCondition())
	{
		if (m_bWorking && GetCondition() < m_fLowestBatteryCharge)
			TurnDeviceInternal(false);
		else if (!m_bWorking && (GetState() == eIdle || GetState() == eIdleZoom) && GetCondition() >= m_fLowestBatteryCharge)
			TurnDeviceInternal(true);
	}

	if (!IsWorking()) return;
	UpdateWork();
}

void CCustomDevice::UpdateHudAdditional(Fmatrix& trans)
{
	CActor* pActor = smart_cast<CActor*>(H_Parent());
	if (!pActor)
		return;

	attachable_hud_item* hi = HudItemData();
	R_ASSERT(hi);

	Fvector curr_offs, curr_rot;
	Fmatrix hud_rotation;

	curr_offs = g_player_hud->m_adjust_mode ? g_player_hud->m_adjust_offset[0][1] : hi->m_measures.m_hands_offset[0][1];
	curr_rot = g_player_hud->m_adjust_mode ? g_player_hud->m_adjust_offset[1][1] : hi->m_measures.m_hands_offset[1][1];

	curr_offs.mul(m_fZoomfactor);
	curr_rot.mul(m_fZoomfactor);

	hud_rotation.identity();
	hud_rotation.rotateX(curr_rot.x);

	Fmatrix hud_rotation_y;
	hud_rotation_y.identity();
	hud_rotation_y.rotateY(curr_rot.y);
	hud_rotation.mulA_43(hud_rotation_y);

	hud_rotation_y.identity();
	hud_rotation_y.rotateZ(curr_rot.z);
	hud_rotation.mulA_43(hud_rotation_y);

	hud_rotation.translate_over(curr_offs);
	trans.mulB_43(hud_rotation);

	if (m_bZoomed)
		m_fZoomfactor += Device.fTimeDelta * 2.f;
	else
		m_fZoomfactor -= Device.fTimeDelta * 2.f;

	clamp(m_fZoomfactor, 0.f, 1.f);

	if (!g_player_hud->inertion_allowed())
		return;

	static float fAvgTimeDelta = Device.fTimeDelta;
	fAvgTimeDelta = _inertion(fAvgTimeDelta, Device.fTimeDelta, 0.8f);

	u8 idx = m_bZoomed ? 1 : 0;

	float fYMag = pActor->fFPCamYawMagnitude;
	float fPMag = pActor->fFPCamPitchMagnitude;

	float fStrafeMaxTime = hi->m_measures.m_strafe_offset[2][idx].y;
	// Макс. время в секундах, за которое мы наклонимся из центрального положения
	if (fStrafeMaxTime <= EPS)
		fStrafeMaxTime = 0.01f;

	float fStepPerUpd = fAvgTimeDelta / fStrafeMaxTime; // Величина изменение фактора поворота

														// Добавляем боковой наклон от движения камеры
	float fCamReturnSpeedMod = 1.5f;
	// Восколько ускоряем нормализацию наклона, полученного от движения камеры (только от бедра)

	// Высчитываем минимальную скорость поворота камеры для начала инерции
	float fStrafeMinAngle = _lerp(
		hi->m_measures.m_strafe_offset[3][0].y,
		hi->m_measures.m_strafe_offset[3][1].y,
		m_fZoomfactor);

	// Высчитываем мксимальный наклон от поворота камеры
	float fCamLimitBlend = _lerp(
		hi->m_measures.m_strafe_offset[3][0].x,
		hi->m_measures.m_strafe_offset[3][1].x,
		m_fZoomfactor);

	// Считаем стрейф от поворота камеры
	if (abs(fYMag) > (m_fLR_CameraFactor == 0.0f ? fStrafeMinAngle : 0.0f))
	{
		//--> Камера крутится по оси Y
		m_fLR_CameraFactor -= (fYMag * fAvgTimeDelta * 0.75f);
		clamp(m_fLR_CameraFactor, -fCamLimitBlend, fCamLimitBlend);
	}
	else
	{
		//--> Камера не поворачивается - убираем наклон
		if (m_fLR_CameraFactor < 0.0f)
		{
			m_fLR_CameraFactor += fStepPerUpd * (m_bZoomed ? 1.0f : fCamReturnSpeedMod);
			clamp(m_fLR_CameraFactor, -fCamLimitBlend, 0.0f);
		}
		else
		{
			m_fLR_CameraFactor -= fStepPerUpd * (m_bZoomed ? 1.0f : fCamReturnSpeedMod);
			clamp(m_fLR_CameraFactor, 0.0f, fCamLimitBlend);
		}
	}

	// Добавляем боковой наклон от ходьбы вбок
	float fChangeDirSpeedMod = 3;
	// Восколько быстро меняем направление направление наклона, если оно в другую сторону от текущего
	u32 iMovingState = pActor->MovingState();
	if ((iMovingState & mcLStrafe) != 0)
	{
		// Движемся влево
		float fVal = (m_fLR_MovingFactor > 0.f ? fStepPerUpd * fChangeDirSpeedMod : fStepPerUpd);
		m_fLR_MovingFactor -= fVal;
	}
	else if ((iMovingState & mcRStrafe) != 0)
	{
		// Движемся вправо
		float fVal = (m_fLR_MovingFactor < 0.f ? fStepPerUpd * fChangeDirSpeedMod : fStepPerUpd);
		m_fLR_MovingFactor += fVal;
	}
	else
	{
		// Двигаемся в любом другом направлении - плавно убираем наклон
		if (m_fLR_MovingFactor < 0.0f)
		{
			m_fLR_MovingFactor += fStepPerUpd;
			clamp(m_fLR_MovingFactor, -1.0f, 0.0f);
		}
		else
		{
			m_fLR_MovingFactor -= fStepPerUpd;
			clamp(m_fLR_MovingFactor, 0.0f, 1.0f);
		}
	}
	clamp(m_fLR_MovingFactor, -1.0f, 1.0f); // Фактор боковой ходьбы не должен превышать эти лимиты

											// Вычисляем и нормализируем итоговый фактор наклона
	float fLR_Factor = m_fLR_MovingFactor;

	// No cam strafe inertia while in freelook mode
	if (pActor->cam_freelook == eflDisabled)
		fLR_Factor += m_fLR_CameraFactor;

	clamp(fLR_Factor, -1.0f, 1.0f); // Фактор боковой ходьбы не должен превышать эти лимиты

									// Производим наклон ствола для нормального режима и аима
	for (int _idx = 0; _idx <= 1; _idx++) //<-- Для плавного перехода
	{
		bool bEnabled = (hi->m_measures.m_strafe_offset[2][_idx].x != 0.0f);
		if (!bEnabled)
			continue;

		Fvector curr_offs, curr_rot;

		// Смещение позиции худа в стрейфе
		curr_offs = hi->m_measures.m_strafe_offset[0][_idx]; // pos
		curr_offs.mul(fLR_Factor); // Умножаем на фактор стрейфа

								   // Поворот худа в стрейфе
		curr_rot = hi->m_measures.m_strafe_offset[1][_idx]; // rot
		curr_rot.mul(-PI / 180.f); // Преобразуем углы в радианы
		curr_rot.mul(fLR_Factor); // Умножаем на фактор стрейфа

								  // Мягкий переход между бедром \ прицелом
		if (_idx == 0)
		{
			// От бедра
			curr_offs.mul(1.f - m_fZoomfactor);
			curr_rot.mul(1.f - m_fZoomfactor);
		}
		else
		{
			// Во время аима
			curr_offs.mul(m_fZoomfactor);
			curr_rot.mul(m_fZoomfactor);
		}

		Fmatrix hud_rotation;
		Fmatrix hud_rotation_y;

		hud_rotation.identity();
		hud_rotation.rotateX(curr_rot.x);

		hud_rotation_y.identity();
		hud_rotation_y.rotateY(curr_rot.y);
		hud_rotation.mulA_43(hud_rotation_y);

		hud_rotation_y.identity();
		hud_rotation_y.rotateZ(curr_rot.z);
		hud_rotation.mulA_43(hud_rotation_y);

		hud_rotation.translate_over(curr_offs);
		trans.mulB_43(hud_rotation);
	}

	//============= Инерция оружия =============//
	// Параметры инерции
	float fInertiaSpeedMod = _lerp(
		hi->m_measures.m_inertion_params.m_tendto_speed,
		hi->m_measures.m_inertion_params.m_tendto_speed_aim,
		m_fZoomfactor);

	float fInertiaReturnSpeedMod = _lerp(
		hi->m_measures.m_inertion_params.m_tendto_ret_speed,
		hi->m_measures.m_inertion_params.m_tendto_ret_speed_aim,
		m_fZoomfactor);

	float fInertiaMinAngle = _lerp(
		hi->m_measures.m_inertion_params.m_min_angle,
		hi->m_measures.m_inertion_params.m_min_angle_aim,
		m_fZoomfactor);

	Fvector4 vIOffsets; // x = L, y = R, z = U, w = D
	vIOffsets.x = _lerp(
		hi->m_measures.m_inertion_params.m_offset_LRUD.x,
		hi->m_measures.m_inertion_params.m_offset_LRUD_aim.x,
		m_fZoomfactor);
	vIOffsets.y = _lerp(
		hi->m_measures.m_inertion_params.m_offset_LRUD.y,
		hi->m_measures.m_inertion_params.m_offset_LRUD_aim.y,
		m_fZoomfactor);
	vIOffsets.z = _lerp(
		hi->m_measures.m_inertion_params.m_offset_LRUD.z,
		hi->m_measures.m_inertion_params.m_offset_LRUD_aim.z,
		m_fZoomfactor);
	vIOffsets.w = _lerp(
		hi->m_measures.m_inertion_params.m_offset_LRUD.w,
		hi->m_measures.m_inertion_params.m_offset_LRUD_aim.w,
		m_fZoomfactor);

	// Высчитываем инерцию из поворотов камеры
	bool bIsInertionPresent = m_fLR_InertiaFactor != 0.0f || m_fUD_InertiaFactor != 0.0f;
	if (abs(fYMag) > fInertiaMinAngle || bIsInertionPresent)
	{
		float fSpeed = fInertiaSpeedMod;
		if (fYMag > 0.0f && m_fLR_InertiaFactor > 0.0f ||
			fYMag < 0.0f && m_fLR_InertiaFactor < 0.0f)
		{
			fSpeed *= 2.f; //--> Ускоряем инерцию при движении в противоположную сторону
		}

		m_fLR_InertiaFactor -= (fYMag * fAvgTimeDelta * fSpeed); // Горизонталь (м.б. > |1.0|)
	}

	if (abs(fPMag) > fInertiaMinAngle || bIsInertionPresent)
	{
		float fSpeed = fInertiaSpeedMod;
		if (fPMag > 0.0f && m_fUD_InertiaFactor > 0.0f ||
			fPMag < 0.0f && m_fUD_InertiaFactor < 0.0f)
		{
			fSpeed *= 2.f; //--> Ускоряем инерцию при движении в противоположную сторону
		}

		m_fUD_InertiaFactor -= (fPMag * fAvgTimeDelta * fSpeed); // Вертикаль (м.б. > |1.0|)
	}

	clamp(m_fLR_InertiaFactor, -1.0f, 1.0f);
	clamp(m_fUD_InertiaFactor, -1.0f, 1.0f);

	// Плавное затухание инерции (основное, но без линейной никогда не опустит инерцию до полного 0.0f)
	m_fLR_InertiaFactor *= clampr(1.f - fAvgTimeDelta * fInertiaReturnSpeedMod, 0.0f, 1.0f);
	m_fUD_InertiaFactor *= clampr(1.f - fAvgTimeDelta * fInertiaReturnSpeedMod, 0.0f, 1.0f);

	// Минимальное линейное затухание инерции при покое (горизонталь)
	if (fYMag == 0.0f)
	{
		float fRetSpeedMod = (fYMag == 0.0f ? 1.0f : 0.75f) * (fInertiaReturnSpeedMod * 0.075f);
		if (m_fLR_InertiaFactor < 0.0f)
		{
			m_fLR_InertiaFactor += fAvgTimeDelta * fRetSpeedMod;
			clamp(m_fLR_InertiaFactor, -1.0f, 0.0f);
		}
		else
		{
			m_fLR_InertiaFactor -= fAvgTimeDelta * fRetSpeedMod;
			clamp(m_fLR_InertiaFactor, 0.0f, 1.0f);
		}
	}

	// Минимальное линейное затухание инерции при покое (вертикаль)
	if (fPMag == 0.0f)
	{
		float fRetSpeedMod = (fPMag == 0.0f ? 1.0f : 0.75f) * (fInertiaReturnSpeedMod * 0.075f);
		if (m_fUD_InertiaFactor < 0.0f)
		{
			m_fUD_InertiaFactor += fAvgTimeDelta * fRetSpeedMod;
			clamp(m_fUD_InertiaFactor, -1.0f, 0.0f);
		}
		else
		{
			m_fUD_InertiaFactor -= fAvgTimeDelta * fRetSpeedMod;
			clamp(m_fUD_InertiaFactor, 0.0f, 1.0f);
		}
	}

	// Применяем инерцию к худу
	float fLR_lim = (m_fLR_InertiaFactor < 0.0f ? vIOffsets.x : vIOffsets.y);
	float fUD_lim = (m_fUD_InertiaFactor < 0.0f ? vIOffsets.z : vIOffsets.w);

	curr_offs = { fLR_lim * -1.f * m_fLR_InertiaFactor, fUD_lim * m_fUD_InertiaFactor, 0.0f };

	hud_rotation.identity();
	hud_rotation.translate_over(curr_offs);
	trans.mulB_43(hud_rotation);
}

void CCustomDevice::OnH_B_Independent(bool just_before_destroy)
{
	inherited::OnH_B_Independent(just_before_destroy);

	if (GetState() != eHidden)
	{
		// Detaching hud item and animation stop in OnH_A_Independent
		TurnDeviceInternal(false);
		SwitchState(eHidden);
	}
}

void CCustomDevice::OnMoveToRuck(const SInvItemPlace& prev)
{
	inherited::OnMoveToRuck(prev);
	if (prev.type == eItemPlaceSlot)
	{
		SwitchState(eHidden);
		g_player_hud->detach_item(this);
	}
	TurnDeviceInternal(false);
	StopCurrentAnimWithoutCallback();
}

void CCustomDevice::on_a_hud_attach()
{
	inherited::on_a_hud_attach();

	if (nullptr == m_ui)
		CreateUI();
}

bool CCustomDevice::render_item_3d_ui_query()
{
	return IsWorking();
}

void CCustomDevice::render_item_3d_ui()
{
	R_ASSERT(HudItemData());
	inherited::render_item_3d_ui();
}
