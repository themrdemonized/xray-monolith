#include "pch_script.h"
#include "WeaponAmmo.h"
#include "script_game_object.h"

using namespace luabind;

#pragma optimize("s",on)
void CWeaponAmmo::script_register(lua_State *L)
{
	module(L)
		[
			class_<CWeaponAmmo, CGameObject>("CWeaponAmmo")
			.def(constructor<>())
			.def_readwrite("m_boxSize", &CWeaponAmmo::m_boxSize)
			.def_readwrite("m_boxCurr", &CWeaponAmmo::m_boxCurr)
			.def_readwrite("m_tracer", &CWeaponAmmo::m_tracer)
			.def_readwrite("m_4to1_tracer", &CWeaponAmmo::m_4to1_tracer)
			.def("Weight", &CWeaponAmmo::Weight)
			.def("Cost", &CWeaponAmmo::Cost)
		];
}