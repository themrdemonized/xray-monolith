#include "stdafx.h"
#include "WeaponStatMgun.h"
#include "../Include/xrRender/Kinematics.h"
#include "../xrphysics/PhysicsShell.h"
#include "weaponAmmo.h"
#include "object_broker.h"
#include "ai_sounds.h"
#include "actor.h"
#include "actorEffector.h"
#include "camerafirsteye.h"
#include "xr_level_controller.h"
#include "game_object_space.h"
#include "level.h"

#ifdef STATIONARYMGUN_NEW
#include "xrServer_Objects_ALife.h"
#include "ai_space.h"
#include "alife_simulator.h"
#include "alife_object_registry.h"

#include "CharacterPhysicsSupport.h"
#include "detail_path_manager.h"
#include "stalker_animation_manager.h"
#include "stalker_movement_manager_smart_cover.h"
#include "ai/stalker/ai_stalker.h"
#include "../xrEngine/gamemtllib.h"
#include "cameralook.h"
#include "HUDManager.h"
#endif

void CWeaponStatMgun::BoneCallbackX(CBoneInstance* B)
{
	CWeaponStatMgun* P = static_cast<CWeaponStatMgun*>(B->callback_param());
	Fmatrix rX;
	rX.rotateX(P->m_cur_x_rot);
	B->mTransform.mulB_43(rX);

#ifdef STATIONARYMGUN_NEW
	if (P->OwnerActor() && P->OwnerActor()->IsMyCamera())
	{
		P->UpdateCamera();
	}
#endif
}

void CWeaponStatMgun::BoneCallbackY(CBoneInstance* B)
{
	CWeaponStatMgun* P = static_cast<CWeaponStatMgun*>(B->callback_param());
	Fmatrix rY;
	rY.rotateY(P->m_cur_y_rot);
	B->mTransform.mulB_43(rY);

#ifdef STATIONARYMGUN_NEW
	if (P->OwnerActor() && P->OwnerActor()->IsMyCamera())
	{
		P->UpdateCamera();
	}
#endif
}

CWeaponStatMgun::CWeaponStatMgun()
{
#ifdef STATIONARYMGUN_NEW
	m_rotate_x_speed = 0.0F;
	m_rotate_y_speed = 0.0F;
	m_drop_bone = BI_NONE;
	m_desire_angle.set(0.0F, 0.0F);
	m_desire_angle_enable = false;

	m_actor_bone = BI_NONE;
	m_exit_position.set(0, 0, 0);
	m_actor_offsets.set(0, 0, 0);

	m_camera_bone = BI_NONE;
	m_camera_bone_def = BI_NONE;
	m_camera_bone_aim = BI_NONE;
	m_zoom_factor_def = 1.0F;
	m_zoom_factor_aim = 1.0F;
	m_camera_position.set(0, 0, 0);
	m_zoom_status = false;

	m_bullet_bones.clear();
	m_bullet_count = 0;

	m_barrels.clear();
	m_iShotNum = 0;

	m_reload_delay = 0.0F;
	m_single_shot_wpn = FALSE;
	m_unlimited_ammo = true;
	m_reload_consume_callback = nullptr;
	m_shot_effector._set("");

	m_next_ammoType_on_reload.reset();
	m_ammoType = 0;
	m_ammoTypes.clear();
	iMagazineSize = 1;

	fireDispersionOwnerScale = 1.0F;
	m_on_before_use_callback = nullptr;
    m_on_range_fov_callback = "";
#endif

	m_firing_disabled = false;
	m_Ammo = xr_new<CCartridge>();

#ifdef STATIONARYMGUN_NEW
	camera[eCamFirst] = xr_new<CCameraFirstEye>(this, CCameraBase::flRelativeLink | CCameraBase::flPositionRigid);
	camera[eCamFirst]->tag = eCamFirst;
	camera[eCamFirst]->Load("mounted_weapon_cam");

	camera[eCamChase] = xr_new<CCameraLook>(this);
	camera[eCamChase]->tag = eCamChase;
	camera[eCamChase]->Load("actor_look_cam");

	OnCameraChange(eCamFirst);

	m_anim_weapon.Init(this);
	m_sound_mgr.Init(this);
#else
	camera = xr_new<CCameraFirstEye>(
		this, CCameraBase::flRelativeLink | CCameraBase::flPositionRigid | CCameraBase::flDirectionRigid);
	camera->Load("mounted_weapon_cam");
#endif

	p_overheat = NULL;
}

CWeaponStatMgun::~CWeaponStatMgun()
{
	delete_data(m_Ammo);
#ifdef STATIONARYMGUN_NEW
	for (int i = 0; i < eCamSize; i++)
	{
		xr_delete(camera[i]);
	}
#else
	xr_delete(camera);
#endif
}

void CWeaponStatMgun::SetBoneCallbacks()
{
	//m_pPhysicsShell->EnabledCallbacks(FALSE);
#ifdef STATIONARYMGUN_NEW
	Fvector vec;
	Visual()->dcast_PKinematics()->LL_GetTransform(m_rotate_y_bone).getHPB(vec);
	m_cur_x_rot = 0.0F;
	m_cur_y_rot = -vec.x;
	ClampRotationHorz(m_cur_y_rot, -vec.x, -m_lim_y_rot.y, -m_lim_y_rot.x);
#endif

	CBoneInstance& biX = smart_cast<IKinematics*>(Visual())->LL_GetBoneInstance(m_rotate_x_bone);
	biX.set_callback(bctCustom, BoneCallbackX, this);
	CBoneInstance& biY = smart_cast<IKinematics*>(Visual())->LL_GetBoneInstance(m_rotate_y_bone);
	biY.set_callback(bctCustom, BoneCallbackY, this);
}

void CWeaponStatMgun::ResetBoneCallbacks()
{
	CBoneInstance& biX = smart_cast<IKinematics*>(Visual())->LL_GetBoneInstance(m_rotate_x_bone);
#ifdef STATIONARYMGUN_NEW
	biX.set_callback(bctPhysics, PPhysicsShell()->GetBonesCallback(), PPhysicsShell()->get_Element(m_rotate_x_bone));
#else
	biX.reset_callback();
#endif

	CBoneInstance& biY = smart_cast<IKinematics*>(Visual())->LL_GetBoneInstance(m_rotate_y_bone);
#ifdef STATIONARYMGUN_NEW
	biY.set_callback(bctPhysics, PPhysicsShell()->GetBonesCallback(), PPhysicsShell()->get_Element(m_rotate_y_bone));
#else
	biY.reset_callback();
#endif

#ifdef STATIONARYMGUN_NEW
	biX.mTransform.mulB_43(Fmatrix().rotateY(-m_cur_x_rot));
	biY.mTransform.mulB_43(Fmatrix().rotateY(-m_cur_y_rot));
#endif
	//m_pPhysicsShell->EnabledCallbacks(TRUE);
}

void CWeaponStatMgun::Load(LPCSTR section)
{
	inheritedPH::Load(section);
	inheritedShooting::Load(section);

	m_sounds.LoadSound(section, "snd_shoot", "sndShot", false, SOUND_TYPE_WEAPON_SHOOTING);

#ifdef STATIONARYMGUN_NEW
	/* Remove. */
#else
	m_Ammo->Load(pSettings->r_string(section, "ammo_class"), 0);
#endif
	camMaxAngle = pSettings->r_float(section, "cam_max_angle");
	camMaxAngle = _abs(deg2rad(camMaxAngle));
	camRelaxSpeed = pSettings->r_float(section, "cam_relax_speed");
	camRelaxSpeed = _abs(deg2rad(camRelaxSpeed));

	m_overheat_enabled = pSettings->line_exist(section, "overheat_enabled")
		                     ? !!pSettings->r_bool(section, "overheat_enabled")
		                     : false;
	m_overheat_time_quant = READ_IF_EXISTS(pSettings, r_float, section, "overheat_time_quant", 0.025f);
	m_overheat_decr_quant = READ_IF_EXISTS(pSettings, r_float, section, "overheat_decr_quant", 0.002f);
	m_overheat_threshold = READ_IF_EXISTS(pSettings, r_float, section, "overheat_threshold", 110.f);
	m_overheat_particles = READ_IF_EXISTS(pSettings, r_string, section, "overheat_particles",
	                                      "damage_fx\\burn_creatures00");

	m_bEnterLocked = !!READ_IF_EXISTS(pSettings, r_bool, section, "lock_enter", false);
	m_bExitLocked = !!READ_IF_EXISTS(pSettings, r_bool, section, "lock_exit", false);

	VERIFY(!fis_zero(camMaxAngle));
	VERIFY(!fis_zero(camRelaxSpeed));

#ifdef STATIONARYMGUN_NEW
	m_sounds.LoadSound(section, "snd_shoot", "sndShoot", false, SOUND_TYPE_WEAPON_SHOOTING);
	m_sounds.LoadSound(section, "snd_empty", "sndEmpty", false, SOUND_TYPE_WEAPON_EMPTY_CLICKING);
	m_sounds.LoadSound(section, "snd_reload", "sndReload", true, SOUND_TYPE_WEAPON_RECHARGING);

	m_zoom_factor_def = READ_IF_EXISTS(pSettings, r_float, section, "zoom_factor_def", 1.0F);
	m_zoom_factor_aim = READ_IF_EXISTS(pSettings, r_float, section, "zoom_factor_aim", 1.0F);
	m_reload_delay = READ_IF_EXISTS(pSettings, r_float, section, "reload_delay", 0.0F);
	m_single_shot_wpn = !!READ_IF_EXISTS(pSettings, r_bool, section, "is_single_shot_wpn", FALSE);
	m_unlimited_ammo = !!READ_IF_EXISTS(pSettings, r_bool, section, "unlimited_ammo", false);
	m_reload_consume_callback = READ_IF_EXISTS(pSettings, r_string, section, "reload_consume", nullptr);
	m_shot_effector._set(READ_IF_EXISTS(pSettings, r_string, section, "shot_effector", ""));

	m_ammoTypes.clear();
	LPCSTR ammo_class = pSettings->r_string(section, "ammo_class");
	if (ammo_class && strlen(ammo_class))
	{
		string128 tmp;
		int n = _GetItemCount(ammo_class);
		for (int i = 0; i < n; ++i)
		{
			_GetItem(ammo_class, i, tmp);
			m_ammoTypes.push_back(tmp);
		}
	}
	m_ammoType = 0;
	m_DefaultCartridge.Load(m_ammoTypes[m_ammoType].c_str(), m_ammoType);

	iMagazineSize = READ_IF_EXISTS(pSettings, r_s32, section, "ammo_mag_size", 1);
	SetAmmoElapsed(READ_IF_EXISTS(pSettings, r_s32, section, "ammo_elapsed", iMagazineSize));

	fireDispersionOwnerScale = READ_IF_EXISTS(pSettings, r_float, section, "fire_dispersion_owner_scale", 1.0F);
	if (pSettings->line_exist(cNameSect_str(), "on_before_use"))
	{
		m_on_before_use_callback = READ_IF_EXISTS(pSettings, r_string, cNameSect_str(), "on_before_use", "");
	}
    if (pSettings->line_exist(cNameSect_str(), "on_range_fov"))
    {
        m_on_range_fov_callback = READ_IF_EXISTS(pSettings, r_string, cNameSect_str(), "on_range_fov", "");
    }

	UpdateBulletVisibility(iAmmoElapsed);

	m_barrels.clear();
	if (pSettings->line_exist(cNameSect_str(), "barrels"))
	{
		LPCSTR str = pSettings->r_string(cNameSect_str(), "barrels");
		string128 sec;
		int n = _GetItemCount(str);
		for (int i = 0; i < n; ++i)
		{
			_GetItem(str, i, sec);
			if (strlen(sec))
			{
				m_barrels.emplace_back(this, sec);
				m_barrels.back().Load(cNameSect_str());
			}
		}
		if (iMagazineSize && m_barrels.size() && (iMagazineSize % m_barrels.size() > 0))
		{
			Msg("ERROR: [%s][%s] the magazine size %d is not divisible to the number of barrels %d", cName().c_str(), cNameSect_str(), iMagazineSize, m_barrels.size());
			FATAL("");
		}
		BarrelAmmoElapsedCorrecting();
	}
#endif
}

BOOL CWeaponStatMgun::net_Spawn(CSE_Abstract* DC)
{
	if (!inheritedPH::net_Spawn(DC)) return FALSE;

	IKinematics* K = smart_cast<IKinematics*>(Visual());
	CInifile* pUserData = K->LL_UserData();

	R_ASSERT2(pUserData, "Empty WeaponStatMgun user data!");

	m_rotate_x_bone = K->LL_BoneID(pUserData->r_string("mounted_weapon_definition", "rotate_x_bone"));
	m_rotate_y_bone = K->LL_BoneID(pUserData->r_string("mounted_weapon_definition", "rotate_y_bone"));
	m_fire_bone = K->LL_BoneID(pUserData->r_string("mounted_weapon_definition", "fire_bone"));
	m_camera_bone = K->LL_BoneID(pUserData->r_string("mounted_weapon_definition", "camera_bone"));

#ifdef STATIONARYMGUN_NEW
	CPHSkeleton::Spawn(DC);
#else
	U16Vec fixed_bones;
	fixed_bones.push_back(K->LL_GetBoneRoot());
	PPhysicsShell() = P_build_Shell(this, false, fixed_bones);
#endif

	CBoneData& bdX = K->LL_GetData(m_rotate_x_bone);
	VERIFY(bdX.IK_data.type==jtJoint);
	m_lim_x_rot.set(bdX.IK_data.limits[0].limit.x, bdX.IK_data.limits[0].limit.y);
	CBoneData& bdY = K->LL_GetData(m_rotate_y_bone);
	VERIFY(bdY.IK_data.type==jtJoint);
	m_lim_y_rot.set(bdY.IK_data.limits[1].limit.x, bdY.IK_data.limits[1].limit.y);


	xr_vector<Fmatrix> matrices;
	K->LL_GetBindTransform(matrices);
	m_i_bind_x_xform.invert(matrices[m_rotate_x_bone]);
	m_i_bind_y_xform.invert(matrices[m_rotate_y_bone]);
	m_bind_x_rot = matrices[m_rotate_x_bone].k.getP();
	m_bind_y_rot = matrices[m_rotate_y_bone].k.getH();
	m_bind_x.set(matrices[m_rotate_x_bone].c);
	m_bind_y.set(matrices[m_rotate_y_bone].c);

#ifdef STATIONARYMGUN_NEW
	/*
	Initial directions are always H = 0 P = 0. Even when the bones were created with angled directions.
	Example: When a bone is created with H = 60 degrees, the bone has a H = 60 degrees angle to the model.
	But it is H = 0 degree to the bone itself.
	*/
	m_cur_x_rot = 0;
	m_cur_y_rot = 0;
#else
	m_cur_x_rot = m_bind_x_rot;
	m_cur_y_rot = m_bind_y_rot;
#endif

	m_destEnemyDir.setHP(m_bind_y_rot, m_bind_x_rot);
	XFORM().transform_dir(m_destEnemyDir);

#ifdef STATIONARYMGUN_NEW
	CInifile *ini = K->LL_UserData();
	const LPCSTR mwd = "mounted_weapon_definition";

	m_rotate_x_speed = deg2rad(READ_IF_EXISTS(ini, r_float, mwd, "rotate_x_speed", 10.0F));
	m_rotate_y_speed = deg2rad(READ_IF_EXISTS(ini, r_float, mwd, "rotate_y_speed", 10.0F));
	m_drop_bone = ini->line_exist(mwd, "drop_bone") ? K->LL_BoneID(ini->r_string(mwd, "drop_bone")) : BI_NONE;
	m_actor_bone = ini->line_exist(mwd, "actor_bone") ? K->LL_BoneID(ini->r_string(mwd, "actor_bone")) : BI_NONE;

	m_exit_position = READ_IF_EXISTS(ini, r_fvector3, mwd, "exit_position", Fvector().set(0, 0, 0));
	m_camera_bone_def = ini->line_exist(mwd, "camera_bone_def") ? K->LL_BoneID(ini->r_string(mwd, "camera_bone_def")) : BI_NONE;
	m_camera_bone_aim = ini->line_exist(mwd, "camera_bone_aim") ? K->LL_BoneID(ini->r_string(mwd, "camera_bone_aim")) : BI_NONE;
	m_camera_position = READ_IF_EXISTS(ini, r_fvector3, mwd, "camera_position", Fvector().set(0, 1, 0));

	m_bullet_bones.clear();
	if (ini->line_exist(mwd, "bullet_bones"))
	{
		LPCSTR str = ini->r_string(mwd, "bullet_bones");
		for (int k = 0, n = _GetItemCount(str); k < n; ++k)
		{
			string128 bone_name;
			_GetItem(str, k, bone_name);
			m_bullet_bones.push_back(K->LL_BoneID(bone_name));
		}
	}

	if (ini->section_exist("animation"))
	{
		m_animation.SetAnimation(SStmAnimActor::eAnimBody, READ_IF_EXISTS(ini, r_string, "animation", "idle_body", nullptr));
		m_animation.SetAnimation(SStmAnimActor::eAnimLegs, READ_IF_EXISTS(ini, r_string, "animation", "idle_legs", nullptr));
	}

	for (auto &B : m_barrels)
	{
		B.net_Spawn(DC);
		B.Light_Create();
	}
	CSE_ALifeStationaryMgun *E = smart_cast<CSE_ALifeStationaryMgun *>(DC);
	VERIFY3(E, cNameSect_str(), "No CSE_ALifeStationaryMgun");

#ifdef STATIONARYMGUN_NEW
	{
		SetAmmoElapsed(0);
		SetAmmoType(E->ammo_type);

		for (int idx = 0; idx < E->m_barrels.size(); idx++)
		{
			if (idx < m_barrels.size())
			{
				m_barrels.at(idx).SetAmmoElapsed(E->m_barrels.at(idx).a_elapsed);
			}
		}
		iAmmoElapsed = E->a_elapsed;
		SetAmmoElapsed(iAmmoElapsed);
	}
#endif

	LPCSTR custom_cam_first = READ_IF_EXISTS(ini, r_string, "camera", "cam_first", nullptr);
	if (custom_cam_first && pSettings->section_exist(custom_cam_first))
	{
		camera[eCamFirst]->Load(custom_cam_first);
	}

	LPCSTR custom_cam_chase = READ_IF_EXISTS(ini, r_string, "camera", "cam_chase", nullptr);
	if (custom_cam_chase && pSettings->section_exist(custom_cam_chase))
	{
		camera[eCamChase]->Load(custom_cam_chase);
	}

	m_anim_weapon.net_Spawn(DC);
	m_sound_mgr.net_Spawn(DC);

	{
		/* Hack. net_spawn() of CScriptBinderObjectWrapper runs first. This allows overriding configs read in engine net_Spawn(). */
		LPCSTR str = READ_IF_EXISTS(pSettings, r_string, cNameSect_str(), "net_spawn_after", nullptr);
		::luabind::functor<bool> lua_function;
		if (str && ai().script_engine().functor(str, lua_function))
		{
			lua_function(lua_game_object());
		}
	}
#endif

	inheritedShooting::Light_Create();

	processing_activate();
	setVisible(TRUE);
	setEnabled(TRUE);

#ifdef STATIONARYMGUN_NEW
	PPhysicsShell()->Enable();
	PPhysicsShell()->add_ObjectContactCallback(IgnoreOwnerCallback);

	if (PPhysicsShell() && m_ignore_collision_flag)
	{
		CPhysicsShellHolder::active_ignore_collision();
	}
#endif

	return TRUE;
}

void CWeaponStatMgun::net_Destroy()
{
	if (p_overheat)
	{
		if (p_overheat->IsPlaying())
			p_overheat->Stop(FALSE);
		CParticlesObject::Destroy(p_overheat);
	}

#ifdef HOLDERCUSTOM_NEW
#ifdef STATIONARYMGUN_NEW
	if (Owner() && Owner()->cast_stalker())
	{
		Owner()->cast_stalker()->detach_Holder();
	}
#endif
#endif

#ifdef STATIONARYMGUN_NEW
	Action(eWpnActivate, 0);
	PPhysicsShell()->remove_ObjectContactCallback(IgnoreOwnerCallback);
#endif

	inheritedPH::net_Destroy();
#ifdef STATIONARYMGUN_NEW
	CShootingObject::StopFlameParticles();
	CShootingObject::StopLight();
	CShootingObject::Light_Destroy();
	for (auto &B : m_barrels)
	{
		B.StopFlameParticles();
		B.StopLight();
		B.Light_Destroy();
	}
	CPHUpdateObject::Deactivate();
	CPHSkeleton::RespawnInit();
#endif
	processing_deactivate();
}

void CWeaponStatMgun::net_Export(NET_Packet& P) // export to server
{
#ifdef STATIONARYMGUN_NEW

#else
	inheritedPH::net_Export(P);
#endif
	P.w_u8(IsWorking() ? 1 : 0);
	save_data(m_destEnemyDir, P);

#ifdef STATIONARYMGUN_NEW
	P.w_u8(m_ammoType);
	P.w_u16(u16(iAmmoElapsed & 0xffff));

	u16 num = m_barrels.size();
	P.w_u16(num);
	for (int idx = 0; idx < num; idx++)
	{
		m_barrels.at(idx).net_Export(P);
	}
#endif
}

void CWeaponStatMgun::net_Import(NET_Packet& P) // import from server
{
	inheritedPH::net_Import(P);
	u8 state = P.r_u8();
	load_data(m_destEnemyDir, P);

	if (TRUE == IsWorking() && !state) FireEnd();
	if (FALSE == IsWorking() && state) FireStart();

#ifdef STATIONARYMGUN_NEW
	m_ammoType = P.r_u8();
	iAmmoElapsed = P.r_u16();

	u16 num = P.r_u16();
	for (int idx = 0; idx < num; idx++)
	{
		if (idx < m_barrels.size())
		{
			m_barrels.at(idx).net_Import(P);
		}
	}

	SetAmmoElapsed(iAmmoElapsed);
#endif
}

void CWeaponStatMgun::UpdateCL()
{
	inheritedPH::UpdateCL();

#ifdef STATIONARYMGUN_NEW
	if (OwnerActor())
	{
		/* Actor will call UpdateEx() to update machine gun instead.
		Doing this to keep weapon rotation and actor camera match each others. */
		return;
	}
	UpdateEx(90.0f);
#else
	UpdateBarrelDir();
	UpdateFire();

	if (OwnerActor() && OwnerActor()->IsMyCamera())
	{
		cam_Update(Device.fTimeDelta, g_fov);
		OwnerActor()->Cameras().UpdateFromCamera(Camera());
		OwnerActor()->Cameras().ApplyDevice(VIEWPORT_NEAR);
	}
#endif
}

#ifdef STATIONARYMGUN_NEW
void CWeaponStatMgun::UpdateEx(float fov)
{
	if (PPhysicsShell())
	{
		PPhysicsShell()->InterpolateGlobalTransform(&XFORM());
		Visual()->dcast_PKinematics()->CalculateBones();
	}

	UpdateState();
	UpdateBarrelDir();
	UpdateFire();
	UpdateSound();

	if (Owner() == nullptr)
		return;

#ifdef HOLDERCUSTOM_NEW
#ifdef STATIONARYMGUN_NEW
	/* Dead owner. Kick out. Just in case. We kick them out when on death callback already. */
	if (Owner()->cast_stalker() && !Owner()->cast_stalker()->g_Alive())
	{
		Owner()->cast_stalker()->detach_Holder();
		return;
	}
#endif
#endif

	/* Update owner position. */
	if (m_actor_bone != BI_NONE)
	{
		Fvector pos;
		Fmatrix xfm = Visual()->dcast_PKinematics()->LL_GetTransform(m_actor_bone);
		xfm.transform_tiny(pos, m_actor_offsets);
		Owner()->XFORM().mul_43(XFORM(), xfm.translate_over(pos));
	}
	else
	{
		/* Should not let this happen. It indicates that there is no actor bone or it is invalid. Should fix. */
		Owner()->XFORM().set(XFORM());
	}

	/* Update owner animations. */
	UpdateAnimation();

	/* Always update camera last. */
	if (OwnerActor() && OwnerActor()->IsMyCamera())
	{
		UpdateCamera();
	}

#if 0
	if (Device.dwFrame % 50 == 0)
	{
		Fvector ang;
		Visual()->dcast_PKinematics()->LL_GetTransform(m_rotate_y_bone).getHPB(ang);
		Msg("rot:%.2f ang:%.2f cam:%.2f", rad2deg(m_cur_y_rot), rad2deg(ang.x), rad2deg(Camera()->yaw));
	}
#endif
}
#endif

//void CWeaponStatMgun::Hit(	float P, Fvector &dir,	CObject* who, 
//							s16 element,Fvector p_in_object_space, 
//							float impulse, ALife::EHitType hit_type)
void CWeaponStatMgun::Hit(SHit* pHDS)
{
#ifdef STATIONARYMGUN_NEW
	inheritedPH::Hit(pHDS);
#else
	if (NULL == Owner())
		//		inheritedPH::Hit(P,dir,who,element,p_in_object_space,impulse,hit_type);
		inheritedPH::Hit(pHDS);
#endif
}

void CWeaponStatMgun::UpdateBarrelDir()
{
	IKinematics* K = smart_cast<IKinematics*>(Visual());
	m_fire_bone_xform = K->LL_GetTransform(m_fire_bone);

	m_fire_bone_xform.mulA_43(XFORM());
	m_fire_pos.set(0, 0, 0);
	m_fire_bone_xform.transform_tiny(m_fire_pos);
	m_fire_dir.set(0, 0, 1);
	m_fire_bone_xform.transform_dir(m_fire_dir);

	m_allow_fire = true;
	Fmatrix XFi;
	XFi.invert(XFORM());
	Fvector dep;

#ifdef STATIONARYMGUN_NEW
	if (m_barrels.size())
	{
		for (auto &B : m_barrels)
		{
			B.UpdateEx();
		}
	}

	if (!IsActive())
		return;

	/*
		Take m_i_bind_y_xform as base for both bone x and bone_rotate_y assuming bone_rotate_y always has 0 pitch.
		And reset dep before calculating.
	*/
	{
		// x angle
		if (m_desire_angle_enable)
		{
			dep.setHP(m_desire_angle.x, m_desire_angle.y);
		}
		else
		{
			XFi.transform_dir(dep, m_destEnemyDir);
		}
		m_i_bind_y_xform.transform_dir(dep);
		dep.normalize();
		m_tgt_x_rot = angle_normalize_signed(m_bind_x_rot - dep.getP());
		clamp(m_tgt_x_rot, -m_lim_x_rot.y, -m_lim_x_rot.x);
	}
	{
		// y angle
		if (m_desire_angle_enable)
		{
			dep.setHP(m_desire_angle.x, m_desire_angle.y);
		}
		else
		{
			XFi.transform_dir(dep, m_destEnemyDir);
		}
		m_i_bind_y_xform.transform_dir(dep);
		dep.normalize();
		m_tgt_y_rot = angle_normalize_signed(m_bind_y_rot - dep.getH());
		ClampRotationHorz(m_tgt_y_rot, m_cur_y_rot, -m_lim_y_rot.y, -m_lim_y_rot.x);
	}

	switch (GetState())
	{
	case eStateIdle:
	case eStateFire:
		m_cur_x_rot = angle_inertion_var(m_cur_x_rot, m_tgt_x_rot, m_rotate_x_speed, m_rotate_x_speed, PI, Device.fTimeDelta);
		m_cur_y_rot = angle_inertion_var(m_cur_y_rot, m_tgt_y_rot, m_rotate_y_speed, m_rotate_y_speed, PI, Device.fTimeDelta);
		break;
	case eStateReload:
		m_cur_x_rot = m_bind_x_rot;
		break;
	default:
		break;
	}
#else
	XFi.transform_dir(dep, m_destEnemyDir);
	{
		// x angle
		m_i_bind_x_xform.transform_dir(dep);
		dep.normalize();
		m_tgt_x_rot = angle_normalize_signed(m_bind_x_rot - dep.getP());
		float sv_x = m_tgt_x_rot;

		clamp(m_tgt_x_rot, -m_lim_x_rot.y, -m_lim_x_rot.x);
		if (!fsimilar(sv_x, m_tgt_x_rot, EPS_L)) m_allow_fire = FALSE;
	}
	{
		// y angle
		m_i_bind_y_xform.transform_dir(dep);
		dep.normalize();
		m_tgt_y_rot = angle_normalize_signed(m_bind_y_rot - dep.getH());
		float sv_y = m_tgt_y_rot;
		clamp(m_tgt_y_rot, -m_lim_y_rot.y, -m_lim_y_rot.x);
		if (!fsimilar(sv_y, m_tgt_y_rot, EPS_L)) m_allow_fire = FALSE;
	}

	m_cur_x_rot = angle_inertion_var(m_cur_x_rot, m_tgt_x_rot, 0.5f, 3.5f, PI_DIV_6, Device.fTimeDelta);
	m_cur_y_rot = angle_inertion_var(m_cur_y_rot, m_tgt_y_rot, 0.5f, 3.5f, PI_DIV_6, Device.fTimeDelta);
#endif
}

void CWeaponStatMgun::cam_Update(float dt, float fov)
{
#ifdef STATIONARYMGUN_NEW
	Fvector P;
	Fvector D;
	D.set(0, 0, 0);
	CCameraBase *cam = Camera();

	switch (cam->tag)
	{
	case eCamFirst:
	{
		u16 bone_id = m_camera_bone;
		if (m_camera_bone_def != BI_NONE)
		{
			bone_id = (IsCameraZoom() && (m_camera_bone_aim != BI_NONE)) ? m_camera_bone_aim : m_camera_bone_def;
		}
		Fmatrix xfm = Visual()->dcast_PKinematics()->LL_GetTransform(bone_id);
		XFORM().transform_tiny(P, xfm.c);
		OwnerActor()->Orientation().yaw = -cam->yaw;
		OwnerActor()->Orientation().pitch = -cam->pitch;
	}
	break;
	case eCamChase:
	{
		XFORM().transform_tiny(P, m_camera_position);
	}
	break;
	}

	float zoom_factor = (IsCameraZoom()) ? m_zoom_factor_aim : m_zoom_factor_def;
	cam->f_fov = fov / zoom_factor;
	cam->Update(P, D);
	Level().Cameras().UpdateFromCamera(cam);
#else
	camera->f_fov = g_fov;

	Fvector P, Da;
	Da.set(0, 0, 0);

	IKinematics* K = smart_cast<IKinematics*>(Visual());
	K->CalculateBones_Invalidate();
	K->CalculateBones(TRUE);
	const Fmatrix& C = K->LL_GetTransform(m_camera_bone);
	XFORM().transform_tiny(P, C.c);

	Fvector d = C.k;
	XFORM().transform_dir(d);
	Fvector2 des_cam_dir;

	d.getHP(des_cam_dir.x, des_cam_dir.y);
	des_cam_dir.mul(-1.0f);


	Camera()->yaw = angle_inertion_var(Camera()->yaw, des_cam_dir.x, 0.5f, 7.5f, PI_DIV_6, Device.fTimeDelta);
	Camera()->pitch = angle_inertion_var(Camera()->pitch, des_cam_dir.y, 0.5f, 7.5f, PI_DIV_6, Device.fTimeDelta);


	if (OwnerActor())
	{
		// rotate head
		OwnerActor()->Orientation().yaw = -Camera()->yaw;
		OwnerActor()->Orientation().pitch = -Camera()->pitch;
	}


	Camera()->Update(P, Da);
	Level().Cameras().UpdateFromCamera(Camera());
#endif
}

void CWeaponStatMgun::renderable_Render()
{
	inheritedPH::renderable_Render();

	RenderLight();

#ifdef STATIONARYMGUN_NEW
	if (m_barrels.size())
	{
		for (auto &B : m_barrels)
		{
			B.RenderLight();
		}
	}
#endif
}

void CWeaponStatMgun::SetDesiredDir(float h, float p)
{
	m_destEnemyDir.setHP(h, p);
}

void CWeaponStatMgun::ClampRotationHorz(float &tgt_val, const float &cur_val, const float &lim_min, const float &lim_max)
{
	/* Rotating limit must be lesser than 180 in both direction. */
	if (abs(lim_min) < PI || abs(lim_max) < PI)
	{
		/* Target is outside of rotating limit. Clamp to the closest limit. */
		if (tgt_val < lim_min || tgt_val > lim_max)
		{
			if (angle_difference(tgt_val, lim_min) < angle_difference(tgt_val, lim_max))
				tgt_val = lim_min;
			else
				tgt_val = lim_max;
		}
		/* If the rotation to reach tgt_val from cur_val crosses 180 degrees in the back, make it swings around 0. */
		if (abs(tgt_val - cur_val) >= PI)
		{
			tgt_val = 0.0F;
		}
	}
}

void CWeaponStatMgun::Action(u16 id, u32 flags)
{
	inheritedHolder::Action(id, flags);
	switch (id)
	{
#ifdef STATIONARYMGUN_NEW
	case eWpnFire:
		if (flags == 1 && GetState() == eStateIdle)
		{
			SwitchState(eStateFire);
		}
		if (flags != 1 && GetState() == eStateFire)
		{
			SwitchState(eStateIdle);
		}
		break;
	case eWpnActivate:
		if (flags == 1 && m_bActive != true)
		{
			m_bActive = true;
			SwitchState(eStateIdle);
			SetBoneCallbacks();
		}
		if (flags != 1 && m_bActive == true)
		{
			m_bActive = false;
			SwitchState(eStateIdle);
			ResetBoneCallbacks();
		}
		break;
	case eWpnReload:
		if (GetState() == eStateIdle || GetState() == eStateFire)
		{
			SwitchState(eStateReload);
		}
		break;
#else
	case kWPN_FIRE:
		{
			if (flags == CMD_START) FireStart();
			else FireEnd();
		}
		break;
#endif
	}
}

void CWeaponStatMgun::SetParam(int id, Fvector2 val)
{
	inheritedHolder::SetParam(id, val);
	switch (id)
	{
	case DESIRED_DIR:
		SetDesiredDir(val.x, val.y);
		break;
	}
}

#ifdef STATIONARYMGUN_NEW
void CWeaponStatMgun::SetParam(int id, Fvector val)
{
	inheritedHolder::SetParam(id, val);
	switch (id)
	{
	case eWpnDesiredPos:
		Fvector vec = Fmatrix().mul_43(XFORM(), Visual()->dcast_PKinematics()->LL_GetTransform(m_rotate_y_bone)).c;
		m_destEnemyDir.sub(val, vec).normalize_safe();
		m_desire_angle_enable = false;
		break;
	case eWpnDesiredDir:
		m_destEnemyDir.set(val).normalize_safe();
		m_desire_angle_enable = false;
		break;
	case eWpnDesiredAng:
		m_desire_angle.set(val.x, val.y);
		m_desire_angle_enable = true;
		break;
	}
}
#endif

bool CWeaponStatMgun::attach_Actor(CGameObject* actor)
{
#ifdef STATIONARYMGUN_NEW
	if (Owner())
		return false;

	if (!actor)
		return false;

	inheritedHolder::attach_Actor(actor);
	Action(eWpnActivate, 1);
	SetFeelVisionIgnore(true);

	if (OwnerActor())
	{
		switch (OwnerActor()->active_cam())
		{
		case eacFirstEye:
			OnCameraChange(eCamFirst);
			break;
		case eacLookAt:
			OnCameraChange(eCamChase);
			break;
		default:
			OnCameraChange(eCamFirst);
			break;
		}
		Camera()->yaw = m_cur_y_rot;
		Camera()->pitch = m_cur_x_rot;
	}

	m_anim_weapon.Play(SStmAnimWeapon::eStmAnimWeapon_idle);
	return true;
#else
	if (Owner())
		return false;
	actor->setVisible(0);
	inheritedHolder::attach_Actor(actor);
	SetBoneCallbacks();
	FireEnd();
	return true;
#endif
}

void CWeaponStatMgun::detach_Actor()
{
#ifdef STATIONARYMGUN_NEW
	if (!Owner())
		return;

	if (OwnerActor())
	{
		OwnerActor()->setVisible(TRUE);
	}
	inheritedHolder::detach_Actor();
	Action(eWpnActivate, 0);
	SetFeelVisionIgnore(false);
	m_anim_weapon.Play(SStmAnimWeapon::eStmAnimWeapon_idle);
	m_anim_weapon.HandRemove();
#else
	Owner()->setVisible(1);
	inheritedHolder::detach_Actor();
	ResetBoneCallbacks();
	FireEnd();
#endif
}

Fvector CWeaponStatMgun::ExitPosition()
{
#ifdef STATIONARYMGUN_NEW
	Fvector vec;
	XFORM().transform_tiny(vec, m_exit_position);
	return vec;
#else
	Fvector pos; pos.set(0.f, 0.f, 0.f);
	pos.sub(camera->Direction()).normalize();
	pos.y = 0.f;
	return Fvector(XFORM().c).add(pos);
#endif
}

#ifdef STATIONARYMGUN_NEW
void CWeaponStatMgun::SpawnInitPhysics(CSE_Abstract *D)
{
	CSE_PHSkeleton *so = smart_cast<CSE_PHSkeleton *>(D);
	R_ASSERT(so);
	IKinematics *K = Visual()->dcast_PKinematics();
	CreateSkeleton(D);
	K->CalculateBones_Invalidate();
	K->CalculateBones(TRUE);
	CPHUpdateObject::Activate();
	PPhysicsShell()->applyImpulse(Fvector().set(0.0F, -1.0F, 0.0F), 0.1F);
}

void CWeaponStatMgun::CreateSkeleton(CSE_Abstract *po)
{
	if (!Visual())
		return;
	IKinematics *K = Visual()->dcast_PKinematics();
	phys_shell_verify_object_model(*this);
	U16Vec fixed_bones;
	fixed_bones.push_back(K->LL_GetBoneRoot());
	m_pPhysicsShell = P_build_Shell(this, false, fixed_bones);
	m_pPhysicsShell->SetPrefereExactIntegration();
	ApplySpawnIniToPhysicShell(&po->spawn_ini(), m_pPhysicsShell, fixed_bones.size() > 0);
	ApplySpawnIniToPhysicShell(K->LL_UserData(), m_pPhysicsShell, fixed_bones.size() > 0);
}

void CWeaponStatMgun::shedule_Update(u32 dt)
{
	inherited::shedule_Update(dt);
	CPHSkeleton::Update(dt);
}

void CWeaponStatMgun::net_Save(NET_Packet &P)
{
	inherited::net_Save(P);
	CPHSkeleton::SaveNetState(P);
	P.w_vec3(Position());
	Fvector ang;
	XFORM().getXYZ(ang);
	P.w_vec3(ang);
}

BOOL CWeaponStatMgun::net_SaveRelevant()
{
	return TRUE;
}

BOOL CWeaponStatMgun::AlwaysTheCrow()
{
	return TRUE;
}

bool CWeaponStatMgun::is_ai_obstacle() const
{
	return true;
}

void CWeaponStatMgun::IgnoreOwnerCallback(bool &do_colide, bool bo1, dContact &c, SGameMtl *mt1, SGameMtl *mt2)
{
	if (do_colide == false)
		return;

	dxGeomUserData *gd1 = bo1 ? PHRetrieveGeomUserData(c.geom.g1) : PHRetrieveGeomUserData(c.geom.g2);
	dxGeomUserData *gd2 = bo1 ? PHRetrieveGeomUserData(c.geom.g2) : PHRetrieveGeomUserData(c.geom.g1);
	CGameObject *obj = (gd1) ? smart_cast<CGameObject *>(gd1->ph_ref_object) : nullptr;
	CGameObject *who = (gd2) ? smart_cast<CGameObject *>(gd2->ph_ref_object) : nullptr;
	if (!obj || !who)
	{
		return;
	}

	CWeaponStatMgun *stm = smart_cast<CWeaponStatMgun *>(obj);
	CGameObject *owner = (stm) ? stm->Owner() : nullptr;
	if (owner && (owner->ID() == who->ID()))
	{
		do_colide = false;
	}
}

bool CWeaponStatMgun::Use(const Fvector &pos, const Fvector &dir, const Fvector &foot_pos)
{
	if (Owner())
		return false;

	if (m_on_before_use_callback && strlen(m_on_before_use_callback))
	{
		::luabind::functor<bool> lua_function;
		if (ai().script_engine().functor(m_on_before_use_callback, lua_function))
		{
			if (!lua_function(lua_game_object(), pos, dir, foot_pos))
			{
				return false;
			}
		}
	}
	return true;
}

void CWeaponStatMgun::OnCameraChange(u16 type)
{
	if (active_camera == nullptr)
	{
		active_camera = camera[type];
		return;
	}

	if (active_camera->tag != type)
	{
		CCameraBase *cam = camera[type];
		if (active_camera->tag == eCamFirst)
		{
			Fvector ang;
			XFORM().getHPB(ang);
			cam->pitch = active_camera->pitch - ang.y;
			cam->yaw = active_camera->yaw - ang.x;
		}
		else
		{
			Fvector ang;
			XFORM().getHPB(ang);
			cam->pitch = active_camera->pitch + ang.y;
			cam->yaw = active_camera->yaw + ang.x;
		}
		active_camera = camera[type];
	}

	if (OwnerActor())
	{
		if (Camera()->tag == eCamFirst)
		{
			OwnerActor()->setVisible(FALSE);
			m_anim_weapon.HandCreate();
		}
		else
		{
			OwnerActor()->setVisible(TRUE);
			m_anim_weapon.HandRemove();
		}
	}
}

void CWeaponStatMgun::UpdateCamera()
{
	PPhysicsShell()->InterpolateGlobalTransform(&XFORM());
	Visual()->dcast_PKinematics()->CalculateBones();

	cam_Update(Device.fTimeDelta, g_fov);
	OwnerActor()->Cameras().UpdateFromCamera(Camera());
	OwnerActor()->Cameras().ApplyDevice(R_VIEWPORT_NEAR);

	if (IsCameraZoom())
	{
		SetParam(eWpnDesiredDir, Camera()->Direction());
	}
	else
	{
		Fvector pos = Camera()->Position();
		Fvector dir = Camera()->Direction();
		collide::rq_result &R = HUD().GetRQ();
		Fvector vec = Fvector().mad(pos, dir, std::max(R.range, 30.0F));
		SetParam(eWpnDesiredDir, Fvector().sub(vec, pos).normalize());
	}
}

bool CWeaponStatMgun::IsCameraZoom()
{
	switch (GetState())
	{
	case eStateIdle:
	case eStateFire:
		return m_zoom_status;
	case eStateReload:
		break;
	default:
		break;
	}
	return false;
}

void CWeaponStatMgun::UpdateSound()
{
	if (IsActive() && (GetState() == eStateIdle || GetState() == eStateFire))
	{
		m_sound_mgr.RotatePlay(true);
		bool play = false;
		if (m_sound_mgr.m_rotating_x && !fsimilar(m_cur_x_rot, m_tgt_x_rot, EPS_L))
			play = true;
		if (m_sound_mgr.m_rotating_y && !fsimilar(m_cur_y_rot, m_tgt_y_rot, EPS_L))
			play = true;
		m_sound_mgr.RotateVolume((play) ? 1.0F : 0.0F);
	}
	else
	{
		m_sound_mgr.RotatePlay(false);
	}
	m_sound_mgr.Update();
}

void CWeaponStatMgun::SetFeelVisionIgnore(bool enable)
{
#ifdef SPATIAL_CHANGE
	ISpatial *IS = smart_cast<ISpatial *>(this);
	R_ASSERT(IS);
	if (enable)
		IS->spatial.type |= STYPE_FEELVISIONIGNORE;
	else
		IS->spatial.type &= ~STYPE_FEELVISIONIGNORE;
#endif
}

void CWeaponStatMgun::UpdateAnimation()
{
	if (Owner() == nullptr)
		return;

	IKinematicsAnimated *A = Owner()->Visual()->dcast_PKinematicsAnimated();
	VERIFY3(A, cName().c_str(), Owner()->cName().c_str());

	if (Owner()->cast_physics_shell_holder()->character_physics_support())
	{
		CPHMovementControl *mvm = Owner()->cast_physics_shell_holder()->character_physics_support()->movement();
		mvm->SetPosition(Owner()->Position());
		mvm->SetVelocity(Fvector().set(0.0F, 0.0F, 0.0F));
	}

	LPCSTR anim_body = m_animation.GetAnimation(SStmAnimActor::eAnimBody);
	LPCSTR anim_legs = m_animation.GetAnimation(SStmAnimActor::eAnimLegs);

	if (OwnerActor())
	{
		if (anim_body && strlen(anim_body))
		{
			MotionID mid_body = A->ID_Cycle(anim_body);
			if (mid_body.idx != OwnerActor()->m_current_torso.idx)
			{
				// Msg("%s:%d %s old:%d new:%d", __FUNCTION__, __LINE__, Owner()->cNameSect_str(), OwnerActor()->m_current_torso.idx, mid_body.idx);
				A->PlayCycle(mid_body);
				OwnerActor()->m_current_torso = mid_body;
				OwnerActor()->CStepManager::on_animation_start(MotionID(), nullptr);
			}
		}

		if (anim_legs && strlen(anim_legs))
		{
			MotionID mid_legs = A->ID_Cycle(anim_legs);
			if (mid_legs.idx != OwnerActor()->m_current_legs.idx)
			{
				// Msg("%s:%d %s old:%d new:%d", __FUNCTION__, __LINE__, Owner()->cNameSect_str(), OwnerActor()->m_current_legs.idx, mid_legs.idx);
				A->PlayCycle(mid_legs);
				OwnerActor()->m_current_legs = mid_legs;
				OwnerActor()->CStepManager::on_animation_start(MotionID(), nullptr);
			}
		}
	}

	CAI_Stalker *stalker = Owner()->cast_stalker();
	if (stalker)
	{
		if (stalker->animation_movement_controlled())
		{
			stalker->destroy_anim_mov_ctrl();
		}

		if (anim_body && strlen(anim_body))
		{
			MotionID mid_body = A->ID_Cycle(anim_body);
			if (mid_body.idx != stalker->animation().torso().animation().idx)
			{
				// Msg("%s:%d %s BF: old:%d new:%d", __FUNCTION__, __LINE__, Owner()->cNameSect_str(), stalker->animation().torso().animation().idx, mid_body.idx);
				A->PlayCycle(mid_body);
				stalker->animation().torso().animation(mid_body);
				stalker->CStepManager::on_animation_start(MotionID(), nullptr);
			}
		}
		if (anim_legs && strlen(anim_legs))
		{
			MotionID mid_legs = A->ID_Cycle(anim_legs);
			if (mid_legs.idx != stalker->animation().legs().animation().idx)
			{
				// Msg("%s:%d BF: %s old:%d new:%d", __FUNCTION__, __LINE__, Owner()->cNameSect_str(), stalker->animation().legs().animation().idx, mid_legs.idx);
				A->PlayCycle(mid_legs);
				stalker->animation().legs().animation(mid_legs);
				stalker->CStepManager::on_animation_start(MotionID(), nullptr);
			}
		}
		stalker->movement().set_desired_direction(0);
#if 0
		SBoneRotation &body = stalker->movement().m_body;
		SBoneRotation &head = stalker->movement().m_head;
		body.target.yaw = 0.0F;
		body.target.pitch = 0.0F;
		head.target.yaw = 0.0F;
		head.target.pitch = 0.0F;
#endif
	}
}

Fvector2 CWeaponStatMgun::GetTraverseLimitHorz()
{
	return Fvector2().set(-m_lim_y_rot.y, -m_lim_y_rot.x);
}

void CWeaponStatMgun::SetTraverseLimitHorz(Fvector2 vec)
{
	m_lim_y_rot.set(std::min(deg2rad(-vec.y), 0.0F), std::max(deg2rad(-vec.x), 0.0F));
	clamp(m_lim_y_rot.x, -PI, PI);
	clamp(m_lim_y_rot.y, -PI, PI);
}

Fvector2 CWeaponStatMgun::GetTraverseLimitVert()
{
	return Fvector2().set(-m_lim_x_rot.y, -m_lim_x_rot.x);
}

void CWeaponStatMgun::SetTraverseLimitVert(Fvector2 vec)
{
	m_lim_x_rot.set(std::min(deg2rad(-vec.y), 0.0F), std::max(deg2rad(-vec.x), 0.0F));
	clamp(m_lim_x_rot.x, -PI_DIV_2, PI_DIV_2);
	clamp(m_lim_x_rot.y, -PI_DIV_2, PI_DIV_2);
}

/*----------------------------------------------------------------------------------------------------
	SStmAnimActor
----------------------------------------------------------------------------------------------------*/
CWeaponStatMgun::SStmAnimActor::SStmAnimActor()
{
	SetAnimation(eAnimBody, "norm_torso_m134_aim_0");
	SetAnimation(eAnimLegs, "norm_idle_m134");
}

LPCSTR CWeaponStatMgun::SStmAnimActor::GetAnimation(u8 id)
{
	if (id < eAnimSize)
	{
		return m_idle[id].c_str();
	}
	return nullptr;
}

void CWeaponStatMgun::SStmAnimActor::SetAnimation(u8 id, LPCSTR anim)
{
	if (id < eAnimSize && anim)
	{
		m_idle[id]._set(anim);
	}
}

/*----------------------------------------------------------------------------------------------------
	SStmSound
----------------------------------------------------------------------------------------------------*/
CWeaponStatMgun::SStmSound::SStmSound()
{
	m_volume = 1.0f;
	m_rotate_bid = BI_NONE;
	m_rotating_x = false;
	m_rotating_y = false;
}

CWeaponStatMgun::SStmSound::~SStmSound()
{
	m_rotate_snd.stop();
	m_rotate_snd.destroy();
}

void CWeaponStatMgun::SStmSound::Init(CWeaponStatMgun *stm)
{
	R_ASSERT(stm);
	m_stm = stm;
}

BOOL CWeaponStatMgun::SStmSound::net_Spawn(CSE_Abstract *DC)
{
	IKinematics *K = m_stm->Visual()->dcast_PKinematics();
	CInifile *ini = K->LL_UserData();
	const LPCSTR snd = "sounds";
	if (ini->section_exist(snd))
	{
		m_volume = READ_IF_EXISTS(ini, r_float, snd, "volume", 1.0f);
		if (ini->line_exist(snd, "rotate_snd"))
		{
			m_rotate_snd.create(ini->r_string(snd, "rotate_snd"), st_Effect, sg_SourceType);
		}
		m_rotate_bid = ini->line_exist(snd, "rotate_bid") ? K->LL_BoneID(ini->r_string(snd, "rotate_bid")) : BI_NONE;
		m_rotating_x = !!READ_IF_EXISTS(ini, r_bool, snd, "rotating_x", FALSE);
		m_rotating_y = !!READ_IF_EXISTS(ini, r_bool, snd, "rotating_y", FALSE);
	}
	return TRUE;
}

void CWeaponStatMgun::SStmSound::Update()
{
	if (m_rotate_snd._feedback() && (m_rotate_bid != BI_NONE))
	{
		Fvector pos = Fmatrix().mul_43(m_stm->XFORM(), m_stm->Visual()->dcast_PKinematics()->LL_GetTransform(m_rotate_bid)).c;
		m_rotate_snd.set_position(pos);
	}
}

void CWeaponStatMgun::SStmSound::RotatePlay(bool state)
{
	if (state)
	{
		if (m_rotate_bid == BI_NONE)
			return;
		Fvector pos = Fmatrix().mul_43(m_stm->XFORM(), m_stm->Visual()->dcast_PKinematics()->LL_GetTransform(m_rotate_bid)).c;
		if (m_rotate_snd._feedback() == nullptr)
		{
			m_rotate_snd.play_at_pos(m_stm->cast_game_object(), pos, sm_Looped);
		}
	}
	else
	{
		m_rotate_snd.stop();
	}
}

void CWeaponStatMgun::SStmSound::RotateVolume(float vol)
{
	m_rotate_snd.set_volume(vol);
}
#endif

