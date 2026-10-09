////////////////////////////////////////////////////////////////////////////
//	Module 		: script_game_object_script3.cpp
//	Created 	: 17.11.2004
//  Modified 	: 17.11.2004
//	Author		: Dmitriy Iassenev
//	Description : Script game object class script export
////////////////////////////////////////////////////////////////////////////

#include "pch_script.h"
#include "script_game_object.h"
#include "script_game_object_impl.h"
#include "ai_space.h"
#include "script_engine.h"
#include "cover_evaluators.h"
#include "cover_point.h"
#include "cover_manager.h"
#include "ai/stalker/ai_stalker.h"
#include "stalker_animation_manager.h"
#include "stalker_planner.h"
#include "weapon.h"
#include "inventory.h"
#include "customzone.h"
#include "patrol_path_manager.h"
#include "object_handler_planner.h"
#include "object_handler_space.h"
#include "memory_manager.h"
#include "visual_memory_manager.h"
#include "enemy_manager.h"
#include "sound_memory_manager.h"
#include "hit_memory_manager.h"
#include "EntityCondition.h"
#include "sight_manager.h"
#include "stalker_movement_manager_smart_cover.h"
#include "smart_cover.h"
#include "smart_cover_loophole.h"
#include "movement_manager_space.h"
#include "detail_path_manager_space.h"
#include "level_debug.h"
#include "ai/monsters/BaseMonster/base_monster.h"
#include "ai/monsters/state_manager.h"
#include "ai/monsters/monster_cover_manager.h"
#include "ai/monsters/control_manager_custom.h"
#include "ai/monsters/chimera/chimera.h"
#include "ai/monsters/burer/burer.h"
#include "ai/monsters/controller/controller.h"
#include "ai/monsters/poltergeist/poltergeist.h"
#include "trade_parameters.h"
#include "script_ini_file.h"
#include "sound_player.h"
#include "stalker_decision_space.h"
#include "space_restriction_manager.h"
#include "artefact.h"
//Alundaio
#ifdef GAME_OBJECT_EXTENDED_EXPORTS
#include "holder_custom.h"
#include "actor.h"
#include "CharacterPhysicsSupport.h"
#include "player_hud.h"
#include "eatable_item.h"
#include "script_callback_ex.h"
#include "../xrEngine/feel_touch.h"
#include "weaponammo.h"
#include "WeaponMagazinedWGrenade.h"
#endif
//-Alundaio

#include "Torch.h"
#include "Flashlight.h"

namespace MemorySpace
{
	struct CVisibleObject;
	struct CSoundObject;
	struct CHitObject;
};

const CCoverPoint* CScriptGameObject::best_cover(const Fvector& position, const Fvector& enemy_position, float radius,
                                                 float min_enemy_distance, float max_enemy_distance)
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CGameObject : cannot access class member best_cover!");
		return (0);
	}
	stalker->m_ce_best->setup(enemy_position, min_enemy_distance, max_enemy_distance, 0.f);
	const CCoverPoint* point = ai().cover_manager().best_cover(position, radius, *stalker->m_ce_best);
	return (point);
}

const CCoverPoint* CScriptGameObject::safe_cover(const Fvector& position, float radius, float min_distance)
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CGameObject : cannot access class member best_cover!");
		return (0);
	}
	stalker->m_ce_safe->setup(min_distance);
	const CCoverPoint* point = ai().cover_manager().best_cover(position, radius, *stalker->m_ce_safe);
	return (point);
}

const xr_vector<MemorySpace::CVisibleObject>& CScriptGameObject::memory_visible_objects() const
{
	CCustomMonster* monster = smart_cast<CCustomMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CGameObject : cannot access class member memory_visible_objects!");
		NODEFAULT;
	}
	return (monster->memory().visual().objects());
}

const xr_vector<MemorySpace::CSoundObject>& CScriptGameObject::memory_sound_objects() const
{
	CCustomMonster* monster = smart_cast<CCustomMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CGameObject : cannot access class member memory_sound_objects!");
		NODEFAULT;
	}
	return (monster->memory().sound().objects());
}

const xr_vector<MemorySpace::CHitObject>& CScriptGameObject::memory_hit_objects() const
{
	CCustomMonster* monster = smart_cast<CCustomMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CGameObject : cannot access class member memory_hit_objects!");
		NODEFAULT;
	}
	return (monster->memory().hit().objects());
}

void CScriptGameObject::ChangeTeam(u8 team, u8 squad, u8 group)
{
	CCustomMonster* custom_monster = smart_cast<CCustomMonster*>(&object());
	if (!custom_monster)
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CCustomMonster: cannot access class member ChangeTeam!");
	else
		custom_monster->ChangeTeam(team, squad, group);
}

void CScriptGameObject::SetVisualMemoryEnabled(bool enabled)
{
	CCustomMonster* custom_monster = smart_cast<CCustomMonster*>(&object());
	if (!custom_monster)
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CCustomMonster: cannot access class member ChangeTeam!");
	else
		custom_monster->memory().visual().enable(enabled);
}

void CScriptGameObject::set_vision_speed(float value)
{
	CCustomMonster* custom_monster = smart_cast<CCustomMonster*>(&object());
	if (!custom_monster)
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CCustomMonster : cannot access class member set_vision_speed!");
	else
		custom_monster->memory().visual().set_vision_speed(value);
}

void CScriptGameObject::set_visible_enemy_bias(float actor_bias, float npc_bias)
{
	CCustomMonster* custom_monster = smart_cast<CCustomMonster*>(&object());
	if (!custom_monster)
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
                                    "CCustomMonster : cannot access class member set_visible_enemy_bias!");
	else
		custom_monster->memory().enemy().set_visible_enemy_bias(actor_bias, npc_bias);
}

void CScriptGameObject::set_hit_redirect(float max, float falloff)
{
	CCustomMonster* custom_monster = smart_cast<CCustomMonster*>(&object());
	if (!custom_monster)
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CCustomMonster : cannot access class member set_hit_redirect!");
	else
		custom_monster->memory().enemy().set_hit_redirect(max, falloff);
}

void CScriptGameObject::set_view_distance_factor(float value)
{
	CCustomMonster* custom_monster = smart_cast<CCustomMonster*>(&object());
	if (!custom_monster)
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CCustomMonster : cannot access class member set_view_distance_factor!");
	else
		custom_monster->memory().visual().set_view_distance_factor(value);
}

void CScriptGameObject::set_health_restore_boost(float value)
{
	CEntityAlive* entity_alive = smart_cast<CEntityAlive*>(&object());
	if (!entity_alive)
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CEntityAlive : cannot access class member set_health_restore_boost!");
	else
		entity_alive->conditions().set_health_restore_boost(value);
}

int CScriptGameObject::get_monster_state()
{
	CBaseMonster* monster = smart_cast<CBaseMonster*>(&object());
	if (!monster || !monster->StateMan)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CBaseMonster : cannot access class member get_monster_state!");
		return -1;
	}
	return (int)monster->StateMan->get_state_type();
}

bool CScriptGameObject::is_monster_jumping()
{
	CBaseMonster* monster = smart_cast<CBaseMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CBaseMonster : cannot access class member is_monster_jumping!");
		return false;
	}
	return monster->is_jumping();
}

int CScriptGameObject::get_monster_rank()
{
	CBaseMonster* monster = smart_cast<CBaseMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CBaseMonster : cannot access class member get_monster_rank!");
		return -1;
	}
	return monster->Rank();
}

bool CScriptGameObject::ability_invisibility()
{
	CBaseMonster* monster = smart_cast<CBaseMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CBaseMonster : cannot access class member ability_invisibility!");
		return false;
	}
	return monster->ability_invisibility();
}

bool CScriptGameObject::ability_can_drag()
{
	CBaseMonster* monster = smart_cast<CBaseMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CBaseMonster : cannot access class member ability_can_drag!");
		return false;
	}
	return monster->ability_can_drag();
}

bool CScriptGameObject::ability_psi_attack()
{
	CBaseMonster* monster = smart_cast<CBaseMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CBaseMonster : cannot access class member ability_psi_attack!");
		return false;
	}
	return monster->ability_psi_attack();
}

bool CScriptGameObject::ability_earthquake()
{
	CBaseMonster* monster = smart_cast<CBaseMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CBaseMonster : cannot access class member ability_earthquake!");
		return false;
	}
	return monster->ability_earthquake();
}

bool CScriptGameObject::ability_can_jump()
{
	CBaseMonster* monster = smart_cast<CBaseMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CBaseMonster : cannot access class member ability_can_jump!");
		return false;
	}
	return monster->ability_can_jump();
}

bool CScriptGameObject::ability_distant_feel()
{
	CBaseMonster* monster = smart_cast<CBaseMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CBaseMonster : cannot access class member ability_distant_feel!");
		return false;
	}
	return monster->ability_distant_feel();
}

bool CScriptGameObject::ability_run_attack()
{
	CBaseMonster* monster = smart_cast<CBaseMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CBaseMonster : cannot access class member ability_run_attack!");
		return false;
	}
	return monster->ability_run_attack();
}

bool CScriptGameObject::ability_rotation_jump()
{
	CBaseMonster* monster = smart_cast<CBaseMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CBaseMonster : cannot access class member ability_rotation_jump!");
		return false;
	}
	return monster->ability_rotation_jump();
}

bool CScriptGameObject::ability_jump_over_physics()
{
	CBaseMonster* monster = smart_cast<CBaseMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CBaseMonster : cannot access class member ability_jump_over_physics!");
		return false;
	}
	return monster->ability_jump_over_physics();
}

bool CScriptGameObject::can_attack_on_move()
{
	CBaseMonster* monster = smart_cast<CBaseMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CBaseMonster : cannot access class member can_attack_on_move!");
		return false;
	}
	return monster->can_attack_on_move();
}

float CScriptGameObject::get_monster_morale()
{
	CBaseMonster* monster = smart_cast<CBaseMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CBaseMonster : cannot access class member get_monster_morale!");
		return -1.f;
	}
	return monster->Morale.get_morale();
}

float CScriptGameObject::get_psy_influence()
{
	CBaseMonster* monster = smart_cast<CBaseMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CBaseMonster : cannot access class member get_psy_influence!");
		return -1.f;
	}
	return monster->get_psy_influence();
}

float CScriptGameObject::get_radiation_influence()
{
	CBaseMonster* monster = smart_cast<CBaseMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CBaseMonster : cannot access class member get_radiation_influence!");
		return -1.f;
	}
	return monster->get_radiation_influence();
}

float CScriptGameObject::get_fire_influence()
{
	CBaseMonster* monster = smart_cast<CBaseMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CBaseMonster : cannot access class member get_fire_influence!");
		return -1.f;
	}
	return monster->get_fire_influence();
}

u32 CScriptGameObject::get_monster_cover_vertex(const Fvector& enemy_position, float min_dist, float max_dist)
{
	CBaseMonster* monster = smart_cast<CBaseMonster*>(&object());
	if (!monster || !monster->CoverMan)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CBaseMonster : cannot access class member get_monster_cover_vertex!");
		return u32(-1);
	}
	const CCoverPoint* point = monster->CoverMan->find_cover(enemy_position, min_dist, max_dist);
	if (!point)
		return u32(-1);
	return point->level_vertex_id();
}

void CScriptGameObject::set_monster_attack_dist(float min_dist, float max_dist)
{
	CBaseMonster* monster = smart_cast<CBaseMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CBaseMonster : cannot access class member set_monster_attack_dist!");
		return;
	}
	monster->MeleeChecker.set_attack_distance(min_dist, max_dist);
}

void CScriptGameObject::set_monster_jump_params(float min_dist, float max_dist, float max_angle, float max_height,
                                                float delay_ms)
{
	CBaseMonster* monster = smart_cast<CBaseMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CBaseMonster : cannot access class member set_monster_jump_params!");
		return;
	}
	CControlJump* jump = monster->com_man().get_jump_control();
	if (!jump)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CBaseMonster : no jump control for set_monster_jump_params!");
		return;
	}
	jump->set_jump_params(min_dist, max_dist, max_angle, max_height, delay_ms);
}

void CScriptGameObject::set_chimera_attack_params(float attack_radius, float prepare_timeout_ms,
                                                  float attack_timeout_ms, int num_prepare_jumps,
                                                  int num_attack_jumps)
{
	CChimera* chimera = smart_cast<CChimera*>(&object());
	if (!chimera)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CChimera : cannot access class member set_chimera_attack_params!");
		return;
	}
	chimera->set_attack_params(attack_radius, prepare_timeout_ms, attack_timeout_ms, num_prepare_jumps,
	                           num_attack_jumps);
}

void CScriptGameObject::set_burer_gravi_params(float cooldown_ms, float min_dist, float max_dist, float speed,
                                               float radius, float hit_power)
{
	CBurer* burer = smart_cast<CBurer*>(&object());
	if (!burer)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CBurer : cannot access class member set_burer_gravi_params!");
		return;
	}
	if (cooldown_ms >= 0.f) burer->m_gravi.cooldown = (u32)cooldown_ms;
	if (min_dist >= 0.f) burer->m_gravi.min_dist = min_dist;
	if (max_dist >= 0.f) burer->m_gravi.max_dist = max_dist;
	if (speed >= 0.f) burer->m_gravi.speed = speed;
	if (radius >= 0.f) burer->m_gravi.radius = radius;
	if (hit_power >= 0.f) burer->m_gravi.hit_power = hit_power;
}

void CScriptGameObject::set_burer_tele_params(int max_objects, float find_radius, float min_dist, float max_dist,
                                              float fly_velocity)
{
	CBurer* burer = smart_cast<CBurer*>(&object());
	if (!burer)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CBurer : cannot access class member set_burer_tele_params!");
		return;
	}
	if (max_objects >= 0) burer->m_tele_max_handled_objects = (u32)max_objects;
	if (find_radius >= 0.f) burer->m_tele_find_radius = find_radius;
	if (min_dist >= 0.f) burer->m_tele_min_distance = min_dist;
	if (max_dist >= 0.f) burer->m_tele_max_distance = max_dist;
	if (fly_velocity >= 0.f) burer->m_tele_fly_velocity = fly_velocity;
}

void CScriptGameObject::set_controller_tube_params(float damage, float see_duration_ms, float min_delay_ms,
                                                   float min_distance)
{
	CController* controller = smart_cast<CController*>(&object());
	if (!controller)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CController : cannot access class member set_controller_tube_params!");
		return;
	}
	if (damage >= 0.f) controller->m_tube_damage = damage;
	if (see_duration_ms >= 0.f) controller->m_tube_condition_see_duration = (u32)see_duration_ms;
	if (min_delay_ms >= 0.f) controller->m_tube_condition_min_delay = (u32)min_delay_ms;
	if (min_distance >= 0.f) controller->m_tube_condition_min_distance = min_distance;
}

void CScriptGameObject::set_controller_stamina_hit(float value)
{
	CController* controller = smart_cast<CController*>(&object());
	if (!controller)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CController : cannot access class member set_controller_stamina_hit!");
		return;
	}
	controller->set_stamina_hit(value);
}

void CScriptGameObject::set_poltergeist_detection_params(float near_factor, float far_factor, float far_range,
                                                         float speed_factor, float loose_speed)
{
	CPoltergeist* poltergeist = smart_cast<CPoltergeist*>(&object());
	if (!poltergeist)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CPoltergeist : cannot access class member set_poltergeist_detection_params!");
		return;
	}
	poltergeist->set_detection_params(near_factor, far_factor, far_range, speed_factor, loose_speed);
}

void CScriptGameObject::set_poltergeist_height_params(float height_min, float height_max, float change_velocity,
                                                      float min_time_ms, float max_time_ms)
{
	CPoltergeist* poltergeist = smart_cast<CPoltergeist*>(&object());
	if (!poltergeist)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CPoltergeist : cannot access class member set_poltergeist_height_params!");
		return;
	}
	poltergeist->set_height_params(height_min, height_max, change_velocity, min_time_ms, max_time_ms);
}

bool CScriptGameObject::try_monster_jump(const Fvector& position, float factor, bool skip_prepare)
{
	CBaseMonster* monster = smart_cast<CBaseMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CBaseMonster : cannot access class member try_monster_jump!");
		return false;
	}
	return monster->com_man().script_try_jump(position, factor, skip_prepare);
}

bool CScriptGameObject::try_monster_rotation_jump()
{
	CBaseMonster* monster = smart_cast<CBaseMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CBaseMonster : cannot access class member try_monster_rotation_jump!");
		return false;
	}
	return monster->com_man().script_try_rotation_jump();
}

bool CScriptGameObject::try_monster_run_attack()
{
	CBaseMonster* monster = smart_cast<CBaseMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CBaseMonster : cannot access class member try_monster_run_attack!");
		return false;
	}
	return monster->com_man().script_try_run_attack();
}

bool CScriptGameObject::try_monster_threaten()
{
	CBaseMonster* monster = smart_cast<CBaseMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CBaseMonster : cannot access class member try_monster_threaten!");
		return false;
	}
	return monster->com_man().script_try_threaten();
}

bool CScriptGameObject::monster_capture_control(int type)
{
	CBaseMonster* monster = smart_cast<CBaseMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CBaseMonster : cannot access class member monster_capture_control!");
		return false;
	}
	// defer to a scheme owner (mob_capture / logic): never grab components from under it
	if (monster->GetScriptControl())
		return false;
	// an active jump holds a pure capture; stealing a component mid-flight destabilizes it
	if (monster->control().is_captured_pure())
		return false;
	ControlCom::EControlType control_type = (ControlCom::EControlType)type;
	if (!monster->control().is_registered(control_type))
		return false;
	monster->com_man().script_capture(control_type);
	return monster->control().get_capturer(control_type) == &monster->com_man();
}

bool CScriptGameObject::monster_release_control(int type)
{
	CBaseMonster* monster = smart_cast<CBaseMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CBaseMonster : cannot access class member monster_release_control!");
		return false;
	}
	ControlCom::EControlType control_type = (ControlCom::EControlType)type;
	if (!monster->control().is_registered(control_type))
		return false;
	monster->com_man().script_release(control_type);
	return monster->control().get_capturer(control_type) != &monster->com_man();
}

float CScriptGameObject::GetObjectVisibleDistance(const CScriptGameObject* obj)
{
    if (obj == nullptr)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CGameObject: [%s] wrong parameters.", object().cNameSect_str());
    }
    CCustomMonster* custom_monster = smart_cast<CCustomMonster*>(&object());
    if (custom_monster == nullptr)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CGameObject: [%s] cannot access class member CCustomMonster.", object().cNameSect_str());
    }
    float distance = 0.0F;
    return custom_monster->visual_memory()->object_visible_distance(&obj->object(), distance);
}

float CScriptGameObject::GetObjectLuminocity(const CScriptGameObject* obj)
{
    if (obj == nullptr)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CGameObject: [%s] wrong parameters.", object().cNameSect_str());
    }
    CCustomMonster* custom_monster = smart_cast<CCustomMonster*>(&object());
    if (custom_monster == nullptr)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CGameObject: [%s] cannot access class member CCustomMonster.", object().cNameSect_str());
    }
    return custom_monster->visual_memory()->object_luminocity(&obj->object());
}

CScriptGameObject* CScriptGameObject::GetEnemy() const
{
	CCustomMonster* l_tpCustomMonster = smart_cast<CCustomMonster*>(&object());
	if (l_tpCustomMonster && l_tpCustomMonster->g_Alive())
	{
		if (l_tpCustomMonster->GetCurrentEnemy() && !l_tpCustomMonster->GetCurrentEnemy()->getDestroy())
			return (
				l_tpCustomMonster->GetCurrentEnemy()->lua_game_object());
		else return (0);
	}
	else
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CScriptGameObject : cannot access class member GetEnemy!");
		return (0);
	}
}

CScriptGameObject* CScriptGameObject::GetCorpse() const
{
	CCustomMonster* l_tpCustomMonster = smart_cast<CCustomMonster*>(&object());
	if (l_tpCustomMonster)
		if (l_tpCustomMonster->GetCurrentCorpse() && !l_tpCustomMonster->GetCurrentCorpse()->getDestroy())
			return (
				l_tpCustomMonster->GetCurrentCorpse()->lua_game_object());
		else return (0);
	else
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CScriptGameObject : cannot access class member GetCorpse!");
		return (0);
	}
}

bool CScriptGameObject::CheckTypeVisibility(const char* section_name)
{
	CCustomMonster* l_tpCustomMonster = smart_cast<CCustomMonster*>(&object());
	if (l_tpCustomMonster)
		return (l_tpCustomMonster->CheckTypeVisibility(section_name));
	else
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CScriptGameObject : cannot access class member CheckTypeVisibility!");
		return (false);
	}
}

CScriptGameObject* CScriptGameObject::GetCurrentWeapon() const
{
	CAI_Stalker* l_tpStalker = smart_cast<CAI_Stalker*>(&object());
	if (!l_tpStalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CAI_Stalker : cannot access class member GetCurrentWeapon!");
		return (0);
	}
	CGameObject* current_weapon = l_tpStalker->GetCurrentWeapon();
	return (current_weapon ? current_weapon->lua_game_object() : 0);
}

void CScriptGameObject::deadbody_closed(bool status)
{
	CInventoryOwner* inventoryOwner = smart_cast<CInventoryOwner*>(&object());
	if (!inventoryOwner)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CInventoryOwner : cannot access class member deadbody_closed!");
		return;
	}
	inventoryOwner->deadbody_closed(status);
}

bool CScriptGameObject::deadbody_closed_status()
{
	CInventoryOwner* inventoryOwner = smart_cast<CInventoryOwner*>(&object());
	if (!inventoryOwner)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CInventoryOwner : cannot access class member deadbody_closed_status!");
		return (0);
	}
	return inventoryOwner->deadbody_closed_status();
}

void CScriptGameObject::can_select_weapon(bool status)
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CAI_Stalker : cannot access class member can_select_weapon!");
		return;
	}
	stalker->can_select_weapon(status);
}

bool CScriptGameObject::can_select_weapon() const
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CAI_Stalker : cannot access class member can_select_weapon!");
		return (0);
	}
	return stalker->can_select_weapon();
}

void CScriptGameObject::deadbody_can_take(bool status)
{
	CInventoryOwner* inventoryOwner = smart_cast<CInventoryOwner*>(&object());
	if (!inventoryOwner)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CInventoryOwner : cannot access class member deadbody_can_take!");
		return;
	}
	inventoryOwner->deadbody_can_take(status);
}

bool CScriptGameObject::deadbody_can_take_status()
{
	CInventoryOwner* inventoryOwner = smart_cast<CInventoryOwner*>(&object());
	if (!inventoryOwner)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CInventoryOwner : cannot access class member deadbody_can_take_status!");
		return (0);
	}
	return inventoryOwner->deadbody_can_take_status();
}

#include "CustomOutfit.h"

CScriptGameObject* CScriptGameObject::GetCurrentOutfit() const
{
	CInventoryOwner* inventoryOwner = smart_cast<CInventoryOwner*>(&object());
	if (!inventoryOwner)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CInventoryOwner : cannot access class member GetCurrentOutfit!");
		return (0);
	}
	CGameObject* current_equipment = inventoryOwner->GetOutfit();
	return (current_equipment ? current_equipment->lua_game_object() : 0);
}


float CScriptGameObject::GetCurrentOutfitProtection(int hit_type)
{
	CInventoryOwner* inventoryOwner = smart_cast<CInventoryOwner*>(&object());
	if (!inventoryOwner)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CInventoryOwner : cannot access class member GetCurrentOutfitProtection!");
		return (0);
	}
	CGameObject* current_equipment = inventoryOwner->GetOutfit();
	CCustomOutfit* o = smart_cast<CCustomOutfit*>(current_equipment);
	if (!o) return 0.0f;

	return o->GetDefHitTypeProtection(ALife::EHitType(hit_type));
}

CScriptGameObject* CScriptGameObject::GetFood() const
{
	CAI_Stalker* l_tpStalker = smart_cast<CAI_Stalker*>(&object());
	if (!l_tpStalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CAI_Stalker : cannot access class member GetFood!");
		return (0);
	}
	CGameObject* food = l_tpStalker->GetFood() ? &l_tpStalker->GetFood()->object() : 0;
	return (food ? food->lua_game_object() : 0);
}

CScriptGameObject* CScriptGameObject::GetMedikit() const
{
	CAI_Stalker* l_tpStalker = smart_cast<CAI_Stalker*>(&object());
	if (!l_tpStalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CAI_Stalker : cannot access class member GetCurrentWeapon!");
		return (0);
	}
	CGameObject* medikit = l_tpStalker->GetMedikit() ? &l_tpStalker->GetMedikit()->object() : 0;
	return (medikit ? medikit->lua_game_object() : 0);
}

LPCSTR CScriptGameObject::GetPatrolPathName()
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
	{
		CScriptEntity* script_monster = smart_cast<CScriptEntity*>(&object());
		if (!script_monster)
		{
			ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
			                                "CGameObject : cannot access class member GetPatrolPathName!");
			return ("");
		}
		else
			return (script_monster->GetPatrolPathName());
	}
	else
		return (*stalker->movement().patrol().path_name());
}

void CScriptGameObject::add_animation(LPCSTR animation, bool hand_usage, bool use_movement_controller)
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CGameObject : cannot access class member add_animation!");
		return;
	}

	if (stalker->movement().current_params().cover())
	{
		ai().script_engine().script_log(eLuaMessageTypeError,
		                                "Cannot add animation [%s]: object [%s] is in smart_cover!", animation,
		                                stalker->cName().c_str());
	}

	if (stalker->animation().global_selector())
	{
		ai().script_engine().script_log(
			eLuaMessageTypeError,
			"Cannot add animation [%s]: global selector is set for object [%s], in_smart_cover returned [%s]!",
			animation,
			stalker->cName().c_str(),
			in_smart_cover() ? "true" : "false"
		);
		return;
	}

	stalker->animation().add_script_animation(animation, hand_usage, use_movement_controller);
}

void CScriptGameObject::add_animation(LPCSTR animation, bool hand_usage, Fvector position, Fvector rotation,
                                      bool local_animation)
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CGameObject : cannot access class member add_animation!");
		return;
	}

	if (stalker->movement().current_params().cover())
	{
		ai().script_engine().script_log(eLuaMessageTypeError,
		                                "Cannot add animation [%s]: object [%s] is in smart_cover!", animation,
		                                stalker->cName().c_str());
	}

	if (stalker->animation().global_selector())
	{
		ai().script_engine().script_log(
			eLuaMessageTypeError,
			"Cannot add animation [%s]: global selector is set for object [%s], in_smart_cover returned [%s]!",
			animation,
			stalker->cName().c_str(),
			in_smart_cover() ? "true" : "false"
		);
		return;
	}

	stalker->animation().add_script_animation(animation, hand_usage, position, rotation, local_animation);
}

void CScriptGameObject::clear_animations()
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CGameObject : cannot access class member clear_animations!");
		return;
	}
	stalker->animation().clear_script_animations();
}

int CScriptGameObject::animation_count() const
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CGameObject : cannot access class member clear_animations!");
		return (-1);
	}
	return ((int)stalker->animation().script_animations().size());
}

Flags32 CScriptGameObject::get_actor_relation_flags() const
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	THROW(stalker);

	return stalker->m_actor_relation_flags;
}

void CScriptGameObject::set_actor_relation_flags(Flags32 flags)
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	THROW(stalker);
	stalker->m_actor_relation_flags = flags;
}

void CScriptGameObject::set_patrol_path(LPCSTR path_name, const PatrolPathManager::EPatrolStartType patrol_start_type,
                                        const PatrolPathManager::EPatrolRouteType patrol_route_type, bool random)
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CAI_Stalker : cannot access class member movement!");
	else
		stalker->movement().patrol().set_path(path_name, patrol_start_type, patrol_route_type, random);
}

void CScriptGameObject::inactualize_patrol_path()
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CAI_Stalker : cannot access class member movement!");
	else
		stalker->movement().patrol().make_inactual();
}

void CScriptGameObject::set_dest_level_vertex_id(u32 level_vertex_id)
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CAI_Stalker : cannot access class member set_dest_level_vertex_id!");
	else
	{
		if (!ai().level_graph().valid_vertex_id(level_vertex_id))
		{
#ifdef DEBUG
			ai().script_engine().script_log				(ScriptStorage::eLuaMessageTypeError,"CAI_Stalker : invalid vertex id being setup by action %s!",stalker->brain().CStalkerPlanner::current_action().m_action_name);
#endif
			return;
		}
		if (!stalker->movement().restrictions().accessible(level_vertex_id))
		{
			ai().script_engine().script_log(
				ScriptStorage::eLuaMessageTypeError,
				"! you are trying to setup destination for the stalker %s, which is not accessible by its restrictors in[%s] out[%s]",
				stalker->cName().c_str(),
				Level().space_restriction_manager().in_restrictions(stalker->ID()).c_str(),
				Level().space_restriction_manager().out_restrictions(stalker->ID()).c_str()
			);
			return;
		}
		stalker->movement().set_level_dest_vertex(level_vertex_id);
	}
}

void CScriptGameObject::set_dest_game_vertex_id(GameGraph::_GRAPH_ID game_vertex_id)
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CAI_Stalker : cannot access class member set_dest_game_vertex_id!");
	else
	{
		if (!ai().game_graph().valid_vertex_id(game_vertex_id))
		{
#ifdef DEBUG
			ai().script_engine().script_log				(ScriptStorage::eLuaMessageTypeError,"CAI_Stalker : invalid vertex id being setup by action %s!",stalker->brain().CStalkerPlanner::current_action().m_action_name);
#endif
			return;
		}
		stalker->movement().set_game_dest_vertex(game_vertex_id);
	}
}

void CScriptGameObject::set_movement_selection_type(ESelectionType selection_type)
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CAI_Stalker : cannot access class member set_movement_selection_type!");
	stalker->movement().game_selector().set_selection_type(selection_type);
}

CHARACTER_RANK_VALUE CScriptGameObject::GetRank()
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CAI_Stalker : cannot access class member GetRank!");
		return (CHARACTER_RANK_VALUE(0));
	}
	else
		return (stalker->Rank());
}

LPCSTR CScriptGameObject::GetRankName()
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CGameObject: [%s] rank_name() called on non-stalker object",
		                                object().cNameSect_str());
		return ("");
	}
	return (*stalker->CharacterInfo().Rank().id());
}

bool CScriptGameObject::affect_cover() const
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CGameObject: [%s] affect_cover() called on non-stalker object",
		                                object().cNameSect_str());
		return (false);
	}
	return (stalker->brain().affect_cover());
}

void CScriptGameObject::best_cover_invalidate()
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CGameObject: [%s] best_cover_invalidate() called on non-stalker object",
		                                object().cNameSect_str());
		return;
	}
	stalker->best_cover_invalidate();
}

LPCSTR CScriptGameObject::GetCurrentSmartCoverName()
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CGameObject: [%s] get_current_smart_cover_name() called on non-stalker object",
		                                object().cNameSect_str());
		return ("");
	}
	smart_cover::cover const* cover = stalker->get_current_smart_cover();
	if (!cover)
		return ("");
	return (*cover->object().cName());
}

LPCSTR CScriptGameObject::GetCurrentLoopholeId()
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CGameObject: [%s] get_current_loophole_id() called on non-stalker object",
		                                object().cNameSect_str());
		return ("");
	}
	smart_cover::loophole const* loophole = stalker->get_current_loophole();
	if (!loophole)
		return ("");
	return (*loophole->id());
}

void CScriptGameObject::set_desired_position()
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CAI_Stalker : cannot access class member movement!");
	else
		stalker->movement().set_desired_position(0);
}

void CScriptGameObject::set_desired_position(const Fvector* desired_position)
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CAI_Stalker : cannot access class member movement!");
	else
	{
		THROW2(desired_position || stalker->movement().restrictions().accessible(*desired_position), *stalker->cName());
		stalker->movement().set_desired_position(desired_position);
	}
}

void CScriptGameObject::set_desired_direction()
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CAI_Stalker : cannot access class member movement!");
	else
		stalker->movement().set_desired_direction(0);
}

void CScriptGameObject::set_desired_direction(const Fvector* desired_direction)
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CAI_Stalker : cannot access class member movement!");
	else
	{
		if (fsimilar(desired_direction->magnitude(), 0.f))
			ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
			                                "CAI_Stalker : [%s] set_desired_direction - you passed zero direction!",
			                                stalker->cName().c_str());
		else
		{
			if (!fsimilar(desired_direction->magnitude(), 1.f))
				ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
				                                "CAI_Stalker : [%s] set_desired_direction - you passed non-normalized direction!",
				                                stalker->cName().c_str());
		}

		Fvector direction = *desired_direction;
		direction.normalize_safe();
		stalker->movement().set_desired_direction(&direction);
	}
}

void CScriptGameObject::set_body_state(EBodyState body_state)
{
	THROW((body_state == eBodyStateStand) || (body_state == eBodyStateCrouch));
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CAI_Stalker : cannot access class member movement!");
	else
		stalker->movement().set_body_state(body_state);
}

void CScriptGameObject::set_movement_type(EMovementType movement_type)
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CAI_Stalker : cannot access class member movement!");
	else
		stalker->movement().set_movement_type(movement_type);
}

void CScriptGameObject::set_mental_state(EMentalState mental_state)
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CAI_Stalker : cannot access class member movement!");
	else
	{
#if 0//def DEBUG
		if (mental_state != eMentalStateDanger) {
			if (stalker->brain().initialized()) {
				if (stalker->brain().current_action_id() == StalkerDecisionSpace::eWorldOperatorCombatPlanner) {
					ai().script_engine().script_log	(ScriptStorage::eLuaMessageTypeError,"CAI_Stalker : set_mental_state is used during universal combat!, object[%s]", stalker->cName().c_str());
//					return;
				}
			}
		}
#endif // DEBUG
		stalker->movement().set_mental_state(mental_state);
	}
}

void CScriptGameObject::set_path_type(MovementManager::EPathType path_type)
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CAI_Stalker : cannot access class member movement!");
	else
		stalker->movement().set_path_type(path_type);
}

void CScriptGameObject::set_detail_path_type(DetailPathManager::EDetailPathType detail_path_type)
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CAI_Stalker : cannot access class member movement!");
	else
		stalker->movement().set_detail_path_type(detail_path_type);
}

MonsterSpace::EBodyState CScriptGameObject::body_state() const
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CAI_Stalker : cannot access class member body_state!");
		return (MonsterSpace::eBodyStateStand);
	}
	return (stalker->movement().body_state());
}

MonsterSpace::EBodyState CScriptGameObject::target_body_state() const
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CAI_Stalker : cannot access class member body_state!");
		return (MonsterSpace::eBodyStateStand);
	}
	return (stalker->movement().target_body_state());
}

MonsterSpace::EMovementType CScriptGameObject::movement_type() const
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CAI_Stalker : cannot access class member movement_type!");
		return (MonsterSpace::eMovementTypeStand);
	}
	return (stalker->movement().movement_type());
}

MonsterSpace::EMovementType CScriptGameObject::target_movement_type() const
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CAI_Stalker : cannot access class member target_movement_type!");
		return (MonsterSpace::eMovementTypeStand);
	}
	return (stalker->movement().target_movement_type());
}

MonsterSpace::EMentalState CScriptGameObject::mental_state() const
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CAI_Stalker : cannot access class member mental_state!");
		return (MonsterSpace::eMentalStateDanger);
	}
	return (stalker->movement().mental_state());
}

MonsterSpace::EMentalState CScriptGameObject::target_mental_state() const
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CAI_Stalker : cannot access class member mental_state!");
		return (MonsterSpace::eMentalStateDanger);
	}
	return (stalker->movement().target_mental_state());
}

MovementManager::EPathType CScriptGameObject::path_type() const
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CAI_Stalker : cannot access class member path_type!");
		return (MovementManager::ePathTypeNoPath);
	}
	return (stalker->movement().path_type());
}

DetailPathManager::EDetailPathType CScriptGameObject::detail_path_type() const
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CAI_Stalker : cannot access class member detail_path_type!");
		return (DetailPathManager::eDetailPathTypeSmooth);
	}
	return (DetailPathManager::eDetailPathTypeSmooth);
}

void CScriptGameObject::set_sight(SightManager::ESightType sight_type, Fvector* vector3d, u32 dwLookOverDelay)
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CSightManager : cannot access class member set_sight!");
	else
	{
		if ((sight_type == SightManager::eSightTypeDirection) && vector3d && (_abs(vector3d->magnitude() - 1.f) > .01f))
		{
			VERIFY2(false, make_string("non-normalized direction passed [%f][%f][%f]", VPUSH(*vector3d)));
			vector3d->normalize();
		}

		stalker->sight().setup(sight_type, vector3d);
	}
}

void CScriptGameObject::set_sight(SightManager::ESightType sight_type, bool torso_look, bool path)
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CSightManager : cannot access class member set_sight!");
	else
		stalker->sight().setup(sight_type, torso_look, path);
}

void CScriptGameObject::set_sight(SightManager::ESightType sight_type, Fvector& vector3d, bool torso_look = false)
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CSightManager : cannot access class member set_sight!");
	else
	{
		if ((sight_type == SightManager::eSightTypeDirection) && (_abs(vector3d.magnitude() - 1.f) > .01f))
		{
			VERIFY2(false, make_string("non-normalized direction passed [%f][%f][%f]", VPUSH(vector3d)));
			vector3d.normalize();
		}

		stalker->sight().setup(sight_type, vector3d, torso_look);
	}
}

void CScriptGameObject::set_sight(SightManager::ESightType sight_type, Fvector* vector3d)
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CSightManager : cannot access class member set_sight!");
	else
	{
		if ((sight_type == SightManager::eSightTypeDirection) && vector3d && (_abs(vector3d->magnitude() - 1.f) > .01f))
		{
			VERIFY2(false, make_string("non-normalized direction passed [%f][%f][%f]", VPUSH(*vector3d)));
			vector3d->normalize();
		}

		stalker->sight().setup(sight_type, vector3d);
	}
}

void CScriptGameObject::set_sight(CScriptGameObject* object_to_look)
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CSightManager : cannot access class member set_sight!");
	else
		stalker->sight().setup(&object_to_look->object());
}

void CScriptGameObject::set_sight(CScriptGameObject* object_to_look, bool torso_look)
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CSightManager : cannot access class member set_sight!");
	else
		stalker->sight().setup(&object_to_look->object(), torso_look);
}

void CScriptGameObject::set_sight(CScriptGameObject* object_to_look, bool torso_look, bool fire_object)
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CSightManager : cannot access class member set_sight!");
	else
		stalker->sight().setup(&object_to_look->object(), torso_look, fire_object);
}

void CScriptGameObject::set_sight(CScriptGameObject* object_to_look, bool torso_look, bool fire_object, bool no_pitch)
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CSightManager : cannot access class member set_sight!");
	else
		stalker->sight().setup(CSightAction(&object_to_look->object(), torso_look, fire_object, no_pitch));
}

void CScriptGameObject::set_sight(const CMemoryInfo* memory_object, bool torso_look)
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CSightManager : cannot access class member set_sight!");
	else
		stalker->sight().setup(memory_object, torso_look);
}

// CAI_Stalker
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

u32 CScriptGameObject::GetInventoryObjectCount() const
{
	CInventoryOwner* l_tpInventoryOwner = smart_cast<CInventoryOwner*>(&object());
	if (l_tpInventoryOwner)
		return (l_tpInventoryOwner->inventory().dwfGetObjectCount());
	else
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CScriptGameObject : cannot access class member obj_count!");
		return (0);
	}
}

CScriptGameObject* CScriptGameObject::GetActiveItem()
{
	CInventoryOwner* l_tpInventoryOwner = smart_cast<CInventoryOwner*>(&object());
	if (l_tpInventoryOwner)
		if (l_tpInventoryOwner->inventory().ActiveItem())
			return (l_tpInventoryOwner->inventory().ActiveItem()->object().lua_game_object());
		else
			return (0);
	else
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CScriptGameObject : cannot access class member activge_item!");
		return (0);
	}
}

CScriptGameObject* CScriptGameObject::GetObjectByName(LPCSTR caObjectName) const
{
	CInventoryOwner* l_tpInventoryOwner = smart_cast<CInventoryOwner*>(&object());
	CInventoryBox* inventory_box = smart_cast<CInventoryBox*>(&this->object());
	if (l_tpInventoryOwner)
	{
		CInventoryItem* l_tpInventoryItem = l_tpInventoryOwner->inventory().GetItemFromInventory(caObjectName);
		CGameObject* l_tpGameObject = smart_cast<CGameObject*>(l_tpInventoryItem);
		if (!l_tpGameObject)
			return (0);
		else
			return (l_tpGameObject->lua_game_object());
	}
	else if (inventory_box)
	{
		xr_vector<u16>::const_iterator I = inventory_box->m_items.begin();
		xr_vector<u16>::const_iterator E = inventory_box->m_items.end();
		for (; I != E; ++I)
		{
			CGameObject* GO = smart_cast<CGameObject*>(Level().Objects.net_Find(*I));
			if (GO && GO->cNameSect() == caObjectName) {
				return (GO->lua_game_object());
			}
		}
		return (0);
	}
	else
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CScriptGameObject : cannot access class member object!");
		return (0);
	}
}

CScriptGameObject* CScriptGameObject::GetObjectByIndex(int iIndex) const
{
	CInventoryOwner* l_tpInventoryOwner = smart_cast<CInventoryOwner*>(&object());
	if (l_tpInventoryOwner)
	{
		CInventoryItem* l_tpInventoryItem = l_tpInventoryOwner->inventory().tpfGetObjectByIndex(iIndex);
		CGameObject* l_tpGameObject = smart_cast<CGameObject*>(l_tpInventoryItem);
		if (!l_tpGameObject)
			return (0);
		else
			return (l_tpGameObject->lua_game_object());
	}
	else
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CScriptGameObject : cannot access class member object!");
		return (0);
	}
}

CScriptGameObject* CScriptGameObject::GetObjectById(u16 id) const
{
	CInventoryOwner* l_tpInventoryOwner = smart_cast<CInventoryOwner*>(&object());
	CInventoryBox* inventory_box = smart_cast<CInventoryBox*>(&this->object());
	if (l_tpInventoryOwner)
	{
		CInventoryItem* l_tpInventoryItem = l_tpInventoryOwner->inventory().GetItemFromInventory(id);
		CGameObject* l_tpGameObject = smart_cast<CGameObject*>(l_tpInventoryItem);
		if (!l_tpGameObject)
			return (0);
		else
			return (l_tpGameObject->lua_game_object());
	}
	else if (inventory_box)
	{
		xr_vector<u16>::const_iterator I = inventory_box->m_items.begin();
		xr_vector<u16>::const_iterator E = inventory_box->m_items.end();
		for (; I != E; ++I)
		{
			if (*I == id)
			{
				CGameObject* GO = smart_cast<CGameObject*>(Level().Objects.net_Find(*I));
				if (GO) return (GO->lua_game_object());
				return (0);
			}
		}
		return (0);
	}
	else
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
			"CScriptGameObject : cannot access class member object_id!");
		return (0);
	}
}

void CScriptGameObject::EnableAnomaly()
{
	CCustomZone* zone = smart_cast<CCustomZone*>(&object());
	THROW(zone);
	zone->ZoneEnable();
}

void CScriptGameObject::DisableAnomaly()
{
	CCustomZone* zone = smart_cast<CCustomZone*>(&object());
	THROW(zone);
	zone->ZoneDisable();
}

bool CScriptGameObject::IsEnabledAnomaly()
{
    CCustomZone* zone = smart_cast<CCustomZone*>(&object());
    THROW(zone);
    return zone->IsEnabled();
}

void CScriptGameObject::ChangeAnomalyIdlePart(LPCSTR name, bool bIdleLight)
{
	CCustomZone* zone = smart_cast<CCustomZone*>(&object());
	THROW(zone);
	zone->ChangeIdleParticles(name, bIdleLight);
}

float CScriptGameObject::GetAnomalyPower()
{
	CCustomZone* zone = smart_cast<CCustomZone*>(&object());
	THROW(zone);
	return zone->GetMaxPower();
}

void CScriptGameObject::SetAnomalyPower(float p)
{
	CCustomZone* zone = smart_cast<CCustomZone*>(&object());
	THROW(zone);
	zone->SetMaxPower(p);
}

float CScriptGameObject::GetAnomalyRadius()
{
	CCustomZone* zone = smart_cast<CCustomZone*>(&object());
	THROW(zone);
	return zone->GetEffectiveRadius();
}

void CScriptGameObject::SetAnomalyRadius(float p)
{
	CCustomZone* zone = smart_cast<CCustomZone*>(&object());
	THROW(zone);
	zone->SetEffectiveRadius(p);
}

void CScriptGameObject::MoveAnomaly(Fvector pos)
{
	CCustomZone* zone = smart_cast<CCustomZone*>(&object());
	THROW(zone);
	zone->MoveScript(pos);
}

void CScriptGameObject::set_color_animator(LPCSTR name, bool bFlicker, int flickerChance, float flickerDelay,
                                           int framerate)
{
	CTorch* torch = smart_cast<CTorch*>(&object());
	if (torch)
	{
		torch->SetLanim(name, bFlicker, flickerChance, flickerDelay, framerate);
		return;
	}

	CFlashlight* flashlight = smart_cast<CFlashlight*>(&object());
	if (flashlight)
		flashlight->SetLanim(name, bFlicker, flickerChance, flickerDelay, framerate);
}

void CScriptGameObject::reset_color_animator()
{
	CTorch* torch = smart_cast<CTorch*>(&object());
	if (torch)
	{
		torch->ResetLanim();
		return;
	}

	CFlashlight* flashlight = smart_cast<CFlashlight*>(&object());
	if (flashlight)
		flashlight->ResetLanim();
}

bool CScriptGameObject::weapon_strapped() const
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CScriptGameObject : cannot access class member weapon_strapped!");
		return (false);
	}

	bool const result = stalker->weapon_strapped();
	//	Msg					( "[%6d][%s] weapon_strapped = %s", Device.dwTimeGlobal, stalker->cName().c_str(), result ? "true" : "false" );
	return (result);
}

bool CScriptGameObject::weapon_unstrapped() const
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CScriptGameObject : cannot access class member weapon_unstrapped!");
		return (false);
	}
	bool const result = stalker->weapon_unstrapped();
	//	Msg					( "[%6d][%s] weapon_unstrapped = %s", Device.dwTimeGlobal, stalker->cName().c_str(), result ? "true" : "false" );
	return (result);
}

bool CScriptGameObject::path_completed() const
{
	CCustomMonster* monster = smart_cast<CCustomMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CScriptGameObject : cannot access class member path_completed!");
		return (false);
	}
	return (monster->movement().path_completed());
}

void CScriptGameObject::patrol_path_make_inactual()
{
	CCustomMonster* monster = smart_cast<CCustomMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CScriptGameObject : cannot access class member patrol_path_make_inactual!");
		return;
	}
	monster->movement().patrol().make_inactual();
}


Fvector CScriptGameObject::head_orientation() const
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CScriptGameObject : cannot access class member head_orientation!");
		return (Fvector().set(flt_max,flt_max,flt_max));
	}
	const SRotation& r = stalker->movement().head_orientation().current;
	return (Fvector().setHP(-r.yaw, -r.pitch));
}

void CScriptGameObject::info_add(LPCSTR text)
{
#ifdef DEBUG
	DBG().object_info(&object(),this).add_item	(text, D3DCOLOR_XRGB(255,0,0), 0);
#endif
}

void CScriptGameObject::info_clear()
{
#ifdef DEBUG
	DBG().object_info(&object(),this).clear		();
#endif
}

void CScriptGameObject::jump(const Fvector& position, float factor)
{
	CBaseMonster* monster = smart_cast<CBaseMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CScriptGameObject : cannot process jump for not a monster!");
		return;
	}

	monster->jump(position, factor);
}


void CScriptGameObject::make_object_visible_somewhen(CScriptGameObject* object)
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&this->object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CAI_Stalker : cannot access class member make_object_visible_somewhen!");
		return;
	}

	CEntityAlive* entity_alive = smart_cast<CEntityAlive*>(&object->object());
	if (!entity_alive)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CEntityAlive : cannot access class member make_object_visible_somewhen!");
		return;
	}

	stalker->memory().make_object_visible_somewhen(entity_alive);
}

void CScriptGameObject::sell_condition(CScriptIniFile* ini_file, LPCSTR section)
{
	CInventoryOwner* inventory_owner = smart_cast<CInventoryOwner*>(&object());
	if (!inventory_owner)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CInventoryOwner : cannot access class member sell_condition!");
		return;
	}

	inventory_owner->trade_parameters().process(CTradeParameters::action_sell(0), *ini_file, section);
}

void CScriptGameObject::sell_condition(float friend_factor, float enemy_factor)
{
	CInventoryOwner* inventory_owner = smart_cast<CInventoryOwner*>(&object());
	if (!inventory_owner)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CInventoryOwner : cannot access class member sell_condition!");
		return;
	}

	inventory_owner->trade_parameters().default_factors(
		CTradeParameters::action_sell(0),
		CTradeFactors(
			friend_factor,
			enemy_factor
		)
	);
}

void CScriptGameObject::buy_condition(CScriptIniFile* ini_file, LPCSTR section)
{
	CInventoryOwner* inventory_owner = smart_cast<CInventoryOwner*>(&object());
	if (!inventory_owner)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CInventoryOwner : cannot access class member buy_condition!");
		return;
	}

	inventory_owner->trade_parameters().process(CTradeParameters::action_buy(0), *ini_file, section);
}

void CScriptGameObject::buy_condition(float friend_factor, float enemy_factor)
{
	CInventoryOwner* inventory_owner = smart_cast<CInventoryOwner*>(&object());
	if (!inventory_owner)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CInventoryOwner : cannot access class member buy_condition!");
		return;
	}

	inventory_owner->trade_parameters().default_factors(
		CTradeParameters::action_buy(0),
		CTradeFactors(
			friend_factor,
			enemy_factor
		)
	);
}

void CScriptGameObject::show_condition(CScriptIniFile* ini_file, LPCSTR section)
{
	CInventoryOwner* inventory_owner = smart_cast<CInventoryOwner*>(&object());
	if (!inventory_owner)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CInventoryOwner : cannot access class member show_condition!");
		return;
	}

	inventory_owner->trade_parameters().process(
		CTradeParameters::action_show(0),
		*ini_file,
		section
	);
}

void CScriptGameObject::buy_supplies(CScriptIniFile* ini_file, LPCSTR section)
{
	CInventoryOwner* inventory_owner = smart_cast<CInventoryOwner*>(&object());
	if (!inventory_owner)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CInventoryOwner : cannot access class member buy_condition!");
		return;
	}

	inventory_owner->buy_supplies(
		*ini_file,
		section
	);
}

void CScriptGameObject::buy_item_condition_factor(float factor)
{
	CInventoryOwner* inventory_owner = smart_cast<CInventoryOwner*>(&object());
	if (!inventory_owner)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CInventoryOwner : cannot access class member buy_item_condition_factor!");
		return;
	}

	inventory_owner->trade_parameters().buy_item_condition_factor = factor;
}

void CScriptGameObject::buy_item_exponent(float factor)
{
	CInventoryOwner* inventory_owner = smart_cast<CInventoryOwner*>(&object());
	if (!inventory_owner)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CInventoryOwner : cannot access class member buy_item_exponent!");
		return;
	}

	inventory_owner->trade_parameters().buy_item_exponent = factor;
}

void CScriptGameObject::sell_item_exponent(float factor)
{
	CInventoryOwner* inventory_owner = smart_cast<CInventoryOwner*>(&object());
	if (!inventory_owner)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CInventoryOwner : cannot access class member sell_item_exponent!");
		return;
	}

	inventory_owner->trade_parameters().sell_item_exponent = factor;
}

void sell_condition(CScriptIniFile* ini_file, LPCSTR section)
{
	default_trade_parameters().process(CTradeParameters::action_sell(0), *ini_file, section);
}

void sell_condition(float friend_factor, float enemy_factor)
{
	default_trade_parameters().default_factors(
		CTradeParameters::action_sell(0),
		CTradeFactors(
			friend_factor,
			enemy_factor
		)
	);
}

void buy_condition(CScriptIniFile* ini_file, LPCSTR section)
{
	default_trade_parameters().process(CTradeParameters::action_buy(0), *ini_file, section);
}

void buy_condition(float friend_factor, float enemy_factor)
{
	default_trade_parameters().default_factors(
		CTradeParameters::action_buy(0),
		CTradeFactors(
			friend_factor,
			enemy_factor
		)
	);
}

void show_condition(CScriptIniFile* ini_file, LPCSTR section)
{
	default_trade_parameters().process(CTradeParameters::action_show(0), *ini_file, section);
}

LPCSTR CScriptGameObject::sound_prefix() const
{
	CCustomMonster* custom_monster = smart_cast<CCustomMonster*>(&object());
	if (!custom_monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CCustomMonster : cannot access class member sound_prefix!");
		return (0);
	}

	return (*custom_monster->sound().sound_prefix());
}

void CScriptGameObject::sound_prefix(LPCSTR sound_prefix)
{
	CCustomMonster* custom_monster = smart_cast<CCustomMonster*>(&object());
	if (!custom_monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CCustomMonster : cannot access class member sound_prefix!");
		return;
	}

	custom_monster->sound().sound_prefix(sound_prefix);
}

bool CScriptGameObject::is_weapon_going_to_be_strapped(CScriptGameObject const* object) const
{
	if (!object)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CGameObject : cannot access class member is_weapon_going_to_be_strapped (object passed is null)!");
		return false;
	}

	CAI_Stalker const* stalker = smart_cast<CAI_Stalker const*>(&this->object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CGameObject : cannot access class member is_weapon_going_to_be_strapped!");
		return false;
	}

	return stalker->is_weapon_going_to_be_strapped(&object->object());
}

::luabind::object CScriptGameObject::g_fireParams()
{
    ::luabind::object lua_table = ::luabind::newtable(ai().script_engine().lua());
    Fvector pos, dir;
    if (object().cast_actor())
    {
        object().cast_actor()->g_fireParams(nullptr, pos, dir);
        lua_table["pos"] = pos;
        lua_table["dir"] = dir;
        return lua_table;
    }
    if (object().cast_stalker())
    {
        object().cast_stalker()->g_fireParams(nullptr, pos, dir);
        lua_table["pos"] = pos;
        lua_table["dir"] = dir;
        return lua_table;
    }
    ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CGameObject : object invalid.");
    return lua_table;
}

//Alundaio:
#ifdef GAME_OBJECT_EXTENDED_EXPORTS
u16 CScriptGameObject::AmmoGetCount()
{
	CWeaponAmmo* ammo = smart_cast<CWeaponAmmo*>(&object());
	if (!ammo)
		return 0;

	return ammo->m_boxCurr;
}

void CScriptGameObject::AmmoSetCount(u16 count)
{
	CWeaponAmmo* ammo = smart_cast<CWeaponAmmo*>(&object());
	if (!ammo)
		return;

	ammo->m_boxCurr = count;
}

u16 CScriptGameObject::AmmoBoxSize()
{
	CWeaponAmmo* ammo = smart_cast<CWeaponAmmo*>(&object());
	if (!ammo)
		return 0;

	return ammo->m_boxSize;
}

float CScriptGameObject::GetArtefactHealthRestoreSpeed()
{
	CArtefact* artefact = smart_cast<CArtefact*>(&object());
	THROW(artefact);

	return artefact->GetHealthPower();
}

float CScriptGameObject::GetArtefactRadiationRestoreSpeed()
{
	CArtefact* artefact = smart_cast<CArtefact*>(&object());
	THROW(artefact);

	return artefact->GetRadiationPower();
}

float CScriptGameObject::GetArtefactSatietyRestoreSpeed()
{
	CArtefact* artefact = smart_cast<CArtefact*>(&object());
	THROW(artefact);

	return artefact->GetSatietyPower();
}

float CScriptGameObject::GetArtefactPowerRestoreSpeed()
{
	CArtefact* artefact = smart_cast<CArtefact*>(&object());
	THROW(artefact);

	return artefact->GetPowerPower();
}

float CScriptGameObject::GetArtefactBleedingRestoreSpeed()
{
	CArtefact* artefact = smart_cast<CArtefact*>(&object());
	THROW(artefact);

	return artefact->GetBleedingPower();
}

float CScriptGameObject::GetArtefactImmunity(ALife::EHitType hit_type)
{
	CArtefact* artefact = smart_cast<CArtefact*>(&object());
	THROW(artefact);

	return artefact->GetImmunity(hit_type);
}

void CScriptGameObject::SetArtefactHealthRestoreSpeed(float value)
{
	CArtefact* artefact = smart_cast<CArtefact*>(&object());
	THROW(artefact);

	artefact->SetHealthPower(value);
}

void CScriptGameObject::SetArtefactRadiationRestoreSpeed(float value)
{
	CArtefact* artefact = smart_cast<CArtefact*>(&object());
	THROW(artefact);

	artefact->SetRadiationPower(value);
}

void CScriptGameObject::SetArtefactSatietyRestoreSpeed(float value)
{
	CArtefact* artefact = smart_cast<CArtefact*>(&object());
	THROW(artefact);

	artefact->SetSatietyPower(value);
}

void CScriptGameObject::SetArtefactPowerRestoreSpeed(float value)
{
	CArtefact* artefact = smart_cast<CArtefact*>(&object());
	THROW(artefact);

	artefact->SetPowerPower(value);
}

void CScriptGameObject::SetArtefactBleedingRestoreSpeed(float value)
{
	CArtefact* artefact = smart_cast<CArtefact*>(&object());
	THROW(artefact);

	artefact->SetBleedingPower(value);
}

void CScriptGameObject::SetArtefactImmunity(ALife::EHitType hit_type, float value)
{
	CArtefact* artefact = smart_cast<CArtefact*>(&object());
	THROW(artefact);

	return artefact->SetImmunity(hit_type, value);
}

float CScriptGameObject::GetArtefactAdditionalInventoryWeight()
{
	CArtefact* artefact = smart_cast<CArtefact*>(&object());
	THROW(artefact);

	return artefact->m_additional_weight;
}

void CScriptGameObject::SetArtefactAdditionalInventoryWeight(float value)
{
	CArtefact* artefact = smart_cast<CArtefact*>(&object());
	THROW(artefact);

	artefact->m_additional_weight = value;
}

void CScriptGameObject::AttachVehicle(CScriptGameObject* veh, bool bForce)
{
	CActor* actor = smart_cast<CActor*>(&object());
	if (actor)
	{
		CHolderCustom* vehicle = smart_cast<CHolderCustom*>(&veh->object());
		if (vehicle)
			actor->use_HolderEx(vehicle, bForce);
		else
			ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CGameObject : cannot be cast to CHolderCustom!");
	}

#ifdef HOLDERCUSTOM_NEW
	CAI_Stalker *stalker = smart_cast<CAI_Stalker *>(&object());
	if (stalker)
	{
		CHolderCustom *vehicle = smart_cast<CHolderCustom *>(&veh->object());
		if (vehicle)
			stalker->use_HolderEx(vehicle);
		else
			ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CGameObject : cannot be cast to CHolderCustom!");
	}
#endif
}

void CScriptGameObject::DetachVehicle(bool bForce)
{
	CActor* actor = smart_cast<CActor*>(&object());
	if (actor)
	{
		actor->use_HolderEx(NULL, bForce);
	}

#ifdef HOLDERCUSTOM_NEW
	CAI_Stalker *stalker = smart_cast<CAI_Stalker *>(&object());
	if (stalker)
	{
		stalker->use_HolderEx(NULL);
	}
#endif
}

CScriptGameObject* CScriptGameObject::GetAttachedVehicle()
{
#ifdef HOLDERCUSTOM_NEW
	CAI_Stalker *stalker = smart_cast<CAI_Stalker *>(&object());
	if (stalker && stalker->Holder())
	{
		CGameObject *GO = smart_cast<CGameObject *>(stalker->Holder());
		return (GO) ? GO->lua_game_object() : nullptr;
	}
#endif

	CActor* actor = smart_cast<CActor*>(&object());
	if (!actor)
		return (0);

	CHolderCustom* H = actor->Holder();
	if (!H)
		return (0);

	CGameObject* GO = smart_cast<CGameObject*>(H);
	if (!GO)
		return (0);

	return GO->lua_game_object();
}

u32 CScriptGameObject::PlayHudMotion(LPCSTR M, bool bMixIn, u32 state, float speed, float end)
{
	CWeapon* Weapon = object().cast_weapon();
	if (Weapon)
	{
		if (!Weapon->HudAnimationExist(M))
			return 0;

		return Weapon->PlayHUDMotion(M, bMixIn, Weapon, state, speed, end);
	}

	CHudItem* itm = object().cast_inventory_item()->cast_hud_item();
	if (!itm)
		return 0;

	if (!itm->HudAnimationExist(M))
		return 0;

	return itm->PlayHUDMotion(M, bMixIn, itm, state, speed, end);
}

void CScriptGameObject::SwitchState(u32 state)
{
	CWeapon* Weapon = object().cast_weapon();
	if (Weapon)
	{
		Weapon->SwitchState(state);
		return;
	}

	CInventoryItem* IItem = object().cast_inventory_item();
	if (IItem)
	{
		CHudItem* itm = IItem->cast_hud_item();
		if (itm)
			itm->SwitchState(state);
	}
}

u32 CScriptGameObject::GetState()
{
	CWeapon* Weapon = object().cast_weapon();
	if (Weapon)
	{
		return Weapon->GetState();
	}

	CInventoryItem* IItem = object().cast_inventory_item();
	if (IItem)
	{
		CHudItem* itm = IItem->cast_hud_item();
		if (itm)
			return itm->GetState();
	}

	return 65535;
}

bool CScriptGameObject::WeaponInGrenadeMode()
{
	CWeaponMagazinedWGrenade* wpn = smart_cast<CWeaponMagazinedWGrenade*>(&object());
	if (!wpn)
		return false;

	return wpn->m_bGrenadeMode;
}

void CScriptGameObject::SetBoneVisible(LPCSTR bone_name, bool bVisibility, bool bRecursive, bool bHud)
{
	IKinematics* k = nullptr;

	CHudItem* itm = smart_cast<CHudItem*>(&object());
	if (bHud && itm && itm->HudItemData())
		k = itm->HudItemData()->m_model;
	else
		k = object().Visual()->dcast_PKinematics();

	if (!k)
		return;

	u16 bone_id = k->LL_BoneID(bone_name);
	if (bone_id == BI_NONE)
		return;

	if (bVisibility != k->LL_GetBoneVisible(bone_id))
		k->LL_SetBoneVisible(bone_id, bVisibility, bRecursive);

	return;
}

bool CScriptGameObject::IsBoneVisible(LPCSTR bone_name, bool bHud)
{
	IKinematics* k = nullptr;

	CHudItem* itm = smart_cast<CHudItem*>(&object());
	if (bHud && itm && itm->HudItemData())
		k = itm->HudItemData()->m_model;
	else
		k = object().Visual()->dcast_PKinematics();

	if (!k)
		return false;

	u16 bone_id = k->LL_BoneID(bone_name);
	if (bone_id == BI_NONE)
		return false;

	return k->LL_GetBoneVisible(bone_id) == TRUE ? true : false;
}

float CScriptGameObject::GetLuminocityHemi()
{
	CObject* e = smart_cast<CObject*>(&object());
	if (!e || !e->renderable_ROS())
	{
		return 0;
	}
	return e->renderable_ROS()->get_luminocity_hemi();
}

float CScriptGameObject::GetLuminocity()
{
	CObject* e = smart_cast<CObject*>(&object());
	if (!e || !e->renderable_ROS())
	{
		return 0;
	}
	return e->renderable_ROS()->get_luminocity();
}

void CScriptGameObject::ForceSetPosition(Fvector pos, bool enable)
{
	CPhysicsShellHolder* sh = object().cast_physics_shell_holder();
	if (!sh)
		return;

	Fmatrix& M = object().XFORM();
	M.c = pos;

	CPhysicsShell* shell = sh->PPhysicsShell();
	if (shell)
	{
		shell->SetGlTransformDynamic(M);
		if (enable)
			shell->Enable();
	}

	if (sh->character_physics_support())
		sh->character_physics_support()->ForceTransform(M);
}


void CScriptGameObject::ForceSetRotation(Fvector rot, bool enable)
{
	CPhysicsShellHolder* sh = object().cast_physics_shell_holder();
	if (!sh)
		return;

	Fmatrix& M = object().XFORM();
	Fvector pos = M.c;
	M.setHPB(rot.x, rot.y, rot.z);
	M.c = pos;

	CPhysicsShell* shell = sh->PPhysicsShell();
	if (shell)
	{
		shell->SetGlTransformDynamic(M);
		if (enable)
			shell->Enable();
	}

	if (sh->character_physics_support())
		sh->character_physics_support()->ForceTransform(M);
}

void CScriptGameObject::ForceSetAngle(Fvector ang, bool bActivate)
{
	CPhysicsShellHolder *sh = object().cast_physics_shell_holder();
	if (!sh)
		return;

	CPhysicsShell* shell = sh->PPhysicsShell();

	if (bActivate)
		shell->Enable();

	if (shell)
	{
		Fmatrix M = Fmatrix().set(object().XFORM());
		Fvector p = Fvector().set(object().XFORM().c);
		M.setHPB(ang.x, ang.y, ang.z);
		M.translate_over(p);
		object().XFORM().set(M);
		shell->SetGlTransformDynamic(M);
		if (sh->character_physics_support())
			sh->character_physics_support()->ForceTransform(M);
	}
	else
	{
		LPCSTR text = "force_set_angleHPB: object %s has no physics shell!";
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, text, object().Name());
	}
}

Fvector CScriptGameObject::Angle()
{
	Fvector ang;
	object().XFORM().getHPB(ang.x, ang.y, ang.z);
	return ang;
}

void CScriptGameObject::SetRemainingUses(u8 value)
{
	CInventoryItem* IItm = object().cast_inventory_item();
	if (!IItm)
		return;

	CEatableItem* eItm = IItm->cast_eatable_item();
	if (!eItm)
		return;

	eItm->SetRemainingUses(value);
}

u8 CScriptGameObject::GetRemainingUses()
{
	CInventoryItem* IItm = object().cast_inventory_item();
	if (!IItm)
		return 0;

	CEatableItem* eItm = IItm->cast_eatable_item();
	if (!eItm)
		return 0;

	return eItm->GetRemainingUses();
}

u8 CScriptGameObject::GetMaxUses()
{
	CInventoryItem* IItm = object().cast_inventory_item();
	if (!IItm)
		return 0;

	CEatableItem* eItm = IItm->cast_eatable_item();
	if (!eItm)
		return 0;

	return eItm->GetMaxUses();
}

void CScriptGameObject::IterateFeelTouch(::luabind::functor<void> functor)
{
	Feel::Touch* touch = smart_cast<Feel::Touch*>(&object());
	if (touch)
	{
		xr_vector<CObject*>::const_iterator I = touch->feel_touch.begin();
		xr_vector<CObject*>::const_iterator E = touch->feel_touch.end();
		for (; I != E; ++I)
		{
			CObject* o = smart_cast<CObject*>(*I);
			if (o)
				functor(o->ID());
		}
	}
}

void CScriptGameObject::SetSpatialType(u32 sptype)
{
	object().spatial.type = sptype;
}

u32 CScriptGameObject::GetSpatialType()
{
	return object().spatial.type;
}

void CScriptGameObject::DestroyObject()
{
	object().DestroyObject();
}

u8 CScriptGameObject::GetRestrictionType()
{
	CSpaceRestrictor* restr = smart_cast<CSpaceRestrictor*>(&object());
	if (restr)
		return restr->m_space_restrictor_type;

	return (-1);
}

void CScriptGameObject::SetRestrictionType(u8 typ)
{
	CSpaceRestrictor* restr = smart_cast<CSpaceRestrictor*>(&object());
	if (restr)
	{
		restr->m_space_restrictor_type = typ;
		if (typ != RestrictionSpace::eRestrictorTypeNone)
			Level().space_restriction_manager().register_restrictor(restr, RestrictionSpace::ERestrictorTypes(typ));
	}
}

void CScriptGameObject::ForceSetRestrictionType(u8 typ)
{
	CSpaceRestrictor* restr = smart_cast<CSpaceRestrictor*>(&object());
	if (!restr)
		return;

	RestrictionSpace::ERestrictorTypes new_type = RestrictionSpace::ERestrictorTypes(typ);
	switch (new_type)
	{
	    case RestrictionSpace::eDefaultRestrictorTypeNone:
	    case RestrictionSpace::eDefaultRestrictorTypeOut:
	    case RestrictionSpace::eDefaultRestrictorTypeIn:
	    case RestrictionSpace::eRestrictorTypeNone:
	    case RestrictionSpace::eRestrictorTypeIn:
	    case RestrictionSpace::eRestrictorTypeOut:
		    break;
	    default:
		    ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
			    make_string("CGameObject [%s]: invalid restrictor type [%u]!", object().cNameSect().c_str(), typ).c_str());
		    return;
	}

	RestrictionSpace::ERestrictorTypes old_type = RestrictionSpace::ERestrictorTypes(restr->m_space_restrictor_type);
    if (old_type == new_type)
        return;

	if (old_type != RestrictionSpace::eRestrictorTypeNone)
		Level().space_restriction_manager().unregister_restrictor(restr);

	restr->m_space_restrictor_type = typ;
	if (new_type != RestrictionSpace::eRestrictorTypeNone)
		Level().space_restriction_manager().register_restrictor(restr, new_type);
}

void CScriptGameObject::InvalidateRestrictions()
{
	CCustomMonster* monster = smart_cast<CCustomMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
			make_string("CGameObject [%s]: cannot invalidate restrictions (not a CCustomMonster)!", object().cNameSect().c_str()).c_str());
		return;
	}

	monster->movement().restrictions().actual(false);
}

// demonized: add getters and setters for pathfinding for npcs around anomalies and damage for npcs
bool CScriptGameObject::get_enable_anomalies_pathfinding()
{
	auto stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
			"CGameObject : cannot access class member m_enable_anomalies_pathfinding!");
		return false;
	}
	return stalker->m_enable_anomalies_pathfinding;
}
void CScriptGameObject::set_enable_anomalies_pathfinding(bool v)
{
	auto stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
			"CGameObject : cannot access class member m_enable_anomalies_pathfinding!");
		return;
	}
	stalker->m_enable_anomalies_pathfinding = v;
}
bool CScriptGameObject::get_enable_anomalies_damage()
{
	auto stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
			"CGameObject : cannot access class member m_enable_anomalies_damage!");
		return false;
	}
	return stalker->m_enable_anomalies_damage;
}
void CScriptGameObject::set_enable_anomalies_damage(bool v)
{
	auto stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
			"CGameObject : cannot access class member m_enable_anomalies_damage!");
		return;
	}
	stalker->m_enable_anomalies_damage = v;
}
bool CScriptGameObject::inside_anomaly()
{
	auto stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
			"CGameObject : cannot call inside_anomaly (not a CAI_Stalker)!");
		return false;
	}
	return stalker->inside_anomaly();
}
#endif
//-Alundaio
