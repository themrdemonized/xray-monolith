////////////////////////////////////////////////////////////////////////////
//	Module 		: script_game_object_script2.cpp
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
#include "explosive.h"
#include "script_zone.h"
#include "object_handler.h"
#include "script_hit.h"
#include "../Include/xrRender/Kinematics.h"
#include "pda.h"
#include "InfoPortion.h"
#include "memory_manager.h"
#include "ai_phrasedialogmanager.h"
#include "xrMessages.h"
#include "custommonster.h"
#include "memory_manager.h"
#include "visual_memory_manager.h"
#include "sound_memory_manager.h"
#include "hit_memory_manager.h"
#include "enemy_manager.h"
#include "item_manager.h"
#include "danger_manager.h"
#include "memory_space.h"
#include "actor.h"
#include "../Include/xrRender/Kinematics.h"
#include "../xrEngine/CameraBase.h"
#include "ai/stalker/ai_stalker.h"
#include "car.h"
#include "movement_manager.h"
#include "detail_path_manager.h"
#include "Inventory.h"
#include "InventoryOwner.h"
#include "CharacterPhysicsSupport.h"

void CScriptGameObject::explode(u32 level_time)
{
	CExplosive* explosive = smart_cast<CExplosive*>(&object());
	if (object().H_Parent())
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CExplosive : cannot explode object wiht parent!");
		return;
	}

	if (!explosive)
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CExplosive : cannot access class member explode!");
	else
	{
		Fvector normal;
		explosive->FindNormal(normal);
		explosive->SetInitiator(object().ID());
		explosive->GenExplodeEvent(object().Position(), normal);
	}
}

bool CScriptGameObject::active_zone_contact(u16 id)
{
	CScriptZone* script_zone = smart_cast<CScriptZone*>(&object());
	if (!script_zone)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CScriptZone : cannot access class member active_zone_contact!");
		return (false);
	}
	return (script_zone->active_contact(id));
}

CScriptGameObject* CScriptGameObject::best_weapon()
{
	CObjectHandler* object_handler = smart_cast<CAI_Stalker*>(&object());
	if (!object_handler)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CScriptEntity : cannot access class member best_weapon!");
		return (0);
	}
	else
	{
		//Alundaio: extra security
		CGameObject* game_object = object_handler->best_weapon() ? &object_handler->best_weapon()->object() : NULL;
		if (!game_object)
			return (0);

		if (!game_object->H_Parent() || game_object->H_Parent()->ID() != object().ID())
			return (0);
		//-Alundaio
		return (game_object->lua_game_object());
	}
}

void CScriptGameObject::set_item(MonsterSpace::EObjectAction object_action)
{
	CObjectHandler* object_handler = smart_cast<CAI_Stalker*>(&object());
	if (!object_handler)
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CObjectHandler : cannot access class member set_item!");
	else
		object_handler->set_goal(object_action);
}

void CScriptGameObject::set_item(MonsterSpace::EObjectAction object_action, CScriptGameObject* lua_game_object)
{
	CObjectHandler* object_handler = smart_cast<CAI_Stalker*>(&object());
	if (!object_handler)
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CObjectHandler : cannot access class member set_item!");
	else
		object_handler->set_goal(object_action, lua_game_object ? &lua_game_object->object() : 0);
}

void CScriptGameObject::set_item(MonsterSpace::EObjectAction object_action, CScriptGameObject* lua_game_object,
                                 u32 queue_size)
{
	CObjectHandler* object_handler = smart_cast<CAI_Stalker*>(&object());
	if (!object_handler)
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CObjectHandler : cannot access class member set_item!");
	else
		object_handler->set_goal(object_action, lua_game_object ? &lua_game_object->object() : 0, queue_size,
		                         queue_size);
}

void CScriptGameObject::set_item(MonsterSpace::EObjectAction object_action, CScriptGameObject* lua_game_object,
                                 u32 queue_size, u32 queue_interval)
{
	CObjectHandler* object_handler = smart_cast<CAI_Stalker*>(&object());
	if (!object_handler)
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CObjectHandler : cannot access class member set_item!");
	else
		object_handler->set_goal(object_action, lua_game_object ? &lua_game_object->object() : 0, queue_size,
		                         queue_size, queue_interval, queue_interval);
}

void CScriptGameObject::play_cycle(LPCSTR anim, bool mix_in)
{
	IKinematicsAnimated* sa = smart_cast<IKinematicsAnimated*>(object().Visual());
	if (sa)
	{
		MotionID m = sa->ID_Cycle(anim);
		if (m) sa->PlayCycle(m, (BOOL)mix_in);
		else
		{
			ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CGameObject : has not cycle %s",
			                                anim);
		}
	}
	else
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CGameObject : is not animated object");
	}
}

void CScriptGameObject::play_cycle(LPCSTR anim)
{
	play_cycle(anim, true);
}

void CScriptGameObject::Hit(CScriptHit* tpLuaHit)
{
	CScriptHit& tLuaHit = *tpLuaHit;
	NET_Packet P;
	SHit HS;
	HS.GenHeader(GE_HIT, object().ID());
	HS.ApplyScriptHit(&tLuaHit);
	if (xr_strlen(tLuaHit.m_caBoneName))
	{
		IKinematics* V = smart_cast<IKinematics*>(object().Visual());
		if (V)
			HS.boneID = (V->LL_BoneID(tLuaHit.m_caBoneName));
	}
	HS.p_in_bone_space = Fvector().set(0, 0, 0);
	HS.Write_Packet(P);

	object().u_EventSend(P);
}


#pragma todo("Dima to Dima : find out why user defined conversion operators work incorrect")

CScriptGameObject::operator CObject*()
{
	return (&object());
}

CScriptGameObject* CScriptGameObject::GetBestEnemy()
{
	const CCustomMonster* monster = smart_cast<const CCustomMonster*>(&object());
	if (!monster)
		return (0);

	if (monster->memory().enemy().selected())
		return (monster->memory().enemy().selected()->lua_game_object());
	return (0);
}

const CDangerObject* CScriptGameObject::GetBestDanger()
{
	const CCustomMonster* monster = smart_cast<const CCustomMonster*>(&object());
	if (!monster)
		return (0);

	if (!monster->memory().danger().selected())
		return (0);

	return (monster->memory().danger().selected());
}

CScriptGameObject* CScriptGameObject::GetBestItem()
{
	const CCustomMonster* monster = smart_cast<const CCustomMonster*>(&object());
	if (!monster)
		return (0);

	if (monster->memory().item().selected())
		return (monster->memory().item().selected()->lua_game_object());
	return (0);
}

u32 CScriptGameObject::memory_time(const CScriptGameObject& lua_game_object)
{
	CCustomMonster* monster = smart_cast<CCustomMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CScriptEntity : cannot access class member memory!");
		return (0);
	}
	else
		return (monster->memory().memory_time(&lua_game_object.object()));
}

Fvector CScriptGameObject::memory_position(const CScriptGameObject& lua_game_object)
{
	CCustomMonster* monster = smart_cast<CCustomMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CScriptEntity : cannot access class member memory!");
		return (Fvector().set(0.f, 0.f, 0.f));
	}
	else
		return (monster->memory().memory_position(&lua_game_object.object()));
}

void CScriptGameObject::enable_memory_object(CScriptGameObject* game_object, bool enable)
{
	CCustomMonster* monster = smart_cast<CCustomMonster*>(&object());
	if (!monster)
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CGameObject : cannot access class member enable_memory_object!");
	else
		monster->memory().enable(&game_object->object(), enable);
}

const xr_vector<CNotYetVisibleObject>& CScriptGameObject::not_yet_visible_objects() const
{
	CCustomMonster* monster = smart_cast<CCustomMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CGameObject : cannot access class member not_yet_visible_objects!");
		NODEFAULT;
	}
	return (monster->memory().visual().not_yet_visible_objects());
}

float CScriptGameObject::visibility_threshold() const
{
	CCustomMonster* monster = smart_cast<CCustomMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CGameObject : cannot access class member visibility_threshold!");
		NODEFAULT;
	}
	return (monster->memory().visual().visibility_threshold());
}

void CScriptGameObject::enable_vision(bool value)
{
	CCustomMonster* monster = smart_cast<CCustomMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CVisualMemoryManager : cannot access class member enable_vision!");
		return;
	}
	monster->memory().visual().enable(value);
}

bool CScriptGameObject::vision_enabled() const
{
	CCustomMonster* monster = smart_cast<CCustomMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CVisualMemoryManager : cannot access class member vision_enabled!");
		return (false);
	}
	return (monster->memory().visual().enabled());
}

void CScriptGameObject::set_sound_threshold(float value)
{
	CCustomMonster* monster = smart_cast<CCustomMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CSoundMemoryManager : cannot access class member set_sound_threshold!");
		return;
	}
	monster->memory().sound().set_threshold(value);
}

void CScriptGameObject::restore_sound_threshold()
{
	CCustomMonster* monster = smart_cast<CCustomMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CSoundMemoryManager : cannot access class member restore_sound_threshold!");
		return;
	}
	monster->memory().sound().restore_threshold();
}

void CScriptGameObject::SetStartDialog(LPCSTR dialog_id)
{
	CAI_PhraseDialogManager* pDialogManager = smart_cast<CAI_PhraseDialogManager*>(&object());
	if (!pDialogManager) return;
	pDialogManager->SetStartDialog(dialog_id);
}

void CScriptGameObject::GetStartDialog()
{
	CAI_PhraseDialogManager* pDialogManager = smart_cast<CAI_PhraseDialogManager*>(&object());
	if (!pDialogManager) return;
	pDialogManager->GetStartDialog();
}

void CScriptGameObject::RestoreDefaultStartDialog()
{
	CAI_PhraseDialogManager* pDialogManager = smart_cast<CAI_PhraseDialogManager*>(&object());
	if (!pDialogManager) return;
	pDialogManager->RestoreDefaultStartDialog();
}

void CScriptGameObject::SetActorPosition(Fvector pos, bool bskip_collision_correct)
{
	CActor* actor = smart_cast<CActor*>(&object());
	if (actor)
	{
		if (bskip_collision_correct)
		{
			Fmatrix F = actor->XFORM();
			F.c = pos;
			actor->XFORM().set(F);
			if (actor->character_physics_support()->movement()->CharacterExist())
			{
				actor->character_physics_support()->movement()->SetPosition(F.c);
				actor->character_physics_support()->movement()->SetVelocity(0.f, 0.f, 0.f);
			}
		}
		else
		{
			Fmatrix F = actor->XFORM();
			F.c = pos;
			actor->ForceTransform(F);
		}
	}
	else
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "ScriptGameObject : attempt to call SetActorPosition method for non-actor object");
}

void CScriptGameObject::SetNpcPosition(Fvector pos)
{
	CCustomMonster* obj = smart_cast<CCustomMonster*>(&object());
	if (obj)
	{
		Fmatrix F = obj->XFORM();
		F.c = pos;
		obj->movement().detail().make_inactual();
		if (obj->animation_movement_controlled())
			obj->destroy_anim_mov_ctrl();
		obj->ForceTransform(F);
		//		actor->XFORM().c = pos;
	}
	else
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "ScriptGameObject : attempt to call SetActorPosition method for non-CCustomMonster object");
}

// demonized: add pitch for set actor direction
void CScriptGameObject::SetActorDirection(float dir, float pitch, float roll)
{
	CActor* actor = smart_cast<CActor*>(&object());
	if (actor)
	{
		actor->cam_Active()->Set(dir, pitch, roll);
		//		actor->XFORM().setXYZ(0,dir,0);
	} else
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
			"ScriptGameObject : attempt to call SetActorDirection method for non-actor object");
}

void CScriptGameObject::SetActorDirection(float dir, float pitch)
{
	SetActorDirection(dir, pitch, 0);
}

void CScriptGameObject::SetActorDirection(float dir)
{
	SetActorDirection(dir, 0, 0);
}

// HPB vector input
void CScriptGameObject::SetActorDirection(const Fvector& dir)
{
	SetActorDirection(dir.x, dir.y, dir.z);
}

void CScriptGameObject::DisableHitMarks(bool disable)
{
	CActor* actor = smart_cast<CActor*>(&object());
	if (actor)
		actor->DisableHitMarks(disable);
	else
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "ScriptGameObject : attempt to call DisableHitMarks method for non-actor object");
}

bool CScriptGameObject::DisableHitMarks() const
{
	CActor* actor = smart_cast<CActor*>(&object());
	if (actor)
		return actor->DisableHitMarks();
	else
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "ScriptGameObject : attempt to call DisableHitMarks method for non-actor object");
		return false;
	}
}

Fvector CScriptGameObject::GetMovementSpeed() const
{
	CActor* actor = smart_cast<CActor*>(&object());
	if (!actor)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "ScriptGameObject : attempt to call GetMovementSpeed method for non-actor object");
		NODEFAULT;
	}
	//return actor->GetMovementSpeed();
	return actor->character_physics_support()->movement()->GetVelocity();
}

CHolderCustom* CScriptGameObject::get_current_holder()
{
	CActor* actor = smart_cast<CActor*>(&object());

	if (actor)
		return actor->Holder();
	else
		return NULL;
}

void CScriptGameObject::set_ignore_monster_threshold(float ignore_monster_threshold)
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CAI_Stalker : cannot access class member set_ignore_monster_threshold!");
		return;
	}
	clamp(ignore_monster_threshold, 0.f, 1.f);
	stalker->memory().enemy().ignore_monster_threshold(ignore_monster_threshold);
}

void CScriptGameObject::restore_ignore_monster_threshold()
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CAI_Stalker : cannot access class member restore_ignore_monster_threshold!");
		return;
	}
	stalker->memory().enemy().restore_ignore_monster_threshold();
}

float CScriptGameObject::ignore_monster_threshold() const
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CAI_Stalker : cannot access class member ignore_monster_threshold!");
		return (0.f);
	}
	return (stalker->memory().enemy().ignore_monster_threshold());
}

void CScriptGameObject::set_max_ignore_monster_distance(const float& max_ignore_monster_distance)
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CAI_Stalker : cannot access class member set_max_ignore_monster_distance!");
		return;
	}
	stalker->memory().enemy().max_ignore_monster_distance(max_ignore_monster_distance);
}

void CScriptGameObject::restore_max_ignore_monster_distance()
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CAI_Stalker : cannot access class member restore_max_ignore_monster_distance!");
		return;
	}
	stalker->memory().enemy().restore_max_ignore_monster_distance();
}

float CScriptGameObject::max_ignore_monster_distance() const
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CAI_Stalker : cannot access class member max_ignore_monster_distance!");
		return (0.f);
	}
	return (stalker->memory().enemy().max_ignore_monster_distance());
}

CCar* CScriptGameObject::get_car()
{
	CCar* car = smart_cast<CCar*>(&object());
	if (!car)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CGameObject : cannot access class member get_car!");
		NODEFAULT;
	}
	return car;
}

#ifdef DEBUG
void CScriptGameObject::debug_planner				(const script_planner *planner)
{
	CAI_Stalker		*stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker) {
		ai().script_engine().script_log			(ScriptStorage::eLuaMessageTypeError,"CAI_Stalker : cannot access class member debug_planner!");
		return;
	}

	stalker->debug_planner	(planner);
}
#endif

u32 CScriptGameObject::location_on_path(float distance, Fvector* location)
{
	if (!location)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CAI_Stalker : location_on_path -> specify destination location!");
		return (u32(-1));
	}

	CCustomMonster* monster = smart_cast<CCustomMonster*>(&object());
	if (!monster)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CAI_Stalker : cannot access class member location_on_path!");
		return (u32(-1));
	}

	VERIFY(location);
	return (monster->movement().detail().location_on_path(monster, distance, *location));
}

bool CScriptGameObject::is_there_items_to_pickup() const
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "CAI_Stalker : cannot access class member is_there_items_to_pickup!");
		return false;
	}
	return (!!stalker->memory().item().selected());
}

void CScriptGameObject::ResetBoneProtections(LPCSTR imm_sect, LPCSTR bone_sect)
{
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (!stalker)
		return;

	stalker->ResetBoneProtections(imm_sect, bone_sect);
}

#include "stalker_animation_manager.h"
#include "CharacterPhysicsSupport.h"
#include "PhysicsShellHolder.h"

void CScriptGameObject::set_visual_name(LPCSTR visual, bool bForce)
{
	if (strcmp(visual, object().cNameVisual().c_str()) == 0)
		return;

	NET_Packet P;
	object().u_EventGen(P, GE_CHANGE_VISUAL, object().ID());
	P.w_stringZ(visual);
	object().u_EventSend(P);

	CActor* actor = smart_cast<CActor*>(&object());
	if (actor)
	{
		actor->ChangeVisual(visual);
		return;
	}

	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
	if (stalker)
	{
		stalker->ChangeVisual(visual);

		IKinematicsAnimated* V = smart_cast<IKinematicsAnimated*>(stalker->Visual());
		if (V)
		{
			if (!stalker->g_Alive())
			{
				stalker->m_pPhysics_support->in_Die(false);
			}
			else
			{
				stalker->CStepManager::reload(stalker->cNameSect().c_str());
			}

			stalker->CDamageManager::reload(*stalker->cNameSect(), "damage", pSettings);
			stalker->ResetBoneProtections(NULL, NULL);
			stalker->reattach_items();
			stalker->m_pPhysics_support->in_ChangeVisual();
			stalker->animation().reload();
		}

		return;
	}

	object().cNameVisual_set(visual);
	object().Visual()->dcast_PKinematics()->CalculateBones_Invalidate();
	object().Visual()->dcast_PKinematics()->CalculateBones(TRUE);
}

float CScriptGameObject::get_current_weight()
{
	CInventoryOwner* inv_owner = smart_cast<CInventoryOwner*>(&object());

	if (inv_owner)
		return inv_owner->inventory().TotalWeight();

	return 0;
}

float CScriptGameObject::get_max_weight()
{
	CInventoryOwner* inv_owner = smart_cast<CInventoryOwner*>(&object());

	if (inv_owner)
		return inv_owner->inventory().GetMaxWeight();

	return 0;
}

LPCSTR CScriptGameObject::get_visual_name() const
{
	return object().cNameVisual().c_str();
}

#include <Inventory.h>
#include <Weapon.h>
#include <xr_level_controller.h>

void CScriptGameObject::reload_weapon()
{
	CActor* act = smart_cast<CActor*>(&object());
	if (act)
	{
		PIItem it = act->inventory().ActiveItem();
		if (it)
		{
			CWeapon* wpn = it->cast_weapon();
			if (wpn)
				wpn->Action(kWPN_RELOAD, CMD_START);
		}
	}
}

// demonized: get talking npc
CScriptGameObject* CScriptGameObject::get_talking_npc() {
	CInventoryOwner* pInvOwner = smart_cast<CInventoryOwner*>(&object());
	if (!pInvOwner)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
										"CScriptGameObject : get_talking_npc works only with CInventoryOwner!");
		return nullptr;
	}

	auto* tp = pInvOwner->GetTalkPartner();
	if (!tp) return nullptr;
	
	auto* g_obj = tp->cast_game_object();
	if (!g_obj) return nullptr;

	return g_obj->lua_game_object();
}

// demonized: get scope UI
luabind::object CScriptGameObject::get_scope_ui() {
	CWeapon* weapon = smart_cast<CWeapon*>(&object());
	luabind::object table = luabind::newtable(ai().script_engine().lua());
	if (!weapon)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
										"CScriptGameObject : get_scope_ui works only with CWeapon object!");
		return table;
	}
	
	auto& zoomTextureWndList = weapon->ZoomTexture()->GetChildWndList();
	if (zoomTextureWndList.empty()) {
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
										"CScriptGameObject : get_scope_ui, no scope texture found for %s!", weapon->cNameSect().c_str());
		return table;
	}

	luabind::object staticChildren = luabind::newtable(ai().script_engine().lua());

	for (int i = 0; i < zoomTextureWndList.size(); i++) {
		CUIStatic* staticWnd = smart_cast<CUIStatic*>(zoomTextureWndList[i]);
		if (!staticWnd) {
			ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
											"CScriptGameObject : get_scope_ui, can't cast scope texture %d to CUIStatic for %s!", i, weapon->cNameSect().c_str());
		} else {
			staticChildren[i + 1] = staticWnd;
		}
	}

	table["name"] = weapon->m_scope_tex_name.c_str();
	table["uiWindow"] = weapon->ZoomTexture();
	table["statics"] = staticChildren;
	
	return table;
}

void CScriptGameObject::set_scope_ui(LPCSTR scope_texture) {
	CWeapon* weapon = smart_cast<CWeapon*>(&object());
	if (!weapon)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
										"CScriptGameObject : set_scope_ui works only with CWeapon object!");
		return;
	}

	auto wnd = weapon->ZoomTexture();
	if (!wnd) {
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
										"CScriptGameObject : set_scope_ui no scope found for %s!", weapon->cNameSect().c_str());
		return;
	}

	weapon->SetUIScope(scope_texture);
}
