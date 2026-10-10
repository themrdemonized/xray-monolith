#include "pch_script.h"
#include "script_monster_home_info.h"
#include "script_game_object.h"

using namespace luabind;

#pragma optimize("s",on)
void CScriptMonsterHomeInfo::script_register(lua_State* L)
{
	module(L)
	[
		class_<CScriptMonsterHomeInfo>("MonsterHomeInfo")
		.def_readwrite("point", &CScriptMonsterHomeInfo::point)
		.def_readwrite("radius_min", &CScriptMonsterHomeInfo::radius_min)
		.def_readwrite("radius_mid", &CScriptMonsterHomeInfo::radius_mid)
		.def_readwrite("radius_max", &CScriptMonsterHomeInfo::radius_max)
		.def_readwrite("aggressive", &CScriptMonsterHomeInfo::aggressive)
		.def_readwrite("has_home", &CScriptMonsterHomeInfo::has_home)
		.def_readwrite("at_home", &CScriptMonsterHomeInfo::at_home)
	];
}
