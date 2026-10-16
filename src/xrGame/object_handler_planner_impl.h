////////////////////////////////////////////////////////////////////////////
//	Module 		: object_handler_planner_impl.h
//	Created 	: 11.03.2004
//  Modified 	: 01.12.2004
//	Author		: Dmitriy Iassenev
//	Description : Object handler action planner inline functions implementation
////////////////////////////////////////////////////////////////////////////

#pragma once

IC bool CObjectHandlerPlanner::object_action(u32 action_id, CObject* object)
{
	return ((action_id >> 8) == object->ID());
}

IC ALife::_OBJECT_ID CObjectHandlerPlanner::current_action_object_id() const
{
	return (action_object_id(current_action_id()));
}

IC ALife::_OBJECT_ID CObjectHandlerPlanner::action_object_id(_condition_type action_id) const
{
	return (action_id >> 8);
}

IC void CObjectHandlerPlanner::add_condition(CActionBase<CAI_Stalker>* action, u32 id,
                                             ObjectHandlerSpace::EWorldProperties property, _value_type value)
{
	action->add_condition(CWorldProperty(uid(id, property), value));
}

IC void CObjectHandlerPlanner::add_effect(CActionBase<CAI_Stalker>* action, u32 id,
                                          ObjectHandlerSpace::EWorldProperties property, _value_type value)
{
	action->add_effect(CWorldProperty(uid(id, property), value));
}
