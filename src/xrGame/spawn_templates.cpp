////////////////////////////////////////////////////////////////////////////
//	Module 		: spawn_templates.cpp
//	Created 	: 16.09.2026
//	Description : Spawn template registry shared by ltx overlays, fragments and level packs
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "spawn_templates.h"
#include "xrmessages.h"

namespace
{
	using namespace spawn_overlays;

	const float MATCH_TOLERANCE = 0.5f;

	void index_names(const SPAWN_GRAPH& spawns, NAME_INDEX& index)
	{
		auto I = spawns.vertices().begin();
		const auto E = spawns.vertices().end();
		for (; I != E; ++I)
			index[shared_str(I->second->data()->object().name_replace())].push_back(I->first);
	}

	void collect_ids(const SPAWN_GRAPH& spawns, STemplateIds& ids)
	{
		ids.next_spawn_id = 0;
		auto I = spawns.vertices().begin();
		const auto E = spawns.vertices().end();
		for (; I != E; ++I)
		{
			const CSE_ALifeObject* object = smart_cast<CSE_ALifeObject*>(&I->second->data()->object());
			if (object && object->m_story_id != INVALID_STORY_ID)
				ids.story_ids.insert(object->m_story_id);
			if (object && object->m_spawn_story_id != INVALID_SPAWN_STORY_ID)
				ids.spawn_story_ids.insert(object->m_spawn_story_id);
			if (I->first >= ids.next_spawn_id)
				ids.next_spawn_id = ALife::_SPAWN_ID(I->first + 1);
		}
	}
}

spawn_overlays::STemplates::STemplates(SPAWN_GRAPH& spawns_) : spawns(spawns_)
{
	index_names(spawns, index);
	collect_ids(spawns, ids);
}

bool spawn_overlays::find_template(const SPAWN_GRAPH& spawns, const NAME_INDEX& index, const LPCSTR name, const LPCSTR match_position, ALife::_SPAWN_ID& spawn_id, string256& reason)
{
	const auto found = index.find(shared_str(name));
	if (found == index.end() || found->second.empty())
	{
		xr_strcpy(reason, "no spawn template with that name");
		return false;
	}

	const xr_vector<ALife::_SPAWN_ID>& candidates = found->second;
	if (!match_position)
	{
		if (candidates.size() > 1)
		{
			xr_sprintf(reason, "%d templates share this name, add match_position = x,y,z", candidates.size());
			return false;
		}
		spawn_id = candidates.front();
		return true;
	}

	Fvector position;
	if (3 != sscanf(match_position, "%f,%f,%f", &position.x, &position.y, &position.z))
	{
		xr_strcpy(reason, "match_position must be x,y,z");
		return false;
	}

	float best = flt_max;
	for (const auto candidate : candidates)
	{
		const float distance = spawns.vertex(candidate)->data()->object().o_Position.distance_to(position);
		if (distance >= best)
			continue;
		best = distance;
		spawn_id = candidate;
	}
	if (best > MATCH_TOLERANCE)
	{
		xr_sprintf(reason, "no template with that name within %.1f m of match_position (nearest %.1f m)", MATCH_TOLERANCE, best);
		return false;
	}
	return true;
}

bool spawn_overlays::same_object_present(const SPAWN_GRAPH& spawns, const NAME_INDEX& index, const CGameGraph& graph, const LPCSTR name, const GameGraph::_LEVEL_ID level_id, const Fvector& position, ALife::_SPAWN_ID* match)
{
	const auto found = index.find(shared_str(name));
	if (found == index.end())
		return false;
	const xr_vector<ALife::_SPAWN_ID>& candidates = found->second;
	for (const auto candidate : candidates)
	{
		const CSE_ALifeObject* object = smart_cast<CSE_ALifeObject*>(&spawns.vertex(candidate)->data()->object());
		if (!object || !graph.valid_vertex_id(object->m_tGraphID))
			continue;
		if (graph.vertex(object->m_tGraphID)->level_id() != level_id)
			continue;
		if (object->o_Position.distance_to(position) <= SAME_OBJECT_TOLERANCE)
		{
			if (match)
				*match = candidate;
			return true;
		}
	}
	return false;
}

void spawn_overlays::remove_template(STemplates& templates, const LPCSTR name, const ALife::_SPAWN_ID spawn_id)
{
	auto* alife_object = smart_cast<CSE_ALifeObject*>(&templates.spawns.vertex(spawn_id)->data()->object());
	if (alife_object)
	{
		release_id(templates.ids.story_ids, alife_object->m_story_id);
		release_id(templates.ids.spawn_story_ids, alife_object->m_spawn_story_id);
	}
	templates.spawns.remove_vertex(spawn_id);
	xr_vector<ALife::_SPAWN_ID>& ids = templates.index[shared_str(name)];
	ids.erase(std::remove(ids.begin(), ids.end(), spawn_id), ids.end());
}

bool spawn_overlays::claim_id(xr_set<u32>& taken, const u32 id)
{
	return id == u32(-1) || taken.insert(id).second;
}

void spawn_overlays::release_id(xr_set<u32>& taken, const u32 id)
{
	if (id != u32(-1))
		taken.erase(id);
}

bool spawn_overlays::reassign_id(xr_set<u32>& taken, u32& field, const u32 new_value)
{
	if (new_value == field)
		return true;
	if (!claim_id(taken, new_value))
		return false;
	release_id(taken, field);
	field = new_value;
	return true;
}

CSE_Abstract* spawn_overlays::create_from_packet(NET_Packet& packet, string256& reason)
{
	u16 id;
	packet.r_begin(id);
	if (id != M_SPAWN)
	{
		xr_strcpy(reason, "not an M_SPAWN packet");
		return nullptr;
	}
	string64 section;
	packet.r_stringZ_s(section);
	CSE_Abstract* object = F_entity_Create(section);
	if (!object)
	{
		xr_sprintf(reason, "section '%s' is not in system.ltx", section);
		return nullptr;
	}
	if (!object->Spawn_Read(packet))
	{
		F_entity_Destroy(object);
		xr_sprintf(reason, "'%s' has an unreadable spawn packet", section);
		return nullptr;
	}
	return object;
}

int spawn_overlays::prepare_template(const STemplateIds& ids, CSE_Abstract* object, const CGameGraph& graph, CSE_ALifeDynamicObject*& out, string256& reason)
{
	out = nullptr;
	if (smart_cast<CSE_ALifeCreatureAbstract*>(object))
	{
		xr_strcpy(reason, "creatures are spawned by scripts, not from spawn files");
		return 0;
	}
	auto* dynamic_object = smart_cast<CSE_ALifeDynamicObject*>(object);
	if (!dynamic_object)
	{
		xr_strcpy(reason, "not an alife object");
		return 0;
	}
	if (object->ID_Parent != u16(-1))
	{
		xr_strcpy(reason, "nested in another object (ID_Parent), not supported");
		return 0;
	}
	if (ids.next_spawn_id == ALife::_SPAWN_ID(-1))
	{
		xr_strcpy(reason, "no free spawn ids left");
		return 0;
	}

	if (auto* changer = smart_cast<CSE_ALifeLevelChanger*>(object))
	{
		if (const int result = resolve_destination(*changer, graph, reason); result != 1)
			return result;
	}

	out = dynamic_object;
	return 1;
}

void spawn_overlays::register_template(STemplates& templates, CSE_ALifeDynamicObject* dynamic_object, const LPCSTR source)
{
	const LPCSTR name = dynamic_object->name_replace();
	if (!claim_id(templates.ids.story_ids, dynamic_object->m_story_id))
	{
		Msg("! [spawn_overlays] %s: %s story_id %d is already taken, cleared", source, name, dynamic_object->m_story_id);
		dynamic_object->m_story_id = INVALID_STORY_ID;
	}
	if (!claim_id(templates.ids.spawn_story_ids, dynamic_object->m_spawn_story_id))
	{
		Msg("! [spawn_overlays] %s: %s spawn_story_id %d is already taken, cleared", source, name, dynamic_object->m_spawn_story_id);
		dynamic_object->m_spawn_story_id = INVALID_SPAWN_STORY_ID;
	}

	dynamic_object->m_tSpawnID = templates.ids.next_spawn_id;
	templates.spawns.add_vertex(xr_new<CServerEntityWrapper>(dynamic_object), templates.ids.next_spawn_id);
	templates.index[shared_str(name)].push_back(templates.ids.next_spawn_id);
	++templates.ids.next_spawn_id;
}

bool spawn_overlays::relocate(CSE_ALifeObject& object, const CGameGraph& graph, const GameGraph::_LEVEL_ID level_id, const Fvector& position, string256& reason)
{
	u32 vertex_id;
	float distance;
	if (!graph.nearest_vertex(level_id, position, vertex_id, distance))
	{
		xr_sprintf(reason, "level %d has no graph vertices", level_id);
		return false;
	}
	object.o_Position = position;
	object.m_tGraphID = GameGraph::_GRAPH_ID(vertex_id);
	object.m_tNodeID = graph.vertex(vertex_id)->level_vertex_id();
	object.m_fDistance = distance;
	return true;
}

int spawn_overlays::resolve_destination(CSE_ALifeLevelChanger& changer, const CGameGraph& graph, string256& reason)
{
	const GameGraph::SLevel* level = graph.header().level(*changer.m_caLevelToChange, true);
	if (!level)
	{
		xr_strcpy(reason, *changer.m_caLevelToChange);
		return -1;
	}

	u32 vertex_id;
	float distance;
	if (!graph.nearest_vertex(level->id(), changer.m_tNextPosition, vertex_id, distance))
	{
		xr_sprintf(reason, "destination level '%s' has no graph vertices", *changer.m_caLevelToChange);
		return 0;
	}
	changer.m_tNextGraphID = GameGraph::_GRAPH_ID(vertex_id);
	changer.m_dwNextNodeID = graph.vertex(vertex_id)->level_vertex_id();
	return 1;
}
