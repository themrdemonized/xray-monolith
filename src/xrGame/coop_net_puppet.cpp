////////////////////////////////////////////////////////////////////////////
//	Module 		: coop_net_puppet.cpp
//	Description : Anomaly Co-op: a monster whose position comes from the network
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "coop_net_puppet.h"

namespace
{
	const float teleport_distance = 15.f; // a jump this long between two keys is a real teleport
	const u32 max_extrapolation = 250; // ms past the newest key before the puppet stops and waits
	const u32 max_keys = 64;
	const float ease_time = 0.15f; // s: a correction fades to a third this fast
	const float snap_distance = 5.f; // a correction this long is a real jump: shown at once
	const float min_delay = 50.f, max_delay = 500.f, start_delay = 200.f;
	const float delay_rate = 0.1f; // the delay changes by at most this share of real time: playback speeds up or slows down by 10%
	// a game that hangs for a moment (loading, saving, a busy PC) loses that time on its clock: the engine caps a
	// frame's step. A clock difference that moves this much at once is such a hang, not the link getting slower
	const s64 clock_jump = 300;
}

CCoopNetPuppet::CCoopNetPuppet(u32 delay) : m_fixed_delay(delay), m_body(255), m_look_valid(false)
{
	m_look.set(0.f, 0.f, 1.f);
	reset();
}

void CCoopNetPuppet::reset()
{
	m_keys.clear();
	m_delay = m_fixed_delay ? float(m_fixed_delay) : start_delay;
	m_lag = 0.0;
	m_lag_valid = false;
	m_late = 0.f;
	m_late_min = flt_max;
	m_late_window = 0;
	m_interval = 100.f;
	m_offset = 0;
	m_offset_valid = false;
	m_last.set(0.f, 0.f, 0.f);
	m_corr.set(0.f, 0.f, 0.f);
	m_last_time = 0;
	m_last_valid = false;
	m_speed = 0.f;
	m_vel.set(0.f, 0.f, 0.f);
}

void CCoopNetPuppet::push(const Fvector& p, u32 remote_time)
{
	if (!m_keys.empty())
	{
		// the sender's clock went back a lot: its game was restarted or reloaded
		if (remote_time + 5000 < m_keys.back().t)
			reset();
		else if (remote_time <= m_keys.back().t)
			return; // old or repeated
		else
		{
			float gap = float(remote_time - m_keys.back().t);
			if (gap < 1000.f)
				m_interval = m_interval * 0.9f + gap * 0.1f;
			if (m_keys.back().p.distance_to(p) > teleport_distance)
			{
				m_keys.clear();
				m_last_valid = false;
				m_corr.set(0.f, 0.f, 0.f);
			}
			else if (gap > 3.f * m_interval + 100.f)
			{
				// nothing came for a while (the sender only reports what moves): it stood still until
				// just before this key, so start moving from the old spot one step earlier, not across the pause
				key hold;
				hold.t = remote_time - u32(m_interval);
				hold.p = m_keys.back().p;
				if (hold.t > m_keys.back().t)
					m_keys.push_back(hold);
			}
		}
	}

	// the fastest delivery seen is the best guess of the clock difference; creep up slowly
	// so a single early packet or a changed route does not hold the estimate forever
	s64 offset = s64(Device.dwTimeGlobal) - s64(remote_time);
	if (!m_offset_valid || offset < m_offset)
	{
		// our game hung: its clock fell behind the sender's. Move the playback clock along at once, the same
		// moment of the partner's movement stays on screen instead of lagging by the hang for minutes
		if (m_offset_valid && m_lag_valid && m_offset - offset > clock_jump)
			m_lag -= double(m_offset - offset);
		m_offset = offset;
		m_offset_valid = true;
	}
	else
		++m_offset;

	// how late this key is compared to the fastest one: keep the peak, let it fade over a few seconds
	float late = float(offset - m_offset);
	m_late = std::max(late, m_late * 0.97f);

	// the sender's game hung: every key comes late by the time its clock lost. When that holds for a whole
	// second it is the clock, not the link: take it into the clock difference at once
	m_late_min = std::min(m_late_min, late);
	if (remote_time - m_late_window >= 1000)
	{
		if (m_late_window && m_late_min > float(clock_jump))
		{
			s64 shift = s64(m_late_min);
			m_offset += shift;
			if (m_lag_valid)
				m_lag += double(shift);
			m_late = std::max(0.f, m_late - m_late_min);
		}
		m_late_window = remote_time;
		m_late_min = flt_max;
	}

	key k;
	k.t = remote_time;
	k.p = p;
	m_keys.push_back(k);
	while (m_keys.size() > max_keys)
		m_keys.pop_front();
}

bool CCoopNetPuppet::sample(Fvector& out)
{
	if (m_keys.empty())
		return false;

	u32 now = Device.dwTimeGlobal;

	// enough delay that the next key is almost always there before it is needed
	float target = m_fixed_delay ? float(m_fixed_delay) : clampr(m_late + m_interval + 20.f, min_delay, max_delay);
	// both the delay and the clock estimate change gradually: a jump in either would make the puppet jump
	double want = double(m_offset) + double(target);
	if (!m_lag_valid)
	{
		m_lag = want;
		m_lag_valid = true;
	}
	else if (now > m_last_time)
	{
		double step = double(now - m_last_time) * delay_rate;
		m_lag += std::max(-step, std::min(step, want - m_lag));
	}
	m_delay = float(m_lag - double(m_offset));

	s64 t = s64(now) - s64(m_lag); // in the sender's clock

	while (m_keys.size() > 2 && s64(m_keys[1].t) <= t)
		m_keys.pop_front();

	const key& a = m_keys[0];
	if (m_keys.size() == 1 || t <= s64(a.t))
		out = a.p;
	else
	{
		const key& b = m_keys[1];
		float span = float(b.t - a.t);
		if (t <= s64(b.t))
			out.lerp(a.p, b.p, float(t - s64(a.t)) / span);
		else
		{
			// past the newest key: keep going along the last step for a moment, then wait
			float over = float(std::min<s64>(t - s64(b.t), max_extrapolation));
			Fvector v;
			v.sub(b.p, a.p).div(span);
			out.mad(b.p, v, over);
		}
	}

	if (m_last_valid && now > m_last_time)
	{
		// a key came later than the delay allowed: the puppet ran ahead on the last step and the real
		// position is behind it. Do not pull it back in one frame, ease the difference out instead
		float dt = float(now - m_last_time) / 1000.f;
		m_corr.mul(expf(-dt / ease_time));
		Fvector shown;
		shown.add(out, m_corr);
		float step = shown.distance_to(m_last);
		float plausible = std::max(0.3f, m_speed * dt * 3.f);
		if (step > plausible && step < snap_distance)
		{
			Fvector back;
			back.sub(m_last, shown);
			m_corr.add(back);
			shown = m_last;
		}
		else if (step >= snap_distance)
		{
			m_corr.set(0.f, 0.f, 0.f);
			shown = out;
		}
		out = shown;

		Fvector d;
		d.sub(out, m_last);
		d.y = 0.f;
		float v = d.magnitude() / (float(now - m_last_time) / 1000.f);
		m_speed = m_speed * 0.8f + v * 0.2f;
		d.div(float(now - m_last_time) / 1000.f);
		m_vel.mul(0.8f).mad(d, 0.2f);
	}
	m_last = out;
	m_last_time = now;
	m_last_valid = true;
	return true;
}
