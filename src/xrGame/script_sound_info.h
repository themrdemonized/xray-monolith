#pragma once

#include "script_export_space.h"

class CScriptGameObject;

class CScriptSoundInfo
{
public:
	CScriptGameObject* who;
	Fvector position;
	float power;
	int time;
	int dangerous;
	int type;
	int count;


	CScriptSoundInfo()
	{
		who = 0;
		time = 0;
		dangerous = 0;
		power = 0.f;
		position = Fvector().set(0.f, 0.f, 0.f);
		type = 0;
		count = 0;
	}

	void set(CScriptGameObject* p_who, bool p_danger, Fvector p_position, float p_power, int p_time, int p_type,
	         int p_count)
	{
		who = p_who;
		position = p_position;
		power = p_power;
		time = p_time;
		dangerous = int(p_danger);
		type = p_type;
		count = p_count;
	}

DECLARE_SCRIPT_REGISTER_FUNCTION
};
