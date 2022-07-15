#include "pch_script.h"
#include "player_hud.h"

using namespace luabind;

player_hud* get_player_hud()
{
	return g_player_hud;
}

#pragma optimize("s",on)
void player_hud::script_register(lua_State* L)
{
	module(L)
	[
		class_<player_hud>("player_hud")
		.def(constructor<>())
		.def("set_hands", &player_hud::load_script)
		.def("reset_hands", &player_hud::reset_model_script),

		def("get_player_hud", &get_player_hud)
	];
}
