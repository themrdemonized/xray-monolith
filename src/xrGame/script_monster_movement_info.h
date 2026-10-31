#pragma once

#include "script_export_space.h"

class CScriptMonsterMovementInfo
{
public:
	float velocity_current;  // CControlMovement::velocity_current() - the applied AI speed (= m_fCurSpeed)
	float velocity_target;   // CControlMovement::velocity_target()  - the speed the active state asked for
	float real_velocity;     // CControlMovement::real_velocity()    - the physics translation (XZ, going dir)
	bool path_completed;     // CMovementManager::path_completed() - reached the path end (arrived, correctly idle)
	bool path_actual;        // CMovementManager::actual_all()     - path is current/valid (false = rebuilding or unreachable)
	bool path_active;        // CControl_ComBase::is_active()      - the path-builder component is driving movement (false = yielded to a capture)

	CScriptMonsterMovementInfo()
	{
		velocity_current = 0.f;
		velocity_target = 0.f;
		real_velocity = 0.f;
		path_completed = false;
		path_actual = false;
		path_active = false;
	}

	void set(float p_velocity_current, float p_velocity_target, float p_real_velocity,
	         bool p_path_completed, bool p_path_actual, bool p_path_active)
	{
		velocity_current = p_velocity_current;
		velocity_target = p_velocity_target;
		real_velocity = p_real_velocity;
		path_completed = p_path_completed;
		path_actual = p_path_actual;
		path_active = p_path_active;
	}

DECLARE_SCRIPT_REGISTER_FUNCTION
};
