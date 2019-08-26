////////////////////////////////////////////////////////////////////////////
//	Module 		: actor_script.cpp
//	Created 	: 17.01.2008
//  Modified 	: 17.01.2008
//	Author		: Dmitriy Iassenev
//	Description : actor script export
////////////////////////////////////////////////////////////////////////////

#include "pch_script.h"
#include "actor.h"
#include "level_changer.h"
#include "ActorCondition.h"

using namespace luabind;

#pragma optimize("s",on)
void CActor::script_register(lua_State *L)
{
	CActorCondition::script_register(L);
	
	module(L)
	[
		class_<CActor,CGameObject>("CActor")
			.def(constructor<>())
			.def("conditions", &CActor::conditions)
			.def("inventory_disabled", &CActor::inventory_disabled)
			.def("set_inventory_disabled", &CActor::set_inventory_disabled)
#ifndef	BENCHMARK_BUILD
		,
		class_<CLevelChanger,CGameObject>("CLevelChanger")
			.def(constructor<>())
#endif	//	BENCHMARK_BUILD
	];
}
