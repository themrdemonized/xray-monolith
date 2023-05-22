#include "pch_script.h"
#include "WeaponAK74.h"
#include "Weapon.h"
#include "WeaponMagazined.h"
#include "WeaponMagazinedWGrenade.h"

CWeaponAK74::CWeaponAK74(ESoundTypes eSoundType) : CWeaponMagazinedWGrenade(eSoundType)
{}

CWeaponAK74::~CWeaponAK74()
{}

using namespace luabind;

#pragma optimize("s",on)
void CWeaponAK74::script_register	(lua_State *L)
{
	module(L)
	[
		class_<CWeaponAK74,CGameObject>("CWeaponAK74")
			.def(constructor<>()),
			
		class_<CWeapon,CGameObject>("CWeapon")
			.def(constructor<>())
			.def("can_kill", (bool (CWeapon::*)() const)&CWeapon::can_kill)
			
			.def("IsGrenadeLauncherAttached", &CWeapon::IsGrenadeLauncherAttached)
			.def("GrenadeLauncherAttachable", &CWeapon::GrenadeLauncherAttachable)
			.def("GetGrenadeLauncherName", &CWeapon::GetGrenadeLauncherNameScript)
			
			.def("IsScopeAttached", &CWeapon::IsScopeAttached)
			.def("ScopeAttachable", &CWeapon::ScopeAttachable)
			.def("GetScopeName", &CWeapon::GetScopeNameScript)
			
			.def("IsSilencerAttached", &CWeapon::IsSilencerAttached)
			.def("SilencerAttachable", &CWeapon::SilencerAttachable)
			.def("GetSilencerName", &CWeapon::GetSilencerNameScript)
			
			.def("IsZoomEnabled", &CWeapon::IsZoomEnabled)
			.def("IsZoomed", &CWeapon::IsZoomed)
			.def("GetZoomFactor", &CWeapon::GetZoomFactor)
			.def("SetZoomFactor", &CWeapon::SetZoomFactor)
			
			.def("IsSingleHanded", &CWeapon::IsSingleHanded)
			
			.def("GetBaseDispersion", &CWeapon::GetBaseDispersion)
			.def("GetFireDispersion", &CWeapon::GetFireDispersionScript)
			
			.def("GetMisfireStartCondition", &CWeapon::GetMisfireStartCondition)
			.def("GetMisfireEndCondition", &CWeapon::GetMisfireEndCondition)
			
			.def("GetAmmoElapsed", &CWeapon::GetAmmoElapsed)
			.def("GetAmmoMagSize", &CWeapon::GetAmmoMagSize)
			.def("GetSuitableAmmoTotal", &CWeapon::GetSuitableAmmoTotal)
			.def("SetAmmoElapsed", &CWeapon::SetAmmoElapsed)
			.def("SwitchAmmoType", &CWeapon::SwitchAmmoType)
			.def("GetMagazineWeight", &CWeapon::GetMagazineWeightScript)
			.def("GetAmmoCount_forType", &CWeapon::GetAmmoCount_forType_Script)
			.def("set_ef_main_weapon_type", &CWeapon::set_ef_main_weapon_type)
			.def("set_ef_weapon_type", &CWeapon::set_ef_weapon_type)
			.def("SetAmmoType", &CWeapon::SetAmmoType)
			.def("GetAmmoType", &CWeapon::GetAmmoType)
			.def("AmmoTypeForEach", &CWeapon::AmmoTypeForEach)
			.def("RPM", &CWeapon::RPMScript)
			.def("RealRPM", &CWeapon::RealRPMScript)
			.def("ModeRPM", &CWeapon::ModeRPMScript)
			.def("ModeRealRPM", &CWeapon::ModeRealRPMScript)
			.def("GetZoomType", &CWeapon::GetZoomType)
			
			.def("Get_PDM_Base", &CWeapon::Get_PDM_Base)
			.def("Get_Silencer_PDM_Base", &CWeapon::Get_Silencer_PDM_Base)
			.def("Get_Scope_PDM_Base", &CWeapon::Get_Scope_PDM_Base)
			.def("Get_Launcher_PDM_Base", &CWeapon::Get_Launcher_PDM_Base)
			.def("Get_PDM_BuckShot", &CWeapon::Get_PDM_BuckShot)
			.def("Get_PDM_Vel_F", &CWeapon::Get_PDM_Vel_F)
			.def("Get_Silencer_PDM_Vel", &CWeapon::Get_Silencer_PDM_Vel)
			.def("Get_Scope_PDM_Vel", &CWeapon::Get_Scope_PDM_Vel)
			.def("Get_Launcher_PDM_Vel", &CWeapon::Get_Launcher_PDM_Vel)
			.def("Get_PDM_Accel_F", &CWeapon::Get_PDM_Accel_F)
			.def("Get_Silencer_PDM_Accel", &CWeapon::Get_Silencer_PDM_Accel)
			.def("Get_Scope_PDM_Accel", &CWeapon::Get_Scope_PDM_Accel)
			.def("Get_Launcher_PDM_Accel", &CWeapon::Get_Launcher_PDM_Accel)
			.def("Get_PDM_Crouch", &CWeapon::Get_PDM_Crouch)
			.def("Get_PDM_Crouch_NA", &CWeapon::Get_PDM_Crouch_NA)
			.def("GetCrosshairInertion", &CWeapon::GetCrosshairInertion)
			.def("Get_Silencer_CrosshairInertion", &CWeapon::Get_Silencer_CrosshairInertion)
			.def("Get_Scope_CrosshairInertion", &CWeapon::Get_Scope_CrosshairInertion)
			.def("Get_Launcher_CrosshairInertion", &CWeapon::Get_Launcher_CrosshairInertion)
			
			.def("GetFirstBulletDisp", &CWeapon::GetFirstBulletDisp)
			.def("GetHitPower", &CWeapon::GetHitPower)
			.def("GetHitPowerCritical", &CWeapon::GetHitPowerCritical)
			.def("GetHitImpulse", &CWeapon::GetHitImpulse)
			.def("GetFireDistance", &CWeapon::GetFireDistance)
			.def("GetFireMode", &CWeapon::GetCurrentFireMode)

			.def("GetInertionAimFactor", &CWeapon::GetInertionAimFactor)

			// Setters
			.def("SetFireDispersion", &CWeapon::SetFireDispersionScript)
			.def("SetMisfireStartCondition", &CWeapon::SetMisfireStartCondition)
			.def("SetMisfireEndCondition", &CWeapon::SetMisfireEndCondition)
			.def("SetRPM", &CWeapon::SetRPM)
			.def("SetRealRPM", &CWeapon::SetRealRPM)
			.def("SetModeRPM", &CWeapon::SetModeRPM)
			.def("SetModeRealRPM", &CWeapon::SetModeRealRPM)
			.def("Set_PDM_Base", &CWeapon::Set_PDM_Base)
			.def("Set_Silencer_PDM_Base", &CWeapon::Set_Silencer_PDM_Base)
			.def("Set_Scope_PDM_Base", &CWeapon::Set_Scope_PDM_Base)
			.def("Set_Launcher_PDM_Base", &CWeapon::Set_Launcher_PDM_Base)
			.def("Set_PDM_BuckShot", &CWeapon::Set_PDM_BuckShot)
			.def("Set_PDM_Vel_F", &CWeapon::Set_PDM_Vel_F)
			.def("Set_Silencer_PDM_Vel", &CWeapon::Set_Silencer_PDM_Vel)
			.def("Set_Scope_PDM_Vel", &CWeapon::Set_Scope_PDM_Vel)
			.def("Set_Launcher_PDM_Vel", &CWeapon::Set_Launcher_PDM_Vel)
			.def("Set_PDM_Accel_F", &CWeapon::Set_PDM_Accel_F)
			.def("Set_Silencer_PDM_Accel", &CWeapon::Set_Silencer_PDM_Accel)
			.def("Set_Scope_PDM_Accel", &CWeapon::Set_Scope_PDM_Accel)
			.def("Set_Launcher_PDM_Accel", &CWeapon::Set_Launcher_PDM_Accel)
			.def("Set_PDM_Crouch", &CWeapon::Set_PDM_Crouch)
			.def("Set_PDM_Crouch_NA", &CWeapon::Set_PDM_Crouch_NA)
			.def("SetCrosshairInertion", &CWeapon::SetCrosshairInertion)
			.def("Set_Silencer_CrosshairInertion", &CWeapon::Set_Silencer_CrosshairInertion)
			.def("Set_Scope_CrosshairInertion", &CWeapon::Set_Scope_CrosshairInertion)
			.def("Set_Launcher_CrosshairInertion", &CWeapon::Set_Launcher_CrosshairInertion)
			.def("SetFirstBulletDisp", &CWeapon::SetFirstBulletDisp)
			.def("SetHitPower", &CWeapon::SetHitPower)
			.def("SetHitPowerCritical", &CWeapon::SetHitPowerCritical)
			.def("SetHitImpulse", &CWeapon::SetHitImpulse)
			.def("SetFireDistance", &CWeapon::SetFireDistance)

			// demonized: World model on stalkers adjustments
			.def("Set_mOffset", &CWeapon::set_mOffset)
			.def("Set_mStrapOffset", &CWeapon::set_mStrapOffset)
			.def("Set_mFirePoint", &CWeapon::set_mFirePoint)
			.def("Set_mFirePoint2", &CWeapon::set_mFirePoint2)
			.def("Set_mShellPoint", &CWeapon::set_mShellPoint)

			// Cam Recoil
			// Getters
			.def("GetCamRelaxSpeed", &CWeapon::GetCamRelaxSpeed)
			.def("GetCamRelaxSpeed_AI", &CWeapon::GetCamRelaxSpeed_AI)
			.def("GetCamDispersion", &CWeapon::GetCamDispersion)
			.def("GetCamDispersionInc", &CWeapon::GetCamDispersionInc)
			.def("GetCamDispersionFrac", &CWeapon::GetCamDispersionFrac)
			.def("GetCamMaxAngleVert", &CWeapon::GetCamMaxAngleVert)
			.def("GetCamMaxAngleHorz", &CWeapon::GetCamMaxAngleHorz)
			.def("GetCamStepAngleHorz", &CWeapon::GetCamStepAngleHorz)
			.def("GetZoomCamRelaxSpeed", &CWeapon::GetZoomCamRelaxSpeed)
			.def("GetZoomCamRelaxSpeed_AI", &CWeapon::GetZoomCamRelaxSpeed_AI)
			.def("GetZoomCamDispersion", &CWeapon::GetZoomCamDispersion)
			.def("GetZoomCamDispersionInc", &CWeapon::GetZoomCamDispersionInc)
			.def("GetZoomCamDispersionFrac", &CWeapon::GetZoomCamDispersionFrac)
			.def("GetZoomCamMaxAngleVert", &CWeapon::GetZoomCamMaxAngleVert)
			.def("GetZoomCamMaxAngleHorz", &CWeapon::GetZoomCamMaxAngleHorz)
			.def("GetZoomCamStepAngleHorz", &CWeapon::GetZoomCamStepAngleHorz)

			// Setters
			.def("SetCamRelaxSpeed", &CWeapon::SetCamRelaxSpeed)
			.def("SetCamRelaxSpeed_AI", &CWeapon::SetCamRelaxSpeed_AI)
			.def("SetCamDispersion", &CWeapon::SetCamDispersion)
			.def("SetCamDispersionInc", &CWeapon::SetCamDispersionInc)
			.def("SetCamDispersionFrac", &CWeapon::SetCamDispersionFrac)
			.def("SetCamMaxAngleVert", &CWeapon::SetCamMaxAngleVert)
			.def("SetCamMaxAngleHorz", &CWeapon::SetCamMaxAngleHorz)
			.def("SetCamStepAngleHorz", &CWeapon::SetCamStepAngleHorz)
			.def("SetZoomCamRelaxSpeed", &CWeapon::SetZoomCamRelaxSpeed)
			.def("SetZoomCamRelaxSpeed_AI", &CWeapon::SetZoomCamRelaxSpeed_AI)
			.def("SetZoomCamDispersion", &CWeapon::SetZoomCamDispersion)
			.def("SetZoomCamDispersionInc", &CWeapon::SetZoomCamDispersionInc)
			.def("SetZoomCamDispersionFrac", &CWeapon::SetZoomCamDispersionFrac)
			.def("SetZoomCamMaxAngleVert", &CWeapon::SetZoomCamMaxAngleVert)
			.def("SetZoomCamMaxAngleHorz", &CWeapon::SetZoomCamMaxAngleHorz)
			.def("SetZoomCamStepAngleHorz", &CWeapon::SetZoomCamStepAngleHorz)
			
			.def("Cost", &CWeapon::Cost)
			.def("Weight", &CWeapon::Weight)

			.def("IsMisfire", &CWeapon::IsMisfire)
			.def("SetMisfire", &CWeapon::SetMisfireScript)

			.def("IsPending", &CWeapon::IsPending)
			.def("SetPending", &CWeapon::SetPending)

			.enum_("EWeaponStates")
			[
				value("eFire", int(EWeaponStates::eFire)),
				value("eFire2", int(EWeaponStates::eFire2)),
				value("eReload", int(EWeaponStates::eReload)),
				value("eMisfire", int(EWeaponStates::eMisfire)),
				value("eSwitch", int(EWeaponStates::eSwitch)),
				value("eSwitchMode", int(EWeaponStates::eSwitchMode))
			]
			.enum_("EWeaponSubStates")
			[
				value("eSubstateReloadBegin", int(EWeaponSubStates::eSubstateReloadBegin)),
				value("eSubstateReloadInProcess", int(EWeaponSubStates::eSubstateReloadInProcess)),
				value("eSubstateReloadEnd", int(EWeaponSubStates::eSubstateReloadEnd))
			],
			
		class_<CWeaponMagazined,CWeapon>("CWeaponMagazined")
			.def(constructor<>())
			.def("SetFireMode", &CWeaponMagazined::SetFireMode),
			
		class_<CWeaponMagazinedWGrenade,CWeaponMagazined>("CWeaponMagazinedWGrenade")
			.def(constructor<>())
			.def("GetGrenadeLauncherMode", &CWeaponMagazinedWGrenade::GetGrenadeLauncherMode)
			.def("SetGrenadeLauncherMode", &CWeaponMagazinedWGrenade::SetGrenadeLauncherMode)

			.def("SetAmmoElapsed2", &CWeaponMagazinedWGrenade::SetAmmoElapsed2)
			.def("GetAmmoElapsed2", &CWeaponMagazinedWGrenade::GetAmmoElapsed2)
			.def("GetAmmoMagSize2", &CWeaponMagazinedWGrenade::GetAmmoMagSize2)
			.def("SetAmmoType2", &CWeaponMagazinedWGrenade::SetAmmoType2)
			.def("GetAmmoType2", &CWeaponMagazinedWGrenade::GetAmmoType2)
			.def("AmmoTypeForEach2", &CWeaponMagazinedWGrenade::AmmoTypeForEach2)
	];
}
