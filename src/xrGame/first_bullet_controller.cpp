#include "stdafx.h"
#include "first_bullet_controller.h"
#include "level.h"

first_bullet_controller::first_bullet_controller()
{
	m_actor_velocity_limit = 0.0f;
	m_use_first_bullet = false;
	m_last_short_time = 0;
	m_shot_timeout = 0;
	m_fire_dispertion = 0;
}

void first_bullet_controller::load(shared_str const& section)
{
}

bool first_bullet_controller::is_bullet_first(float actor_linear_velocity) const
{
	return false;
}

void first_bullet_controller::make_shot()
{
}
