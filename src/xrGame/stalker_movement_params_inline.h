////////////////////////////////////////////////////////////////////////////
//	Module 		: stalker_movement_params_inline.h
//	Created 	: 23.12.2005
//  Modified 	: 23.12.2005
//	Author		: Dmitriy Iassenev
//	Description : Stalker movement parameters class inline functions
////////////////////////////////////////////////////////////////////////////

#ifndef STALKER_MOVEMENT_PARAMS_INLINE_H_INCLUDED
#define STALKER_MOVEMENT_PARAMS_INLINE_H_INCLUDED

#include "smart_cover.h"

IC void stalker_movement_params::construct(stalker_movement_manager_smart_cover* manager)
{
	VERIFY(!m_manager);
	VERIFY(manager);
	m_manager = manager;
}

IC void stalker_movement_params::desired_position(Fvector const* position)
{
	if (!position)
	{
		m_desired_position_impl.set(flt_max, flt_max, flt_max);
		m_desired_position = 0;
		return;
	}

	cover_id("");

	m_desired_position_impl = *position;
	m_desired_position = &m_desired_position_impl;
}

IC Fvector const* stalker_movement_params::desired_position() const
{
	return (m_desired_position);
}

IC void stalker_movement_params::desired_direction(Fvector const* direction)
{
	if (!direction)
	{
		m_desired_direction_impl.set(flt_max, flt_max, flt_max);
		m_desired_direction = 0;
		return;
	}

	cover_id("");

	m_desired_direction_impl = *direction;
	VERIFY(fsimilar(m_desired_direction_impl.magnitude(), 1.f));
	m_desired_direction = &m_desired_direction_impl;
}

IC Fvector const* stalker_movement_params::desired_direction() const
{
	return (m_desired_direction);
}

IC shared_str const& stalker_movement_params::cover_id() const
{
	return (m_cover_id);
}

IC smart_cover::cover const* stalker_movement_params::cover() const
{
	return (m_cover);
}

IC void stalker_movement_params::cover_fire_object(CGameObject const* object)
{
	m_cover_fire_object = object;
	if (!object)
		return;

	m_cover_fire_position = 0;
	m_cover_fire_position_impl.set(flt_max, flt_max, flt_max);
}

IC CGameObject const* stalker_movement_params::cover_fire_object() const
{
	return (m_cover_fire_object);
}

IC void stalker_movement_params::cover_fire_position(Fvector const* position)
{
	if (!position)
	{
		m_cover_fire_position = 0;
		m_cover_fire_position_impl.set(flt_max, flt_max, flt_max);
		return;
	}

	m_cover_fire_object = 0;
	m_cover_fire_position_impl = *position;
	m_cover_fire_position = &m_cover_fire_position_impl;
}

IC Fvector const* stalker_movement_params::cover_fire_position() const
{
	return (m_cover_fire_position);
}

IC void stalker_movement_params::cover_loophole_id(shared_str const& loophole_id)
{
	cover_fire_object(0);
	cover_fire_position(0);

	if (m_cover_loophole_id == loophole_id)
		return;

	m_cover_loophole_id = loophole_id;
	m_selected_loophole_actual = false;
	m_cover_selected_loophole = 0;

	if (!loophole_id.size())
	{
		m_cover_loophole = 0;
		return;
	}

	VERIFY(m_cover);

	typedef smart_cover::cover::Loopholes Loopholes;
	Loopholes const& loopholes = m_cover->description()->loopholes();
	Loopholes::const_iterator i =
		std::find_if(
			loopholes.begin(),
			loopholes.end(),
			loophole_id_predicate(loophole_id)
		);

	VERIFY2(
		i != loopholes.end(),
		make_string(
			"loophole [%s] not present in smart_cover [%s]",
			loophole_id.c_str(),
			m_cover_id.c_str()
		)
	);

	m_cover_loophole = *i;
}

#endif // #ifndef STALKER_MOVEMENT_PARAMS_INLINE_H_INCLUDED
