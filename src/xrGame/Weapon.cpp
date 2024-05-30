////////////////////////////////////////////////////////////////////////////
//	Modified by Axel DominatoR
//	Last updated: 13/08/2015
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "Weapon.h"
#include "ParticlesObject.h"
#include "entity_alive.h"
#include "inventory_item_impl.h"
#include "inventory.h"
#include "xrserver_objects_alife_items.h"
#include "actor.h"
#include "actoreffector.h"
#include "level.h"
#include "xr_level_controller.h"
#include "game_cl_base.h"
#include "../Include/xrRender/Kinematics.h"
#include "ai_object_location.h"
#include "../xrphysics/mathutils.h"
#include "object_broker.h"
#include "player_hud.h"
#include "gamepersistent.h"
#include "effectorFall.h"
#include "debug_renderer.h"
#include "static_cast_checked.hpp"
#include "clsid_game.h"
#include "weaponBinocularsVision.h"
#include "ui/UIWindow.h"
#include "ui/UIXmlInit.h"
#include "Torch.h"
#include "../xrCore/vector.h"
#include "ActorNightVision.h"
#include "HUDManager.h"
#include "WeaponMagazinedWGrenade.h"
#include "../xrEngine/GameMtlLib.h"
#include "../Layers/xrRender/xrRender_console.h"
#include "pch_script.h"
#include "script_game_object.h"

#define WEAPON_REMOVE_TIME		60000
#define ROTATION_TIME			0.25f


float f_weapon_deterioration = 1.0f;
extern CUIXml* pWpnScopeXml;

//////////
extern float scope_radius;

Flags32 zoomFlags = {};
extern float n_zoom_step_count;
float sens_multiple = 1.0f;


float CWeapon::SDS_Radius(bool alt) {
	// hack for GL to always return 0, fix later
	if (m_zoomtype == 2)
		return 0.0;

	shared_str scope_tex_name;
	if (zoomFlags.test(SDS))
	{
		if (0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonScope) && m_scopes.size())
		{
			scope_tex_name = READ_IF_EXISTS(pSettings, r_string, GetScopeName(), alt ? "scope_texture_alt" : "scope_texture", NULL);
		}
		else
		{
			scope_tex_name = READ_IF_EXISTS(pSettings, r_string, cNameSect(), alt ? "scope_texture_alt" : "scope_texture", NULL);
		}

		if (scope_tex_name != 0) {
			auto item = listScopeRadii.find(scope_tex_name);
			if (item != listScopeRadii.end()) {
				return item->second;
			}
			else {
				return 0.0;
			}
		}
		else {
			return 0.0;
		}
	}
	else {
		return 0.0;
	}
}

//////////

CWeapon::CWeapon()
{
	SetState(eHidden);
	SetNextState(eHidden);
	m_sub_state = eSubstateReloadBegin;
	m_bTriStateReload = false;
	SetDefaults();

	m_Offset.identity();
	m_StrapOffset.identity();

	m_iAmmoCurrentTotal = 0;
	m_BriefInfo_CalcFrame = 0;

	iAmmoElapsed = -1;
	iMagazineSize = -1;
	m_ammoType = 0;

	eHandDependence = hdNone;
	m_APk = 1.0f;

	m_zoom_params.m_fCurrentZoomFactor = g_fov;
	m_zoom_params.m_fZoomRotationFactor = 0.f;
	m_zoom_params.m_pVision = NULL;
	m_zoom_params.m_pNight_vision = NULL;
	m_zoom_params.m_fSecondVPFovFactor = 0.0f;

	m_altAimPos = false;
	m_zoomtype = 0;

	m_pCurrentAmmo = NULL;

	m_pFlameParticles2 = NULL;
	m_sFlameParticles2 = NULL;

	m_fCurrentCartirdgeDisp = 1.f;

	m_strap_bone0 = 0;
	m_strap_bone1 = 0;
	m_StrapOffset.identity();
	m_strapped_mode = false;
	m_can_be_strapped = false;
	m_ef_main_weapon_type = u32(-1);
	m_ef_weapon_type = u32(-1);
	m_UIScope = NULL;
	m_set_next_ammoType_on_reload = undefined_ammo_type;
	m_crosshair_inertion = 0.f;
	m_activation_speed_is_overriden = false;
	m_cur_scope = 0;
	m_bRememberActorNVisnStatus = false;
	m_fLR_ShootingFactor = 0.f;
	m_fUD_ShootingFactor = 0.f;
	m_fBACKW_ShootingFactor = 0.f;
	m_bCanBeLowered = false;
	m_fSafeModeRotateTime = 0.f;
	bClearJamOnly = false;

	//PP.RQ.range = 0.f;
	//PP.RQ.set(NULL, 0.f, -1);

	bHasBulletsToHide = false;
	bullet_cnt = 0;
	IsCustomReloadAvaible = false;
	temperature = 0.f;	//--DSR-- SilencerOverheat
}

extern int scope_2dtexactive; //crookr
CWeapon::~CWeapon()
{
	xr_delete(m_UIScope);
	delete_data(m_scopes);
}

void CWeapon::Hit(SHit* pHDS)
{
	inherited::Hit(pHDS);
}

void CWeapon::UpdateXForm()
{
	if (Device.dwFrame == dwXF_Frame)
		return;

	dwXF_Frame = Device.dwFrame;

	if (!H_Parent())
		return;

	// Get access to entity and its visual
	CEntityAlive* E = smart_cast<CEntityAlive*>(H_Parent());

	if (!E)
	{
		if (!IsGameTypeSingle())
			UpdatePosition(H_Parent()->XFORM());

		return;
	}

	const CInventoryOwner* parent = smart_cast<const CInventoryOwner*>(E);
	if (parent && parent->use_simplified_visual())
		return;

	if (parent->attached(this))
		return;

	IKinematics* V = smart_cast<IKinematics*>(E->Visual());
	VERIFY(V);

	// Get matrices
	int boneL = -1, boneR = -1, boneR2 = -1;

	// this ugly case is possible in case of a CustomMonster, not a Stalker, nor an Actor
	E->g_WeaponBones(boneL, boneR, boneR2);

	if (boneR == -1) return;

	if ((HandDependence() == hd1Hand) || (GetState() == eReload) || (!E->g_Alive()))
		boneL = boneR2;

	V->CalculateBones();
	Fmatrix& mL = V->LL_GetTransform(u16(boneL));
	Fmatrix& mR = V->LL_GetTransform(u16(boneR));
	// Calculate
	Fmatrix mRes;
	Fvector R, D, N;
	D.sub(mL.c, mR.c);

	if (fis_zero(D.magnitude()))
	{
		mRes.set(E->XFORM());
		mRes.c.set(mR.c);
	}
	else
	{
		D.normalize();
		R.crossproduct(mR.j, D);

		N.crossproduct(D, R);
		N.normalize();

		mRes.set(R, N, D, mR.c);
		mRes.mulA_43(E->XFORM());
	}

	UpdatePosition(mRes);
}

void CWeapon::UpdateFireDependencies_internal()
{
	if (Device.dwFrame != dwFP_Frame)
	{
		dwFP_Frame = Device.dwFrame;

		UpdateXForm();

		if (GetHUDmode())
		{
			HudItemData()->setup_firedeps(m_current_firedeps);
			VERIFY(_valid(m_current_firedeps.m_FireParticlesXForm));
		}
		else
		{
			// 3rd person or no parent
			Fmatrix& parent = XFORM();
			Fvector& fp = vLoadedFirePoint;
			Fvector& fp2 = vLoadedFirePoint2;
			Fvector& sp = vLoadedShellPoint;

			parent.transform_tiny(m_current_firedeps.vLastFP, fp);
			parent.transform_tiny(m_current_firedeps.vLastFP2, fp2);
			parent.transform_tiny(m_current_firedeps.vLastSP, sp);

			m_current_firedeps.vLastFD.set(0.f, 0.f, 1.f);
			parent.transform_dir(m_current_firedeps.vLastFD);

			m_current_firedeps.m_FireParticlesXForm.set(parent);
			VERIFY(_valid(m_current_firedeps.m_FireParticlesXForm));
		}
	}
}

void updateCurrentScope() {
	if (!g_pGameLevel) return;

	CInventoryOwner* pGameObject = smart_cast<CInventoryOwner*>(Level().Objects.net_Find(0));
	if (pGameObject) {
		if (pGameObject->inventory().ActiveItem()) {
			CWeapon* weapon = smart_cast<CWeapon*>(pGameObject->inventory().ActiveItem());
			if (weapon) {
				weapon->UpdateZoomParams();
			}
		}
	}
}

void CWeapon::UpdateZoomParams() {
	//////////
	m_zoom_params.m_fMinBaseZoomFactor = READ_IF_EXISTS(pSettings, r_float, cNameSect(), "min_scope_zoom_factor", 200.0f);


	float zoom_multiple = 1.0f;
	if (zoomFlags.test(SDS_ZOOM) && (SDS_Radius(m_zoomtype == 1) > 0.0)) {
		zoom_multiple = scope_scrollpower;
	}

	//////////

	// Load scopes.xml if it's not loaded
	if (pWpnScopeXml == nullptr)
	{
		pWpnScopeXml = new CUIXml();
		pWpnScopeXml->Load(CONFIG_PATH, UI_PATH, "scopes.xml");
	}

	// update zoom factor
	if (m_zoomtype == 2) //GL
	{
		m_zoom_params.m_bUseDynamicZoom = m_zoom_params.m_bUseDynamicZoom_GL || READ_IF_EXISTS(pSettings, r_bool, cNameSect(), "scope_dynamic_zoom_gl", false);
		m_zoom_params.m_fScopeZoomFactor = g_player_hud->m_adjust_mode ? g_player_hud->m_adjust_zoom_factor[1] : READ_IF_EXISTS(pSettings, r_float, cNameSect(), "gl_zoom_factor", 0);
	} else if (m_zoomtype == 1) //Alt
	{
		m_zoom_params.m_bUseDynamicZoom = m_zoom_params.m_bUseDynamicZoom_Alt || READ_IF_EXISTS(pSettings, r_bool, cNameSect(), "scope_dynamic_zoom_alt", false);
		m_zoom_params.m_fScopeZoomFactor = (g_player_hud->m_adjust_mode ? g_player_hud->m_adjust_zoom_factor[2] : READ_IF_EXISTS(pSettings, r_float, cNameSect(), "scope_zoom_factor_alt", 0)) / (READ_IF_EXISTS(pSettings, r_string, cNameSect(), "scope_texture_alt", NULL) && zoomFlags.test(SDS_ZOOM) && (SDS_Radius(true) > 0.0) ? zoom_multiple : 1);
	} else //Main Sight
	{
		m_zoom_params.m_bUseDynamicZoom = m_zoom_params.m_bUseDynamicZoom_Primary || READ_IF_EXISTS(pSettings, r_bool, cNameSect(), "scope_dynamic_zoom", false);
		if (g_player_hud->m_adjust_mode)
		{
			m_zoom_params.m_fScopeZoomFactor = g_player_hud->m_adjust_zoom_factor[0] / zoom_multiple;
		} else if (ALife::eAddonPermanent != m_eScopeStatus && 0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonScope) && m_scopes.size())
		{
			m_zoom_params.m_fScopeZoomFactor = pSettings->r_float(GetScopeName(), "scope_zoom_factor") / zoom_multiple;
		} else
		{
			m_zoom_params.m_fScopeZoomFactor = m_zoom_params.m_fBaseZoomFactor / zoom_multiple;
		}
	}

	if (IsZoomed()) {
		scope_radius = SDS_Radius(m_zoomtype == 1);
		if (m_zoomtype == 0 && zoomFlags.test(SDS_SPEED) && (scope_radius > 0.0)) {
			sens_multiple = scope_scrollpower;
		} else {
			sens_multiple = 1.0f;
		}


		if (m_zoom_params.m_bUseDynamicZoom) {
			SetZoomFactor(m_fRTZoomFactor / zoom_multiple);
		} else {
			SetZoomFactor(m_zoom_params.m_fScopeZoomFactor);
		}
	}
}

void CWeapon::UpdateUIScope()
{
	UpdateZoomParams();

	// Change or remove scope texture
	shared_str scope_tex_name;
	if (m_zoomtype == 0)
	{
		if (0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonScope) && m_scopes.size())
		{
			scope_tex_name = pSettings->r_string(GetScopeName(), "scope_texture");
		}
		else
		{
			scope_tex_name = READ_IF_EXISTS(pSettings, r_string, cNameSect(), "scope_texture", NULL);
		}
	}
	else if (m_zoomtype == 1)
	{
		scope_tex_name = READ_IF_EXISTS(pSettings, r_string, cNameSect(), "scope_texture_alt", NULL);
	}

	if (!g_dedicated_server)
	{
		xr_delete(m_UIScope);
		scope_2dtexactive = 0; //crookr

		if (!scope_tex_name || scope_tex_name.equal("none") || g_player_hud->m_adjust_mode)
			return;

		m_UIScope = xr_new<CUIWindow>();
		CUIXmlInit::InitWindow(*pWpnScopeXml, scope_tex_name.c_str(), 0, m_UIScope);
	}
}

void CWeapon::SwitchZoomType()
{
	if (m_zoomtype == 0 && (m_altAimPos || g_player_hud->m_adjust_mode))
	{
        SetZoomType(1);
        m_zoom_params.m_bUseDynamicZoom = m_zoom_params.m_bUseDynamicZoom_Alt || READ_IF_EXISTS(pSettings, r_bool, cNameSect(), "scope_dynamic_zoom_alt", false);
	}
	else if (IsGrenadeLauncherAttached())
	{
		SwitchState(eSwitch);
		return;
	}
	else if (m_zoomtype != 0)
	{
        SetZoomType(0);
        m_zoom_params.m_bUseDynamicZoom = m_zoom_params.m_bUseDynamicZoom_Primary || READ_IF_EXISTS(pSettings, r_bool, cNameSect(), "scope_dynamic_zoom", false);
	}

	UpdateUIScope();
}

void CWeapon::SetZoomType(u8 new_zoom_type)
{
    int previous_zoom_type = m_zoomtype;
    m_zoomtype = new_zoom_type;

    luabind::functor<void> funct;
    if (ai().script_engine().functor("_G.CWeapon_OnSwitchZoomType", funct))
    {
        funct(this->lua_game_object(), previous_zoom_type, m_zoomtype);
    }
}

extern float g_ironsights_factor;

float CWeapon::GetHudFov()
{
	float base = inherited::GetHudFov();

	/*
	if (m_zoom_params.m_fBaseZoomFactor == 0.f)
	{
		base = _lerp(base, base * (g_ironsights_factor / 1.7f), m_zoom_params.m_fZoomRotationFactor);
		clamp(base, 0.1f, 1.f);
	}
	*/

	return base;
}

void CWeapon::ForceUpdateFireParticles()
{
	if (!GetHUDmode())
	{
		//update particlesXFORM real bullet direction
		if (!H_Parent()) return;

		Fvector p, d;
		smart_cast<CEntity*>(H_Parent())->g_fireParams(this, p, d);

		Fmatrix _pxf;
		_pxf.k = d;
		_pxf.i.crossproduct(Fvector().set(0.0f, 1.0f, 0.0f), _pxf.k);
		_pxf.j.crossproduct(_pxf.k, _pxf.i);
		_pxf.c = XFORM().c;

		m_current_firedeps.m_FireParticlesXForm.set(_pxf);
	}
}

void CWeapon::Load(LPCSTR section)
{
	inherited::Load(section);
	CShootingObject::Load(section);

	if (pSettings->line_exist(section, "flame_particles_2"))
		m_sFlameParticles2 = pSettings->r_string(section, "flame_particles_2");

	// load ammo classes
	m_ammoTypes.clear();
	LPCSTR S = pSettings->r_string(section, "ammo_class");
	if (S && S[0])
	{
		string128 _ammoItem;
		int count = _GetItemCount(S);
		for (int it = 0; it < count; ++it)
		{
			_GetItem(S, it, _ammoItem);
			m_ammoTypes.push_back(_ammoItem);
		}
	}

	iAmmoElapsed = pSettings->r_s32(section, "ammo_elapsed");
	iMagazineSize = pSettings->r_s32(section, "ammo_mag_size");

	////////////////////////////////////////////////////
	// дисперсия стрельбы

	//подбрасывание камеры во время отдачи
	u8 rm = READ_IF_EXISTS(pSettings, r_u8, section, "cam_return", 1);
	cam_recoil.ReturnMode = (rm == 1);

	rm = READ_IF_EXISTS(pSettings, r_u8, section, "cam_return_stop", 0);
	cam_recoil.StopReturn = (rm == 1);

	float temp_f = 0.0f;
	temp_f = pSettings->r_float(section, "cam_relax_speed");
	cam_recoil.RelaxSpeed = _abs(deg2rad(temp_f));
	//AVO: commented out as very minor and is clashing with weapon mods
	//UNDONE after non fatal VERIFY implementation
	VERIFY(!fis_zero(cam_recoil.RelaxSpeed));
	if (fis_zero(cam_recoil.RelaxSpeed))
	{
		cam_recoil.RelaxSpeed = EPS_L;
	}

	cam_recoil.RelaxSpeed_AI = cam_recoil.RelaxSpeed;
	if (pSettings->line_exist(section, "cam_relax_speed_ai"))
	{
		temp_f = pSettings->r_float(section, "cam_relax_speed_ai");
		cam_recoil.RelaxSpeed_AI = _abs(deg2rad(temp_f));
		VERIFY(!fis_zero(cam_recoil.RelaxSpeed_AI));
		if (fis_zero(cam_recoil.RelaxSpeed_AI))
		{
			cam_recoil.RelaxSpeed_AI = EPS_L;
		}
	}
	temp_f = pSettings->r_float(section, "cam_max_angle");
	cam_recoil.MaxAngleVert = _abs(deg2rad(temp_f));
	VERIFY(!fis_zero(cam_recoil.MaxAngleVert));
	if (fis_zero(cam_recoil.MaxAngleVert))
	{
		cam_recoil.MaxAngleVert = EPS;
	}

	temp_f = pSettings->r_float(section, "cam_max_angle_horz");
	cam_recoil.MaxAngleHorz = _abs(deg2rad(temp_f));
	VERIFY(!fis_zero(cam_recoil.MaxAngleHorz));
	if (fis_zero(cam_recoil.MaxAngleHorz))
	{
		cam_recoil.MaxAngleHorz = EPS;
	}

	temp_f = pSettings->r_float(section, "cam_step_angle_horz");
	cam_recoil.StepAngleHorz = deg2rad(temp_f);

	cam_recoil.DispersionFrac = _abs(READ_IF_EXISTS(pSettings, r_float, section, "cam_dispersion_frac", 0.7f));

	//ïîäáðàñûâàíèå êàìåðû âî âðåìÿ îòäà÷è â ðåæèìå zoom ==> ironsight or scope
	//zoom_cam_recoil.Clone( cam_recoil ); ==== íåëüçÿ !!!!!!!!!!
	zoom_cam_recoil.RelaxSpeed = cam_recoil.RelaxSpeed;
	zoom_cam_recoil.RelaxSpeed_AI = cam_recoil.RelaxSpeed_AI;
	zoom_cam_recoil.DispersionFrac = cam_recoil.DispersionFrac;
	zoom_cam_recoil.MaxAngleVert = cam_recoil.MaxAngleVert;
	zoom_cam_recoil.MaxAngleHorz = cam_recoil.MaxAngleHorz;
	zoom_cam_recoil.StepAngleHorz = cam_recoil.StepAngleHorz;

	zoom_cam_recoil.ReturnMode = cam_recoil.ReturnMode;
	zoom_cam_recoil.StopReturn = cam_recoil.StopReturn;

	if (pSettings->line_exist(section, "zoom_cam_relax_speed"))
	{
		zoom_cam_recoil.RelaxSpeed = _abs(deg2rad(pSettings->r_float(section, "zoom_cam_relax_speed")));
		VERIFY(!fis_zero(zoom_cam_recoil.RelaxSpeed));
		if (fis_zero(zoom_cam_recoil.RelaxSpeed))
		{
			zoom_cam_recoil.RelaxSpeed = EPS_L;
		}
	}
	if (pSettings->line_exist(section, "zoom_cam_relax_speed_ai"))
	{
		zoom_cam_recoil.RelaxSpeed_AI = _abs(deg2rad(pSettings->r_float(section, "zoom_cam_relax_speed_ai")));
		VERIFY(!fis_zero(zoom_cam_recoil.RelaxSpeed_AI));
		if (fis_zero(zoom_cam_recoil.RelaxSpeed_AI))
		{
			zoom_cam_recoil.RelaxSpeed_AI = EPS_L;
		}
	}
	if (pSettings->line_exist(section, "zoom_cam_max_angle"))
	{
		zoom_cam_recoil.MaxAngleVert = _abs(deg2rad(pSettings->r_float(section, "zoom_cam_max_angle")));
		VERIFY(!fis_zero(zoom_cam_recoil.MaxAngleVert));
		if (fis_zero(zoom_cam_recoil.MaxAngleVert))
		{
			zoom_cam_recoil.MaxAngleVert = EPS;
		}
	}
	if (pSettings->line_exist(section, "zoom_cam_max_angle_horz"))
	{
		zoom_cam_recoil.MaxAngleHorz = _abs(deg2rad(pSettings->r_float(section, "zoom_cam_max_angle_horz")));
		VERIFY(!fis_zero(zoom_cam_recoil.MaxAngleHorz));
		if (fis_zero(zoom_cam_recoil.MaxAngleHorz))
		{
			zoom_cam_recoil.MaxAngleHorz = EPS;
		}
	}
	if (pSettings->line_exist(section, "zoom_cam_step_angle_horz"))
	{
		zoom_cam_recoil.StepAngleHorz = deg2rad(pSettings->r_float(section, "zoom_cam_step_angle_horz"));
	}
	if (pSettings->line_exist(section, "zoom_cam_dispersion_frac"))
	{
		zoom_cam_recoil.DispersionFrac = _abs(pSettings->r_float(section, "zoom_cam_dispersion_frac"));
	}

	m_pdm.m_fPDM_disp_base = pSettings->r_float(section, "PDM_disp_base");
	m_pdm.m_fPDM_disp_vel_factor = pSettings->r_float(section, "PDM_disp_vel_factor");
	m_pdm.m_fPDM_disp_accel_factor = pSettings->r_float(section, "PDM_disp_accel_factor");
	m_pdm.m_fPDM_disp_crouch = pSettings->r_float(section, "PDM_disp_crouch");
	m_pdm.m_fPDM_disp_crouch_no_acc = pSettings->r_float(section, "PDM_disp_crouch_no_acc");
	m_pdm.m_fPDM_disp_buckShot = READ_IF_EXISTS(pSettings, r_float, section, "PDM_disp_buckshot", 1.f);
	m_crosshair_inertion = READ_IF_EXISTS(pSettings, r_float, section, "crosshair_inertion", 5.91f);
	m_first_bullet_controller.load(section);
	fireDispersionConditionFactor = pSettings->r_float(section, "fire_dispersion_condition_factor");

	// modified by Peacemaker [17.10.08]
	//	misfireProbability			  = pSettings->r_float(section,"misfire_probability");
	//	misfireConditionK			  = READ_IF_EXISTS(pSettings, r_float, section, "misfire_condition_k",	1.0f);
	misfireStartCondition = pSettings->r_float(section, "misfire_start_condition");
	misfireEndCondition = READ_IF_EXISTS(pSettings, r_float, section, "misfire_end_condition", 0.f);
	misfireStartProbability = READ_IF_EXISTS(pSettings, r_float, section, "misfire_start_prob", 0.f);
	misfireEndProbability = pSettings->r_float(section, "misfire_end_prob");
	conditionDecreasePerShot = pSettings->r_float(section, "condition_shot_dec");
	conditionDecreasePerQueueShot = READ_IF_EXISTS(pSettings, r_float, section, "condition_queue_shot_dec",
	                                               conditionDecreasePerShot);

	vLoadedFirePoint = pSettings->r_fvector3(section, "fire_point");

	if (pSettings->line_exist(section, "fire_point2"))
		vLoadedFirePoint2 = pSettings->r_fvector3(section, "fire_point2");
	else
		vLoadedFirePoint2 = vLoadedFirePoint;

	// hands
	eHandDependence = EHandDependence(pSettings->r_s32(section, "hand_dependence"));
	m_bIsSingleHanded = true;
	if (pSettings->line_exist(section, "single_handed"))
		m_bIsSingleHanded = !!pSettings->r_bool(section, "single_handed");
	//
	m_fMinRadius = pSettings->r_float(section, "min_radius");
	m_fMaxRadius = pSettings->r_float(section, "max_radius");

	// èíôîðìàöèÿ î âîçìîæíûõ àïãðåéäàõ è èõ âèçóàëèçàöèè â èíâåíòàðå
	m_eScopeStatus = (ALife::EWeaponAddonStatus)pSettings->r_s32(section, "scope_status");
	m_eSilencerStatus = (ALife::EWeaponAddonStatus)pSettings->r_s32(section, "silencer_status");
	m_eGrenadeLauncherStatus = (ALife::EWeaponAddonStatus)pSettings->r_s32(section, "grenade_launcher_status");

	m_altAimPos = READ_IF_EXISTS(pSettings, r_bool, section, "use_alt_aim_hud", false);

	m_zoom_params.m_bZoomEnabled = !!pSettings->r_bool(section, "zoom_enabled");
	m_zoom_params.m_fZoomRotateTime = pSettings->r_float(section, "zoom_rotate_time");
	m_fZoomRotateModifier = READ_IF_EXISTS(pSettings, r_float, section, "zoom_rotate_modifier", 1);
	m_zoom_params.m_fBaseZoomFactor = READ_IF_EXISTS(pSettings, r_float, cNameSect(), "scope_zoom_factor", 0);

	if (m_eScopeStatus == ALife::eAddonAttachable)
	{
		if (pSettings->line_exist(section, "scopes_sect"))
		{
			LPCSTR str = pSettings->r_string(section, "scopes_sect");
			for (int i = 0, count = _GetItemCount(str); i < count; ++i)
			{
				string128 scope_section;
				_GetItem(str, i, scope_section);
				m_scopes.push_back(scope_section);
			}
		}
		else
		{
			m_scopes.push_back(section);
		}
	}
	else if (m_eScopeStatus == ALife::eAddonPermanent)
	{
		shared_str scope_tex_name = READ_IF_EXISTS(pSettings, r_string, cNameSect(), "scope_texture", NULL);

		if (!!scope_tex_name && !scope_tex_name.equal("none") && !g_player_hud->m_adjust_mode)
		{
			if (!g_dedicated_server)
			{
				m_UIScope = xr_new<CUIWindow>();
				if (!pWpnScopeXml)
				{
					pWpnScopeXml = xr_new<CUIXml>();
					pWpnScopeXml->Load(CONFIG_PATH, UI_PATH, "scopes.xml");
				}
				CUIXmlInit::InitWindow(*pWpnScopeXml, scope_tex_name.c_str(), 0, m_UIScope);
			}
		}
	}

	if (m_eSilencerStatus == ALife::eAddonAttachable)
	{
		m_sSilencerName = pSettings->r_string(section, "silencer_name");
		m_iSilencerX = pSettings->r_s32(section, "silencer_x");
		m_iSilencerY = pSettings->r_s32(section, "silencer_y");
	}

	if (m_eGrenadeLauncherStatus == ALife::eAddonAttachable)
	{
		m_sGrenadeLauncherName = pSettings->r_string(section, "grenade_launcher_name");
		m_iGrenadeLauncherX = pSettings->r_s32(section, "grenade_launcher_x");
		m_iGrenadeLauncherY = pSettings->r_s32(section, "grenade_launcher_y");
	}

	InitAddons();
	if (pSettings->line_exist(section, "weapon_remove_time"))
		m_dwWeaponRemoveTime = pSettings->r_u32(section, "weapon_remove_time");
	else
		m_dwWeaponRemoveTime = WEAPON_REMOVE_TIME;

	if (pSettings->line_exist(section, "auto_spawn_ammo"))
		m_bAutoSpawnAmmo = pSettings->r_bool(section, "auto_spawn_ammo");
	else
		m_bAutoSpawnAmmo = TRUE;

	m_zoom_params.m_fSecondVPFovFactor = READ_IF_EXISTS(pSettings, r_float, section, "scope_lense_fov", 0.0f);
	m_zoom_params.m_bHideCrosshairInZoom = true;

	if (pSettings->line_exist(hud_sect, "zoom_hide_crosshair"))
		m_zoom_params.m_bHideCrosshairInZoom = !!pSettings->r_bool(hud_sect, "zoom_hide_crosshair");

	Fvector def_dof;
	def_dof.set(-1, -1, -1);
	m_zoom_params.m_ZoomDof = READ_IF_EXISTS(pSettings, r_fvector3, section, "zoom_dof", Fvector().set(-1, -1, -1));
	m_zoom_params.m_bZoomDofEnabled = !def_dof.similar(m_zoom_params.m_ZoomDof);

	m_zoom_params.m_ReloadDof = READ_IF_EXISTS(pSettings, r_fvector4, section, "reload_dof",
	                                           Fvector4().set(-1, -1, -1, -1));

	//Swartz: empty reload
	m_zoom_params.m_ReloadEmptyDof = READ_IF_EXISTS(pSettings, r_fvector4, section, "reload_empty_dof",
	                                                Fvector4().set(-1, -1, -1, -1));
	//-Swartz

	m_bHasTracers = !!READ_IF_EXISTS(pSettings, r_bool, section, "tracers", true);
	m_u8TracerColorID = READ_IF_EXISTS(pSettings, r_u8, section, "tracers_color_ID", u8(-1));

	string256 temp;
	for (int i = egdNovice; i < egdCount; ++i)
	{
		strconcat(sizeof(temp), temp, "hit_probability_", get_token_name(difficulty_type_token, i));
		m_hit_probability[i] = READ_IF_EXISTS(pSettings, r_float, section, temp, 1.f);
	}

	m_zoom_params.m_bUseDynamicZoom = READ_IF_EXISTS(pSettings, r_bool, section, "scope_dynamic_zoom", FALSE);
	m_zoom_params.m_sUseZoomPostprocess = 0;
	m_zoom_params.m_sUseBinocularVision = 0;

	// Added by Axel, to enable optional condition use on any item
	m_flags.set(FUsingCondition, READ_IF_EXISTS(pSettings, r_bool, section, "use_condition", TRUE));

	m_APk = READ_IF_EXISTS(pSettings, r_float, section, "ap_modifier", 1.0f);

	m_bCanBeLowered = READ_IF_EXISTS(pSettings, r_bool, section, "can_be_lowered", false);

	m_fSafeModeRotateTime = READ_IF_EXISTS(pSettings, r_float, section, "weapon_lower_speed", 1.f);

	UpdateUIScope();

	// Rezy safemode blend anms
	m_safemode_anm[0].name = READ_IF_EXISTS(pSettings, r_string, *hud_sect, "safemode_anm", nullptr);
	m_safemode_anm[1].name = READ_IF_EXISTS(pSettings, r_string, *hud_sect, "safemode_anm2", nullptr);
	m_safemode_anm[0].speed = READ_IF_EXISTS(pSettings, r_float, *hud_sect, "safemode_anm_speed", 1.f);
	m_safemode_anm[1].speed = READ_IF_EXISTS(pSettings, r_float, *hud_sect, "safemode_anm_speed2", 1.f);
	m_safemode_anm[0].power = READ_IF_EXISTS(pSettings, r_float, *hud_sect, "safemode_anm_power", 1.f);
	m_safemode_anm[1].power = READ_IF_EXISTS(pSettings, r_float, *hud_sect, "safemode_anm_power2", 1.f);

	m_shoot_shake_mat.identity();

	//--DSR-- SilencerOverheat_start
	if (m_eSilencerStatus == ALife::eAddonAttachable)
	{
		auto kinematics = smart_cast<IKinematics*>(renderable.visual);
		if (kinematics)
		{
			auto visual = kinematics->GetVisualByBone("wpn_silencer");
			if (visual)
				visual->MarkAsGlowing(true);
		}
	}
	//--DSR-- SilencerOverheat_end
}

// demonized: World model on stalkers adjustments
void CWeapon::set_mFirePoint(Fvector &fire_point) {
	vLoadedFirePoint = fire_point;
}

void CWeapon::set_mFirePoint2(Fvector &fire_point) {
	vLoadedFirePoint2 = fire_point;
}

void CWeapon::set_mShellPoint(Fvector &fire_point) {
	vLoadedShellPoint = fire_point;
}

void CWeapon::LoadFireParams(LPCSTR section)
{
	cam_recoil.Dispersion = deg2rad(pSettings->r_float(section, "cam_dispersion"));
	cam_recoil.DispersionInc = 0.0f;

	if (pSettings->line_exist(section, "cam_dispersion_inc"))
	{
		cam_recoil.DispersionInc = deg2rad(pSettings->r_float(section, "cam_dispersion_inc"));
	}

	zoom_cam_recoil.Dispersion = cam_recoil.Dispersion;
	zoom_cam_recoil.DispersionInc = cam_recoil.DispersionInc;

	if (pSettings->line_exist(section, "zoom_cam_dispersion"))
	{
		zoom_cam_recoil.Dispersion = deg2rad(pSettings->r_float(section, "zoom_cam_dispersion"));
	}
	if (pSettings->line_exist(section, "zoom_cam_dispersion_inc"))
	{
		zoom_cam_recoil.DispersionInc = deg2rad(pSettings->r_float(section, "zoom_cam_dispersion_inc"));
	}

	CShootingObject::LoadFireParams(section);
};

void GetZoomData(const float scope_factor, float& delta, float& min_zoom_factor);

BOOL CWeapon::net_Spawn(CSE_Abstract* DC)
{
	if (m_zoom_params.m_bUseDynamicZoom)
	{
		float delta, min_zoom_factor;
		GetZoomData(m_zoom_params.m_fScopeZoomFactor, delta, min_zoom_factor);
		m_fRTZoomFactor = min_zoom_factor;
	}
	else
		m_fRTZoomFactor = m_zoom_params.m_fScopeZoomFactor;

	BOOL bResult = inherited::net_Spawn(DC);
	CSE_Abstract* e = (CSE_Abstract*)(DC);
	CSE_ALifeItemWeapon* E = smart_cast<CSE_ALifeItemWeapon*>(e);
	
	iAmmoElapsed = E->a_elapsed;
	m_flagsAddOnState = E->m_addon_flags.get();
	m_ammoType = E->ammo_type;
	SetState(E->wpn_state);
	SetNextState(E->wpn_state);

	m_DefaultCartridge.Load(m_ammoTypes[m_ammoType].c_str(), m_ammoType, m_APk);
	if (iAmmoElapsed)
	{
		m_fCurrentCartirdgeDisp = m_DefaultCartridge.param_s.kDisp;
		for (int i = 0; i < iAmmoElapsed; ++i)
			m_magazine.push_back(m_DefaultCartridge);
	}

	UpdateAddonsVisibility();
	InitAddons();

	m_dwWeaponIndependencyTime = 0;

	VERIFY((u32) iAmmoElapsed == m_magazine.size());
	m_bAmmoWasSpawned = false;

	return bResult;
}

void CWeapon::net_Destroy()
{
	inherited::net_Destroy();

	//óäàëèòü îáúåêòû ïàðòèêëîâ
	StopFlameParticles();
	StopFlameParticles2();
	StopLight();
	Light_Destroy();

	while (m_magazine.size()) m_magazine.pop_back();
}

BOOL CWeapon::IsUpdating()
{
	bool bIsActiveItem = m_pInventory && m_pInventory->ActiveItem() == this;
	return bIsActiveItem || bWorking; // || IsPending() || getVisible();
}

void CWeapon::net_Export(NET_Packet& P)
{
	inherited::net_Export(P);

	P.w_float_q8(GetCondition(), 0.0f, 1.0f);

	u8 need_upd = IsUpdating() ? 1 : 0;
	P.w_u8(need_upd);
	P.w_u16(u16(iAmmoElapsed));
	P.w_u8(m_flagsAddOnState);
	P.w_u8(m_ammoType);
	P.w_u8((u8)GetState());
	P.w_u8((u8)IsZoomed());
}

void CWeapon::net_Import(NET_Packet& P)
{
	inherited::net_Import(P);

	float _cond;
	P.r_float_q8(_cond, 0.0f, 1.0f);
	SetCondition(_cond);

	u8 flags = 0;
	P.r_u8(flags);

	u16 ammo_elapsed = 0;
	P.r_u16(ammo_elapsed);

	u8 NewAddonState;
	P.r_u8(NewAddonState);

	m_flagsAddOnState = NewAddonState;
	UpdateAddonsVisibility();

	u8 ammoType, wstate;
	P.r_u8(ammoType);
	P.r_u8(wstate);

	u8 Zoom;
	P.r_u8((u8)Zoom);

	if (H_Parent() && H_Parent()->Remote())
	{
		if (Zoom) OnZoomIn();
		else OnZoomOut();
	};
	switch (wstate)
	{
	case eFire:
	case eFire2:
	case eSwitch:
	case eReload:
		{
		}
		break;
	default:
		{
		if (ammoType >= m_ammoTypes.size())
				Msg("!! Weapon [%d], State - [%d]", ID(), wstate);
			else
			{
				m_ammoType = ammoType;
				SetAmmoElapsed((ammo_elapsed));
			}
		}
		break;
	}

	VERIFY((u32) iAmmoElapsed == m_magazine.size());
}

void CWeapon::save(NET_Packet& output_packet)
{
	inherited::save(output_packet);
	save_data(iAmmoElapsed, output_packet);
	save_data(m_cur_scope, output_packet);
	save_data(m_flagsAddOnState, output_packet);
	save_data(m_ammoType, output_packet);
	save_data(m_zoom_params.m_bIsZoomModeNow, output_packet);
	save_data(m_bRememberActorNVisnStatus, output_packet);
}

void CWeapon::load(IReader& input_packet)
{
	inherited::load(input_packet);
	load_data(iAmmoElapsed, input_packet);
	load_data(m_cur_scope, input_packet);
	load_data(m_flagsAddOnState, input_packet);
	UpdateAddonsVisibility();
	load_data(m_ammoType, input_packet);
	load_data(m_zoom_params.m_bIsZoomModeNow, input_packet);

	if (m_zoom_params.m_bIsZoomModeNow)
		OnZoomIn();
	else
		OnZoomOut();

	load_data(m_bRememberActorNVisnStatus, input_packet);
}

void CWeapon::OnEvent(NET_Packet& P, u16 type)
{
	switch (type)
	{
	case GE_ADDON_CHANGE:
		{
			P.r_u8(m_flagsAddOnState);
			InitAddons();
			UpdateAddonsVisibility();
		}
		break;

	case GE_WPN_STATE_CHANGE:
		{
			u8 state;
			P.r_u8(state);
			P.r_u8(m_sub_state);
			//			u8 NewAmmoType =
			P.r_u8();
			u8 AmmoElapsed = P.r_u8();
			u8 NextAmmo = P.r_u8();
			if (NextAmmo == undefined_ammo_type)
				m_set_next_ammoType_on_reload = undefined_ammo_type;
			else
				m_set_next_ammoType_on_reload = NextAmmo;

			if (OnClient()) SetAmmoElapsed(int(AmmoElapsed));
			OnStateSwitch(u32(state), GetState());
		}
		break;
	default:
		{
			inherited::OnEvent(P, type);
		}
		break;
	}
};

void CWeapon::shedule_Update(u32 dT)
{
	inherited::shedule_Update(dT);
	//--DSR-- SilencerOverheat_start
	temperature -= (float)dT / 1000.f * sil_glow_cool_temp_rate;
	if (temperature < 0.f)
		temperature = 0.f;
	//--DSR-- SilencerOverheat_end
}

void CWeapon::OnH_B_Independent(bool just_before_destroy)
{
	RemoveShotEffector();

	inherited::OnH_B_Independent(just_before_destroy);

	FireEnd();
	SetPending(FALSE);
	SwitchState(eHidden);

	m_strapped_mode = false;
	m_zoom_params.m_bIsZoomModeNow = false;
	UpdateXForm();

	if (ParentIsActor())
		Actor()->set_safemode(false);
}

void CWeapon::OnH_A_Independent()
{
	m_fLR_ShootingFactor = 0.f;
	m_fUD_ShootingFactor = 0.f;
	m_fBACKW_ShootingFactor = 0.f;
	m_dwWeaponIndependencyTime = Level().timeServer();
	inherited::OnH_A_Independent();
	Light_Destroy();
	UpdateAddonsVisibility();
};

void CWeapon::OnH_A_Chield()
{
	inherited::OnH_A_Chield();
	UpdateAddonsVisibility();
};

void CWeapon::OnActiveItem()
{
	//. from Activate
	UpdateAddonsVisibility();
	m_BriefInfo_CalcFrame = 0;

	//. Show
	SwitchState(eShowing);
	//-

	inherited::OnActiveItem();
	//åñëè ìû çàíðóæàåìñÿ è îðóæèå áûëî â ðóêàõ
	//.	SetState					(eIdle);
	//.	SetNextState				(eIdle);
}

void CWeapon::OnHiddenItem()
{
	m_BriefInfo_CalcFrame = 0;

	if (IsGameTypeSingle())
		SwitchState(eHiding);
	else
		SwitchState(eHidden);

	OnZoomOut();
	inherited::OnHiddenItem();

	m_set_next_ammoType_on_reload = undefined_ammo_type;
}

void CWeapon::SendHiddenItem()
{
	if (!CHudItem::object().getDestroy() && m_pInventory)
	{
		// !!! Just single entry for given state !!!
		NET_Packet P;
		CHudItem::object().u_EventGen(P, GE_WPN_STATE_CHANGE, CHudItem::object().ID());
		P.w_u8(u8(eHiding));
		P.w_u8(u8(m_sub_state));
		P.w_u8(m_ammoType);
		P.w_u8(u8(iAmmoElapsed & 0xff));
		P.w_u8(m_set_next_ammoType_on_reload);
		CHudItem::object().u_EventSend(P, net_flags(TRUE, TRUE, FALSE, TRUE));
		SetPending(TRUE);
	}
}

bool CWeapon::NeedBlendAnm()
{
	if (GetState() == eIdle && Actor()->is_safemode())
		return true;

	if (IsZoomed() && psDeviceFlags2.test(rsAimSway))
		return true;

	return inherited::NeedBlendAnm();
}

void CWeapon::OnH_B_Chield()
{
	m_dwWeaponIndependencyTime = 0;
	inherited::OnH_B_Chield();

	OnZoomOut();
	m_set_next_ammoType_on_reload = undefined_ammo_type;
}

extern u32 hud_adj_mode;

bool CWeapon::AllowBore()
{
	return !Actor()->is_safemode();
}

void CWeapon::UpdateCL()
{
	inherited::UpdateCL();
	UpdateHUDAddonsVisibility();
	//ïîäñâåòêà îò âûñòðåëà
	UpdateLight();

	//íàðèñîâàòü ïàðòèêëû
	UpdateFlameParticles();
	UpdateFlameParticles2();

	if (!IsGameTypeSingle())
		make_Interpolation();

	if ((GetNextState() == GetState()) && IsGameTypeSingle() && H_Parent() == Level().CurrentEntity())
	{
		CActor* pActor = smart_cast<CActor*>(H_Parent());
		if (pActor && !pActor->AnyMove() && this == pActor->inventory().ActiveItem())
		{
			if (hud_adj_mode == 0 &&
				g_player_hud->script_anim_part == u8(-1) &&
				GetState() == eIdle &&
				(Device.dwTimeGlobal - m_dw_curr_substate_time > 20000) &&
				!IsZoomed() &&
				g_player_hud->attached_item(1) == NULL)
			{
				if (AllowBore())
					SwitchState(eBore);

				ResetSubStateTime();
			}
		}
	}

	if (m_zoom_params.m_pNight_vision && !need_renderable())
	{
		if (!m_zoom_params.m_pNight_vision->IsActive())
		{
			CActor* pA = smart_cast<CActor *>(H_Parent());
			R_ASSERT(pA);
			if (pA->GetNightVisionStatus())
			{
				m_bRememberActorNVisnStatus = pA->GetNightVisionStatus();
				pA->SwitchNightVision(false, false, false);
			}
			m_zoom_params.m_pNight_vision->Start(m_zoom_params.m_sUseZoomPostprocess, pA, false);
		}
	}
	else if (m_bRememberActorNVisnStatus)
	{
		m_bRememberActorNVisnStatus = false;
		EnableActorNVisnAfterZoom();
	}

	if (m_zoom_params.m_pVision)
		m_zoom_params.m_pVision->Update();
}

void CWeapon::EnableActorNVisnAfterZoom()
{
	CActor* pA = smart_cast<CActor *>(H_Parent());
	if (IsGameTypeSingle() && !pA)
		pA = g_actor;

	if (pA)
	{
		pA->SwitchNightVision(true, false, false);
		pA->GetNightVision()->PlaySounds(CNightVisionEffector::eIdleSound);
	}
}

bool CWeapon::need_renderable()
{
	return !Device.m_SecondViewport.IsSVPFrame() && !(IsZoomed() && ZoomTexture() && !IsRotatingToZoom());
}

void CWeapon::renderable_Render()
{
	UpdateXForm();

	//íàðèñîâàòü ïîäñâåòêó
	RenderLight();

	//åñëè ìû â ðåæèìå ñíàéïåðêè, òî ñàì HUD ðèñîâàòü íå íàäî
	if (IsZoomed() && !IsRotatingToZoom() && ZoomTexture())
		RenderHud(FALSE);
	else
		RenderHud(TRUE);

	inherited::renderable_Render();
}

void CWeapon::signal_HideComplete()
{
	m_fLR_ShootingFactor = 0.f;
	m_fUD_ShootingFactor = 0.f;
	m_fBACKW_ShootingFactor = 0.f;
	if (H_Parent())
		setVisible(FALSE);
	SetPending(FALSE);
}

void CWeapon::SetDefaults()
{
	SetPending(FALSE);

	m_flags.set(FUsingCondition, TRUE);
	bMisfire = false;
	m_flagsAddOnState = 0;
	m_zoom_params.m_bIsZoomModeNow = false;
}

void CWeapon::UpdatePosition(const Fmatrix& trans)
{
	Position().set(trans.c);
	XFORM().mul(trans, m_strapped_mode ? m_StrapOffset : m_Offset);
	VERIFY(!fis_zero(DET(renderable.xform)));
}

bool CWeapon::Action(u16 cmd, u32 flags)
{
	if (inherited::Action(cmd, flags)) return true;

	CActor* pActor = smart_cast<CActor*>(H_Parent());

	switch (cmd)
	{
	case kWPN_FIRE:
		if (IsPending())
			return false;

		if (flags & CMD_START)
		{
			if (pActor && pActor->is_safemode())
			{
				pActor->set_safemode(false);
				return true;
			}

			FireStart();
		}
		else
			FireEnd();
		return true;
	case kWPN_NEXT:
		{
			//Disabled for script usage
			//return SwitchAmmoType(flags);
			return true;
		}

	case kWPN_ZOOM:
		if (IsZoomEnabled())
		{
			if (psActorFlags.test(AF_AIM_TOGGLE))
			{
				if (flags & CMD_START)
				{
					if (!IsZoomed())
					{
						if (!IsPending())
						{
							if (pActor && pActor->is_safemode())
								pActor->set_safemode(false);

							if (GetState() != eAimStart && HudAnimationExist("anm_idle_aim_start"))
								SwitchState(eAimStart);
							else if (GetState() != eIdle)
								SwitchState(eIdle);

							OnZoomIn();
						}
					}
					else
					{
						if (GetState() != eAimEnd && HudAnimationExist("anm_idle_aim_end"))
							SwitchState(eAimEnd);

						OnZoomOut();
					}
				}
			}
			else
			{
				if (flags & CMD_START)
				{
					if (!IsZoomed() && !IsPending())
					{
						if (pActor && pActor->is_safemode())
							pActor->set_safemode(false);

						if (GetState() != eAimStart && HudAnimationExist("anm_idle_aim_start"))
							SwitchState(eAimStart);
						else if (GetState() != eIdle)
							SwitchState(eIdle);

						OnZoomIn();
					}
				}
				else if (IsZoomed())
				{
					if (GetState() != eAimEnd && HudAnimationExist("anm_idle_aim_end"))
						SwitchState(eAimEnd);
					OnZoomOut();
				}
			}
			return true;
		}
		else
			return false;

	case kWPN_ZOOM_INC:
	case kWPN_ZOOM_DEC:
		if (IsZoomEnabled() && IsZoomed() && (flags & CMD_START))
		{
			if (cmd == kWPN_ZOOM_INC) ZoomInc();
			else ZoomDec();
			return true;
		}
		else
			return false;
	case kWPN_FUNC:
		{
			if (flags & CMD_START && !IsPending())
			{
				if (pActor && pActor->is_safemode())
					pActor->set_safemode(false);

				SwitchZoomType();
			}
			return true;
		}
	case kSAFEMODE:
		{
			if (pActor && flags & CMD_START && !IsPending() && m_bCanBeLowered)
			{
				bool new_state = !pActor->is_safemode();
				pActor->set_safemode(new_state);
				SetPending(TRUE);

				if (!new_state && m_safemode_anm[1].name)
					PlayBlendAnm(m_safemode_anm[1].name, m_safemode_anm[1].speed, m_safemode_anm[1].power, false);
				else if (m_safemode_anm[0].name)
					PlayBlendAnm(m_safemode_anm[0].name, m_safemode_anm[0].speed, m_safemode_anm[0].power, false);
			}
			return true;
		}
	break;
	}
	return false;
}

bool CWeapon::SwitchAmmoType(u32 flags)
{
	if (IsPending() || OnClient())
		return false;

	if (!(flags & CMD_START))
		return false;

	u8 l_newType = m_ammoType;
	bool b1, b2;
	do
	{
		l_newType = u8((u32(l_newType + 1)) % m_ammoTypes.size());
		b1 = (l_newType != m_ammoType);
		b2 = unlimited_ammo() ? false : (!m_pInventory->GetAny(m_ammoTypes[l_newType].c_str()));
	}
	while (b1 && b2);

	if (l_newType != m_ammoType)
	{
		m_set_next_ammoType_on_reload = l_newType;
		if (OnServer())
		{
			Reload();
		}
	}
	return true;
}

void CWeapon::SpawnAmmo(u32 boxCurr, LPCSTR ammoSect, u32 ParentID)
{
	if (!m_ammoTypes.size()) return;
	if (OnClient()) return;
	m_bAmmoWasSpawned = true;

	int l_type = 0;
	l_type %= m_ammoTypes.size();

	if (!ammoSect) ammoSect = m_ammoTypes[l_type].c_str();

	++l_type;
	l_type %= m_ammoTypes.size();

	CSE_Abstract* D = F_entity_Create(ammoSect);

	{
		CSE_ALifeItemAmmo* l_pA = smart_cast<CSE_ALifeItemAmmo*>(D);
		R_ASSERT(l_pA);
		l_pA->m_boxSize = (u16)pSettings->r_s32(ammoSect, "box_size");
		D->s_name = ammoSect;
		D->set_name_replace("");
		//.		D->s_gameid					= u8(GameID());
		D->s_RP = 0xff;
		D->ID = 0xffff;
		if (ParentID == 0xffffffff)
			D->ID_Parent = (u16)H_Parent()->ID();
		else
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

int CWeapon::GetSuitableAmmoTotal(bool use_item_to_spawn) const
{
	if (const_cast<CWeapon*>(this)->unlimited_ammo())
		return 999;

	int ae_count = iAmmoElapsed;
	if (!m_pInventory)
	{
		return ae_count;
	}

	//÷òîá íå äåëàòü ëèøíèõ ïåðåñ÷åòîâ
	if (m_pInventory->ModifyFrame() <= m_BriefInfo_CalcFrame)
	{
		return ae_count + m_iAmmoCurrentTotal;
	}
	m_BriefInfo_CalcFrame = Device.dwFrame;

	m_iAmmoCurrentTotal = 0;
	for (u8 i = 0; i < u8(m_ammoTypes.size()); ++i)
	{
		m_iAmmoCurrentTotal += GetAmmoCount_forType(m_ammoTypes[i]);

		if (!use_item_to_spawn)
		{
			continue;
		}
		if (!inventory_owner().item_to_spawn())
		{
			continue;
		}
		m_iAmmoCurrentTotal += inventory_owner().ammo_in_box_to_spawn();
	}
	return ae_count + m_iAmmoCurrentTotal;
}

int CWeapon::GetAmmoCount(u8 ammo_type) const
{
	VERIFY(m_pInventory);
	R_ASSERT(ammo_type < m_ammoTypes.size());

	return GetAmmoCount_forType(m_ammoTypes[ammo_type]);
}

int CWeapon::GetAmmoCount_forType(shared_str const& ammo_type) const
{
	int res = 0;

	TIItemContainer::iterator itb = m_pInventory->m_belt.begin();
	TIItemContainer::iterator ite = m_pInventory->m_belt.end();
	for (; itb != ite; ++itb)
	{
		CWeaponAmmo* pAmmo = smart_cast<CWeaponAmmo*>(*itb);
		if (pAmmo && (pAmmo->cNameSect() == ammo_type))
		{
			res += pAmmo->m_boxCurr;
		}
	}

	itb = m_pInventory->m_ruck.begin();
	ite = m_pInventory->m_ruck.end();
	for (; itb != ite; ++itb)
	{
		CWeaponAmmo* pAmmo = smart_cast<CWeaponAmmo*>(*itb);
		if (pAmmo && (pAmmo->cNameSect() == ammo_type))
		{
			res += pAmmo->m_boxCurr;
		}
	}
	return res;
}

float CWeapon::GetConditionMisfireProbability() const
{
	// modified by Peacemaker [17.10.08]
	//	if(GetCondition() > 0.95f)
	//		return 0.0f;
	if (GetCondition() > misfireStartCondition)
		return 0.0f;
	if (GetCondition() < misfireEndCondition)
		return misfireEndProbability;
	//	float mis = misfireProbability+powf(1.f-GetCondition(), 3.f)*misfireConditionK;
	float mis = misfireStartProbability + (
		(misfireStartCondition - GetCondition()) * // condition goes from 1.f to 0.f
		(misfireEndProbability - misfireStartProbability) / // probability goes from 0.f to 1.f
		((misfireStartCondition == misfireEndCondition)
			 ? // !!!say "No" to devision by zero
			 misfireStartCondition
			 : (misfireStartCondition - misfireEndCondition))
	);
	clamp(mis, 0.0f, 0.99f);
	return mis;
}

BOOL CWeapon::CheckForMisfire()
{
	if (OnClient()) return FALSE;

	float rnd = ::Random.randF(0.f, 1.f);
	float mp = GetConditionMisfireProbability();
	if (rnd < mp)
	{
		FireEnd();

		bMisfire = true;
		SwitchState(eMisfire);

		return TRUE;
	}
	else
	{
		return FALSE;
	}
}

bool CWeapon::IsMisfire() const
{
	return bMisfire;
}

void CWeapon::SetMisfireScript(bool b)
{
	bMisfire = b;
}

void CWeapon::Reload()
{
	OnZoomOut();
}

void CWeapon::HUD_VisualBulletUpdate(bool force, int force_idx)
{
	if (!bHasBulletsToHide)
		return;

	if (!GetHUDmode())	return;

	bool hide = true;

	//Msg("Print %d bullets", last_hide_bullet);

	if (last_hide_bullet == bullet_cnt || force) hide = false;

	for (u8 b = 0; b < bullet_cnt; b++)
	{
		u16 bone_id = HudItemData()->m_model->LL_BoneID(bullets_bones[b]);

		if (bone_id != BI_NONE)
			HudItemData()->set_bone_visible(bullets_bones[b], !hide);

		if (b == last_hide_bullet) hide = false;
	}
}

bool CWeapon::IsGrenadeLauncherAttached() const
{
	return (ALife::eAddonAttachable == m_eGrenadeLauncherStatus &&
			0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher)) ||
		ALife::eAddonPermanent == m_eGrenadeLauncherStatus;
}

bool CWeapon::IsScopeAttached() const
{
	return (ALife::eAddonAttachable == m_eScopeStatus &&
		0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonScope)) ||
		ALife::eAddonPermanent == m_eScopeStatus;
}

bool CWeapon::IsSilencerAttached() const
{
	return (ALife::eAddonAttachable == m_eSilencerStatus &&
		0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonSilencer)) ||
		ALife::eAddonPermanent == m_eSilencerStatus;
}

bool CWeapon::GrenadeLauncherAttachable()
{
	return (ALife::eAddonAttachable == m_eGrenadeLauncherStatus);
}

bool CWeapon::ScopeAttachable()
{
	return (ALife::eAddonAttachable == m_eScopeStatus);
}

bool CWeapon::SilencerAttachable()
{
	return (ALife::eAddonAttachable == m_eSilencerStatus);
}

#define WPN_SCOPE "wpn_scope"
#define WPN_SILENCER "wpn_silencer"
#define WPN_GRENADE_LAUNCHER "wpn_launcher"

void CWeapon::UpdateHUDAddonsVisibility()
{
	//actor only
	if (!GetHUDmode()) return;

	static shared_str wpn_scope = WPN_SCOPE;
	static shared_str wpn_silencer = WPN_SILENCER;
	static shared_str wpn_grenade_launcher = WPN_GRENADE_LAUNCHER;

	//.	return;

	if (ScopeAttachable())
	{
		HudItemData()->set_bone_visible(wpn_scope, IsScopeAttached());
	}

	if (m_eScopeStatus == ALife::eAddonDisabled)
	{
		HudItemData()->set_bone_visible(wpn_scope, FALSE, TRUE);
	}
	else if (m_eScopeStatus == ALife::eAddonPermanent)
		HudItemData()->set_bone_visible(wpn_scope, TRUE, TRUE);

	if (SilencerAttachable())
	{
		HudItemData()->set_bone_visible(wpn_silencer, IsSilencerAttached());
	}
	if (m_eSilencerStatus == ALife::eAddonDisabled)
	{
		HudItemData()->set_bone_visible(wpn_silencer, FALSE, TRUE);
	}
	else if (m_eSilencerStatus == ALife::eAddonPermanent)
		HudItemData()->set_bone_visible(wpn_silencer, TRUE, TRUE);

	if (GrenadeLauncherAttachable())
	{
		HudItemData()->set_bone_visible(wpn_grenade_launcher, IsGrenadeLauncherAttached());
	}
	if (m_eGrenadeLauncherStatus == ALife::eAddonDisabled)
	{
		HudItemData()->set_bone_visible(wpn_grenade_launcher, FALSE, TRUE);
	}
	else if (m_eGrenadeLauncherStatus == ALife::eAddonPermanent)
		HudItemData()->set_bone_visible(wpn_grenade_launcher, TRUE, TRUE);
}

void CWeapon::UpdateAddonsVisibility()
{
	static shared_str wpn_scope = WPN_SCOPE;
	static shared_str wpn_silencer = WPN_SILENCER;
	static shared_str wpn_grenade_launcher = WPN_GRENADE_LAUNCHER;

	IKinematics* pWeaponVisual = smart_cast<IKinematics*>(Visual());
	R_ASSERT(pWeaponVisual);

	u16 bone_id;
	UpdateHUDAddonsVisibility();

	pWeaponVisual->CalculateBones_Invalidate();

	bone_id = pWeaponVisual->LL_BoneID(wpn_scope);
	if (ScopeAttachable())
	{
		if (IsScopeAttached())
		{
			if (!pWeaponVisual->LL_GetBoneVisible(bone_id))
				pWeaponVisual->LL_SetBoneVisible(bone_id, TRUE, TRUE);
		}
		else
		{
			if (pWeaponVisual->LL_GetBoneVisible(bone_id))
				pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
		}
	}
	if (m_eScopeStatus == ALife::eAddonDisabled && bone_id != BI_NONE &&
		pWeaponVisual->LL_GetBoneVisible(bone_id))
	{
		pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
		//		Log("scope", pWeaponVisual->LL_GetBoneVisible		(bone_id));
	}
	bone_id = pWeaponVisual->LL_BoneID(wpn_silencer);
	if (SilencerAttachable())
	{
		if (IsSilencerAttached())
		{
			if (!pWeaponVisual->LL_GetBoneVisible(bone_id))
				pWeaponVisual->LL_SetBoneVisible(bone_id, TRUE, TRUE);
		}
		else
		{
			if (pWeaponVisual->LL_GetBoneVisible(bone_id))
				pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
		}
	}
	if (m_eSilencerStatus == ALife::eAddonDisabled && bone_id != BI_NONE &&
		pWeaponVisual->LL_GetBoneVisible(bone_id))
	{
		pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
		//		Log("silencer", pWeaponVisual->LL_GetBoneVisible	(bone_id));
	}

	bone_id = pWeaponVisual->LL_BoneID(wpn_grenade_launcher);
	if (GrenadeLauncherAttachable())
	{
		if (IsGrenadeLauncherAttached())
		{
			if (!pWeaponVisual->LL_GetBoneVisible(bone_id))
				pWeaponVisual->LL_SetBoneVisible(bone_id, TRUE, TRUE);
		}
		else
		{
			if (pWeaponVisual->LL_GetBoneVisible(bone_id))
				pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
		}
	}
	if (m_eGrenadeLauncherStatus == ALife::eAddonDisabled && bone_id != BI_NONE &&
		pWeaponVisual->LL_GetBoneVisible(bone_id))
	{
		pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
		//		Log("gl", pWeaponVisual->LL_GetBoneVisible			(bone_id));
	}

	pWeaponVisual->CalculateBones_Invalidate();
	pWeaponVisual->CalculateBones(TRUE);
}

void CWeapon::InitAddons()
{
	UpdateUIScope();
}

bool CWeapon::ZoomHideCrosshair()
{
	if (g_player_hud->m_adjust_mode)
		return false;

	return m_zoom_params.m_bHideCrosshairInZoom || ZoomTexture();
}

float CWeapon::CurrentZoomFactor()
{
	return m_zoom_params.m_fScopeZoomFactor;
};

void CWeapon::OnZoomIn()
{
    //////////
    scope_radius = SDS_Radius(m_zoomtype == 1);

	if ((scope_radius > 0.0) && zoomFlags.test(SDS_SPEED)) {
		sens_multiple = scope_scrollpower;
	}
	else {
		sens_multiple = 1.0f;
	}
    //////////
    
	m_zoom_params.m_bIsZoomModeNow = true;
	if (m_zoom_params.m_bUseDynamicZoom)
		SetZoomFactor(scope_radius > 0.0 ? m_fRTZoomFactor / scope_scrollpower : m_fRTZoomFactor);
	else
		SetZoomFactor(CurrentZoomFactor());

	if (m_zoom_params.m_bZoomDofEnabled && !IsScopeAttached())
		GamePersistent().SetEffectorDOF(m_zoom_params.m_ZoomDof);

	if (GetHUDmode())
		GamePersistent().SetPickableEffectorDOF(true);

	if (m_zoom_params.m_sUseBinocularVision.size() && IsScopeAttached() && NULL == m_zoom_params.m_pVision)
		m_zoom_params.m_pVision = xr_new<CBinocularsVision>(m_zoom_params.m_sUseBinocularVision);

	if (m_zoom_params.m_sUseZoomPostprocess.size() && IsScopeAttached())
	{
		CActor* pA = smart_cast<CActor *>(H_Parent());
		if (pA)
		{
			if (NULL == m_zoom_params.m_pNight_vision)
			{
				m_zoom_params.m_pNight_vision = xr_new<CNightVisionEffector>(
					m_zoom_params.m_sUseZoomPostprocess);
			}
		}
	}

	g_player_hud->updateMovementLayerState();
}

void CWeapon::OnZoomOut()
{
	m_zoom_params.m_bIsZoomModeNow = false;
    if (m_zoom_params.m_bUseDynamicZoom)
    {
        m_fRTZoomFactor = scope_radius > 0.0 ? GetZoomFactor() * scope_scrollpower : GetZoomFactor(); //store current
    }
    
	m_zoom_params.m_fCurrentZoomFactor = g_fov;

	GamePersistent().RestoreEffectorDOF();

	if (GetHUDmode())
		GamePersistent().SetPickableEffectorDOF(false);

	ResetSubStateTime();

	xr_delete(m_zoom_params.m_pVision);
	if (m_zoom_params.m_pNight_vision)
	{
		m_zoom_params.m_pNight_vision->Stop(100000.0f, false);
		xr_delete(m_zoom_params.m_pNight_vision);
	}

	g_player_hud->updateMovementLayerState();
    
    scope_radius = 0.0;
    scope_2dtexactive = 0;
    sens_multiple = 1.0f;

}

CUIWindow* CWeapon::ZoomTexture()
{
	if (UseScopeTexture())
		return m_UIScope;
	else
	{
		scope_2dtexactive = 0; //crookr
		return NULL;
	}
}

void CWeapon::SwitchState(u32 S)
{
	if (OnClient()) return;

#ifndef MASTER_GOLD
    if ( bDebug )
    {
        Msg("---Server is going to send GE_WPN_STATE_CHANGE to [%d], weapon_section[%s], parent[%s]",
            S, cNameSect().c_str(), H_Parent() ? H_Parent()->cName().c_str() : "NULL Parent");
    }
#endif // #ifndef MASTER_GOLD

	SetNextState(S);
	if (CHudItem::object().Local() && !CHudItem::object().getDestroy() && m_pInventory && OnServer())
	{
		// !!! Just single entry for given state !!!
		NET_Packet P;
		CHudItem::object().u_EventGen(P, GE_WPN_STATE_CHANGE, CHudItem::object().ID());
		P.w_u8(u8(S));
		P.w_u8(u8(m_sub_state));
		P.w_u8(m_ammoType);
		P.w_u8(u8(iAmmoElapsed & 0xff));
		P.w_u8(m_set_next_ammoType_on_reload);
		CHudItem::object().u_EventSend(P, net_flags(TRUE, TRUE, FALSE, TRUE));
	}
}

void CWeapon::OnMagazineEmpty()
{
	VERIFY((u32) iAmmoElapsed == m_magazine.size());
}

void CWeapon::reinit()
{
	CShootingObject::reinit();
	CHudItemObject::reinit();
}

void CWeapon::reload(LPCSTR section)
{
	CShootingObject::reload(section);
	CHudItemObject::reload(section);

	m_can_be_strapped = true;
	m_strapped_mode = false;

	if (pSettings->line_exist(section, "strap_bone0"))
		m_strap_bone0 = pSettings->r_string(section, "strap_bone0");
	else
		m_can_be_strapped = false;

	if (pSettings->line_exist(section, "strap_bone1"))
		m_strap_bone1 = pSettings->r_string(section, "strap_bone1");
	else
		m_can_be_strapped = false;

	if (m_eScopeStatus == ALife::eAddonAttachable && m_scopes.size())
	{
		m_addon_holder_range_modifier = READ_IF_EXISTS(pSettings, r_float, GetScopeName(), "holder_range_modifier",
		                                               m_holder_range_modifier);
		m_addon_holder_fov_modifier = READ_IF_EXISTS(pSettings, r_float, GetScopeName(), "holder_fov_modifier",
		                                             m_holder_fov_modifier);
	}
	else
	{
		m_addon_holder_range_modifier = m_holder_range_modifier;
		m_addon_holder_fov_modifier = m_holder_fov_modifier;
	}

	{
		Fvector pos, ypr;
		pos = pSettings->r_fvector3(section, "position");
		ypr = pSettings->r_fvector3(section, "orientation");
		ypr.mul(PI / 180.f);

		m_Offset.setHPB(ypr.x, ypr.y, ypr.z);
		m_Offset.translate_over(pos);
	}

	m_StrapOffset = m_Offset;
	if (pSettings->line_exist(section, "strap_position") && pSettings->line_exist(section, "strap_orientation"))
	{
		Fvector pos, ypr;
		pos = pSettings->r_fvector3(section, "strap_position");
		ypr = pSettings->r_fvector3(section, "strap_orientation");
		ypr.mul(PI / 180.f);

		m_StrapOffset.setHPB(ypr.x, ypr.y, ypr.z);
		m_StrapOffset.translate_over(pos);
	}
	else
		m_can_be_strapped = false;

	m_ef_main_weapon_type = READ_IF_EXISTS(pSettings, r_u32, section, "ef_main_weapon_type", u32(-1));
	m_ef_weapon_type = READ_IF_EXISTS(pSettings, r_u32, section, "ef_weapon_type", u32(-1));
}

// demonized: World model on stalkers adjustments
void CWeapon::set_mOffset(Fvector position, Fvector orientation) {
	orientation.mul(PI / 180.f);

	m_Offset.setHPB(orientation.x, orientation.y, orientation.z);
	m_Offset.translate_over(position);
}

void CWeapon::set_mStrapOffset(Fvector position, Fvector orientation) {
	orientation.mul(PI / 180.f);

	m_StrapOffset.setHPB(orientation.x, orientation.y, orientation.z);
	m_StrapOffset.translate_over(position);
}

void CWeapon::create_physic_shell()
{
	CPhysicsShellHolder::create_physic_shell();
}

bool CWeapon::ActivationSpeedOverriden(Fvector& dest, bool clear_override)
{
	if (m_activation_speed_is_overriden)
	{
		if (clear_override)
		{
			m_activation_speed_is_overriden = false;
		}

		dest = m_overriden_activation_speed;
		return true;
	}

	return false;
}

void CWeapon::SetActivationSpeedOverride(Fvector const& speed)
{
	m_overriden_activation_speed = speed;
	m_activation_speed_is_overriden = true;
}

void CWeapon::activate_physic_shell()
{
	UpdateXForm();
	CPhysicsShellHolder::activate_physic_shell();
}

void CWeapon::setup_physic_shell()
{
	CPhysicsShellHolder::setup_physic_shell();
}

int g_iWeaponRemove = 1;

bool CWeapon::NeedToDestroyObject() const
{
	if (GameID() == eGameIDSingle) return false;
	if (Remote()) return false;
	if (H_Parent()) return false;
	if (g_iWeaponRemove == -1) return false;
	if (g_iWeaponRemove == 0) return true;
	if (TimePassedAfterIndependant() > m_dwWeaponRemoveTime)
		return true;

	return false;
}

ALife::_TIME_ID CWeapon::TimePassedAfterIndependant() const
{
	if (!H_Parent() && m_dwWeaponIndependencyTime != 0)
		return Level().timeServer() - m_dwWeaponIndependencyTime;
	else
		return 0;
}

bool CWeapon::can_kill() const
{
	if (GetSuitableAmmoTotal(true) || m_ammoTypes.empty())
		return (true);

	return (false);
}

CInventoryItem* CWeapon::can_kill(CInventory* inventory) const
{
	if (const_cast<CWeapon*>(this)->unlimited_ammo() || GetAmmoElapsed() || m_ammoTypes.empty())
		return (const_cast<CWeapon*>(this));

	TIItemContainer::iterator I = inventory->m_all.begin();
	TIItemContainer::iterator E = inventory->m_all.end();
	for (; I != E; ++I)
	{
		CInventoryItem* inventory_item = smart_cast<CInventoryItem*>(*I);
		if (!inventory_item)
			continue;

		xr_vector<shared_str>::const_iterator i = std::find(m_ammoTypes.begin(), m_ammoTypes.end(),
		                                                    inventory_item->object().cNameSect());
		if (i != m_ammoTypes.end())
			return (inventory_item);
	}

	return (0);
}

const CInventoryItem* CWeapon::can_kill(const xr_vector<const CGameObject*>& items) const
{
	if (const_cast<CWeapon*>(this)->unlimited_ammo() || m_ammoTypes.empty())
		return (this);

	xr_vector<const CGameObject*>::const_iterator I = items.begin();
	xr_vector<const CGameObject*>::const_iterator E = items.end();
	for (; I != E; ++I)
	{
		const CInventoryItem* inventory_item = smart_cast<const CInventoryItem*>(*I);
		if (!inventory_item)
			continue;

		xr_vector<shared_str>::const_iterator i = std::find(m_ammoTypes.begin(), m_ammoTypes.end(),
		                                                    inventory_item->object().cNameSect());
		if (i != m_ammoTypes.end())
			return (inventory_item);
	}

	return (0);
}

bool CWeapon::ready_to_kill() const
{
	//Alundaio
	const CInventoryOwner* io = smart_cast<const CInventoryOwner*>(H_Parent());
	if (!io)
		return false;

	if (io->inventory().ActiveItem() == NULL || io->inventory().ActiveItem()->object().ID() != ID())
		return false;
	//-Alundaio
	return (
		!IsMisfire() &&
		((GetState() == eIdle) || (GetState() == eFire) || (GetState() == eFire2)) &&
		GetAmmoElapsed()
	);
}

ICF static BOOL pick_trace_callback(collide::rq_result& result, LPVOID params)
{
	PickParam* pp = (PickParam*)params;
	++pp->pass;

	if (result.O)
	{
		pp->RQ = result;
		return FALSE;
	}
	else
	{
		CDB::TRI* T = Level().ObjectSpace.GetStaticTris() + result.element;

		SGameMtl* mtl = GMLib.GetMaterialByIdx(T->material);
		pp->power *= mtl->fVisTransparencyFactor;
		if (pp->power > 0.34f)
		{
			return TRUE;
		}
	}
	pp->RQ = result;
	return FALSE;
}

/*void CWeapon::net_Relcase(CObject* object)
{
	if (!ParentIsActor())
		return;

	if (PP.RQ.O == object)
		PP.RQ.O = NULL;

	RQS.r_clear();
}*/

// Обновление координат текущего худа
void CWeapon::UpdateHudAdditional(Fmatrix& trans)
{
	CActor* pActor = smart_cast<CActor*>(H_Parent());
	if (!pActor)
		return;

	if (IsZoomed() && (pActor->is_safemode()))
		OnZoomOut();

	attachable_hud_item* hi = HudItemData();
	R_ASSERT(hi);

	/*PP.RQ.O = 0;
	PP.RQ.range = 3.f;
	PP.RQ.element = -1;
	PP.power = 1.0f;
	PP.pass = 0;
	RQS.r_clear();

	const Fmatrix& fire_mat = HudItemData()->m_model->LL_GetTransform(HudItemData()->m_measures.m_fire_bone);
	Fvector pos; // = get_LastFP();
	Fvector offs = g_player_hud->m_adjust_mode ? g_player_hud->m_adjust_firepoint_shell[0][0] : HudItemData()->m_measures.m_fire_point_offset;
	offs.z -= g_freelook_z_offset;
	fire_mat.transform_tiny(pos, offs);
	HudItemData()->m_item_transform.transform_tiny(pos);
	Fvector offs;
	fire_mat.transform_tiny(offs, { 0, 0, -pos.z -.5f }); //otherwise you can shoot through thin walls
	pos.add(offs);
	
	// add RQ for weapon barrel collision
	collide::ray_defs RD(pos, get_ParticlesXFORM().k, 3.f, CDB::OPT_CULL, collide::rqtBoth);
	if (Level().ObjectSpace.RayQuery(RQS, RD, pick_trace_callback, &PP, NULL, Level().CurrentEntity()))
		clamp(PP.RQ.range, 0.f, 3.f);

	//Msg("RQ range: %f", PP.RQ.range);
	*/

	u8 idx = GetCurrentHudOffsetIdx();

	//============= Поворот ствола во время аима =============//
	{
		Fvector curr_offs, curr_rot;

		if (g_player_hud->m_adjust_mode)
		{
			if (idx == 0)
			{
				curr_offs = g_player_hud->m_adjust_offset[0][5]; //pos,normal2
				curr_rot = g_player_hud->m_adjust_offset[1][5]; //rot,normal2
			}
			else
			{
				curr_offs = g_player_hud->m_adjust_offset[0][idx]; //pos,aim
				curr_rot = g_player_hud->m_adjust_offset[1][idx]; //rot,aim
			}
		}
		else
		{
			if (idx == 0)
			{
				curr_offs = hi->m_measures.m_hands_offset[0][5]; //pos,normal2
				curr_rot = hi->m_measures.m_hands_offset[1][5]; //pos,normal2
			}
			else {
				curr_offs = hi->m_measures.m_hands_offset[0][idx]; //pos,aim
				curr_rot = hi->m_measures.m_hands_offset[1][idx]; //rot,aim
			}
		}

		float factor;
		
		if (idx == 4 || last_idx == 4)
			factor = Device.fTimeDelta / m_fSafeModeRotateTime;
		else
			factor = Device.fTimeDelta /
			(m_zoom_params.m_fZoomRotateTime * cur_silencer_koef.zoom_rotate_time * cur_scope_koef.zoom_rotate_time
					* cur_launcher_koef.zoom_rotate_time);

		if (curr_offs.similar(m_hud_offset[0], EPS))
		{
			m_hud_offset[0].set(curr_offs);
		}
		else
		{
			Fvector diff;
			diff.set(curr_offs);
			diff.sub(m_hud_offset[0]);
			diff.mul(factor * 2.5f);
			m_hud_offset[0].add(diff);
		}

		if (curr_rot.similar(m_hud_offset[1], EPS))
		{
			m_hud_offset[1].set(curr_rot);
		}
		else
		{
			Fvector diff;
			diff.set(curr_rot);
			diff.sub(m_hud_offset[1]);
			diff.mul(factor * 2.5f);
			m_hud_offset[1].add(diff);
		}

		// Remove pending state before weapon has fully moved to the new position to remove some delay
		if (curr_offs.similar(m_hud_offset[0], .02f) && curr_rot.similar(m_hud_offset[1], .02f))
		{
			if ((idx == 4 || last_idx == 4) && IsPending()) SetPending(FALSE);
			last_idx = idx;
		}

		Fmatrix hud_rotation;
		hud_rotation.identity();
		hud_rotation.rotateX(m_hud_offset[1].x);

		Fmatrix hud_rotation_y;
		hud_rotation_y.identity();
		hud_rotation_y.rotateY(m_hud_offset[1].y);
		hud_rotation.mulA_43(hud_rotation_y);

		hud_rotation_y.identity();
		hud_rotation_y.rotateZ(m_hud_offset[1].z);
		hud_rotation.mulA_43(hud_rotation_y);

		hud_rotation.translate_over(m_hud_offset[0]);
		trans.mulB_43(hud_rotation);

		if (pActor->IsZoomAimingMode())
			m_zoom_params.m_fZoomRotationFactor += factor;
		else
			m_zoom_params.m_fZoomRotationFactor -= factor;

		clamp(m_zoom_params.m_fZoomRotationFactor, 0.f, 1.f);
	}

	//============= Подготавливаем общие переменные =============//
	clamp(idx, u8(0), u8(1));
	bool bForAim = (idx == 1);

	static float fAvgTimeDelta = Device.fTimeDelta;
	fAvgTimeDelta = _inertion(fAvgTimeDelta, Device.fTimeDelta, 0.8f);

	//============= Сдвиг оружия при стрельбе =============//
	if (hi->m_measures.m_shooting_params.bShootShake)
	{
		// Параметры сдвига
		float fShootingReturnSpeedMod = _lerp(
			hi->m_measures.m_shooting_params.m_ret_speed,
			hi->m_measures.m_shooting_params.m_ret_speed_aim,
			m_zoom_params.m_fZoomRotationFactor);

		float fShootingBackwOffset = _lerp(
			hi->m_measures.m_shooting_params.m_shot_offset_BACKW.x,
			hi->m_measures.m_shooting_params.m_shot_offset_BACKW.y,
			m_zoom_params.m_fZoomRotationFactor);

		Fvector4 vShOffsets; // x = L, y = R, z = U, w = D
		vShOffsets.x = _lerp(
			hi->m_measures.m_shooting_params.m_shot_max_offset_LRUD.x,
			hi->m_measures.m_shooting_params.m_shot_max_offset_LRUD_aim.x,
			m_zoom_params.m_fZoomRotationFactor);
		vShOffsets.y = _lerp(
			hi->m_measures.m_shooting_params.m_shot_max_offset_LRUD.y,
			hi->m_measures.m_shooting_params.m_shot_max_offset_LRUD_aim.y,
			m_zoom_params.m_fZoomRotationFactor);
		vShOffsets.z = _lerp(
			hi->m_measures.m_shooting_params.m_shot_max_offset_LRUD.z,
			hi->m_measures.m_shooting_params.m_shot_max_offset_LRUD_aim.z,
			m_zoom_params.m_fZoomRotationFactor);
		vShOffsets.w = _lerp(
			hi->m_measures.m_shooting_params.m_shot_max_offset_LRUD.w,
			hi->m_measures.m_shooting_params.m_shot_max_offset_LRUD_aim.w,
			m_zoom_params.m_fZoomRotationFactor);

		// Плавное затухание сдвига от стрельбы (основное, но без линейной никогда не опустит до полного 0.0f)
		m_fLR_ShootingFactor *= clampr(1.f - fAvgTimeDelta * fShootingReturnSpeedMod, 0.0f, 1.0f);
		m_fUD_ShootingFactor *= clampr(1.f - fAvgTimeDelta * fShootingReturnSpeedMod, 0.0f, 1.0f);
		m_fBACKW_ShootingFactor *= clampr(1.f - fAvgTimeDelta * fShootingReturnSpeedMod, 0.0f, 1.0f);

		// Минимальное линейное затухание сдвига от стрельбы при покое (горизонталь)
		{
			float fRetSpeedMod = fShootingReturnSpeedMod * 0.125f;
			if (m_fLR_ShootingFactor < 0.0f)
			{
				m_fLR_ShootingFactor += fAvgTimeDelta * fRetSpeedMod;
				clamp(m_fLR_ShootingFactor, -1.0f, 0.0f);
			}
			else
			{
				m_fLR_ShootingFactor -= fAvgTimeDelta * fRetSpeedMod;
				clamp(m_fLR_ShootingFactor, 0.0f, 1.0f);
			}
		}

		// Минимальное линейное затухание сдвига от стрельбы при покое (вертикаль)
		{
			float fRetSpeedMod = fShootingReturnSpeedMod * 0.125f;
			if (m_fUD_ShootingFactor < 0.0f)
			{
				m_fUD_ShootingFactor += fAvgTimeDelta * fRetSpeedMod;
				clamp(m_fUD_ShootingFactor, -1.0f, 0.0f);
			}
			else
			{
				m_fUD_ShootingFactor -= fAvgTimeDelta * fRetSpeedMod;
				clamp(m_fUD_ShootingFactor, 0.0f, 1.0f);
			}
		}

		// Минимальное линейное затухание сдвига от стрельбы при покое (вперёд\назад)
		{
			float fRetSpeedMod = fShootingReturnSpeedMod * 0.125f;
			m_fBACKW_ShootingFactor -= fAvgTimeDelta * fRetSpeedMod;
			clamp(m_fBACKW_ShootingFactor, 0.0f, 1.0f);
		}

		// Применяем сдвиг от стрельбы к худу
		{
			float fLR_lim = (m_fLR_ShootingFactor < 0.0f ? vShOffsets.x : vShOffsets.y);
			float fUD_lim = (m_fUD_ShootingFactor < 0.0f ? vShOffsets.z : vShOffsets.w);

			Fvector curr_offs;
			curr_offs = {
				fLR_lim * m_fLR_ShootingFactor, fUD_lim * -1.f * m_fUD_ShootingFactor,
				-1.f * fShootingBackwOffset * m_fBACKW_ShootingFactor
			};

			m_shoot_shake_mat.translate_over(curr_offs);
			trans.mulB_43(m_shoot_shake_mat);
		}
	}

	//======== Проверяем доступность инерции и стрейфа ========//
	if (!g_player_hud->inertion_allowed())
		return;

	float fYMag = pActor->fFPCamYawMagnitude;
	float fPMag = pActor->fFPCamPitchMagnitude;

	//============= Боковой стрейф с оружием =============//
	// Рассчитываем фактор боковой ходьбы
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
		m_zoom_params.m_fZoomRotationFactor);

	// Высчитываем мксимальный наклон от поворота камеры
	float fCamLimitBlend = _lerp(
		hi->m_measures.m_strafe_offset[3][0].x,
		hi->m_measures.m_strafe_offset[3][1].x,
		m_zoom_params.m_fZoomRotationFactor);

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
			m_fLR_CameraFactor += fStepPerUpd * (bForAim ? 1.0f : fCamReturnSpeedMod);
			clamp(m_fLR_CameraFactor, -fCamLimitBlend, 0.0f);
		}
		else
		{
			m_fLR_CameraFactor -= fStepPerUpd * (bForAim ? 1.0f : fCamReturnSpeedMod);
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
			curr_offs.mul(1.f - m_zoom_params.m_fZoomRotationFactor);
			curr_rot.mul(1.f - m_zoom_params.m_fZoomRotationFactor);
		}
		else
		{
			// Во время аима
			curr_offs.mul(m_zoom_params.m_fZoomRotationFactor);
			curr_rot.mul(m_zoom_params.m_fZoomRotationFactor);
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
		m_zoom_params.m_fZoomRotationFactor);

	float fInertiaReturnSpeedMod = _lerp(
		hi->m_measures.m_inertion_params.m_tendto_ret_speed,
		hi->m_measures.m_inertion_params.m_tendto_ret_speed_aim,
		m_zoom_params.m_fZoomRotationFactor);

	float fInertiaMinAngle = _lerp(
		hi->m_measures.m_inertion_params.m_min_angle,
		hi->m_measures.m_inertion_params.m_min_angle_aim,
		m_zoom_params.m_fZoomRotationFactor);

	Fvector4 vIOffsets; // x = L, y = R, z = U, w = D
	vIOffsets.x = _lerp(
		hi->m_measures.m_inertion_params.m_offset_LRUD.x,
		hi->m_measures.m_inertion_params.m_offset_LRUD_aim.x,
		m_zoom_params.m_fZoomRotationFactor);
	vIOffsets.y = _lerp(
		hi->m_measures.m_inertion_params.m_offset_LRUD.y,
		hi->m_measures.m_inertion_params.m_offset_LRUD_aim.y,
		m_zoom_params.m_fZoomRotationFactor);
	vIOffsets.z = _lerp(
		hi->m_measures.m_inertion_params.m_offset_LRUD.z,
		hi->m_measures.m_inertion_params.m_offset_LRUD_aim.z,
		m_zoom_params.m_fZoomRotationFactor);
	vIOffsets.w = _lerp(
		hi->m_measures.m_inertion_params.m_offset_LRUD.w,
		hi->m_measures.m_inertion_params.m_offset_LRUD_aim.w,
		m_zoom_params.m_fZoomRotationFactor);

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

	Fvector curr_offs;
	curr_offs = {fLR_lim * -1.f * m_fLR_InertiaFactor, fUD_lim * m_fUD_InertiaFactor, 0.0f};

	Fmatrix hud_rotation;
	hud_rotation.identity();
	hud_rotation.translate_over(curr_offs);
	trans.mulB_43(hud_rotation);
}

// Добавить эффект сдвига оружия от выстрела
void CWeapon::AddHUDShootingEffect()
{
	if (IsHidden() || ParentIsActor() == false)
		return;

	// Отдача назад
	m_fBACKW_ShootingFactor = 1.0f;

	// Отдача в бока
	float fPowerMin = 0.0f;
	attachable_hud_item* hi = HudItemData();
	if (hi != nullptr)
	{
		if (!hi->m_measures.m_shooting_params.bShootShake)
			return;
		fPowerMin = clampr(hi->m_measures.m_shooting_params.m_min_LRUD_power, 0.0f, 0.99f);
	}

	float fPowerRnd = 1.0f - fPowerMin;

	m_fLR_ShootingFactor = ::Random.randF(-fPowerRnd, fPowerRnd);
	m_fLR_ShootingFactor += (m_fLR_ShootingFactor >= 0.0f ? fPowerMin : -fPowerMin);

	m_fUD_ShootingFactor = ::Random.randF(-fPowerRnd, fPowerRnd);
	m_fUD_ShootingFactor += (m_fUD_ShootingFactor >= 0.0f ? fPowerMin : -fPowerMin);
}


void CWeapon::SetAmmoElapsed(int ammo_count)
{
	iAmmoElapsed = ammo_count;

	u32 uAmmo = u32(iAmmoElapsed);

	if (uAmmo != m_magazine.size())
	{
		if (uAmmo > m_magazine.size())
		{
			CCartridge l_cartridge;
			l_cartridge.Load(m_ammoTypes[m_ammoType].c_str(), m_ammoType, m_APk);
			while (uAmmo > m_magazine.size())
				m_magazine.push_back(l_cartridge);
		}
		else
		{
			while (uAmmo < m_magazine.size())
				m_magazine.pop_back();
		};
	};
}

u32 CWeapon::ef_main_weapon_type() const
{
	VERIFY(m_ef_main_weapon_type != u32(-1));
	return (m_ef_main_weapon_type);
}

u32 CWeapon::ef_weapon_type() const
{
	VERIFY(m_ef_weapon_type != u32(-1));
	return (m_ef_weapon_type);
}

bool CWeapon::IsNecessaryItem(const shared_str& item_sect)
{
	return (std::find(m_ammoTypes.begin(), m_ammoTypes.end(), item_sect) != m_ammoTypes.end());
}

void CWeapon::modify_holder_params(float& range, float& fov) const
{
	if (!IsScopeAttached())
	{
		inherited::modify_holder_params(range, fov);
		return;
	}
	range *= m_addon_holder_range_modifier;
	fov *= m_addon_holder_fov_modifier;
}

bool CWeapon::render_item_ui_query()
{
	bool b_is_active_item = (m_pInventory->ActiveItem() == this);
	bool res = b_is_active_item && IsZoomed() && ZoomHideCrosshair() && ZoomTexture() && !IsRotatingToZoom();
	return res;
}

void CWeapon::render_item_ui()
{
	if (m_zoom_params.m_pVision)
		m_zoom_params.m_pVision->Draw();

	ZoomTexture()->Update();
	ZoomTexture()->Draw();

	//crookr
	scope_2dtexactive = ZoomTexture()->IsShown() ? 1 : 0;
}

bool CWeapon::unlimited_ammo()
{
	if (IsGameTypeSingle())
	{
		if (m_pInventory)
		{
			return inventory_owner().unlimited_ammo() && m_DefaultCartridge.m_flags.test(CCartridge::cfCanBeUnlimited);
		}
		else
			return false;
	}

	return ((GameID() == eGameIDDeathmatch) &&
		m_DefaultCartridge.m_flags.test(CCartridge::cfCanBeUnlimited));
};

float CWeapon::GetMagazineWeight(const decltype(CWeapon::m_magazine)& mag) const
{
	float res = 0;
	const char* last_type = nullptr;
	float last_ammo_weight = 0;
	for (auto& c : mag)
	{
		// Usually ammos in mag have same type, use this fact to improve performance
		if (last_type != c.m_ammoSect.c_str())
		{
			last_type = c.m_ammoSect.c_str();
			last_ammo_weight = c.Weight();
		}
		res += last_ammo_weight;
	}
	return res;
}

void CWeapon::AmmoTypeForEach(const luabind::functor<bool> &funct)
{
	for (u8 i = 0; i < u8(m_ammoTypes.size()); ++i)
	{
		if (funct(i, *m_ammoTypes[i]))
			break;
	}
}

float CWeapon::Weight() const
{
	float res = CInventoryItemObject::Weight();

	if (IsGrenadeLauncherAttached() && GetGrenadeLauncherName().size())
	{
		res += pSettings->r_float(GetGrenadeLauncherName(), "inv_weight");
	}
	if (IsScopeAttached() && m_scopes.size())
	{
		res += pSettings->r_float(GetScopeName(), "inv_weight");
	}
	if (IsSilencerAttached() && GetSilencerName().size())
	{
		res += pSettings->r_float(GetSilencerName(), "inv_weight");
	}

	res += GetMagazineWeight(m_magazine);
	return res;
}

bool CWeapon::show_crosshair()
{
	return !IsPending() && (!IsZoomed() || !ZoomHideCrosshair());
}

bool CWeapon::show_indicators()
{
	return !(IsZoomed() && ZoomTexture());
}

float CWeapon::GetConditionToShow() const
{
	return (GetCondition()); //powf(GetCondition(),4.0f));
}

BOOL CWeapon::ParentMayHaveAimBullet()
{
	CObject* O = H_Parent();
	CEntityAlive* EA = smart_cast<CEntityAlive*>(O);
	return EA->cast_actor() != 0;
}

extern u32 hud_adj_mode;

void CWeapon::debug_draw_firedeps()
{
#ifdef DEBUG
    if(hud_adj_mode==5||hud_adj_mode==6||hud_adj_mode==7)
    {
        CDebugRenderer			&render = Level().debug_renderer();

        if(hud_adj_mode==5)
            render.draw_aabb(get_LastFP(),	0.005f,0.005f,0.005f,D3DCOLOR_XRGB(255,0,0));

        if(hud_adj_mode==6)
            render.draw_aabb(get_LastFP2(),	0.005f,0.005f,0.005f,D3DCOLOR_XRGB(0,0,255));

        if(hud_adj_mode==7)
            render.draw_aabb(get_LastSP(),		0.005f,0.005f,0.005f,D3DCOLOR_XRGB(0,255,0));
    }
#endif // DEBUG
}

const float& CWeapon::hit_probability() const
{
	VERIFY((g_SingleGameDifficulty >= egdNovice) && (g_SingleGameDifficulty <= egdMaster));
	return (m_hit_probability[egdNovice]);
}

void CWeapon::OnStateSwitch(u32 S, u32 oldState)
{
	inherited::OnStateSwitch(S, oldState);
	m_BriefInfo_CalcFrame = 0;

	if (GetState() == eReload)
	{
		if (iAmmoElapsed == 0) //Swartz: re-written to use reload empty DOF
		{
			if (H_Parent() == Level().CurrentEntity() && !fsimilar(m_zoom_params.m_ReloadEmptyDof.w, -1.0f))
			{
				CActor* current_actor = smart_cast<CActor*>(H_Parent());
				if (current_actor)
					current_actor->Cameras().AddCamEffector(xr_new<CEffectorDOF>(m_zoom_params.m_ReloadEmptyDof));
			}
		}
		else
		{
			if (H_Parent() == Level().CurrentEntity() && !fsimilar(m_zoom_params.m_ReloadDof.w, -1.0f))
			{
				CActor* current_actor = smart_cast<CActor*>(H_Parent());
				if (current_actor)
					current_actor->Cameras().AddCamEffector(xr_new<CEffectorDOF>(m_zoom_params.m_ReloadDof));
			}
		}
	}

	if (ParentIsActor() && smart_cast<CActor*>(H_Parent())->inventory().ActiveItem() == this && GetState() != eIdle)
		Actor()->set_safemode(false);
}

void CWeapon::OnAnimationEnd(u32 state)
{
	inherited::OnAnimationEnd(state);
}

u8 CWeapon::GetCurrentHudOffsetIdx()
{
	CActor* pActor = smart_cast<CActor*>(H_Parent());
	if (!pActor) return 0;

	if (m_bCanBeLowered && Actor()->is_safemode())
		return 4;
	else if (!IsZoomed())
		return 0;
	else if (m_zoomtype == 1)
		return 3;
	else
		return 1;
}

void CWeapon::render_hud_mode()
{
	RenderLight();
}

bool CWeapon::MovingAnimAllowedNow()
{
	return !IsZoomed();
}

bool CWeapon::IsHudModeNow()
{
	return (HudItemData() != NULL);
}

void NewGetZoomData(const float scope_factor, float& delta, float& min_zoom_factor, float zoom, float min_zoom)
{
	
	float def_fov = float(g_fov);
	float min_zoom_k = 0.3f;
	float delta_factor_total = def_fov - scope_factor;
	VERIFY(delta_factor_total > 0);
	float loc_min_zoom_factor = ((atan(tan(def_fov * (0.5 * PI / 180)) / g_ironsights_factor) / (0.5 * PI / 180)) / 0.75f) * (scope_radius > 0.0 ? scope_scrollpower : 1);

	//Msg("min zoom factor %f, min zoom %f, loc min zoom factor %f", min_zoom_factor, min_zoom, loc_min_zoom_factor);

	if (min_zoom < loc_min_zoom_factor) {
		min_zoom_factor = min_zoom;
	}
	else {
		min_zoom_factor = loc_min_zoom_factor;
	}

	delta = ((delta_factor_total * (1 - min_zoom_k)) / n_zoom_step_count) * (zoom / def_fov);
}

void CWeapon::ZoomInc()
{
	if (!IsScopeAttached()) return;
	if (!m_zoom_params.m_bUseDynamicZoom) return;
	float delta, min_zoom_factor;
	float power = scope_radius > 0.0 ? scope_scrollpower : 1;
	//
	if (zoomFlags.test(NEW_ZOOM)) {
		NewGetZoomData(m_zoom_params.m_fScopeZoomFactor * power, delta, min_zoom_factor, GetZoomFactor() * power, m_zoom_params.m_fMinBaseZoomFactor);
	}
	else {
        GetZoomData(m_zoom_params.m_fScopeZoomFactor * power, delta, min_zoom_factor);
	}
	//
	float f = GetZoomFactor() * power - delta;
	clamp(f, m_zoom_params.m_fScopeZoomFactor * power, min_zoom_factor);
	SetZoomFactor(f / power);
	//
	m_fRTZoomFactor = GetZoomFactor() * power;
}

void CWeapon::ZoomDec()
{
	if (!IsScopeAttached()) return;
	if (!m_zoom_params.m_bUseDynamicZoom) return;
	float delta, min_zoom_factor;
	float power = scope_radius > 0.0 ? scope_scrollpower : 1;
	//
	if (zoomFlags.test(NEW_ZOOM)) {
		NewGetZoomData(m_zoom_params.m_fScopeZoomFactor * power, delta, min_zoom_factor, GetZoomFactor() * power, m_zoom_params.m_fMinBaseZoomFactor);
	}
	else {
        GetZoomData(m_zoom_params.m_fScopeZoomFactor * power, delta, min_zoom_factor);
	}
	//
	float f = GetZoomFactor() * power + delta;
	clamp(f, m_zoom_params.m_fScopeZoomFactor * power, min_zoom_factor);
	SetZoomFactor(f / power);
	//
	m_fRTZoomFactor = GetZoomFactor() * power;
}

u32 CWeapon::Cost() const
{
	u32 res = CInventoryItem::Cost();
	if (IsGrenadeLauncherAttached() && GetGrenadeLauncherName().size())
	{
		res += pSettings->r_u32(GetGrenadeLauncherName(), "cost");
	}
	if (IsScopeAttached() && m_scopes.size())
	{
		res += pSettings->r_u32(GetScopeName(), "cost");
	}
	if (IsSilencerAttached() && GetSilencerName().size())
	{
		res += pSettings->r_u32(GetSilencerName(), "cost");
	}

	if (iAmmoElapsed)
	{
		float w = pSettings->r_float(m_ammoTypes[m_ammoType].c_str(), "cost");
		float bs = pSettings->r_float(m_ammoTypes[m_ammoType].c_str(), "box_size");

		res += iFloor(w * (iAmmoElapsed / bs));
	}
	return res;
}

float CWeapon::GetSecondVPFov() const
{
	if (m_zoom_params.m_bUseDynamicZoom && IsSecondVPZoomPresent())
		return (m_fRTZoomFactor / 100.f) * g_fov;

	return GetSecondVPZoomFactor() * g_fov;
}

void CWeapon::UpdateSecondVP()
{
	if (!(ParentIsActor() && (m_pInventory != NULL) && (m_pInventory->ActiveItem() == this)))
		return;

	CActor* pActor = smart_cast<CActor*>(H_Parent());
	Device.m_SecondViewport.SetSVPActive(m_zoomtype == 0 && pActor->cam_Active() == pActor->cam_FirstEye() && IsSecondVPZoomPresent() && m_zoom_params.m_fZoomRotationFactor > 0.05f);
}
