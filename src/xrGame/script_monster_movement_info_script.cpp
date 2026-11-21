#include "pch_script.h"
#include "script_monster_movement_info.h"
#include "script_game_object.h"
#include "ai/monsters/control_com_defs.h"

using namespace luabind;

namespace
{
	struct monster_path_capturer_tag {};
}

#pragma optimize("s",on)
void CScriptMonsterMovementInfo::script_register(lua_State* L)
{
	module(L)
	[
		class_<CScriptMonsterMovementInfo>("MonsterMovementInfo")
		.def_readwrite("velocity_current", &CScriptMonsterMovementInfo::velocity_current)
		.def_readwrite("velocity_target", &CScriptMonsterMovementInfo::velocity_target)
		.def_readwrite("real_velocity", &CScriptMonsterMovementInfo::real_velocity)
		.def_readwrite("path_completed", &CScriptMonsterMovementInfo::path_completed)
		.def_readwrite("path_actual", &CScriptMonsterMovementInfo::path_actual)
		.def_readwrite("path_capturer", &CScriptMonsterMovementInfo::path_capturer),

		// The components that can hold a monster's path; path_capturer returns one of these, or none.
		class_<enum_exporter<monster_path_capturer_tag>>("monster_path_capturer")
		.enum_("capturer")
		[
			value("none", -1),
			value("sequencer", int(ControlCom::eControlSequencer)),
			value("triple_animation", int(ControlCom::eControlTripleAnimation)),
			value("jump", int(ControlCom::eControlJump)),
			value("rotation_jump", int(ControlCom::eControlRotationJump)),
			value("run_attack", int(ControlCom::eControlRunAttack)),
			value("threaten", int(ControlCom::eControlThreaten)),
			value("melee_jump", int(ControlCom::eControlMeleeJump)),
			value("custom1", int(ControlCom::eComCustom1)),
			value("critical_wound", int(ControlCom::eComCriticalWound)),
			value("anti_aim", int(ControlCom::eAntiAim))
		]
	];
}
