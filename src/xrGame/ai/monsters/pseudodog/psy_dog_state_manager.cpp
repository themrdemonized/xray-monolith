#include "stdafx.h"
#include "psy_dog.h"
#include "psy_dog_state_manager.h"
#include "../../../actor.h"
#include "../control_direction_base.h"
#include "../control_movement_base.h"
#include "../control_path_builder_base.h"
#include "../control_animation_base.h"
#include "../../../ai_object_location.h"
#include "../../../sound_player.h"
#include "../../../level_graph.h"

#include "psy_dog_state_psy_attack.h"

CStateManagerPsyDog::CStateManagerPsyDog(CAI_PseudoDog* monster) : inherited(monster)
{
	// dormant: no ladder branch selects it; the inherited pseudodog selector honors a
	// monster_on_combat_action proposal for it through this registration
	add_state(eStateAttack_Psy, xr_new<CStatePsyDogPsyAttack<CAI_PseudoDog>>(monster));
}

void CStateManagerPsyDog::execute()
{
	// 	const CEntityAlive* enemy	= object->EnemyMan.get_enemy();
	// 
	// 	if (enemy && smart_cast<const CActor*>(enemy) && smart_cast<CPsyDog*>(object)->must_hide()) {
	// 		
	// 		select_state(eStateAttack_Psy); 
	// 	
	// 		// выполнить текущее состояние
	// 		get_state_current()->execute();
	// 
	// 		prev_substate = current_substate;
	// 	} else {
	inherited::execute();
	//	}
}
