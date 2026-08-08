#include "pch_script.h"
#include "alife_space.h"
#include "Car.h"
#include "CarWeapon.h"
#include "script_game_object.h"

using namespace luabind;

#pragma optimize("s",on)
void CCar::script_register(lua_State* L)
{
	module(L)
	[
#ifdef CAR_NEW
		class_<CCar, bases<CGameObject, CHolderCustom, CExplosive>>("CCar")
#else
		class_<CCar, bases<CGameObject, CHolderCustom>>("CCar")
#endif
		.enum_("wpn_action")
		[
			value("eWpnDesiredDir", int(CCarWeapon::eWpnDesiredDir)),
			value("eWpnDesiredPos", int(CCarWeapon::eWpnDesiredPos)),
			value("eWpnActivate", int(CCarWeapon::eWpnActivate)),
			value("eWpnFire", int(CCarWeapon::eWpnFire)),
			value("eWpnAutoFire", int(CCarWeapon::eWpnAutoFire)),
			value("eWpnToDefaultDir", int(CCarWeapon::eWpnToDefaultDir))
		]
		.def("Action", &CCar::Action)
		//		.def("SetParam",		(void (CCar::*)(int,Fvector2)) &CCar::SetParam)
		.def("SetParam", (void (CCar::*)(int, Fvector))&CCar::SetParam)
		.def("CanHit", &CCar::WpnCanHit)
		.def("FireDirDiff", &CCar::FireDirDiff)
		.def("IsObjectVisible", &CCar::isObjectVisible)
		.def("HasWeapon", &CCar::HasWeapon)
		.def("CurrentVel", &CCar::CurrentVel)
		.def("GetSpeed", &CCar::GetSpeed)
		.def("SetTargetSpeed", &CCar::SetTargetSpeed)
		.def("ClearTargetSpeed", &CCar::ClearTargetSpeed)
		.def("SetThrottle", &CCar::SetThrottle)
		.def("SetSteer", &CCar::SetSteer)
		.def("GetWheelFriction", &CCar::GetWheelFriction)
		.def("SetWheelFriction", &CCar::SetWheelFriction)
		.def("TransmissionUp", &CCar::TransmissionUp)
		.def("TransmissionDown", &CCar::TransmissionDown)
		.def("CurrentTransmission", &CCar::CurrentTransmission)
		.def("Transmission", &CCar::Transmission)
		.def("RefWheelMaxSpeed", &CCar::RefWheelMaxSpeed)
		.def("EnginePower", &CCar::EnginePower)
		.def("EngineCurTorque", &CCar::EngineCurTorque)
		.def("DoorOpen", &CCar::DoorOpen)
		.def("DoorClose", &CCar::DoorClose)
		.def("DoorUse", &CCar::DoorUse)
		.def("DoorSwitch", &CCar::DoorSwitch)
		.def("IsDoor", (bool (CCar::*)(u16))&CCar::is_Door)
		.def("GetfHealth", &CCar::GetfHealth)
		.def("SetfHealth", &CCar::SetfHealth)
		.def("SetExplodeTime", &CCar::SetExplodeTime)
		.def("ExplodeTime", &CCar::ExplodeTime)
		.def("CarExplode", &CCar::CarExplode)
		/************************************************** added by Ray Twitty (aka Shadows) START **************************************************/
		.def("GetfFuel", &CCar::GetfFuel)
		.def("SetfFuel", &CCar::SetfFuel)
		.def("GetfFuelTank", &CCar::GetfFuelTank)
		.def("SetfFuelTank", &CCar::SetfFuelTank)
		.def("GetfFuelConsumption", &CCar::GetfFuelConsumption)
		.def("SetfFuelConsumption", &CCar::SetfFuelConsumption)
		.def("ChangefFuel", &CCar::ChangefFuel)
		.def("ChangefHealth", &CCar::ChangefHealth)
		.def("PlayDamageParticles", &CCar::PlayDamageParticles)
		.def("StopDamageParticles", &CCar::StopDamageParticles)
		.def("StartEngine", &CCar::StartEngine)
		.def("StopEngine", &CCar::StopEngine)
		.def("IsActiveEngine", &CCar::isActiveEngine)
		.def("HandBreak", &CCar::HandBreak)
		.def("ReleaseHandBreak", &CCar::ReleaseHandBreak)
		.def("GetRPM", &CCar::GetRPM)
		.def("SetRPM", &CCar::SetRPM)
		/*************************************************** added by Ray Twitty (aka Shadows) END ***************************************************/

#ifdef CAR_NEW
		.def("SetUseAction", &CCar::SetUseAction)
		.def("GetZoomFactor", &CCar::GetZoomFactor)
		.def("GetViewportNear", &CCar::GetViewportNear)
		.def("SetViewportNear", &CCar::SetViewportNear)
		.def("IsScopeEnable", &CCar::IsScopeEnable)
		.def("GetScopeSize", &CCar::GetScopeSize)
		.def("GetScopeActive", &CCar::GetScopeActive)
		.def("SetScopeActive", &CCar::SetScopeActive)

		.def("CCarDrone_GetPowerEfficiency", &CCar::CCarDrone_GetPowerEfficiency)
		.def("CCarDrone_SetPowerEfficiency", &CCar::CCarDrone_SetPowerEfficiency)

		.def("VisualCamera_GetDesireAngle", &CCar::VisualCamera_GetDesireAngle)
		.def("VisualCamera_GetRotXCur", &CCar::VisualCamera_GetRotXCur)
		.def("VisualCamera_GetRotYCur", &CCar::VisualCamera_GetRotYCur)
#endif
		.def(constructor<>())
	];
}
