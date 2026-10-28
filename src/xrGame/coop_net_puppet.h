////////////////////////////////////////////////////////////////////////////
//	Module 		: coop_net_puppet.h
//	Description : Anomaly Co-op: a monster whose position comes from the network
//	              (the partner's puppet) instead of its own path
////////////////////////////////////////////////////////////////////////////

#pragma once

// Positions arrive with the sender's clock (its time_global). They are played back a little
// behind real time, so uneven delivery on a laggy link is absorbed, and interpolated every frame.
// With delay 0 the playback delay follows the link: how late keys arrive and how often they come.
class CCoopNetPuppet
{
public:
	struct key
	{
		u32 t; // sender's time, ms
		Fvector p;
	};

	CCoopNetPuppet(u32 delay);

	void push(const Fvector& p, u32 remote_time);
	// position for this frame; false while nothing has arrived yet
	bool sample(Fvector& out);
	float speed() const { return m_speed; }
	// horizontal, m/s, smoothed: where it is really going
	const Fvector& velocity() const { return m_vel; }
	// body state from the sender (0 crouch, 1 stand), 255 = up to its own AI
	u8 body() const { return m_body; }
	void set_body(u8 body) { m_body = body; }
	// where the sender looks; false until it says
	bool look(Fvector& dir) const { dir = m_look; return m_look_valid; }
	void set_look(const Fvector& dir) { m_look = dir; m_look_valid = !fis_zero(dir.square_magnitude()); }
	void set_delay(u32 delay) { m_fixed_delay = delay; }
	u32 delay() const { return u32(m_delay); }

private:
	void reset();

	xr_deque<key> m_keys;
	u32 m_fixed_delay; // ms, 0 = adapt to the link
	float m_delay; // playback delay in use, ms (for display)
	double m_lag; // local time minus the sender's time being shown: clock difference plus delay, eased
	bool m_lag_valid;
	float m_late; // how late keys arrive past the fastest one, ms (peak, slowly decaying)
	float m_late_min; // the least late key since m_late_window, ms
	u32 m_late_window; // sender's time the current one second window began
	float m_interval; // between keys, ms (smoothed)
	s64 m_offset; // local time minus sender's time, the smallest seen lately
	bool m_offset_valid;
	Fvector m_last; // shown last frame
	Fvector m_corr; // shown minus computed: a correction being eased out after a late key
	u32 m_last_time;
	bool m_last_valid;
	float m_speed; // horizontal, m/s, smoothed
	Fvector m_vel;
	u8 m_body;
	Fvector m_look;
	bool m_look_valid;
};
