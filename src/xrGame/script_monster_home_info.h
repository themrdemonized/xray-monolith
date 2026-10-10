#pragma once

#include "script_export_space.h"

class CScriptMonsterHomeInfo
{
public:
	Fvector point;
	float radius_min;
	float radius_mid;
	float radius_max;
	bool aggressive;
	bool has_home;
	bool at_home;

	CScriptMonsterHomeInfo()
	{
		point = Fvector().set(0.f, 0.f, 0.f);
		radius_min = 0.f;
		radius_mid = 0.f;
		radius_max = 0.f;
		aggressive = false;
		has_home = false;
		at_home = false;
	}

	void set(Fvector p_point, float p_radius_min, float p_radius_mid, float p_radius_max, bool p_aggressive,
	         bool p_has_home, bool p_at_home)
	{
		point = p_point;
		radius_min = p_radius_min;
		radius_mid = p_radius_mid;
		radius_max = p_radius_max;
		aggressive = p_aggressive;
		has_home = p_has_home;
		at_home = p_at_home;
	}

DECLARE_SCRIPT_REGISTER_FUNCTION
};
