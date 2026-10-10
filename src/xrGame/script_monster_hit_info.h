#pragma once

#include "script_export_space.h"

class CScriptGameObject;

class CScriptMonsterHitInfo
{
public:
	CScriptGameObject* who;
	Fvector direction;
	int time;
	Fvector position;
	int count;


	CScriptMonsterHitInfo()
	{
		who = 0;
		time = 0;
		direction = Fvector().set(0.f, 0.f, 1.f);
		position = Fvector().set(0.f, 0.f, 0.f);
		count = 0;
	}

	void set(CScriptGameObject* p_who, Fvector p_direction, int p_time, Fvector p_position, int p_count)
	{
		who = p_who;
		direction = p_direction;
		time = p_time;
		position = p_position;
		count = p_count;
	}

DECLARE_SCRIPT_REGISTER_FUNCTION
};
