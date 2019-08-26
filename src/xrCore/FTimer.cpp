#include "stdafx.h"
#pragma hdrstop

XRCORE_API BOOL g_bEnableStatGather = FALSE;

CStatTimer::CStatTimer()
{
	accum = 0;
	result = 0.f;
	count = 0;
}

void CStatTimer::FrameStart()
{
	accum = 0;
	count = 0;
}

void CStatTimer::FrameEnd()
{
	float _time = 1000.f * float(double(accum) / double(CPU::qpc_freq));
	if (_time > result) result = _time;
	else result = 0.99f * result + 0.01f * _time;
}

XRCORE_API pauseMngr& g_pauseMngr()
{
	static pauseMngr manager;
	return manager;
}

pauseMngr::pauseMngr() : m_paused(false)
{
	m_timers.reserve(3);
}

void pauseMngr::Pause(const bool b)
{
	if (m_paused == b)return;

	xr_vector<CTimer_paused*>::iterator it = m_timers.begin();
	for (; it != m_timers.end(); ++it)
		(*it)->Pause(b);

	m_paused = b;
}

void pauseMngr::Register(CTimer_paused& t)
{
	m_timers.push_back(&t);
}

void pauseMngr::UnRegister(CTimer_paused& t)
{
	const auto it = std::find(m_timers.cbegin(), m_timers.cend(), &t);
	if (it != m_timers.end())
		m_timers.erase(it);
}
