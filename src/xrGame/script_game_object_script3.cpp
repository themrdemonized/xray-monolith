////////////////////////////////////////////////////////////////////////////
//	Module 		: script_game_object_script3.cpp
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
#include "physics_shell_scripted.h"
#include "helicopter.h"
#include "HangingLamp.h"
#include "holder_custom.h"
#include "script_zone.h"
#include "relation_registry.h"
#include "GameTask.h"
#include "car.h"
#include "ZoneCampfire.h"
#include "physicobject.h"
#include "artefact.h"
#include "sight_manager_space.h"
#include "script_attachment_manager.h"

using namespace luabind;

class_<CScriptGameObject> script_register_game_object2(class_<CScriptGameObject> &&instance)
{
	return std::move(instance)
		.def("add_sound",
		     SAFE_WRAP((u32 (CScriptGameObject::*)(LPCSTR, u32, ESoundTypes, u32, u32, u32))(&CScriptGameObject::add_sound)))
		.def("add_sound",
		     SAFE_WRAP((u32 (CScriptGameObject::*)(LPCSTR, u32, ESoundTypes, u32, u32, u32, LPCSTR))(&CScriptGameObject::add_sound
		     )))
		.def("add_combat_sound",
		     SAFE_WRAP((u32 (CScriptGameObject::*)(LPCSTR, u32, ESoundTypes, u32, u32, u32, LPCSTR))(&CScriptGameObject::
		     add_combat_sound)))
		.def("remove_sound", SAFE_WRAP(&CScriptGameObject::remove_sound))
		.def("set_sound_mask", SAFE_WRAP(&CScriptGameObject::set_sound_mask))
		.def("play_sound", SAFE_WRAP((void (CScriptGameObject::*)(u32))(&CScriptGameObject::play_sound)))
		.def("play_sound", SAFE_WRAP((void (CScriptGameObject::*)(u32, u32))(&CScriptGameObject::play_sound)))
		.def("play_sound", SAFE_WRAP((void (CScriptGameObject::*)(u32, u32, u32))(&CScriptGameObject::play_sound)))
		.def("play_sound", SAFE_WRAP((void (CScriptGameObject::*)(u32, u32, u32, u32))(&CScriptGameObject::play_sound)))
		.def("play_sound", SAFE_WRAP((void (CScriptGameObject::*)(u32, u32, u32, u32, u32))(&CScriptGameObject::play_sound)))
		.def("play_sound", SAFE_WRAP((void (CScriptGameObject::*)(u32, u32, u32, u32, u32, u32))(&CScriptGameObject::play_sound)))
		.def("binded_object", SAFE_WRAP(&CScriptGameObject::binded_object))
		.def("set_previous_point", SAFE_WRAP(&CScriptGameObject::set_previous_point))
		.def("set_start_point", SAFE_WRAP(&CScriptGameObject::set_start_point))
		.def("get_current_point_index", SAFE_WRAP(&CScriptGameObject::get_current_patrol_point_index))
		.def("path_completed", SAFE_WRAP(&CScriptGameObject::path_completed))
		.def("patrol_path_make_inactual", SAFE_WRAP(&CScriptGameObject::patrol_path_make_inactual))
		.def("enable_memory_object", SAFE_WRAP(&CScriptGameObject::enable_memory_object))
		.def("active_sound_count", SAFE_WRAP((int (CScriptGameObject::*)())(&CScriptGameObject::active_sound_count)))
		.def("active_sound_count", SAFE_WRAP((int (CScriptGameObject::*)(bool))(&CScriptGameObject::active_sound_count)))
		.def("best_cover", SAFE_WRAP(&CScriptGameObject::best_cover))
		.def("best_cover_invalidate", SAFE_WRAP(&CScriptGameObject::best_cover_invalidate))
		.def("affect_cover", SAFE_WRAP(&CScriptGameObject::affect_cover))
		.def("safe_cover", SAFE_WRAP(&CScriptGameObject::safe_cover))
		.def("spawn_ini", SAFE_WRAP(&CScriptGameObject::spawn_ini))
		.def("memory_remove_links", SAFE_WRAP(&CScriptGameObject::memory_remove_links))
		.def("memory_visible_objects", SAFE_WRAP(&CScriptGameObject::memory_visible_objects), return_stl_iterator)
		.def("memory_sound_objects", SAFE_WRAP(&CScriptGameObject::memory_sound_objects), return_stl_iterator)
		.def("memory_hit_objects", SAFE_WRAP(&CScriptGameObject::memory_hit_objects), return_stl_iterator)
		.def("not_yet_visible_objects", SAFE_WRAP(&CScriptGameObject::not_yet_visible_objects), return_stl_iterator)
		.def("visibility_threshold", SAFE_WRAP(&CScriptGameObject::visibility_threshold))
		.def("enable_vision", SAFE_WRAP(&CScriptGameObject::enable_vision))
		.def("vision_enabled", SAFE_WRAP(&CScriptGameObject::vision_enabled))
		.def("set_sound_threshold", SAFE_WRAP(&CScriptGameObject::set_sound_threshold))
		.def("restore_sound_threshold", SAFE_WRAP(&CScriptGameObject::restore_sound_threshold))

		// sight manager
		.def("set_sight",
		     SAFE_WRAP((void (CScriptGameObject::*)(SightManager::ESightType sight_type, Fvector* vector3d, u32 dwLookOverDelay))(
			     &CScriptGameObject::set_sight)))
		.def("set_sight",
		     SAFE_WRAP((void (CScriptGameObject::*)(SightManager::ESightType sight_type, bool torso_look, bool path))(&
			     CScriptGameObject::set_sight)))
		.def("set_sight",
		     SAFE_WRAP((void (CScriptGameObject::*)(SightManager::ESightType sight_type, Fvector& vector3d, bool torso_look))(&
			     CScriptGameObject::set_sight)))
		.def("set_sight",
		     SAFE_WRAP((void (CScriptGameObject::*)(SightManager::ESightType sight_type, Fvector* vector3d))(&CScriptGameObject::
			     set_sight)))
		.def("set_sight",
		     SAFE_WRAP((void (CScriptGameObject::*)(CScriptGameObject* object_to_look))(&CScriptGameObject::set_sight)))
		.def("set_sight",
		     SAFE_WRAP((void (CScriptGameObject::*)(CScriptGameObject* object_to_look, bool torso_look))(&CScriptGameObject::
			     set_sight)))
		.def("set_sight",
		     SAFE_WRAP((void (CScriptGameObject::*)(CScriptGameObject* object_to_look, bool torso_look, bool fire_object))(&
			     CScriptGameObject::set_sight)))
		.def("set_sight",
		     SAFE_WRAP((void (CScriptGameObject::*)(CScriptGameObject* object_to_look, bool torso_look, bool fire_object,
		                                  bool no_pitch))(&CScriptGameObject::set_sight)))
		//		.def("set_sight",					(void (CScriptGameObject::*)(const MemorySpace::CMemoryInfo *memory_object, bool	torso_look))(&CScriptGameObject::set_sight))

		// object handler
		.def("set_item", SAFE_WRAP((void (CScriptGameObject::*)(MonsterSpace::EObjectAction))(&CScriptGameObject::set_item)))
		.def("set_item",
		     SAFE_WRAP((void (CScriptGameObject::*)(MonsterSpace::EObjectAction, CScriptGameObject*))(&CScriptGameObject::set_item
		     )))
		.def("set_item",
		     SAFE_WRAP((void (CScriptGameObject::*)(MonsterSpace::EObjectAction, CScriptGameObject*, u32))(&CScriptGameObject::
			     set_item)))
		.def("set_item",
		     SAFE_WRAP((void (CScriptGameObject::*)(MonsterSpace::EObjectAction, CScriptGameObject*, u32, u32))(&CScriptGameObject
			     ::set_item)))

		.def("is_body_turning", SAFE_WRAP(&CScriptGameObject::is_body_turning))

		// Backwards compatibility
		.def("get_bone_id", SAFE_WRAP((u16(CScriptGameObject::*)(LPCSTR))(&CScriptGameObject::bone_id)))
		.def("get_bone_id", SAFE_WRAP((u16(CScriptGameObject::*)(LPCSTR, bool))(&CScriptGameObject::bone_id)))

		.def("bone_id", SAFE_WRAP((u16(CScriptGameObject::*)(LPCSTR))(&CScriptGameObject::bone_id)))
		.def("bone_id", SAFE_WRAP((u16(CScriptGameObject::*)(LPCSTR, bool))(&CScriptGameObject::bone_id)))

		.def("bone_name", SAFE_WRAP((LPCSTR(CScriptGameObject::*)(u16))(&CScriptGameObject::bone_name)))
		.def("bone_name", SAFE_WRAP((LPCSTR(CScriptGameObject::*)(u16, bool))(&CScriptGameObject::bone_name)))

		.def("bone_position", SAFE_WRAP((Fvector(CScriptGameObject::*)(u16))(&CScriptGameObject::bone_position)))
		.def("bone_position", SAFE_WRAP((Fvector(CScriptGameObject::*)(u16, bool))(&CScriptGameObject::bone_position)))
		.def("bone_position", SAFE_WRAP((Fvector(CScriptGameObject::*)(LPCSTR))(&CScriptGameObject::bone_position)))
		.def("bone_position", SAFE_WRAP((Fvector(CScriptGameObject::*)(LPCSTR, bool))(&CScriptGameObject::bone_position)))

		.def("bone_direction", SAFE_WRAP((Fvector(CScriptGameObject::*)(u16))(&CScriptGameObject::bone_direction)))
		.def("bone_direction", SAFE_WRAP((Fvector(CScriptGameObject::*)(u16, bool))(&CScriptGameObject::bone_direction)))
		.def("bone_direction", SAFE_WRAP((Fvector(CScriptGameObject::*)(LPCSTR))(&CScriptGameObject::bone_direction)))
		.def("bone_direction", SAFE_WRAP((Fvector(CScriptGameObject::*)(LPCSTR, bool))(&CScriptGameObject::bone_direction)))

		.def("bone_transform", SAFE_WRAP((Fmatrix(CScriptGameObject::*)(u16))(&CScriptGameObject::bone_transform)))
		.def("bone_transform", SAFE_WRAP((Fmatrix(CScriptGameObject::*)(u16, bool))(&CScriptGameObject::bone_transform)))
		.def("bone_transform", SAFE_WRAP((Fmatrix(CScriptGameObject::*)(LPCSTR))(&CScriptGameObject::bone_transform)))
		.def("bone_transform", SAFE_WRAP((Fmatrix(CScriptGameObject::*)(LPCSTR, bool))(&CScriptGameObject::bone_transform)))

		.def("bone_parent", SAFE_WRAP((u16(CScriptGameObject::*)(u16))(&CScriptGameObject::bone_parent)))
		.def("bone_parent", SAFE_WRAP((u16(CScriptGameObject::*)(u16, bool))(&CScriptGameObject::bone_parent)))
		.def("bone_parent", SAFE_WRAP((u16(CScriptGameObject::*)(LPCSTR))(&CScriptGameObject::bone_parent)))
		.def("bone_parent", SAFE_WRAP((u16(CScriptGameObject::*)(LPCSTR, bool))(&CScriptGameObject::bone_parent)))

		.def("bone_visible", SAFE_WRAP((bool (CScriptGameObject::*)(u16))(&CScriptGameObject::is_bone_visible)))
		.def("bone_visible", SAFE_WRAP((bool (CScriptGameObject::*)(u16, bool))(&CScriptGameObject::is_bone_visible)))
		.def("bone_visible", SAFE_WRAP((bool (CScriptGameObject::*)(LPCSTR))(&CScriptGameObject::is_bone_visible)))
		.def("bone_visible", SAFE_WRAP((bool (CScriptGameObject::*)(LPCSTR, bool))(&CScriptGameObject::is_bone_visible)))

		.def("set_bone_visible", SAFE_WRAP((void (CScriptGameObject::*)(u16, bool, bool))(&CScriptGameObject::set_bone_visible)))
		.def("set_bone_visible", SAFE_WRAP((void (CScriptGameObject::*)(u16, bool, bool, bool))(&CScriptGameObject::set_bone_visible)))
		.def("set_bone_visible", SAFE_WRAP((void (CScriptGameObject::*)(LPCSTR, bool, bool))(&CScriptGameObject::set_bone_visible)))
		.def("set_bone_visible", SAFE_WRAP((void (CScriptGameObject::*)(LPCSTR, bool, bool, bool))(&CScriptGameObject::set_bone_visible)))

		// demonized: list all bones
		.def("list_bones", SAFE_WRAP(&CScriptGameObject::list_bones))

		//////////////////////////////////////////////////////////////////////////
		// Space restrictions
		//////////////////////////////////////////////////////////////////////////
		.def("add_restrictions", SAFE_WRAP(&CScriptGameObject::add_restrictions))
		.def("remove_restrictions", SAFE_WRAP(&CScriptGameObject::remove_restrictions))
		.def("remove_all_restrictions", SAFE_WRAP(&CScriptGameObject::remove_all_restrictions))
		.def("in_restrictions", SAFE_WRAP(&CScriptGameObject::in_restrictions))
		.def("out_restrictions", SAFE_WRAP(&CScriptGameObject::out_restrictions))
		.def("base_in_restrictions", SAFE_WRAP(&CScriptGameObject::base_in_restrictions))
		.def("base_out_restrictions", SAFE_WRAP(&CScriptGameObject::base_out_restrictions))
		.def("accessible", SAFE_WRAP(&CScriptGameObject::accessible_position))
		.def("accessible", SAFE_WRAP(&CScriptGameObject::accessible_vertex_id))
		.def("accessible_nearest", SAFE_WRAP(&CScriptGameObject::accessible_nearest), out_value<3>())

		//////////////////////////////////////////////////////////////////////////
		.def("enable_attachable_item", SAFE_WRAP(&CScriptGameObject::enable_attachable_item))
		.def("attachable_item_enabled", SAFE_WRAP(&CScriptGameObject::attachable_item_enabled))
		.def("night_vision_allowed", SAFE_WRAP(&CScriptGameObject::night_vision_allowed))
		.def("enable_night_vision", SAFE_WRAP(&CScriptGameObject::enable_night_vision))
		.def("night_vision_enabled", SAFE_WRAP(&CScriptGameObject::night_vision_enabled))
		.def("enable_torch", SAFE_WRAP(&CScriptGameObject::enable_torch))
		.def("torch_enabled", SAFE_WRAP(&CScriptGameObject::torch_enabled))
		.def("attachable_item_load_attach", SAFE_WRAP(&CScriptGameObject::attachable_item_load_attach))

		// VodoXleb: add force update for torch for npc
		.def("update_torch", SAFE_WRAP(&CScriptGameObject::update_torch))

		.def("weapon_strapped", SAFE_WRAP(&CScriptGameObject::weapon_strapped))
		.def("weapon_unstrapped", SAFE_WRAP(&CScriptGameObject::weapon_unstrapped))

		//////////////////////////////////////////////////////////////////////////
		//inventory owner
		//////////////////////////////////////////////////////////////////////////

		.enum_("EPdaMsg")
		[
			value("dialog_pda_msg", int(ePdaMsgDialog)),
			value("info_pda_msg", int(ePdaMsgInfo)),
			value("no_pda_msg", int(ePdaMsgMax))
		]

		.def("give_info_portion", &CScriptGameObject::GiveInfoPortion)
		.def("disable_info_portion", &CScriptGameObject::DisableInfoPortion)
		.def("give_game_news",
		     (void (CScriptGameObject::*)(LPCSTR, LPCSTR, LPCSTR, int, int))(&CScriptGameObject::GiveGameNews))
		.def("give_game_news",
		     (void (CScriptGameObject::*)(LPCSTR, LPCSTR, LPCSTR, int, int, int))(&CScriptGameObject::GiveGameNews))

		.def("give_talk_message",
		     (void (CScriptGameObject::*)(LPCSTR, LPCSTR, LPCSTR))(&CScriptGameObject::AddIconedTalkMessage_old))
		//old version, must remove!
		.def("give_talk_message2",
		     (void (CScriptGameObject::*)(LPCSTR, LPCSTR, LPCSTR, LPCSTR))(&CScriptGameObject::AddIconedTalkMessage))

		.def("has_info", &CScriptGameObject::HasInfo)
		.def("dont_has_info", &CScriptGameObject::DontHasInfo)

		.def("get_task_state", &CScriptGameObject::GetGameTaskState)
		.def("set_task_state", &CScriptGameObject::SetGameTaskState)
		.def("give_task", &CScriptGameObject::GiveTaskToActor, adopt<2>())
		.def("set_active_task", &CScriptGameObject::SetActiveTask)
		.def("is_active_task", &CScriptGameObject::IsActiveTask)
		.def("get_task", &CScriptGameObject::GetTask)

		.def("is_talking", &CScriptGameObject::IsTalking)
		.def("stop_talk", &CScriptGameObject::StopTalk)
		.def("enable_talk", &CScriptGameObject::EnableTalk)
		.def("disable_talk", &CScriptGameObject::DisableTalk)
		.def("is_talk_enabled", &CScriptGameObject::IsTalkEnabled)

		.def("enable_trade", &CScriptGameObject::EnableTrade)
		.def("disable_trade", &CScriptGameObject::DisableTrade)
		.def("is_trade_enabled", &CScriptGameObject::IsTradeEnabled)
		.def("enable_inv_upgrade", &CScriptGameObject::EnableInvUpgrade)
		.def("disable_inv_upgrade", &CScriptGameObject::DisableInvUpgrade)
		.def("is_inv_upgrade_enabled", &CScriptGameObject::IsInvUpgradeEnabled)

		.def("disable_show_hide_sounds", SAFE_WRAP(&CScriptGameObject::SetPlayShHdRldSounds))
		.def("inventory_for_each", &CScriptGameObject::ForEachInventoryItems)
		.def("drop_item", &CScriptGameObject::DropItem)
		.def("drop_item_and_teleport", &CScriptGameObject::DropItemAndTeleport)
		.def("transfer_item", SAFE_WRAP(&CScriptGameObject::TransferItem))
		.def("take_item", SAFE_WRAP(&CScriptGameObject::TakeItem))
		.def("transfer_money", SAFE_WRAP(&CScriptGameObject::TransferMoney))
		.def("give_money", SAFE_WRAP(&CScriptGameObject::GiveMoney))
		.def("money", SAFE_WRAP(&CScriptGameObject::Money))

		// Tronex
		.def("iterate_inventory", &CScriptGameObject::IterateInventory)
		.def("iterate_ruck", &CScriptGameObject::IterateRuck)
		.def("iterate_belt", &CScriptGameObject::IterateBelt)
		.def("iterate_inventory_box", &CScriptGameObject::IterateInventoryBox)
		.def("make_item_active", SAFE_WRAP(&CScriptGameObject::MakeItemActive))
		.def("move_to_ruck", &CScriptGameObject::MoveItemToRuck)
		.def("move_to_slot", &CScriptGameObject::MoveItemToSlot)
		.def("move_to_belt", &CScriptGameObject::MoveItemToBelt)
		.def("is_on_belt", SAFE_WRAP(&CScriptGameObject::IsOnBelt))
		.def("item_on_belt", SAFE_WRAP(&CScriptGameObject::ItemOnBelt))
		.def("belt_count", SAFE_WRAP(&CScriptGameObject::BeltSize))

		.def("item_allow_trade", &CScriptGameObject::ItemAllowTrade)
		.def("item_deny_trade", &CScriptGameObject::ItemDenyTrade)
	
		.def("switch_to_trade", &CScriptGameObject::SwitchToTrade)
		.def("switch_to_upgrade", &CScriptGameObject::SwitchToUpgrade)
		.def("switch_to_talk", &CScriptGameObject::SwitchToTalk)
		.def("run_talk_dialog", &CScriptGameObject::RunTalkDialog)
		.def("allow_break_talk_dialog", SAFE_WRAP(&CScriptGameObject::AllowBreakTalkDialog))

		.def("hide_weapon", &CScriptGameObject::HideWeapon)
		.def("restore_weapon", &CScriptGameObject::RestoreWeapon)

		.def("weapon_is_grenadelauncher", SAFE_WRAP(&CScriptGameObject::Weapon_IsGrenadeLauncherAttached))
		.def("weapon_is_scope", SAFE_WRAP(&CScriptGameObject::Weapon_IsScopeAttached))
		.def("weapon_is_silencer", SAFE_WRAP(&CScriptGameObject::Weapon_IsSilencerAttached))

		.def("weapon_grenadelauncher_status", SAFE_WRAP(&CScriptGameObject::Weapon_GrenadeLauncher_Status))
		.def("weapon_scope_status", SAFE_WRAP(&CScriptGameObject::Weapon_Scope_Status))
		.def("weapon_silencer_status", SAFE_WRAP(&CScriptGameObject::Weapon_Silencer_Status))

		.def("allow_sprint", &CScriptGameObject::AllowSprint)

		.def("set_start_dialog", &CScriptGameObject::SetStartDialog)
		.def("get_start_dialog", &CScriptGameObject::GetStartDialog)
		.def("restore_default_start_dialog", &CScriptGameObject::RestoreDefaultStartDialog)

		.def("goodwill", &CScriptGameObject::GetGoodwill)
		.def("set_goodwill", &CScriptGameObject::SetGoodwill)
		.def("force_set_goodwill", &CScriptGameObject::ForceSetGoodwill)
		.def("change_goodwill", &CScriptGameObject::ChangeGoodwill)

		.def("general_goodwill", SAFE_WRAP(&CScriptGameObject::GetAttitude))
		.def("set_relation", &CScriptGameObject::SetRelation)

		.def("community_goodwill", &CScriptGameObject::GetCommunityGoodwill_obj)
		.def("set_community_goodwill", &CScriptGameObject::SetCommunityGoodwill_obj)

		.def("sympathy", &CScriptGameObject::GetSympathy)
		.def("set_sympathy", &CScriptGameObject::SetSympathy)

		//////////////////////////////////////////////////////////////////////////
		.def("profile_name", &CScriptGameObject::ProfileName)
		.def("character_name", &CScriptGameObject::CharacterName)
		.def("character_icon", &CScriptGameObject::CharacterIcon)
		.def("character_rank", &CScriptGameObject::CharacterRank)
		.def("character_dialogs", &CScriptGameObject::CharacterDialogs)
		.def("set_character_rank", &CScriptGameObject::SetCharacterRank)
		.def("change_character_rank", &CScriptGameObject::ChangeCharacterRank)
		.def("character_reputation", &CScriptGameObject::CharacterReputation)
		.def("set_character_reputation", &CScriptGameObject::SetCharacterReputation)
		.def("change_character_reputation", &CScriptGameObject::ChangeCharacterReputation)
		.def("character_community", &CScriptGameObject::CharacterCommunity)
		.def("set_character_community", &CScriptGameObject::SetCharacterCommunity)

		.def("get_actor_relation_flags", SAFE_WRAP(&CScriptGameObject::get_actor_relation_flags))
		.def("set_actor_relation_flags", SAFE_WRAP(&CScriptGameObject::set_actor_relation_flags))
		.def("sound_voice_prefix", &CScriptGameObject::sound_voice_prefix)

		.enum_("ACTOR_RELATIONS")
		[
			value("relation_attack", int(RELATION_REGISTRY::ATTACK)),
			value("relation_fight_help_monster", int(RELATION_REGISTRY::FIGHT_HELP_MONSTER)),
			value("relation_fight_help_human", int(RELATION_REGISTRY::FIGHT_HELP_HUMAN)),
			value("relation_kill", int(RELATION_REGISTRY::KILL))
		]

		.enum_("CLSIDS")
		[
			value("no_pda_msg", int(ePdaMsgMax))
		]

		//CustomZone
		.def("set_restrictor_type", &CScriptGameObject::SetRestrictionType)
		.def("force_set_restrictor_type", &CScriptGameObject::ForceSetRestrictionType)
		.def("invalidate_restrictions", SAFE_WRAP(&CScriptGameObject::InvalidateRestrictions))
		.def("get_restrictor_type", &CScriptGameObject::GetRestrictionType)
		.def("enable_anomaly", SAFE_WRAP(&CScriptGameObject::EnableAnomaly))
		.def("disable_anomaly", SAFE_WRAP(&CScriptGameObject::DisableAnomaly))
		.def("is_enabled_anomaly", SAFE_WRAP(&CScriptGameObject::IsEnabledAnomaly))  // demonized
		.def("set_idle_particles", SAFE_WRAP(&CScriptGameObject::ChangeAnomalyIdlePart))
		.def("get_anomaly_power", SAFE_WRAP(&CScriptGameObject::GetAnomalyPower))
		.def("set_anomaly_power", SAFE_WRAP(&CScriptGameObject::SetAnomalyPower))

		.def("get_anomaly_radius", SAFE_WRAP(&CScriptGameObject::GetAnomalyRadius))
		.def("set_anomaly_radius", SAFE_WRAP(&CScriptGameObject::SetAnomalyRadius))
		.def("set_anomaly_position", SAFE_WRAP(&CScriptGameObject::MoveAnomaly))

		.def("get_artefact_health", SAFE_WRAP(&CScriptGameObject::GetArtefactHealthRestoreSpeed))
		.def("get_artefact_radiation", SAFE_WRAP(&CScriptGameObject::GetArtefactRadiationRestoreSpeed))
		.def("get_artefact_satiety", SAFE_WRAP(&CScriptGameObject::GetArtefactSatietyRestoreSpeed))
		.def("get_artefact_power", SAFE_WRAP(&CScriptGameObject::GetArtefactPowerRestoreSpeed))
		.def("get_artefact_bleeding", SAFE_WRAP(&CScriptGameObject::GetArtefactBleedingRestoreSpeed))
		.def("get_artefact_immunity", SAFE_WRAP(&CScriptGameObject::GetArtefactImmunity))
		.def("get_artefact_additional_inventory_weight", SAFE_WRAP(&CScriptGameObject::GetArtefactAdditionalInventoryWeight))

		.def("set_artefact_health", SAFE_WRAP(&CScriptGameObject::SetArtefactHealthRestoreSpeed))
		.def("set_artefact_radiation", SAFE_WRAP(&CScriptGameObject::SetArtefactRadiationRestoreSpeed))
		.def("set_artefact_satiety", SAFE_WRAP(&CScriptGameObject::SetArtefactSatietyRestoreSpeed))
		.def("set_artefact_power", SAFE_WRAP(&CScriptGameObject::SetArtefactPowerRestoreSpeed))
		.def("set_artefact_bleeding", SAFE_WRAP(&CScriptGameObject::SetArtefactBleedingRestoreSpeed))
		.def("set_artefact_immunity", SAFE_WRAP(&CScriptGameObject::SetArtefactImmunity))
		.def("set_artefact_additional_inventory_weight", SAFE_WRAP(&CScriptGameObject::SetArtefactAdditionalInventoryWeight))

		//HELICOPTER
		.def("get_helicopter", SAFE_WRAP(&CScriptGameObject::get_helicopter))
		.def("get_car", SAFE_WRAP(&CScriptGameObject::get_car))
#ifdef STATIONARYMGUN_NEW
		.def("get_stmgun", &CScriptGameObject::get_stmgun)
#endif
#ifdef PROJECTOR_NEW
		.def("get_projector", &CScriptGameObject::get_projector)
#endif
		.def("get_hanging_lamp", &CScriptGameObject::get_hanging_lamp)
		.def("get_physics_shell", &CScriptGameObject::get_physics_shell)
		.def("get_holder_class", &CScriptGameObject::get_custom_holder)
		.def("get_current_holder", &CScriptGameObject::get_current_holder)
		//usable object
		.def("set_tip_text", &CScriptGameObject::SetTipText)
		.def("set_tip_text_default", &CScriptGameObject::SetTipTextDefault)
		.def("set_nonscript_usable", &CScriptGameObject::SetNonscriptUsable)

		// Script Zone
		.def("active_zone_contact", SAFE_WRAP(&CScriptGameObject::active_zone_contact))
		.def("inside", SAFE_WRAP((bool (CScriptGameObject::*)(const Fvector&, float) const)(&CScriptGameObject::inside)))
		.def("inside", SAFE_WRAP((bool (CScriptGameObject::*)(const Fvector&) const)(&CScriptGameObject::inside)))
		.def("set_fastcall", SAFE_WRAP(&CScriptGameObject::set_fastcall))
		.def("set_const_force", SAFE_WRAP(&CScriptGameObject::set_const_force))
		.def("info_add", SAFE_WRAP(&CScriptGameObject::info_add))
		.def("info_clear", SAFE_WRAP(&CScriptGameObject::info_clear))

		// inv box
		.def("is_inv_box_empty", &CScriptGameObject::IsInvBoxEmpty)
		.def("inv_box_closed", &CScriptGameObject::inv_box_closed)
		.def("inv_box_closed_status", &CScriptGameObject::inv_box_closed_status)
		.def("inv_box_can_take", &CScriptGameObject::inv_box_can_take)
		.def("inv_box_can_take_status", &CScriptGameObject::inv_box_can_take_status)

		// monster jumper
		.def("jump", &CScriptGameObject::jump)

		.def("make_object_visible_somewhen", SAFE_WRAP(&CScriptGameObject::make_object_visible_somewhen))

		.def("buy_condition", SAFE_WRAP((void (CScriptGameObject::*)(CScriptIniFile*, LPCSTR))(&CScriptGameObject::buy_condition)))
		.def("buy_condition", SAFE_WRAP((void (CScriptGameObject::*)(float, float))(&CScriptGameObject::buy_condition)))
		.def("show_condition", SAFE_WRAP(&CScriptGameObject::show_condition))
		.def("sell_condition",
		     SAFE_WRAP((void (CScriptGameObject::*)(CScriptIniFile*, LPCSTR))(&CScriptGameObject::sell_condition)))
		.def("sell_condition", SAFE_WRAP((void (CScriptGameObject::*)(float, float))(&CScriptGameObject::sell_condition)))
		.def("buy_supplies", SAFE_WRAP(&CScriptGameObject::buy_supplies))
		.def("buy_item_condition_factor", SAFE_WRAP(&CScriptGameObject::buy_item_condition_factor))
		.def("buy_item_exponent", SAFE_WRAP(&CScriptGameObject::buy_item_exponent))
		.def("sell_item_exponent", SAFE_WRAP(&CScriptGameObject::sell_item_exponent))

		.def("sound_prefix", SAFE_WRAP((LPCSTR (CScriptGameObject::*)() const)(&CScriptGameObject::sound_prefix)))
		.def("sound_prefix", SAFE_WRAP((void (CScriptGameObject::*)(LPCSTR))(&CScriptGameObject::sound_prefix)))

		.def("location_on_path", SAFE_WRAP(&CScriptGameObject::location_on_path))
		.def("is_there_items_to_pickup", SAFE_WRAP(&CScriptGameObject::is_there_items_to_pickup))

		.def("wounded", SAFE_WRAP((bool (CScriptGameObject::*)() const)(&CScriptGameObject::wounded)))
		.def("wounded", SAFE_WRAP((void (CScriptGameObject::*)(bool))(&CScriptGameObject::wounded)))

		// demonized: Toggle movement collision for stalker NPCs
		.def("set_enable_movement_collision", &CScriptGameObject::set_enable_movement_collision)

		.def("mark_item_dropped", &CScriptGameObject::MarkItemDropped)
		.def("marked_dropped", &CScriptGameObject::MarkedDropped)
		.def("unload_magazine", &CScriptGameObject::UnloadMagazine)
		.def("force_unload_magazine", &CScriptGameObject::ForceUnloadMagazine)

		.def("sight_params", SAFE_WRAP(&CScriptGameObject::sight_params))

		.def("movement_enabled", SAFE_WRAP(&CScriptGameObject::enable_movement))
		.def("movement_enabled", SAFE_WRAP(&CScriptGameObject::movement_enabled))

		.def("critically_wounded", SAFE_WRAP(&CScriptGameObject::critically_wounded))
		.def("get_campfire", &CScriptGameObject::get_campfire)
		.def("get_artefact", &CScriptGameObject::get_artefact)
		.def("get_physics_object", &CScriptGameObject::get_physics_object)
		.def("aim_time", SAFE_WRAP((void (CScriptGameObject::*)(CScriptGameObject*, u32))&CScriptGameObject::aim_time))
		.def("aim_time", SAFE_WRAP((u32 (CScriptGameObject::*)(CScriptGameObject*))&CScriptGameObject::aim_time))

		.def("special_danger_move", SAFE_WRAP((void (CScriptGameObject::*)(bool))&CScriptGameObject::special_danger_move))
		.def("special_danger_move", SAFE_WRAP((bool (CScriptGameObject::*)())&CScriptGameObject::special_danger_move))

		.def("sniper_update_rate", SAFE_WRAP((void (CScriptGameObject::*)(bool))&CScriptGameObject::sniper_update_rate))
		.def("sniper_update_rate", SAFE_WRAP((bool (CScriptGameObject::*)() const)&CScriptGameObject::sniper_update_rate))

		.def("sniper_fire_mode", SAFE_WRAP((void (CScriptGameObject::*)(bool))&CScriptGameObject::sniper_fire_mode))
		.def("sniper_fire_mode", SAFE_WRAP((bool (CScriptGameObject::*)() const)&CScriptGameObject::sniper_fire_mode))

		.def("aim_bone_id", SAFE_WRAP((void (CScriptGameObject::*)(LPCSTR))&CScriptGameObject::aim_bone_id))
		.def("aim_bone_id", SAFE_WRAP((LPCSTR (CScriptGameObject::*)() const)&CScriptGameObject::aim_bone_id))

		.def("actor_look_at_point", &CScriptGameObject::ActorLookAtPoint)
		.def("actor_stop_look_at_point", &CScriptGameObject::ActorStopLookAtPoint)
		.def("enable_level_changer", &CScriptGameObject::enable_level_changer)
		.def("is_level_changer_enabled", &CScriptGameObject::is_level_changer_enabled)

		.def("set_level_changer_invitation", &CScriptGameObject::set_level_changer_invitation)
		.def("start_particles", &CScriptGameObject::start_particles)
		.def("stop_particles", &CScriptGameObject::stop_particles)
		//Alundaio: Extended exports
#ifdef ENABLE_CAR
		//For Car
		.def("attach_vehicle", &CScriptGameObject::AttachVehicle)
		.def("detach_vehicle", &CScriptGameObject::DetachVehicle)
		.def("get_attached_vehicle", &CScriptGameObject::GetAttachedVehicle)
#endif
#ifdef GAME_OBJECT_EXTENDED_EXPORTS
		.def("reset_bone_protections", &CScriptGameObject::ResetBoneProtections)
		.def("iterate_feel_touch", &CScriptGameObject::IterateFeelTouch)
		.def("get_luminocity_hemi", &CScriptGameObject::GetLuminocityHemi)
		.def("get_luminocity", &CScriptGameObject::GetLuminocity)
		.def("set_health_ex", &CScriptGameObject::SetHealthEx)
		.def("force_set_position", SAFE_WRAP(&CScriptGameObject::ForceSetPosition))
		.def("force_set_angle", SAFE_WRAP(&CScriptGameObject::ForceSetAngle))
		.def("angle", SAFE_WRAP(&CScriptGameObject::Angle))
		.def("force_set_rotation", SAFE_WRAP(&CScriptGameObject::ForceSetRotation))
		.def("set_spatial_type", SAFE_WRAP(&CScriptGameObject::SetSpatialType))
		.def("get_spatial_type", SAFE_WRAP(&CScriptGameObject::GetSpatialType))
		.def("destroy_object", SAFE_WRAP(&CScriptGameObject::DestroyObject))
		//For Ammo
		.def("ammo_get_count", &CScriptGameObject::AmmoGetCount)
		.def("ammo_set_count", &CScriptGameObject::AmmoSetCount)
		.def("ammo_box_size", &CScriptGameObject::AmmoBoxSize)
		//For Weapons
		.def("weapon_addon_attach", SAFE_WRAP(&CScriptGameObject::Weapon_AddonAttach))
		.def("weapon_addon_detach", SAFE_WRAP(&CScriptGameObject::Weapon_AddonDetach))
		//For Weapon & Outfit
		.def("install_upgrade", SAFE_WRAP(&CScriptGameObject::InstallUpgrade))
		.def("has_upgrade", SAFE_WRAP(&CScriptGameObject::HasUpgrade))
		.def("iterate_installed_upgrades", &CScriptGameObject::IterateInstalledUpgrades)
		.def("weapon_in_grenade_mode", &CScriptGameObject::WeaponInGrenadeMode)
		// For CHudItem
		.def("play_hud_motion", SAFE_WRAP(&CScriptGameObject::PlayHudMotion))
		.def("switch_state", SAFE_WRAP(&CScriptGameObject::SwitchState))
		.def("get_state", SAFE_WRAP(&CScriptGameObject::GetState))
		// For EatableItem
		.def("set_remaining_uses", SAFE_WRAP(&CScriptGameObject::SetRemainingUses))
		.def("get_remaining_uses", SAFE_WRAP(&CScriptGameObject::GetRemainingUses))
		.def("get_max_uses", SAFE_WRAP(&CScriptGameObject::GetMaxUses))
		// Phantom
		.def("phantom_set_enemy", &CScriptGameObject::PhantomSetEnemy)
		// Actor
		.def("set_character_icon", &CScriptGameObject::SetCharacterIcon)
		//casting
		.def("cast_Actor", &CScriptGameObject::cast_Actor)
		.def("cast_Car", &CScriptGameObject::cast_Car)
		.def("cast_Heli", &CScriptGameObject::cast_Heli)
		.def("cast_InventoryOwner", &CScriptGameObject::cast_InventoryOwner)
		.def("cast_InventoryBox", &CScriptGameObject::cast_InventoryBox)
		.def("cast_CustomZone", &CScriptGameObject::cast_CustomZone)
		.def("cast_TorridZone", &CScriptGameObject::cast_TorridZone)
		.def("cast_MosquitoBald", &CScriptGameObject::cast_MosquitoBald)
		.def("cast_ZoneCampfire", &CScriptGameObject::cast_ZoneCampfire)
		.def("cast_InventoryItem", &CScriptGameObject::cast_InventoryItem)
		.def("cast_CustomOutfit", &CScriptGameObject::cast_CustomOutfit)
		.def("cast_Helmet", &CScriptGameObject::cast_Helmet)
		.def("cast_Artefact", &CScriptGameObject::cast_Artefact)
		.def("cast_Ammo", &CScriptGameObject::cast_Ammo)
		.def("cast_Weapon", &CScriptGameObject::cast_Weapon)
		.def("cast_Knife", &CScriptGameObject::cast_Knife)
		.def("cast_WeaponMagazined", &CScriptGameObject::cast_WeaponMagazined)
		.def("cast_WeaponMagazinedWGrenade", &CScriptGameObject::cast_WeaponMagazinedWGrenade)
		.def("cast_EatableItem", SAFE_WRAP(&CScriptGameObject::cast_EatableItem))
		.def("cast_Medkit", SAFE_WRAP(&CScriptGameObject::cast_Medkit))
		.def("cast_Antirad", SAFE_WRAP(&CScriptGameObject::cast_Antirad))
		.def("cast_FoodItem", SAFE_WRAP(&CScriptGameObject::cast_FoodItem))
		.def("cast_BottleItem", SAFE_WRAP(&CScriptGameObject::cast_BottleItem))
		.def("cast_Missile", &CScriptGameObject::cast_Missile)
		.def("cast_Explosive", &CScriptGameObject::cast_Explosive)
		//Alundaio: END

		//Torch
		.def("set_color_animator", &CScriptGameObject::set_color_animator)
		.def("reset_color_animator", &CScriptGameObject::reset_color_animator)
#endif
#ifdef GAME_OBJECT_TESTING_EXPORTS
		//AVO: additional functions
		//.def("is_game_object", &CScriptGameObject::IsGameObject)
		//.def("is_car", &CScriptGameObject::IsCar)
		//.def("is_helicopter", &CScriptGameObject::IsHeli)
		//.def("is_holder", &CScriptGameObject::IsHolderCustom)
		.def("is_entity_alive", &CScriptGameObject::IsEntityAlive)
		.def("is_inventory_item", &CScriptGameObject::IsInventoryItem)
		.def("is_inventory_owner", &CScriptGameObject::IsInventoryOwner)
		.def("is_actor", &CScriptGameObject::IsActor)
		.def("is_custom_monster", &CScriptGameObject::IsCustomMonster)
		.def("is_weapon", &CScriptGameObject::IsWeapon)
		//.def("is_medkit", &CScriptGameObject::IsMedkit)
		//.def("is_eatable_item", &CScriptGameObject::IsEatableItem)
		//.def("is_antirad", &CScriptGameObject::IsAntirad)
		.def("is_outfit", &CScriptGameObject::IsCustomOutfit)
		.def("is_helmet", &CScriptGameObject::IsHelmet)
		.def("is_scope", &CScriptGameObject::IsScope)
		.def("is_silencer", &CScriptGameObject::IsSilencer)
		.def("is_grenade_launcher", &CScriptGameObject::IsGrenadeLauncher)
		.def("is_weapon_magazined", &CScriptGameObject::IsWeaponMagazined)
		.def("is_space_restrictor", &CScriptGameObject::IsSpaceRestrictor)
		.def("is_stalker", &CScriptGameObject::IsStalker)
		.def("is_anomaly", &CScriptGameObject::IsAnomaly)
		.def("is_monster", &CScriptGameObject::IsMonster)
		//.def("is_explosive", &CScriptGameObject::IsExplosive)
		//.def("is_script_zone", &CScriptGameObject::IsScriptZone)
		//.def("is_projector", &CScriptGameObject::IsProjector)
		.def("is_trader", &CScriptGameObject::IsTrader)
		.def("is_hud_item", &CScriptGameObject::IsHudItem)
		//.def("is_food_item", &CScriptGameObject::IsFoodItem)
		.def("is_artefact", &CScriptGameObject::IsArtefact)
		.def("is_ammo", &CScriptGameObject::IsAmmo)
		//.def("is_missile", &CScriptGameObject::IsMissile)
		//.def("is_physics_shell_holder", &CScriptGameObject::IsPhysicsShellHolder)
		//.def("is_grenade", &CScriptGameObject::IsGrenade)
		//.def("is_bottle_item", &CScriptGameObject::IsBottleItem)
		//.def("is_torch", &CScriptGameObject::IsTorch)
		.def("is_weapon_gl", &CScriptGameObject::IsWeaponGL)
		.def("is_inventory_box", &CScriptGameObject::IsInventoryBox)

		.def("get_actor_max_weight", SAFE_WRAP(&CScriptGameObject::GetActorMaxWeight))
		.def("set_actor_max_weight", SAFE_WRAP(&CScriptGameObject::SetActorMaxWeight))
		.def("get_actor_max_walk_weight", SAFE_WRAP(&CScriptGameObject::GetActorMaxWalkWeight))
		.def("set_actor_max_walk_weight", SAFE_WRAP(&CScriptGameObject::SetActorMaxWalkWeight))
		.def("get_additional_max_weight", SAFE_WRAP(&CScriptGameObject::GetAdditionalMaxWeight))
		.def("set_additional_max_weight", SAFE_WRAP(&CScriptGameObject::SetAdditionalMaxWeight))
		.def("get_additional_max_walk_weight", SAFE_WRAP(&CScriptGameObject::GetAdditionalMaxWalkWeight))
		.def("set_additional_max_walk_weight", SAFE_WRAP(&CScriptGameObject::SetAdditionalMaxWalkWeight))
		.def("get_total_weight", SAFE_WRAP(&CScriptGameObject::GetTotalWeight))

		// demonized: force update of weight
		.def("update_weight", SAFE_WRAP(&CScriptGameObject::UpdateWeight))
		.def("get_total_weight_force_update", SAFE_WRAP(&CScriptGameObject::GetTotalWeightForceUpdate))

		// demonized: get luminosity as displayed in ui
		.def("get_actor_ui_luminosity", &CScriptGameObject::GetActorUILuminosity)

		.def("weight", SAFE_WRAP(&CScriptGameObject::Weight))

		.def("get_actor_jump_speed", SAFE_WRAP(&CScriptGameObject::GetActorJumpSpeed))
		.def("set_actor_jump_speed", SAFE_WRAP(&CScriptGameObject::SetActorJumpSpeed))
		.def("get_actor_sprint_koef", SAFE_WRAP(&CScriptGameObject::GetActorSprintKoef))
		.def("set_actor_sprint_koef", SAFE_WRAP(&CScriptGameObject::SetActorSprintKoef))
		.def("get_actor_run_coef", SAFE_WRAP(&CScriptGameObject::GetActorRunCoef))
		.def("set_actor_run_coef", SAFE_WRAP(&CScriptGameObject::SetActorRunCoef))
		.def("get_actor_runback_coef", SAFE_WRAP(&CScriptGameObject::GetActorRunBackCoef))
		.def("set_actor_runback_coef", SAFE_WRAP(&CScriptGameObject::SetActorRunBackCoef))
		//end AVO

		// demonized: Additional exports
		.def("get_actor_walk_accel", SAFE_WRAP(&CScriptGameObject::GetActorWalkAccel))
		.def("set_actor_walk_accel", SAFE_WRAP(&CScriptGameObject::SetActorWalkAccel))
		.def("set_actor_box_y_offset", SAFE_WRAP(&CScriptGameObject::SetActorCamBoxYOffset))
		.def("get_actor_walk_back_coef", SAFE_WRAP(&CScriptGameObject::GetActorWalkBackCoef))
		.def("set_actor_walk_back_coef", SAFE_WRAP(&CScriptGameObject::SetActorWalkBackCoef))
		.def("get_actor_crouch_coef", SAFE_WRAP(&CScriptGameObject::GetActorCrouchCoef))
		.def("set_actor_crouch_coef", SAFE_WRAP(&CScriptGameObject::SetActorCrouchCoef))
		.def("get_actor_climb_coef", SAFE_WRAP(&CScriptGameObject::GetActorClimbCoef))
		.def("set_actor_climb_coef", SAFE_WRAP(&CScriptGameObject::SetActorClimbCoef))
		.def("get_actor_walk_strafe_coef", SAFE_WRAP(&CScriptGameObject::GetActorWalkStrafeCoef))
		.def("set_actor_walk_strafe_coef", SAFE_WRAP(&CScriptGameObject::SetActorWalkStrafeCoef))
		.def("get_actor_run_strafe_coef", SAFE_WRAP(&CScriptGameObject::GetActorRunStrafeCoef))
		.def("set_actor_run_strafe_coef", SAFE_WRAP(&CScriptGameObject::SetActorRunStrafeCoef))
		.def("get_actor_sprint_strafe_coef", SAFE_WRAP(&CScriptGameObject::GetActorSprintStrafeCoef))
		.def("set_actor_sprint_strafe_coef", SAFE_WRAP(&CScriptGameObject::SetActorSprintStrafeCoef))
		.def("get_actor_object_looking_at", SAFE_WRAP(&CScriptGameObject::GetActorObjectLookingAt))
		.def("get_actor_person_looking_at", SAFE_WRAP(&CScriptGameObject::GetActorPersonLookingAt))
		.def("get_actor_default_action_for_object", SAFE_WRAP(&CScriptGameObject::GetActorDefaultActionForObject))

		// demonized: Adjust Lookout factor
		.def("get_actor_lookout_coef", SAFE_WRAP(&CScriptGameObject::GetActorLookoutCoef))
		.def("set_actor_lookout_coef", SAFE_WRAP(&CScriptGameObject::SetActorLookoutCoef))

		// demonized: add getters and setters for pathfinding for npcs around anomalies and damage for npcs
		.def("get_enable_anomalies_pathfinding", SAFE_WRAP(&CScriptGameObject::get_enable_anomalies_pathfinding))
		.def("set_enable_anomalies_pathfinding", SAFE_WRAP(&CScriptGameObject::set_enable_anomalies_pathfinding))
		.def("get_enable_anomalies_damage", SAFE_WRAP(&CScriptGameObject::get_enable_anomalies_damage))
		.def("set_enable_anomalies_damage", SAFE_WRAP(&CScriptGameObject::set_enable_anomalies_damage))

		// demonized: get object currently talking to
		.def("get_talking_npc", &CScriptGameObject::get_talking_npc)

		// demonized: get current scope texture
		.def("get_scope_ui", &CScriptGameObject::get_scope_ui)
		.def("set_scope_ui", &CScriptGameObject::set_scope_ui)
#endif

		.def("set_can_be_harmed", SAFE_WRAP(&CScriptGameObject::SetCanBeHarmed))
		.def("can_be_harmed", SAFE_WRAP(&CScriptGameObject::CanBeHarmed))

		// Lucy: Script Attachments
		.def("add_attachment", SAFE_WRAP(&CScriptGameObject::AddAttachment))
		.def("get_attachment", SAFE_WRAP(&CScriptGameObject::GetAttachment))
		.def("remove_attachment", SAFE_WRAP((void (CScriptGameObject::*)(LPCSTR)) &CScriptGameObject::RemoveAttachment))
		.def("remove_attachment", SAFE_WRAP((void (CScriptGameObject::*)(script_attachment*)) &CScriptGameObject::RemoveAttachment))
		.def("iterate_attachments", SAFE_WRAP(&CScriptGameObject::IterateAttachments))

		.def("get_shaders", SAFE_WRAP(&CScriptGameObject::GetShaders))
		.def("get_default_shaders", SAFE_WRAP(&CScriptGameObject::GetDefaultShaders))
		.def("set_shader", SAFE_WRAP(&CScriptGameObject::SetShaderTexture))
		.def("reset_shader", SAFE_WRAP(&CScriptGameObject::ResetShaderTexture))
		;
}
