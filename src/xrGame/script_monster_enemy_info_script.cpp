#include "pch_script.h"
#include "script_monster_enemy_info.h"
#include "script_game_object.h"

using namespace luabind;

#pragma optimize("s",on)
void CScriptMonsterEnemyInfo::script_register(lua_State* L)
{
	module(L)
	[
		class_<CScriptMonsterEnemyInfo>("MonsterEnemyInfo")
		.def_readwrite("enemy", &CScriptMonsterEnemyInfo::enemy)
		.def_readwrite("position", &CScriptMonsterEnemyInfo::position)
		.def_readwrite("vertex", &CScriptMonsterEnemyInfo::vertex)
		.def_readwrite("time_last_seen", &CScriptMonsterEnemyInfo::time_last_seen)
		.def_readwrite("enemies_count", &CScriptMonsterEnemyInfo::enemies_count)
		.def_readwrite("my_vertex_enemy_last_seen", &CScriptMonsterEnemyInfo::my_vertex_enemy_last_seen)
		.def_readwrite("enemy_vertex_enemy_last_seen", &CScriptMonsterEnemyInfo::enemy_vertex_enemy_last_seen)
	];
}
