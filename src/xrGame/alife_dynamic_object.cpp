////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_dynamic_object.cpp
//	Created 	: 27.10.2005
//  Modified 	: 27.10.2005
//	Author		: Dmitriy Iassenev
//	Description : ALife dynamic object class
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "xrServer_Objects_ALife.h"
#include "alife_simulator.h"
#include "alife_schedule_registry.h"
#include "alife_graph_registry.h"
#include "alife_object_registry.h"
#include "level_graph.h"
#include "game_level_cross_table.h"
#include "game_graph.h"
#include "xrServer.h"
#include "level.h"
#include "map_manager.h"
#include "ai_space.h"
#include "script_engine.h"

// Scripts shipped with Anomaly and GAMMA treat every id >= 65535 as "no object".
// 65535 stays the empty id. Ids above it are real objects and must still resolve.
// The wrapper has to be installed again after a script reload replaces alife_object.
static void allow_expanded_object_ids()
{
	lua_State* L = ai().script_engine().lua();
	if (!L)
		return;

	lua_getglobal(L, "__expanded_alife_object");
	lua_getglobal(L, "alife_object");
	const bool already = lua_isfunction(L, -1) && lua_isfunction(L, -2) && lua_rawequal(L, -1, -2);
	if (!lua_isfunction(L, -1))
	{
		lua_pop(L, 2);
		return;
	}
	lua_getfenv(L, -1);
	lua_remove(L, -2);
	lua_remove(L, -2);
	if (already)
	{
		lua_pop(L, 1);
		return;
	}

	string2048 chunk;
	xr_sprintf(chunk,
		"local max_id = %u\n"
		"local invalid_id = %u\n"
		"local previous = alife_object\n"
		"local function expanded(id)\n"
		"  if type(id) == 'number' and id > invalid_id and id <= max_id then\n"
		"    local sim = alife()\n"
		"    if not sim then return nil end\n"
		"    return sim:object(id)\n"
		"  end\n"
		"  if previous then return previous(id) end\n"
		"end\n"
		"alife_object = expanded\n"
		"rawset(_G, 'alife_object', expanded)\n"
		"rawset(_G, '__expanded_alife_object', expanded)\n"
		"if get_object_by_id and rawget(_G, '__expanded_get_object_by_id') ~= get_object_by_id then\n"
		"  local previous_get = get_object_by_id\n"
		"  local function expanded_get(id)\n"
		"    if type(id) == 'number' and id > invalid_id and id <= max_id then\n"
		"      if AC_ID and id == AC_ID and db and db.actor then return db.actor end\n"
		"      if gameobjects_registry and gameobjects_registry[id] then return gameobjects_registry[id] end\n"
		"      if level and level.object_by_id then return level.object_by_id(id) end\n"
		"      return nil\n"
		"    end\n"
		"    return previous_get(id)\n"
		"  end\n"
		"  get_object_by_id = expanded_get\n"
		"  rawset(_G, 'get_object_by_id', expanded_get)\n"
		"  rawset(_G, '__expanded_get_object_by_id', expanded_get)\n"
		"end\n",
		(u32)ALife::max_object_id,
		(u32)ALife::invalid_object_id);

	if (luaL_loadbuffer(L, chunk, xr_strlen(chunk), "@object_id_compat"))
	{
		Msg("! object id compatibility patch failed: %s", lua_tostring(L, -1));
		lua_pop(L, 2);
		return;
	}
	lua_pushvalue(L, -2);
	lua_setfenv(L, -2);
	if (lua_pcall(L, 0, 0, 0))
	{
		Msg("! object id compatibility patch failed: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
}

void CSE_ALifeDynamicObject::on_spawn()
{
#ifdef DEBUG
	//	Msg			("[LSS] spawning object [%d][%d][%s][%s]",ID,ID_Parent,name(),name_replace());
#endif
}

void CSE_ALifeDynamicObject::on_register()
{
	CSE_ALifeObject* object = this;
	while (object->ID_Parent != ALife::invalid_object_id)
	{
		object = ai().alife().objects().object(object->ID_Parent);
		VERIFY(object);
	}

	if (!alife().graph().level().object(object->ID, true))
		clear_client_data();

	allow_expanded_object_ids();

    ::luabind::functor<void> funct;
    if (ai().script_engine().functor("_G.CSE_ALifeDynamicObject_on_register", funct))
        funct(ID);
}

void CSE_ALifeDynamicObject::on_before_register()
{
}

void CSE_ALifeDynamicObject::on_unregister()
{
	allow_expanded_object_ids();

	::luabind::functor<void> funct;
	if (ai().script_engine().functor("_G.CSE_ALifeDynamicObject_on_unregister", funct))
		funct(ID);
	Level().MapManager().OnObjectDestroyNotify(ID);
}

void CSE_ALifeDynamicObject::switch_online()
{
	R_ASSERT(!m_bOnline);
	m_bOnline = true;
	alife().add_online(this);
}

void CSE_ALifeDynamicObject::switch_offline()
{
	R_ASSERT(m_bOnline);
	m_bOnline = false;
	alife().remove_online(this);

	clear_client_data();
}

void CSE_ALifeDynamicObject::add_online(const bool& update_registries)
{
	if (!update_registries)
		return;

	alife().scheduled().remove(this);
	alife().graph().remove(this, m_tGraphID, false);
}

void CSE_ALifeDynamicObject::add_offline(const xr_vector<ALife::_OBJECT_ID>& saved_children,
                                         const bool& update_registries)
{
	if (!update_registries)
		return;

	alife().scheduled().add(this);
	alife().graph().add(this, m_tGraphID, false);
}

bool CSE_ALifeDynamicObject::synchronize_location()
{
	if (!ai().level_graph().valid_vertex_id(m_tNodeID)) return false;

	if (!ai().level_graph().valid_vertex_position(o_Position) || ai().level_graph().inside(
		ai().level_graph().vertex(m_tNodeID),
		o_Position))
		return (true);

	u32 const new_vertex_id = ai().level_graph().vertex(m_tNodeID, o_Position);
	if (!m_bOnline && !ai().level_graph().inside(new_vertex_id, o_Position))
		return (true);

	m_tNodeID = new_vertex_id;
	GameGraph::_GRAPH_ID tGraphID = ai().cross_table().vertex(m_tNodeID).game_vertex_id();
	if (tGraphID != m_tGraphID)
	{
		if (!m_bOnline)
		{
			Fvector position = o_Position;
			u32 level_vertex_id = m_tNodeID;
			alife().graph().change(this, m_tGraphID, tGraphID);
			if (ai().level_graph().inside(ai().level_graph().vertex(level_vertex_id), position))
			{
				level_vertex_id = m_tNodeID;
				o_Position = position;
			}
		}
		else
		{
			VERIFY(ai().game_graph().vertex(tGraphID)->level_id() == alife().graph().level().level_id());
			m_tGraphID = tGraphID;
		}
	}

	m_fDistance = ai().cross_table().vertex(m_tNodeID).distance();

	return (true);
}

void CSE_ALifeDynamicObject::try_switch_online()
{
	CSE_ALifeSchedulable* schedulable = smart_cast<CSE_ALifeSchedulable*>(this);
	// checking if the abstract monster has just died
	if (schedulable)
	{
		if (!schedulable->need_update(this))
		{
			if (alife().scheduled().object(ID, true))
				alife().scheduled().remove(this);
		}
		else if (!alife().scheduled().object(ID, true))
			alife().scheduled().add(this);
	}

	if (!can_switch_online())
	{
		on_failed_switch_online();
		return;
	}

	if (!can_switch_offline())
	{
		alife().switch_online(this);
		return;
	}

	if (alife().graph().actor()->o_Position.distance_to(o_Position) > alife().online_distance())
	{
		on_failed_switch_online();
		return;
	}

	alife().switch_online(this);
}

void CSE_ALifeDynamicObject::try_switch_offline()
{
	if (!can_switch_offline())
		return;

	if (!can_switch_online())
	{
		alife().switch_offline(this);
		return;
	}

	if (alife().graph().actor()->o_Position.distance_to(o_Position) <= alife().offline_distance())
		return;

	alife().switch_offline(this);
}

bool CSE_ALifeDynamicObject::redundant() const
{
	return (false);
}

/// ---------------------------- CSE_ALifeInventoryBox ---------------------------------------------

void CSE_ALifeInventoryBox::add_online(const bool& update_registries)
{
	CSE_ALifeDynamicObjectVisual* object = (this);

	NET_Packet tNetPacket;
	ClientID clientID;
	clientID.set(
		object->alife().server().GetServerClient() ? object->alife().server().GetServerClient()->ID.value() : 0);

	ALife::OBJECT_IT I = object->children.begin();
	ALife::OBJECT_IT E = object->children.end();
	for (; I != E; ++I)
	{
		CSE_ALifeDynamicObject* l_tpALifeDynamicObject = ai().alife().objects().object(*I);
		CSE_ALifeInventoryItem* l_tpALifeInventoryItem = smart_cast<CSE_ALifeInventoryItem*>(l_tpALifeDynamicObject);
		R_ASSERT2(l_tpALifeInventoryItem, "Non inventory item object has parent?!");
		l_tpALifeInventoryItem->base()->s_flags.or(M_SPAWN_UPDATE);
		CSE_Abstract* l_tpAbstract = smart_cast<CSE_Abstract*>(l_tpALifeInventoryItem);
		object->alife().server().entity_Destroy(l_tpAbstract);

#ifdef DEBUG
		//		if (psAI_Flags.test(aiALife))
//			Msg					("[LSS] Spawning item [%s][%s][%d]",l_tpALifeInventoryItem->base()->name_replace(),*l_tpALifeInventoryItem->base()->s_name,l_tpALifeDynamicObject->ID);
		Msg						(
			"[LSS][%d] Going online [%d][%s][%d] with parent [%d][%s] on '%s'",
			Device.dwFrame,
			Device.dwTimeGlobal,
			l_tpALifeInventoryItem->base()->name_replace(),
			l_tpALifeInventoryItem->base()->ID,
			ID,
			name_replace(),
			"*SERVER*"
		);
#endif

		l_tpALifeDynamicObject->o_Position = object->o_Position;
		l_tpALifeDynamicObject->m_tNodeID = object->m_tNodeID;
		object->alife().server().Process_spawn(tNetPacket, clientID,FALSE, l_tpALifeInventoryItem->base());
		l_tpALifeDynamicObject->s_flags.and(u16(-1) ^ M_SPAWN_UPDATE);
		l_tpALifeDynamicObject->m_bOnline = true;
	}

	CSE_ALifeDynamicObjectVisual::add_online(update_registries);
}

void CSE_ALifeInventoryBox::add_offline(const xr_vector<ALife::_OBJECT_ID>& saved_children,
                                        const bool& update_registries)
{
	CSE_ALifeDynamicObjectVisual* object = (this);

	for (u32 i = 0, n = saved_children.size(); i < n; ++i)
	{
		CSE_ALifeDynamicObject* child = smart_cast<CSE_ALifeDynamicObject*>(
			ai().alife().objects().object(saved_children[i], true));
		// R_ASSERT(child);
		if (!child)
		{
			Msg("[DO] can't switch child [%d] offline, it's null", saved_children[i]);
			continue;
		}
		child->m_bOnline = false;

		CSE_ALifeInventoryItem* inventory_item = smart_cast<CSE_ALifeInventoryItem*>(child);
		VERIFY2(inventory_item, "Non inventory item object has parent?!");
#ifdef DEBUG
		//		if (psAI_Flags.test(aiALife))
//			Msg					("[LSS] Destroying item [%s][%s][%d]",inventory_item->base()->name_replace(),*inventory_item->base()->s_name,inventory_item->base()->ID);
		Msg						(
			"[LSS][%d] Going offline [%d][%s][%d] with parent [%d][%s] on '%s'",
			Device.dwFrame,
			Device.dwTimeGlobal,
			inventory_item->base()->name_replace(),
			inventory_item->base()->ID,
			ID,
			name_replace(),
			"*SERVER*"
		);
#endif

		ALife::_OBJECT_ID item_id = inventory_item->base()->ID;
		inventory_item->base()->ID = object->alife().server().PerformIDgen(item_id);

		if (!child->can_save())
		{
			object->alife().release(child);
			--i;
			--n;
			continue;
		}
		child->clear_client_data();
		object->alife().graph().add(child, child->m_tGraphID, false);
		//		object->alife().graph().attach	(*object,inventory_item,child->m_tGraphID,true);
		alife().graph().remove(child, child->m_tGraphID);
		children.push_back(child->ID);
		child->ID_Parent = ID;
	}


	CSE_ALifeDynamicObjectVisual::add_offline(saved_children, update_registries);
}

void CSE_ALifeDynamicObject::clear_client_data()
{
#ifdef DEBUG
	if (!client_data.empty())
		Msg						("CSE_ALifeDynamicObject::switch_offline: client_data is cleared for [%d][%s]",ID,name_replace());
#endif // DEBUG
	if (!keep_saved_data_anyway())
		client_data.clear();
}

void CSE_ALifeDynamicObject::on_failed_switch_online()
{
	clear_client_data();
}
