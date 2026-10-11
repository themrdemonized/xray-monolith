#pragma once
#include "control_combase.h"

class CControlRunAttack : public CControl_ComCustom<>
{
	float m_min_dist;
	float m_max_dist;

	u32 m_min_delay;
	u32 m_max_delay;

	u32 m_time_next_attack;

public:
	virtual void load(LPCSTR section);
	virtual void reinit();

	// negative argument keeps the current value
	void set_params(float min_dist, float max_dist, float min_delay_ms, float max_delay_ms)
	{
		if (min_dist >= 0.f) m_min_dist = min_dist;
		if (max_dist >= 0.f) m_max_dist = max_dist;
		if (min_delay_ms >= 0.f) m_min_delay = (u32)min_delay_ms;
		if (max_delay_ms >= 0.f) m_max_delay = (u32)max_delay_ms;
	}

	virtual void on_event(ControlCom::EEventType, ControlCom::IEventData*);
	virtual void activate();
	virtual void on_release();
	virtual bool check_start_conditions();
};
