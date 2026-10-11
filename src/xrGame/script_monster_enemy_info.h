#pragma once

#include "script_export_space.h"

class CScriptGameObject;

class CScriptMonsterEnemyInfo
{
public:
	CScriptGameObject* enemy;
	Fvector position;
	u32 vertex;
	int time_last_seen;
	int enemies_count;
	u32 my_vertex_enemy_last_seen;
	u32 enemy_vertex_enemy_last_seen;

	CScriptMonsterEnemyInfo()
	{
		enemy = 0;
		position = Fvector().set(0.f, 0.f, 0.f);
		vertex = u32(-1);
		time_last_seen = 0;
		enemies_count = 0;
		my_vertex_enemy_last_seen = u32(-1);
		enemy_vertex_enemy_last_seen = u32(-1);
	}

	void set(CScriptGameObject* p_enemy, Fvector p_position, u32 p_vertex, int p_time_last_seen,
	         int p_enemies_count, u32 p_my_vertex, u32 p_enemy_vertex)
	{
		enemy = p_enemy;
		position = p_position;
		vertex = p_vertex;
		time_last_seen = p_time_last_seen;
		enemies_count = p_enemies_count;
		my_vertex_enemy_last_seen = p_my_vertex;
		enemy_vertex_enemy_last_seen = p_enemy_vertex;
	}

DECLARE_SCRIPT_REGISTER_FUNCTION
};
