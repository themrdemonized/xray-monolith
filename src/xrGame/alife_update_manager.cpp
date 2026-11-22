////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_update_manager.h
//	Created 	: 25.12.2002
//  Modified 	: 12.05.2004
//	Author		: Dmitriy Iassenev
//	Description : ALife Simulator update manager
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "alife_update_manager.h"
#include "alife_simulator_header.h"
#include "alife_time_manager.h"
#include "alife_graph_registry.h"
#include "alife_schedule_registry.h"
#include "alife_spawn_registry.h"
#include "alife_object_registry.h"
#include "ef_storage.h"
#include "xrserver.h"
#include "level.h"
#include "graph_engine.h"
#include "../xrEngine/x_ray.h"
#include "restriction_space.h"
#include "profiler.h"
#include "mt_config.h"

using namespace ALife;
#ifdef	ENGINE_LUA_ALIFE_UPDAGE_MANAGER_CALLBACKS
 //Alundaio
#endif

extern string_path g_last_saved_game;

class CSwitchPredicate
{
private:
	CALifeSwitchManager* m_switch_manager;

public:
	IC CSwitchPredicate(CALifeSwitchManager* switch_manager)
	{
		m_switch_manager = switch_manager;
	}

	IC bool operator()(CALifeLevelRegistry::_iterator& i, u64 cycle_count, bool) const
	{
		if ((*i).second->m_switch_counter == cycle_count)
			return (false);

		(*i).second->m_switch_counter = cycle_count;
		return (true);
	}

	IC void operator()(CALifeLevelRegistry::_iterator& i, u64 cycle_count) const
	{
		m_switch_manager->switch_object((*i).second);
	}
};

CALifeUpdateManager::CALifeUpdateManager(xrServer* server, LPCSTR section) :
	CALifeSwitchManager(server, section),
	CALifeSurgeManager(server, section),
	CALifeStorageManager(server, section),
	CALifeSimulatorBase(server, section)
{
	shedule.t_min = pSettings->r_s32(section, "schedule_min");
	shedule.t_max = pSettings->r_s32(section, "schedule_max");
	shedule_register();

	m_max_process_time = pSettings->r_s32(section, "process_time");
	m_update_monster_factor = pSettings->r_float(section, "update_monster_factor");
	m_objects_per_update = pSettings->r_u32(section, "objects_per_update");
	m_changing_level = false;
	m_first_time = true;
}

CALifeUpdateManager::~CALifeUpdateManager()
{
	shedule_unregister();
	Device.remove_from_seq_parallel(
		xr_make_delegate(
			this,
			&CALifeUpdateManager::update
		)
	);
}

float CALifeUpdateManager::shedule_Scale()
{
	return (.5f); // (schedule_min + schedule_max)*0.5f
}

u32 g_full_switch_passes = 0;

void CALifeUpdateManager::update_switch()
{
	init_ef_storage();

	START_PROFILE("ALife/switch")
		;
		// after a normal load every update of the warm-up frames checks all objects of the
		// map, and scripts rely on it (a wide online distance for the first seconds). The same
		// number of full checks follows a swap.
		bool full_pass = g_full_switch_passes > 0;
		if (full_pass) --g_full_switch_passes;
		graph().level().update(CSwitchPredicate(this), Device.dwPrecacheFrame > 0 || full_pass);
	STOP_PROFILE
}

void CALifeUpdateManager::update_scheduled(bool init_ef)
{
	if (init_ef)
		init_ef_storage();

	START_PROFILE("ALife/scheduled")
		;
		scheduled().update();
	STOP_PROFILE
}

void CALifeUpdateManager::update()
{
	PROF_EVENT("AI: A-Life Update");
	update_switch();
	update_scheduled(false);
}

void CALifeUpdateManager::shedule_Update(u32 dt)
{
	ISheduled::shedule_Update(dt);

	if (!initialized())
		return;

	if (!m_first_time && g_mt_config.test(mtALife))
	{
		Device.seqParallel.push_back(
			xr_make_delegate(
				this,
				&CALifeUpdateManager::update
			)
		);
		return;
	}

	m_first_time = false;

	START_PROFILE("ALife/update")
		update();
	STOP_PROFILE
}

void CALifeUpdateManager::set_process_time(int microseconds)
{
	graph().set_process_time(float(microseconds) - float(microseconds) * update_monster_factor() / 1000000.f);
}

void CALifeUpdateManager::objects_per_update(const u32& objects_per_update)
{
	scheduled().objects_per_update(objects_per_update);
}

void CALifeUpdateManager::init_ef_storage() const
{
	ai().ef_storage().alife_evaluation(true);
}

extern int g_seamless_level_change;

// A seamless level change does not restart the scripts. What leaves with a map is released, so their memory should
// stay level; this is the net under that: when the scripts have grown by more than the limit since they were last
// started, one level change takes the loading screen, which restarts them. Counted from the lowest use seen since
// the start, so a large mod list that needs more from the beginning is not cut short.
int g_seamless_script_memory_limit = 128; // MB of growth, 0 = no limit (console: seamless_script_memory_limit)
static bool seamless_script_memory_ok()
{
	static lua_State* counted_for = nullptr;
	static int lowest = 0;
	lua_State* L = ai().script_engine().lua();
	int used = lua_gc(L, LUA_GCCOUNT, 0) >> 10;
	if (counted_for == L && g_seamless_script_memory_limit > 0 && used - lowest >= g_seamless_script_memory_limit)
	{
		// most of it may be garbage the collector has not come to yet: count what is really kept
		lua_gc(L, LUA_GCCOLLECT, 0);
		used = lua_gc(L, LUA_GCCOUNT, 0) >> 10;
	}
	if (counted_for != L || used < lowest)
	{
		counted_for = L;
		lowest = used;
	}
	if (g_seamless_script_memory_limit <= 0 || used - lowest < g_seamless_script_memory_limit)
		return true;
	Msg("* seamless: scripts use %d MB, %d MB more than after their start (limit %d), this level change takes the loading screen to restart them",
		used, used - lowest, g_seamless_script_memory_limit);
	return false;
}

bool CALifeUpdateManager::change_level(NET_Packet& net_packet)
{
	if (m_changing_level)
		return (false);

	// with seamless_level_change on, every level change (level changers, travel, jump_to_level)
	// becomes an in-place swap behind a short fade.
	if (g_seamless_level_change && CLevel::seamless_scripts_installed() && seamless_script_memory_ok())
	{
		GameGraph::_GRAPH_ID game_vertex_id;
		u32 level_vertex_id;
		Fvector position, angles;
		net_packet.r(&game_vertex_id, sizeof(game_vertex_id));
		net_packet.r(&level_vertex_id, sizeof(level_vertex_id));
		net_packet.r_vec3(position);
		net_packet.r_vec3(angles);
		Level().seamless_request(game_vertex_id, level_vertex_id, position, angles);
		return (false);
	}

#ifdef	ENGINE_LUA_ALIFE_UPDAGE_MANAGER_CALLBACKS
	::luabind::functor<void> funct;
	if (ai().script_engine().functor("_G.CALifeUpdateManager__on_before_change_level", funct))
		funct(&net_packet);
#endif

	//	prepare_objects_for_save		();
	// we couldn't use prepare_objects_for_save since we need 
	// get updates from client 
	// then change actor server entity 
	// then call client net_Save 
	// then restore actor server entity 
	Level().ClientSend();

	m_changing_level = true;

	GameGraph::_GRAPH_ID safe_graph_vertex_id = graph().actor()->m_tGraphID;
	u32 safe_level_vertex_id = graph().actor()->m_tNodeID;
	Fvector safe_position = graph().actor()->o_Position;
	Fvector safe_angles = graph().actor()->o_Angle;
	SRotation safe_torso = graph().actor()->o_torso;

	GameGraph::_GRAPH_ID holder_safe_graph_vertex_id = GameGraph::_GRAPH_ID(-1);
	u32 holder_safe_level_vertex_id = u32(-1);
	Fvector holder_safe_position = Fvector().set(flt_max,flt_max,flt_max);
	Fvector holder_safe_angles = Fvector().set(flt_max,flt_max,flt_max);
	CSE_ALifeObject* holder = 0;

	net_packet.r(&graph().actor()->m_tGraphID, sizeof(graph().actor()->m_tGraphID));
	net_packet.r(&graph().actor()->m_tNodeID, sizeof(graph().actor()->m_tNodeID));
	net_packet.r_vec3(graph().actor()->o_Position);
	net_packet.r_vec3(graph().actor()->o_Angle);
	if (ai().game_graph().valid_vertex_id(graph().actor()->m_tGraphID))
	{
		const GameGraph::_LEVEL_ID destination_level =
			ai().game_graph().vertex(graph().actor()->m_tGraphID)->level_id();
		pApp->LoadScreenSetLevelLogo(*ai().game_graph().header().level(destination_level).name());
	}

	Level().ClientSave();

	graph().actor()->o_torso.yaw = graph().actor()->o_Angle.y;
	graph().actor()->o_torso.pitch = graph().actor()->o_Angle.x;
	graph().actor()->o_torso.roll = 0.f;

	if (graph().actor()->m_holderID != 0xffff)
	{
		holder = objects().object(graph().actor()->m_holderID);

		holder_safe_graph_vertex_id = holder->m_tGraphID;
		holder_safe_level_vertex_id = holder->m_tNodeID;
		holder_safe_position = holder->o_Position;
		holder_safe_angles = holder->o_Angle;

		holder->m_tGraphID = graph().actor()->m_tGraphID;
		holder->m_tNodeID = graph().actor()->m_tNodeID;
		holder->o_Position = graph().actor()->o_Position;
		holder->o_Angle = graph().actor()->o_Angle;
	}

	string256 autoave_name;
	strconcat(sizeof(autoave_name), autoave_name, Core.UserName, " - ", "autosave");
	LPCSTR temp0 = strstr(**m_server_command_line, "/");
	VERIFY(temp0);
	string256 temp;
	*m_server_command_line = strconcat(sizeof(temp), temp, autoave_name, temp0);

	save(autoave_name);

	graph().actor()->m_tGraphID = safe_graph_vertex_id;
	graph().actor()->m_tNodeID = safe_level_vertex_id;
	graph().actor()->o_Position = safe_position;
	graph().actor()->o_Angle = safe_angles;
	graph().actor()->o_torso = safe_torso;

	if (graph().actor()->m_holderID != 0xffff)
	{
		VERIFY(holder);
		holder->m_tGraphID = holder_safe_graph_vertex_id;
		holder->m_tNodeID = holder_safe_level_vertex_id;
		holder->o_Position = holder_safe_position;
		holder->o_Angle = holder_safe_angles;
	}

	return (true);
}

#include "../xrEngine/igame_persistent.h"

void CALifeUpdateManager::new_game(LPCSTR save_name)
{
	//	g_pGamePersistent->LoadTitle		("st_creating_new_game");
	g_pGamePersistent->LoadTitle();
	Msg("* Creating new game...");

	unload();
	reload(m_section);
	spawns().load(save_name);
	graph().on_load();
	server().PerformIDgen(0x0000);
	time_manager().init(m_section);
	VERIFY(can_register_objects());

	can_register_objects(false);
	spawn_new_objects();
	can_register_objects(true);

	CALifeObjectRegistry::OBJECT_REGISTRY::iterator I = objects().objects().begin();
	CALifeObjectRegistry::OBJECT_REGISTRY::iterator E = objects().objects().end();
	for (; I != E; ++I)
		(*I).second->on_register();

#ifdef DEBUG
	save								(save_name);
#endif // #ifdef DEBUG

	Msg("* New game is successfully created!");
}

extern xr_task_group level_load;

void CALifeUpdateManager::load(LPCSTR game_name, bool no_assert, bool new_only)
{
	PROF_EVENT("Load Alife Simulator");
	//	g_pGamePersistent->LoadTitle		("st_loading_alife_simulator");
	g_pGamePersistent->LoadTitle();

#ifdef DEBUG
	Memory.mem_compact					();
	size_t									memory_usage = Memory.mem_usage();
#endif

	xr_strcpy(g_last_saved_game, game_name);

	if (new_only || !CALifeStorageManager::load(game_name))
	{
		R_ASSERT3(new_only || no_assert && xr_strlen(game_name), "Cannot find the specified saved game ", game_name);
		new_game(game_name);
	}

	if (g_pGameLevel)
		Level().OnAlifeSimulatorLoaded();

#ifdef DEBUG
	Msg									("* Loading alife simulator is successfully completed (%7.3f Mb)",float(Memory.mem_usage() - memory_usage)/1048576.0);
#endif
	//	g_pGamePersistent->LoadTitle		("st_server_connecting");
	g_pGamePersistent->LoadTitle(true, g_pGameLevel->name());
	level_load.wait();
}

void CALifeUpdateManager::reload(LPCSTR section)
{
	CALifeSimulatorBase::reload(section);
	set_process_time((int)m_max_process_time);
	objects_per_update(m_objects_per_update);
}

bool CALifeUpdateManager::load_game(LPCSTR game_name, bool no_assert)
{
	{
		string_path temp, file_name;
		strconcat(sizeof(temp), temp, game_name,SAVE_EXTENSION);
		FS.update_path(file_name, "$game_saves$", temp);
		if (!FS.exist(file_name))
		{
			R_ASSERT3(no_assert, "There is no saved game ", file_name);
			return (false);
		}
	}
	string512 S, S1;
	xr_strcpy(S, **m_server_command_line);
	LPSTR temp = strchr(S, '/');
	R_ASSERT2(temp, "Invalid server options!");
	strconcat(sizeof(S1), S1, game_name, temp);
	*m_server_command_line = S1;
	return (true);
}

void CALifeUpdateManager::set_switch_online(ALife::_OBJECT_ID id, bool value)
{
	CSE_ALifeDynamicObject* object = objects().object(id);
	VERIFY(object);
	object->can_switch_online(value);
}

void CALifeUpdateManager::set_switch_offline(ALife::_OBJECT_ID id, bool value)
{
	CSE_ALifeDynamicObject* object = objects().object(id);
	VERIFY(object);
	object->can_switch_offline(value);
}

void CALifeUpdateManager::set_interactive(ALife::_OBJECT_ID id, bool value)
{
	CSE_ALifeDynamicObject* object = objects().object(id);
	VERIFY(object);
	object->interactive(value);
}

void CALifeUpdateManager::jump_to_level(LPCSTR level_name) const
{
	const CGameGraph::SLevel& level = ai().game_graph().header().level(level_name);
	GameGraph::_GRAPH_ID dest = GameGraph::_GRAPH_ID(-1);
	GraphEngineSpace::CGameLevelParams evaluator(level.id());
	bool failed = !ai().graph_engine().search(ai().game_graph(), graph().actor()->m_tGraphID, GameGraph::_GRAPH_ID(-1),
	                                          0, evaluator);
	if (failed)
	{
#ifndef MASTER_GOLD
		Msg								("! Cannot build path via game graph from the current level to the level %s!",level_name);
#endif // #ifndef MASTER_GOLD
		float min_dist = flt_max;
		Fvector current = ai().game_graph().vertex(graph().actor()->m_tGraphID)->game_point();
		GameGraph::_GRAPH_ID n = ai().game_graph().header().vertex_count();
		for (GameGraph::_GRAPH_ID i = 0; i < n; ++i)
			if (ai().game_graph().vertex(i)->level_id() == level.id())
			{
				float distance = ai().game_graph().vertex(i)->game_point().distance_to_sqr(current);
				if (distance < min_dist)
				{
					min_dist = distance;
					dest = i;
				}
			}
		if (!ai().game_graph().vertex(dest))
		{
			Msg("! There is no game vertices on the level %s, cannot jump to the specified level", level_name);
			return;
		}
	}
	else
		dest = (GameGraph::_GRAPH_ID)evaluator.selected_vertex_id();
	NET_Packet net_packet;
	net_packet.w_begin(M_CHANGE_LEVEL);
	net_packet.w(&dest, sizeof(dest));

	u32 vertex_id = ai().game_graph().vertex(dest)->level_vertex_id();
	net_packet.w(&vertex_id, sizeof(vertex_id));

	Fvector level_point = ai().game_graph().vertex(dest)->level_point();
	net_packet.w(&level_point, sizeof(level_point));
	net_packet.w_vec3(Fvector().set(0.f, 0.f, 0.f));
	Level().Send(net_packet, net_flags(TRUE));
}

bool CALifeUpdateManager::find_level_entry(LPCSTR level_name, GameGraph::_GRAPH_ID& game_vertex_id) const
{
	const CGameGraph::SLevel& level = ai().game_graph().header().level(level_name);
	GraphEngineSpace::CGameLevelParams evaluator(level.id());
	if (ai().graph_engine().search(ai().game_graph(), graph().actor()->m_tGraphID, GameGraph::_GRAPH_ID(-1), 0,
	                               evaluator))
	{
		game_vertex_id = (GameGraph::_GRAPH_ID)evaluator.selected_vertex_id();
		return true;
	}
	float min_dist = flt_max;
	bool found = false;
	Fvector current = ai().game_graph().vertex(graph().actor()->m_tGraphID)->game_point();
	for (GameGraph::_GRAPH_ID i = 0, n = ai().game_graph().header().vertex_count(); i < n; ++i)
		if (ai().game_graph().vertex(i)->level_id() == level.id())
		{
			float distance = ai().game_graph().vertex(i)->game_point().distance_to_sqr(current);
			if (distance < min_dist)
			{
				min_dist = distance;
				game_vertex_id = i;
				found = true;
			}
		}
	return found;
}

// Sends every online object of the current level offline, the actor last. The server
// entities, the simulation and the script engine all stay alive.
void CALifeUpdateManager::switch_offline_all()
{
	xr_vector<ALife::_OBJECT_ID> online;
	for (const auto& it : graph().level().objects())
		if (it.second->m_bOnline && it.second != graph().actor())
			online.push_back(it.second->ID);
	for (ALife::_OBJECT_ID id : online)
	{
		CSE_ALifeDynamicObject* object = objects().object(id, true);
		if (object && object->m_bOnline)
		{
			// What the object saved for its next spawn (CLevel::ClientSave) is kept, as a level change
			// with a loading screen keeps it; going offline by distance drops it.
			xr_vector<u8> saved;
			saved.swap(object->client_data);
			object->switch_offline();
			object->client_data.swap(saved);
		}
	}
	remove_online(graph().actor(), false);
	// the actor's scripts keep running: nothing to load back when it appears on the next level
	graph().actor()->client_data.clear();
}

void CALifeUpdateManager::relocate_actor(GameGraph::_GRAPH_ID game_vertex_id, u32 level_vertex_id,
                                             const Fvector& position, const Fvector& angles)
{
	CSE_ALifeCreatureActor* actor = graph().actor();
	graph().change(actor, actor->m_tGraphID, game_vertex_id);
	actor->m_tNodeID = level_vertex_id;
	actor->o_Position = position;
	actor->o_Angle = angles;
	actor->o_torso.yaw = angles.y;
	actor->o_torso.pitch = angles.x;
	graph().change_level_in_place(objects());
	// Objects without a place in the navigation graph (anomalies, trigger zones, physics
	// objects, stashes) are in no per-vertex list: a load registers them with the current map
	// one by one, and so does the swap. The registry keeps only those of its own map.
	for (const auto& it : objects().objects())
	{
		CSE_ALifeDynamicObject* object = it.second;
		if (!object->m_bDirectControl || !ai().game_graph().valid_vertex_id(object->m_tGraphID))
			continue;
		CSE_ALifeInventoryItem* item = smart_cast<CSE_ALifeInventoryItem*>(object);
		if (item && item->attached())
			continue;
		graph().level().add(object);
	}
}

void CALifeUpdateManager::switch_online_actor()
{
	add_online(graph().actor(), false);
}

void CALifeUpdateManager::teleport_object(ALife::_OBJECT_ID id, GameGraph::_GRAPH_ID game_vertex_id,
                                          u32 level_vertex_id, const Fvector& position)
{
	CSE_ALifeDynamicObject* object = objects().object(id, true);
	if (!object)
	{
		Msg("! cannot teleport entity with id %d", id);
		return;
	}

#ifdef DEBUG
	if (psAI_Flags.test(aiALife)) {
		Msg									("[LSS] teleporting object [%s][%s][%d] from level [%s], position [%f][%f][%f] to level [%s], position [%f][%f][%f]",
			object->name_replace(),
			*object->s_name,
			object->ID,
			*(ai().game_graph().header().level(ai().game_graph().vertex(object->m_tGraphID)->level_id()).name()),
			VPUSH(ai().game_graph().vertex(object->m_tGraphID)->level_point()),
			*(ai().game_graph().header().level(ai().game_graph().vertex(game_vertex_id)->level_id()).name()),
			VPUSH(ai().game_graph().vertex(game_vertex_id)->level_point())
		);
	}
#endif

	CSE_ALifeMonsterAbstract* monster_abstract = smart_cast<CSE_ALifeMonsterAbstract*>(object);
	// teleport the whole online squad along with its member
	if (object->m_bOnline && monster_abstract && monster_abstract->m_group_id != 0xffff)
	{
		CSE_ALifeOnlineOfflineGroup* group = smart_cast<CSE_ALifeOnlineOfflineGroup*>(objects().object(monster_abstract->m_group_id, true));
		if (group && group->m_bOnline)
		{
			xr_vector<ALife::_OBJECT_ID> members;
			for (const auto& it : group->squad_members())
				members.push_back(it.first);

			teleport_object(group->ID, game_vertex_id, level_vertex_id, position);
			for (ALife::_OBJECT_ID member_id : members)
				teleport_object(member_id, game_vertex_id, level_vertex_id, position);
			return;
		}
	}

	if (object->m_bOnline)
		switch_offline(object);
	// a squad member is registered via its group's graph point, not individually
	if (monster_abstract && monster_abstract->m_group_id != 0xffff)
		object->m_tGraphID = game_vertex_id;
	else
		graph().change(object, object->m_tGraphID, game_vertex_id);
	object->m_tNodeID = level_vertex_id;
	object->o_Position = position;
	if (monster_abstract)
		monster_abstract->m_tNextGraphID = object->m_tGraphID;
}

void CALifeUpdateManager::add_restriction(ALife::_OBJECT_ID id, ALife::_OBJECT_ID restriction_id,
                                          const RestrictionSpace::ERestrictorTypes& restriction_type)
{
	CSE_ALifeDynamicObject* object = objects().object(id, true);
	if (!object)
	{
		Msg(
			"! cannot add restriction with id %d to the entity with id %d, because there is no creature with the specified id",
			restriction_id, id);
		return;
	}

	CSE_ALifeDynamicObject* object_restrictor = objects().object(restriction_id, true);
	if (!object_restrictor)
	{
		Msg(
			"! cannot add restriction with id %d to the entity with id %d, because there is no space restrictor with the specified id",
			restriction_id, id);
		return;
	}

	CSE_ALifeCreatureAbstract* creature = smart_cast<CSE_ALifeCreatureAbstract*>(object);
	if (!creature)
	{
		Msg(
			"! cannot add restriction with id %d to the entity with id %d, because there is an object with the specified id, but it is not a creature",
			restriction_id, id);
		return;
	}

	CSE_ALifeSpaceRestrictor* restrictor = smart_cast<CSE_ALifeSpaceRestrictor*>(object_restrictor);
	if (!restrictor)
	{
		Msg(
			"! cannot add restriction with id %d to the entity with id %d, because there is an object with the specified id, but it is not a space restrictor",
			restriction_id, id);
		return;
	}

	switch (restriction_type)
	{
	case RestrictionSpace::eRestrictorTypeOut:
		{
#ifdef DEBUG
			if (std::find(creature->m_dynamic_out_restrictions.begin(),creature->m_dynamic_out_restrictions.end(),restriction_id) != creature->m_dynamic_out_restrictions.end()) {
				Msg							("! cannot add out-restriction with id %d, name %s to the entity with id %d, name %s, because it is already added",restriction_id,restrictor->name_replace(),id,creature->name_replace());
				return;
			}
#endif

			creature->m_dynamic_out_restrictions.push_back(restriction_id);

			break;
		}
	case RestrictionSpace::eRestrictorTypeIn:
		{
#ifdef DEBUG
			if (std::find(creature->m_dynamic_in_restrictions.begin(),creature->m_dynamic_in_restrictions.end(),restriction_id) != creature->m_dynamic_in_restrictions.end()) {
				LogStackTrace				("cannot add in-restriction stack trace");
				Msg							("! cannot add in-restriction with id %d, name %s to the entity with id %d, name %s, because it is already added",restriction_id,restrictor->name_replace(),id,creature->name_replace());
				Msg							("! Please report this log file to Lain");
				return;
			}
#endif

			creature->m_dynamic_in_restrictions.push_back(restriction_id);

			break;
		}
	default:
		{
			Msg("! Invalid restriction type!");
			return;
		}
	}
}

void CALifeUpdateManager::remove_restriction(ALife::_OBJECT_ID id, ALife::_OBJECT_ID restriction_id,
                                             const RestrictionSpace::ERestrictorTypes& restriction_type)
{
	CSE_ALifeDynamicObject* object = objects().object(id, true);
	if (!object)
	{
		Msg(
			"! cannot remove restriction with id %d to the entity with id %d, because there is no creature with the specified id",
			restriction_id, id);
		return;
	}

	CSE_ALifeDynamicObject* object_restrictor = objects().object(restriction_id, true);
	if (!object_restrictor)
	{
		Msg(
			"! cannot remove restriction with id %d to the entity with id %d, because there is no space restrictor with the specified id",
			restriction_id, id);
		return;
	}

	CSE_ALifeCreatureAbstract* creature = smart_cast<CSE_ALifeCreatureAbstract*>(object);
	if (!creature)
	{
		Msg(
			"! cannot remove restriction with id %d to the entity with id %d, because there is an object with the specified id, but it is not a creature",
			restriction_id, id);
		return;
	}

	CSE_ALifeSpaceRestrictor* restrictor = smart_cast<CSE_ALifeSpaceRestrictor*>(object_restrictor);
	if (!restrictor)
	{
		Msg(
			"! cannot remove restriction with id %d to the entity with id %d, because there is an object with the specified id, but it is not a space restrictor",
			restriction_id, id);
		return;
	}

	switch (restriction_type)
	{
	case RestrictionSpace::eRestrictorTypeOut:
		{
			xr_vector<ALife::_OBJECT_ID>::iterator I = std::find(creature->m_dynamic_out_restrictions.begin(),
			                                                     creature->m_dynamic_out_restrictions.end(),
			                                                     restriction_id);
			if (I == creature->m_dynamic_out_restrictions.end())
			{
				Msg(
					"~ cannot remove restriction with id [%d][%s] to the entity with id [%d][%s], because it is not added",
					restriction_id, object_restrictor->name_replace(), id, object->name_replace());
				return;
			}

			creature->m_dynamic_out_restrictions.erase_fast(I);

			break;
		}
	case RestrictionSpace::eRestrictorTypeIn:
		{
			xr_vector<ALife::_OBJECT_ID>::iterator I = std::find(creature->m_dynamic_in_restrictions.begin(),
			                                                     creature->m_dynamic_in_restrictions.end(),
			                                                     restriction_id);
			if (I == creature->m_dynamic_in_restrictions.end())
			{
				Msg(
					"~ cannot remove restriction with id [%d][%s] to the entity with id [%d][%s], because it is not added",
					restriction_id, object_restrictor->name_replace(), id, object->name_replace());
				return;
			}

			creature->m_dynamic_in_restrictions.erase_fast(I);

			break;
		}
	default:
		{
			Msg("! Invalid restriction type!");
			return;
		}
	}
}

void CALifeUpdateManager::remove_all_restrictions(ALife::_OBJECT_ID id,
                                                  const RestrictionSpace::ERestrictorTypes& restriction_type)
{
	CSE_ALifeDynamicObject* object = objects().object(id, true);
	if (!object)
	{
		Msg("! cannot remove restrictions to the entity with id %d, because there is no creature with the specified id",
		    id);
		return;
	}

	CSE_ALifeCreatureAbstract* creature = smart_cast<CSE_ALifeCreatureAbstract*>(object);
	if (!creature)
	{
		Msg(
			"! cannot remove restriction to the entity with id %d, because there is an object with the specified id, but it is not a creature",
			id);
		return;
	}

	switch (restriction_type)
	{
	case RestrictionSpace::eRestrictorTypeOut:
		{
			creature->m_dynamic_out_restrictions.clear();
			break;
		}
	case RestrictionSpace::eRestrictorTypeIn:
		{
			creature->m_dynamic_in_restrictions.clear();
			break;
		}
	default: NODEFAULT;
	}
}

void CALifeUpdateManager::update_switch_all()
{
	init_ef_storage();
	graph().level().update(CSwitchPredicate(this), true);
}
