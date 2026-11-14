////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_schedule_registry.h
//	Created 	: 15.01.2003
//  Modified 	: 12.05.2004
//	Author		: Dmitriy Iassenev
//	Description : ALife schedule registry
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "safe_map_iterator.h"
#include "xrServer_Objects_ALife.h"
#include "ai_debug.h"
#include "profiler.h"

class CALifeScheduleRegistry : public CSafeMapIterator<
		ALife::_OBJECT_ID, CSE_ALifeSchedulable, std::less<ALife::_OBJECT_ID>, false>
{
private:
	struct CUpdatePredicate
	{
		u32 m_count;
		mutable u32 m_current;
		float m_time_limit;
		CTimer m_timer;

		IC CUpdatePredicate(const u32& count, const float& time_limit)
		{
			m_count = count;
			m_current = 0;
			m_time_limit = time_limit;
			m_timer.Start();
		}

		IC bool operator()(_iterator& i, u64 cycle_count, bool) const
		{
			if ((*i).second->m_schedule_counter == cycle_count)
				return (false);

			if (m_current >= m_count)
				return (false);

			if (m_current && (m_time_limit > 0.f) && (m_timer.GetElapsed_sec() >= m_time_limit))
				return (false);

			++m_current;
			(*i).second->m_schedule_counter = cycle_count;

			return (true);
		}

		IC void operator()(_iterator& i, u64 cycle_count) const
		{
			START_PROFILE("ALife/scheduled/update")
				(*i).second->update();
			STOP_PROFILE
		}
	};

protected:
	typedef CSafeMapIterator<ALife::_OBJECT_ID, CSE_ALifeSchedulable, std::less<ALife::_OBJECT_ID>, false> inherited;

protected:
	u32 m_objects_per_update;
	float m_time_limit;

public:
	IC CALifeScheduleRegistry();
	virtual ~CALifeScheduleRegistry();
	void add(CSE_ALifeDynamicObject* object);
	void remove(CSE_ALifeDynamicObject* object, bool no_assert = false);
	IC void update();
	IC CSE_ALifeSchedulable* object(const ALife::_OBJECT_ID& id, bool no_assert = false) const;
	IC const u32& objects_per_update() const;
	IC void objects_per_update(const u32& objects_per_update);
	IC const float& time_limit() const;
	IC void time_limit(const float& time_limit);
};

#include "alife_schedule_registry_inline.h"
