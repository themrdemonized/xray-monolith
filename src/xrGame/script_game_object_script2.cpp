////////////////////////////////////////////////////////////////////////////
//	Module 		: script_game_object_script2.cpp
//	Created 	: 25.09.2003
//  Modified 	: 29.06.2004
//	Author		: Dmitriy Iassenev
//	Description : XRay Script game object script export
////////////////////////////////////////////////////////////////////////////

#include "pch_script.h"
#include "script_game_object.h"
#include "alife_space.h"
#include "script_entity_space.h"
#include "movement_manager_space.h"
#include "pda_space.h"
#include "memory_space.h"
#include "cover_point.h"
#include "script_hit.h"
#include "script_binder_object.h"
#include "script_ini_file.h"
#include "script_sound_info.h"
#include "script_monster_hit_info.h"
#include "script_entity_action.h"
#include "action_planner.h"
//#include "../xrphysics/PhysicsShell.h"
#include "helicopter.h"
#include "script_zone.h"
#include "relation_registry.h"
#include "danger_object.h"
#include "smart_cover_object.h"
#include "detail_path_manager_space.h"
#include "patrol_path_manager_space.h"

using namespace luabind;

extern CScriptActionPlanner* script_action_planner(CScriptGameObject* obj);

class_<CScriptGameObject> script_register_game_object1(class_<CScriptGameObject> &&instance)
{
	return std::move(instance)
		.enum_("relation")
		[
			value("friend", int(ALife::eRelationTypeFriend)),
			value("neutral", int(ALife::eRelationTypeNeutral)),
			value("enemy", int(ALife::eRelationTypeEnemy)),
			value("dummy", int(ALife::eRelationTypeDummy))
		]
		.enum_("action_types")
		[
			value("movement", int(ScriptEntity::eActionTypeMovement)),
			value("watch", int(ScriptEntity::eActionTypeWatch)),
			value("animation", int(ScriptEntity::eActionTypeAnimation)),
			value("sound", int(ScriptEntity::eActionTypeSound)),
			value("particle", int(ScriptEntity::eActionTypeParticle)),
			value("object", int(ScriptEntity::eActionTypeObject)),
			value("action_type_count", int(ScriptEntity::eActionTypeCount))
		]
		.enum_("EPathType")
		[
			value("game_path", int(MovementManager::ePathTypeGamePath)),
			value("level_path", int(MovementManager::ePathTypeLevelPath)),
			value("patrol_path", int(MovementManager::ePathTypePatrolPath)),
			value("no_path", int(MovementManager::ePathTypeNoPath))
		]
		.enum_("ESelectionType")
		[
			value("alifeMovementTypeMask", int(eSelectionTypeMask)),
			value("alifeMovementTypeRandom", int(eSelectionTypeRandomBranching))
		]

		//		.property("visible",				&CScriptGameObject::getVisible,			&CScriptGameObject::setVisible)
		//		.property("enabled",				&CScriptGameObject::getEnabled,			&CScriptGameObject::setEnabled)

		//		.def_readonly("health",				&CScriptGameObject::GetHealth,			&CScriptGameObject::SetHealth)
		.property("health", &CScriptGameObject::GetHealth, &CScriptGameObject::SetHealth)
		.property("psy_health", &CScriptGameObject::GetPsyHealth, &CScriptGameObject::SetPsyHealth)
		.property("power", &CScriptGameObject::GetPower, &CScriptGameObject::SetPower)
		.property("satiety", &CScriptGameObject::GetSatiety, &CScriptGameObject::SetSatiety)
		.property("radiation", &CScriptGameObject::GetRadiation, &CScriptGameObject::SetRadiation)
		.property("morale", &CScriptGameObject::GetMorale, &CScriptGameObject::SetMorale)

		.property("bleeding", &CScriptGameObject::GetBleeding, &CScriptGameObject::ChangeBleeding)

		.def("change_health", &CScriptGameObject::ChangeHealth)
		.def("change_psy_health", &CScriptGameObject::ChangePsyHealth)
		.def("change_power", &CScriptGameObject::ChangePower)
		.def("change_satiety", &CScriptGameObject::ChangeSatiety)
		.def("change_radiation", &CScriptGameObject::ChangeRadiation)
		.def("change_morale", &CScriptGameObject::ChangeMorale)
	
		//		.def("get_bleeding",				&CScriptGameObject::GetBleeding)
		
		// demonized: exports
		.def("xform", SAFE_WRAP(&CScriptGameObject::Xform))
		.def("bounding_box", SAFE_WRAP(&CScriptGameObject::bounding_box))

		.def("center", SAFE_WRAP(&CScriptGameObject::Center))
		.def("position", &CScriptGameObject::Position)
		.def("direction", &CScriptGameObject::Direction)
		.def("clsid", SAFE_WRAP(&CScriptGameObject::clsid))
		.def("id", &CScriptGameObject::ID)
		.def("story_id", &CScriptGameObject::story_id)
		.def("section", SAFE_WRAP(&CScriptGameObject::Section))
		.def("name", SAFE_WRAP(&CScriptGameObject::Name))
		.def("parent", SAFE_WRAP(&CScriptGameObject::Parent))
		.def("mass", &CScriptGameObject::Mass)
		.def("cost", SAFE_WRAP(&CScriptGameObject::Cost))
		.def("condition", SAFE_WRAP(&CScriptGameObject::GetCondition))
		.def("set_condition", SAFE_WRAP(&CScriptGameObject::SetCondition))
		.def("power_critical", SAFE_WRAP(&CScriptGameObject::GetPowerCritical))
		.def("psy_factor", SAFE_WRAP(&CScriptGameObject::GetPsyFactor))
		.def("set_psy_factor", SAFE_WRAP(&CScriptGameObject::SetPsyFactor))

		// Added by Ncenka - allow turn on/off devices
		.def("is_device_enabled", SAFE_WRAP(&CScriptGameObject::IsDeviceEnabled))
		.def("set_device_enabled", SAFE_WRAP(&CScriptGameObject::SetDeviceEnabled))
		
		.def("death_time", &CScriptGameObject::DeathTime)
		//		.def("armor",						&CScriptGameObject::Armor)
		.def("max_health", &CScriptGameObject::MaxHealth)
		.def("accuracy", &CScriptGameObject::Accuracy)
		.def("alive", SAFE_WRAP(&CScriptGameObject::Alive))
		.def("team", &CScriptGameObject::Team)
		.def("squad", &CScriptGameObject::Squad)
		.def("group", &CScriptGameObject::Group)
		.def("change_team", SAFE_WRAP((void (CScriptGameObject::*)(u8, u8, u8))(&CScriptGameObject::ChangeTeam)))
		.def("set_visual_memory_enabled", SAFE_WRAP(&CScriptGameObject::SetVisualMemoryEnabled))
		.def("kill", SAFE_WRAP(&CScriptGameObject::Kill))
		.def("hit", SAFE_WRAP(&CScriptGameObject::Hit))
		.def("play_cycle", SAFE_WRAP((void (CScriptGameObject::*)(LPCSTR))(&CScriptGameObject::play_cycle)))
		.def("play_cycle", SAFE_WRAP((void (CScriptGameObject::*)(LPCSTR, bool))(&CScriptGameObject::play_cycle)))
		.def("fov", &CScriptGameObject::GetFOV)
		.def("range", &CScriptGameObject::GetRange)
		.def("relation", SAFE_WRAP(&CScriptGameObject::GetRelationType))
		.def("script", &CScriptGameObject::SetScriptControl)
		.def("get_script", &CScriptGameObject::GetScriptControl)
		.def("get_script_name", &CScriptGameObject::GetScriptControlName)
		.def("reset_action_queue", SAFE_WRAP(&CScriptGameObject::ResetActionQueue))
		.def("see", SAFE_WRAP(&CScriptGameObject::CheckObjectVisibility))
		.def("see", SAFE_WRAP(&CScriptGameObject::CheckTypeVisibility))
		.def("get_object_visible_distance", SAFE_WRAP(&CScriptGameObject::GetObjectVisibleDistance))
		.def("get_object_luminocity", SAFE_WRAP(&CScriptGameObject::GetObjectLuminocity))

		.def("who_hit_name", SAFE_WRAP(&CScriptGameObject::WhoHitName))
		.def("who_hit_section_name", SAFE_WRAP(&CScriptGameObject::WhoHitSectionName))

		.def("rank", SAFE_WRAP(&CScriptGameObject::GetRank))
		.def("rank_name", SAFE_WRAP(&CScriptGameObject::GetRankName))
		.def("command", SAFE_WRAP(&CScriptGameObject::AddAction))
		.def("action", SAFE_WRAP(&CScriptGameObject::GetCurrentAction), adopt<result>())
		.def("object_count", SAFE_WRAP(&CScriptGameObject::GetInventoryObjectCount))
		.def("object", SAFE_WRAP((CScriptGameObject *(CScriptGameObject::*)(LPCSTR))(&CScriptGameObject::GetObjectByName)))
		.def("object", SAFE_WRAP((CScriptGameObject *(CScriptGameObject::*)(int))(&CScriptGameObject::GetObjectByIndex)))
		.def("object_id", SAFE_WRAP(&CScriptGameObject::GetObjectById))
		.def("active_item", SAFE_WRAP(&CScriptGameObject::GetActiveItem))

		.def("set_callback",
		     SAFE_WRAP((void (CScriptGameObject::*)(GameObject::ECallbackType, const ::luabind::functor<void>&))(&CScriptGameObject
		     ::SetCallback)))
		.def("set_callback",
		     SAFE_WRAP((void (CScriptGameObject::*)(GameObject::ECallbackType, const ::luabind::functor<void>&,
		                                  const ::luabind::object&))(&CScriptGameObject::SetCallback)))
		.def("set_callback", SAFE_WRAP((void (CScriptGameObject::*)(GameObject::ECallbackType))(&CScriptGameObject::SetCallback)))

		.def("set_patrol_extrapolate_callback",
		     SAFE_WRAP((void (CScriptGameObject::*)())(&CScriptGameObject::set_patrol_extrapolate_callback)))
		.def("set_patrol_extrapolate_callback",
		     SAFE_WRAP((void (CScriptGameObject::*)(const ::luabind::functor<bool>&))(&CScriptGameObject::
		     set_patrol_extrapolate_callback)))
		.def("set_patrol_extrapolate_callback",
		     SAFE_WRAP((void (CScriptGameObject::*)(const ::luabind::functor<bool>&, const ::luabind::object&))(&CScriptGameObject::
		     set_patrol_extrapolate_callback)))

		.def("set_enemy_callback", SAFE_WRAP((void (CScriptGameObject::*)())(&CScriptGameObject::set_enemy_callback)))
		.def("set_enemy_callback",
		     SAFE_WRAP((void (CScriptGameObject::*)(const ::luabind::functor<bool>&))(&CScriptGameObject::set_enemy_callback)))
		.def("set_enemy_callback",
		     SAFE_WRAP((void (CScriptGameObject::*)(const ::luabind::functor<bool>&, const ::luabind::object&))(&CScriptGameObject::
		     set_enemy_callback)))

		.def("patrol", SAFE_WRAP(&CScriptGameObject::GetPatrolPathName))

		.def("get_ammo_in_magazine", &CScriptGameObject::GetAmmoElapsed)
		.def("get_ammo_total", &CScriptGameObject::GetSuitableAmmoTotal)
		.def("set_ammo_elapsed", &CScriptGameObject::SetAmmoElapsed)
		//Alundaio
		.def("use", SAFE_WRAP(&CScriptGameObject::Use))
		.def("start_trade", &CScriptGameObject::StartTrade)
		.def("start_upgrade", &CScriptGameObject::StartUpgrade)
		.def("set_ammo_type", &CScriptGameObject::SetAmmoType)
		.def("get_ammo_type", &CScriptGameObject::GetAmmoType)
		.def("get_ammo_count_for_type", &CScriptGameObject::GetAmmoCount)
		.def("get_main_weapon_type", &CScriptGameObject::GetMainWeaponType)
		.def("get_weapon_type", &CScriptGameObject::GetWeaponType)
		.def("set_main_weapon_type", &CScriptGameObject::SetMainWeaponType)
		.def("set_weapon_type", &CScriptGameObject::SetWeaponType)
		.def("has_ammo_type", &CScriptGameObject::HasAmmoType)
		.def("get_weapon_substate", &CScriptGameObject::GetWeaponSubstate)
		.def("set_weight", SAFE_WRAP(&CScriptGameObject::SetWeight))
		//-Alundaio
		.def("set_queue_size", SAFE_WRAP(&CScriptGameObject::SetQueueSize))
		//		.def("best_hit",					&CScriptGameObject::GetBestHit)
		//		.def("best_sound",					&CScriptGameObject::GetBestSound)
		.def("best_danger", &CScriptGameObject::GetBestDanger)
		.def("best_enemy", &CScriptGameObject::GetBestEnemy)
		.def("best_item", &CScriptGameObject::GetBestItem)
		.def("action_count", &CScriptGameObject::GetActionCount)
		.def("action_by_index", SAFE_WRAP(&CScriptGameObject::GetActionByIndex))

		//.def("set_hear_callback",			(void (CScriptGameObject::*)(const ::luabind::object &, LPCSTR))(&CScriptGameObject::SetSoundCallback))
		//.def("set_hear_callback",			(void (CScriptGameObject::*)(const ::luabind::functor<void> &))(&CScriptGameObject::SetSoundCallback))
		//.def("clear_hear_callback",		&CScriptGameObject::ClearSoundCallback)

		.def("memory_time", SAFE_WRAP(&CScriptGameObject::memory_time))
		.def("memory_position", SAFE_WRAP(&CScriptGameObject::memory_position))
		.def("best_weapon", SAFE_WRAP(&CScriptGameObject::best_weapon))
		.def("explode", SAFE_WRAP(&CScriptGameObject::explode))
		.def("get_enemy", SAFE_WRAP(&CScriptGameObject::GetEnemy))
		.def("get_corpse", SAFE_WRAP(&CScriptGameObject::GetCorpse))
		.def("get_enemy_strength", &CScriptGameObject::GetEnemyStrength)
		.def("get_sound_info", SAFE_WRAP(&CScriptGameObject::GetSoundInfo))
		.def("get_monster_hit_info", SAFE_WRAP(&CScriptGameObject::GetMonsterHitInfo))
		.def("bind_object", SAFE_WRAP(&CScriptGameObject::bind_object), adopt<2>())
		.def("motivation_action_manager", &script_action_planner)

		// basemonster
		.def("set_force_anti_aim", &CScriptGameObject::set_force_anti_aim)
		.def("get_force_anti_aim", &CScriptGameObject::get_force_anti_aim)

		.def("set_override_animation", (void (CScriptGameObject::*)(pcstr))(&CScriptGameObject::set_override_animation))
		.def("set_override_animation", (void (CScriptGameObject::*)(u32, u32))(&CScriptGameObject::set_override_animation))
		.def("clear_override_animation", &CScriptGameObject::clear_override_animation)

		// burer
		.def("burer_set_force_gravi_attack", &CScriptGameObject::burer_set_force_gravi_attack)
		.def("burer_get_force_gravi_attack", &CScriptGameObject::burer_get_force_gravi_attack)

		// poltergeist
		.def("poltergeist_set_actor_ignore", &CScriptGameObject::poltergeist_set_actor_ignore)
		.def("poltergeist_get_actor_ignore", &CScriptGameObject::poltergeist_get_actor_ignore)

		// bloodsucker
		.def("force_visibility_state", SAFE_WRAP(&CScriptGameObject::force_visibility_state))
		.def("get_visibility_state", SAFE_WRAP(&CScriptGameObject::get_visibility_state))
		.def("force_stand_sleep_animation", SAFE_WRAP(&CScriptGameObject::force_stand_sleep_animation))
		.def("release_stand_sleep_animation", SAFE_WRAP(&CScriptGameObject::release_stand_sleep_animation))
		.def("set_invisible", SAFE_WRAP(&CScriptGameObject::set_invisible))
		.def("set_manual_invisibility", SAFE_WRAP(&CScriptGameObject::set_manual_invisibility))
		.def("set_alien_control", SAFE_WRAP(&CScriptGameObject::set_alien_control))
		.def("set_enemy", SAFE_WRAP(&CScriptGameObject::set_enemy))
		.def("set_vis_state", SAFE_WRAP(&CScriptGameObject::set_vis_state))
		.def("set_collision_off", SAFE_WRAP(&CScriptGameObject::off_collision))
		.def("set_capture_anim", SAFE_WRAP(&CScriptGameObject::bloodsucker_drag_jump))

		// zombie
		.def("fake_death_fall_down", SAFE_WRAP(&CScriptGameObject::fake_death_fall_down))
		.def("fake_death_stand_up", SAFE_WRAP(&CScriptGameObject::fake_death_stand_up))

		// base monster
		.def("skip_transfer_enemy", &CScriptGameObject::skip_transfer_enemy)
		.def("set_home", (void (CScriptGameObject::*)(LPCSTR, float, float, bool, float))(&CScriptGameObject::set_home))
		.def("set_home", (void (CScriptGameObject::*)(u32, float, float, bool, float))(&CScriptGameObject::set_home))
		.def("remove_home", &CScriptGameObject::remove_home)
		.def("berserk", &CScriptGameObject::berserk)
		.def("can_script_capture", &CScriptGameObject::can_script_capture)
		.def("set_custom_panic_threshold", &CScriptGameObject::set_custom_panic_threshold)
		.def("set_default_panic_threshold", &CScriptGameObject::set_default_panic_threshold)

		// inventory owner
		.def("get_current_outfit", SAFE_WRAP(&CScriptGameObject::GetCurrentOutfit))
		.def("get_current_outfit_protection", SAFE_WRAP(&CScriptGameObject::GetCurrentOutfitProtection))

		.def("deadbody_closed", SAFE_WRAP(&CScriptGameObject::deadbody_closed))
		.def("deadbody_closed_status", SAFE_WRAP(&CScriptGameObject::deadbody_closed_status))
		.def("deadbody_can_take", SAFE_WRAP(&CScriptGameObject::deadbody_can_take))
		.def("deadbody_can_take_status", SAFE_WRAP(&CScriptGameObject::deadbody_can_take_status))

		.def("can_select_weapon", SAFE_WRAP((bool (CScriptGameObject::*)() const)&CScriptGameObject::can_select_weapon))
		.def("can_select_weapon", SAFE_WRAP((void (CScriptGameObject::*)(bool))&CScriptGameObject::can_select_weapon))
		// searchlight
		.def("get_current_direction", &CScriptGameObject::GetCurrentDirection)

		// movement manager
		.def("set_body_state", SAFE_WRAP(&CScriptGameObject::set_body_state))
		.def("set_movement_type", SAFE_WRAP(&CScriptGameObject::set_movement_type))
		.def("set_mental_state", SAFE_WRAP(&CScriptGameObject::set_mental_state))
		.def("set_path_type", SAFE_WRAP(&CScriptGameObject::set_path_type))
		.def("set_detail_path_type", SAFE_WRAP(&CScriptGameObject::set_detail_path_type))

		.def("body_state", SAFE_WRAP(&CScriptGameObject::body_state))
		.def("target_body_state", SAFE_WRAP(&CScriptGameObject::target_body_state))
		.def("movement_type", SAFE_WRAP(&CScriptGameObject::movement_type))
		.def("target_movement_type", SAFE_WRAP(&CScriptGameObject::target_movement_type))
		.def("mental_state", SAFE_WRAP(&CScriptGameObject::mental_state))
		.def("target_mental_state", SAFE_WRAP(&CScriptGameObject::target_mental_state))
		.def("path_type", SAFE_WRAP(&CScriptGameObject::path_type))
		.def("detail_path_type", SAFE_WRAP(&CScriptGameObject::detail_path_type))

		//
		.def("set_desired_position", SAFE_WRAP((void (CScriptGameObject::*)())(&CScriptGameObject::set_desired_position)))
		.def("set_desired_position",
		     SAFE_WRAP((void (CScriptGameObject::*)(const Fvector*))(&CScriptGameObject::set_desired_position)))
		.def("set_desired_direction", SAFE_WRAP((void (CScriptGameObject::*)())(&CScriptGameObject::set_desired_direction)))
		.def("set_desired_direction",
		     SAFE_WRAP((void (CScriptGameObject::*)(const Fvector*))(&CScriptGameObject::set_desired_direction)))
		.def("set_patrol_path", SAFE_WRAP(&CScriptGameObject::set_patrol_path))
		.def("inactualize_patrol_path", SAFE_WRAP(&CScriptGameObject::inactualize_patrol_path))
		.def("set_dest_level_vertex_id", SAFE_WRAP(&CScriptGameObject::set_dest_level_vertex_id))
		.def("set_dest_game_vertex_id", SAFE_WRAP(&CScriptGameObject::set_dest_game_vertex_id))
		.def("set_movement_selection_type", SAFE_WRAP(&CScriptGameObject::set_movement_selection_type))
		.def("level_vertex_id", SAFE_WRAP(&CScriptGameObject::level_vertex_id))
		.def("game_vertex_id", SAFE_WRAP(&CScriptGameObject::game_vertex_id))
		.def("add_animation", SAFE_WRAP((void (CScriptGameObject::*)(LPCSTR, bool, bool))(&CScriptGameObject::add_animation)))
		.def("add_animation",
		     SAFE_WRAP((void (CScriptGameObject::*)(LPCSTR, bool, Fvector, Fvector, bool))(&CScriptGameObject::add_animation)))
		.def("clear_animations", SAFE_WRAP(&CScriptGameObject::clear_animations))
		.def("animation_count", SAFE_WRAP(&CScriptGameObject::animation_count))
		.def("animation_slot", SAFE_WRAP(&CScriptGameObject::animation_slot))

		.def("ignore_monster_threshold", SAFE_WRAP(&CScriptGameObject::set_ignore_monster_threshold))
		.def("restore_ignore_monster_threshold", SAFE_WRAP(&CScriptGameObject::restore_ignore_monster_threshold))
		.def("ignore_monster_threshold", SAFE_WRAP(&CScriptGameObject::ignore_monster_threshold))
		.def("max_ignore_monster_distance", SAFE_WRAP(&CScriptGameObject::set_max_ignore_monster_distance))
		.def("restore_max_ignore_monster_distance", SAFE_WRAP(&CScriptGameObject::restore_max_ignore_monster_distance))
		.def("max_ignore_monster_distance", SAFE_WRAP(&CScriptGameObject::max_ignore_monster_distance))

		.def("eat", SAFE_WRAP(&CScriptGameObject::eat))

		.def("extrapolate_length", SAFE_WRAP((float (CScriptGameObject::*)() const)(&CScriptGameObject::extrapolate_length)))
		.def("extrapolate_length", SAFE_WRAP((void (CScriptGameObject::*)(float))(&CScriptGameObject::extrapolate_length)))

		.def("set_fov", SAFE_WRAP(&CScriptGameObject::set_fov))
		.def("set_range", SAFE_WRAP(&CScriptGameObject::set_range))

		.def("head_orientation", SAFE_WRAP(&CScriptGameObject::head_orientation))

		.def("set_actor_position", &CScriptGameObject::SetActorPosition)
		.def("set_actor_direction", (void (CScriptGameObject::*)(float)) &CScriptGameObject::SetActorDirection)
		.def("set_actor_direction", (void (CScriptGameObject::*)(float, float))& CScriptGameObject::SetActorDirection)
		.def("set_actor_direction", (void (CScriptGameObject::*)(float, float, float))& CScriptGameObject::SetActorDirection)
		.def("set_actor_direction", (void (CScriptGameObject::*)(const Fvector&))&CScriptGameObject::SetActorDirection)

		.def("disable_hit_marks", (void (CScriptGameObject::*)(bool))&CScriptGameObject::DisableHitMarks)
		.def("disable_hit_marks", (bool (CScriptGameObject::*)() const)&CScriptGameObject::DisableHitMarks)
		.def("get_movement_speed", &CScriptGameObject::GetMovementSpeed)
		.def("set_movement_speed", &CScriptGameObject::SetMovementSpeed)	// momopate: db.actor:set_momvement_speed(vector vel)

		.def("set_npc_position", SAFE_WRAP(&CScriptGameObject::SetNpcPosition))

		.def("vertex_in_direction", SAFE_WRAP(&CScriptGameObject::vertex_in_direction))

		.def("item_in_slot", SAFE_WRAP(&CScriptGameObject::item_in_slot))
		.def("active_detector", SAFE_WRAP(&CScriptGameObject::active_device))
		.def("hide_detector", SAFE_WRAP(&CScriptGameObject::hide_device))
		.def("force_hide_detector", SAFE_WRAP(&CScriptGameObject::force_hide_device))
		.def("show_detector", SAFE_WRAP(&CScriptGameObject::show_device))
		.def("active_slot", SAFE_WRAP(&CScriptGameObject::active_slot))
		.def("activate_slot", SAFE_WRAP(&CScriptGameObject::activate_slot))

#ifdef DEBUG
		.def("debug_planner",				SAFE_WRAP(&CScriptGameObject::debug_planner))
#endif // DEBUG
		.def("invulnerable", SAFE_WRAP((bool (CScriptGameObject::*)() const)&CScriptGameObject::invulnerable))
		.def("invulnerable", SAFE_WRAP((void (CScriptGameObject::*)(bool))&CScriptGameObject::invulnerable))

		.def("get_smart_cover_description", &CScriptGameObject::get_smart_cover_description)
		.def("set_visual_name", SAFE_WRAP(&CScriptGameObject::set_visual_name))
		.def("get_inv_weight", &CScriptGameObject::get_current_weight)
		.def("get_inv_max_weight", &CScriptGameObject::get_max_weight)
		.def("get_visual_name", SAFE_WRAP(&CScriptGameObject::get_visual_name))

		.def("can_throw_grenades", SAFE_WRAP((bool (CScriptGameObject::*)() const)&CScriptGameObject::can_throw_grenades))
		.def("can_throw_grenades", SAFE_WRAP((void (CScriptGameObject::*)(bool))&CScriptGameObject::can_throw_grenades))

		.def("group_throw_time_interval",
		     SAFE_WRAP((u32 (CScriptGameObject::*)() const)&CScriptGameObject::group_throw_time_interval))
		.def("group_throw_time_interval",
		     SAFE_WRAP((void (CScriptGameObject::*)(u32))&CScriptGameObject::group_throw_time_interval))

		.def("register_in_combat", SAFE_WRAP(&CScriptGameObject::register_in_combat))
		.def("unregister_in_combat", SAFE_WRAP(&CScriptGameObject::unregister_in_combat))
		.def("find_best_cover", SAFE_WRAP(&CScriptGameObject::find_best_cover))

		.def("use_smart_covers_only", SAFE_WRAP((bool (CScriptGameObject::*)() const)&CScriptGameObject::use_smart_covers_only))
		.def("use_smart_covers_only", SAFE_WRAP((void (CScriptGameObject::*)(bool))&CScriptGameObject::use_smart_covers_only))

		.def("in_smart_cover", SAFE_WRAP(&CScriptGameObject::in_smart_cover))

		.def("set_dest_smart_cover", SAFE_WRAP((void (CScriptGameObject::*)(LPCSTR))&CScriptGameObject::set_dest_smart_cover))
		.def("set_dest_smart_cover", SAFE_WRAP((void (CScriptGameObject::*)())&CScriptGameObject::set_dest_smart_cover))
		.def("get_dest_smart_cover",
		     SAFE_WRAP((CCoverPoint const* (CScriptGameObject::*)())&CScriptGameObject::get_dest_smart_cover))
		.def("get_dest_smart_cover_name", SAFE_WRAP(&CScriptGameObject::get_dest_smart_cover_name))

		.def("set_dest_loophole", SAFE_WRAP((void (CScriptGameObject::*)(LPCSTR))&CScriptGameObject::set_dest_loophole))
		.def("set_dest_loophole", SAFE_WRAP((void (CScriptGameObject::*)())&CScriptGameObject::set_dest_loophole))

		.def("set_smart_cover_target", SAFE_WRAP((void (CScriptGameObject::*)(Fvector))&CScriptGameObject::set_smart_cover_target))
		.def("set_smart_cover_target",
		     SAFE_WRAP((void (CScriptGameObject::*)(CScriptGameObject*))&CScriptGameObject::set_smart_cover_target))
		.def("set_smart_cover_target", SAFE_WRAP((void (CScriptGameObject::*)())&CScriptGameObject::set_smart_cover_target))

		.def("set_smart_cover_target_selector",
		     SAFE_WRAP((void (CScriptGameObject::*)(::luabind::functor<void>))&CScriptGameObject::set_smart_cover_target_selector))
		.def("set_smart_cover_target_selector",
		     SAFE_WRAP((void (CScriptGameObject::*)(::luabind::functor<void>, ::luabind::object))&CScriptGameObject::
		     set_smart_cover_target_selector))
		.def("set_smart_cover_target_selector",
		     SAFE_WRAP((void (CScriptGameObject::*)())&CScriptGameObject::set_smart_cover_target_selector))

		.def("set_smart_cover_target_idle", SAFE_WRAP(&CScriptGameObject::set_smart_cover_target_idle))
		.def("set_smart_cover_target_lookout", SAFE_WRAP(&CScriptGameObject::set_smart_cover_target_lookout))
		.def("set_smart_cover_target_fire", SAFE_WRAP(&CScriptGameObject::set_smart_cover_target_fire))
		.def("set_smart_cover_target_fire_no_lookout", SAFE_WRAP(&CScriptGameObject::set_smart_cover_target_fire_no_lookout))
		.def("set_smart_cover_target_idle", SAFE_WRAP(&CScriptGameObject::set_smart_cover_target_idle))
		.def("set_smart_cover_target_default", SAFE_WRAP(&CScriptGameObject::set_smart_cover_target_default))

		.def("idle_min_time", SAFE_WRAP((void (CScriptGameObject::*)(float))(&CScriptGameObject::idle_min_time)))
		.def("idle_min_time", SAFE_WRAP((float (CScriptGameObject::*)() const)(&CScriptGameObject::idle_min_time)))

		.def("idle_max_time", SAFE_WRAP((void (CScriptGameObject::*)(float))(&CScriptGameObject::idle_max_time)))
		.def("idle_max_time", SAFE_WRAP((float (CScriptGameObject::*)() const)(&CScriptGameObject::idle_max_time)))

		.def("lookout_min_time", SAFE_WRAP((void (CScriptGameObject::*)(float))(&CScriptGameObject::lookout_min_time)))
		.def("lookout_min_time", SAFE_WRAP((float (CScriptGameObject::*)() const)(&CScriptGameObject::lookout_min_time)))

		.def("lookout_max_time", SAFE_WRAP((void (CScriptGameObject::*)(float))(&CScriptGameObject::lookout_max_time)))
		.def("lookout_max_time", SAFE_WRAP((float (CScriptGameObject::*)() const)(&CScriptGameObject::lookout_max_time)))

		.def("in_loophole_fov", SAFE_WRAP(&CScriptGameObject::in_loophole_fov))
		.def("in_current_loophole_fov", SAFE_WRAP(&CScriptGameObject::in_current_loophole_fov))
		.def("in_loophole_range", SAFE_WRAP(&CScriptGameObject::in_loophole_range))
		.def("in_current_loophole_range", SAFE_WRAP(&CScriptGameObject::in_current_loophole_range))

		.def("apply_loophole_direction_distance",
		     SAFE_WRAP((void (CScriptGameObject::*)(float))&CScriptGameObject::apply_loophole_direction_distance))
		.def("apply_loophole_direction_distance",
		     SAFE_WRAP((float (CScriptGameObject::*)() const)&CScriptGameObject::apply_loophole_direction_distance))

		.def("movement_target_reached", SAFE_WRAP(&CScriptGameObject::movement_target_reached))
		.def("suitable_smart_cover", SAFE_WRAP(&CScriptGameObject::suitable_smart_cover))

		.def("take_items_enabled", SAFE_WRAP((void (CScriptGameObject::*)(bool))&CScriptGameObject::take_items_enabled))
		.def("take_items_enabled", SAFE_WRAP((bool (CScriptGameObject::*)() const)&CScriptGameObject::take_items_enabled))

		.def("death_sound_enabled", SAFE_WRAP((void (CScriptGameObject::*)(bool))&CScriptGameObject::death_sound_enabled))
		.def("death_sound_enabled", SAFE_WRAP((bool (CScriptGameObject::*)() const)&CScriptGameObject::death_sound_enabled))

		.def("register_door_for_npc", SAFE_WRAP(&CScriptGameObject::register_door))
		.def("unregister_door_for_npc", &CScriptGameObject::unregister_door)
		.def("on_door_is_open", &CScriptGameObject::on_door_is_open)
		.def("on_door_is_closed", &CScriptGameObject::on_door_is_closed)
		.def("lock_door_for_npc", &CScriptGameObject::lock_door_for_npc)
		.def("unlock_door_for_npc", &CScriptGameObject::unlock_door_for_npc)
		.def("is_door_locked_for_npc", &CScriptGameObject::is_door_locked_for_npc)
		.def("is_door_blocked_by_npc", &CScriptGameObject::is_door_blocked_by_npc)
		.def("is_weapon_going_to_be_strapped", SAFE_WRAP(&CScriptGameObject::is_weapon_going_to_be_strapped))

		.def("reload_weapon", &CScriptGameObject::reload_weapon);
}
