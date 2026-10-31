#include "pch_script.h"
#include "script_monster_movement_info.h"
#include "script_game_object.h"

using namespace luabind;

#pragma optimize("s",on)
void CScriptMonsterMovementInfo::script_register(lua_State* L)
{
	module(L)
	[
		class_<CScriptMonsterMovementInfo>("MonsterMovementInfo")
		.def_readwrite("velocity_current", &CScriptMonsterMovementInfo::velocity_current)
		.def_readwrite("velocity_target", &CScriptMonsterMovementInfo::velocity_target)
		.def_readwrite("real_velocity", &CScriptMonsterMovementInfo::real_velocity)
		.def_readwrite("path_completed", &CScriptMonsterMovementInfo::path_completed)
		.def_readwrite("path_actual", &CScriptMonsterMovementInfo::path_actual)
		.def_readwrite("path_active", &CScriptMonsterMovementInfo::path_active)
	];
}
