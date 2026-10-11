#pragma once
#include "../BaseMonster/base_monster.h"
#include "../../../../xrServerEntities/script_export_space.h"

class CChimera : public CBaseMonster
{
public:
	CChimera();
	virtual ~CChimera();

	virtual void Load(LPCSTR section);
	virtual void reinit();
	virtual void UpdateCL();

	virtual void CheckSpecParams(u32 spec_params);
	virtual void HitEntityInJump(const CEntity* pEntity);
	virtual void jump(Fvector const& position, float factor);

private:
	virtual char* get_monster_class_name() { return "chimera"; }
	virtual EAction CustomVelocityIndex2Action(u32 velocity_index);

	typedef CBaseMonster inherited;

	SVelocityParam m_velocity_rotate;
	SVelocityParam m_velocity_jump_start;

	struct attack_params
	{
		float attack_radius;
		TTime prepare_jump_timeout;
		TTime attack_jump_timeout;
		TTime stealth_timeout;
		float force_attack_distance;
		u32 num_attack_jumps;
		u32 num_prepare_jumps;
	};

	attack_params m_attack_params;

public:
	attack_params const& get_attack_params() const { return m_attack_params; }

	// negative argument keeps the current value
	void set_attack_params(float attack_radius, float prepare_timeout_ms, float attack_timeout_ms,
	                       int num_prepare_jumps, int num_attack_jumps)
	{
		if (attack_radius >= 0.f) m_attack_params.attack_radius = attack_radius;
		if (prepare_timeout_ms >= 0.f) m_attack_params.prepare_jump_timeout = (TTime)prepare_timeout_ms;
		if (attack_timeout_ms >= 0.f) m_attack_params.attack_jump_timeout = (TTime)attack_timeout_ms;
		if (num_prepare_jumps >= 0) m_attack_params.num_prepare_jumps = (u32)num_prepare_jumps;
		if (num_attack_jumps >= 0) m_attack_params.num_attack_jumps = (u32)num_attack_jumps;
	}


DECLARE_SCRIPT_REGISTER_FUNCTION
};
