////////////////////////////////////////////////////////////////////////////
//	Module 		: spawn_templates.h
//	Created 	: 16.09.2026
//	Description : Spawn template registry shared by ltx overlays, fragments and level packs
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "alife_spawn_registry.h"

class NET_Packet;

namespace spawn_overlays
{
	typedef CALifeSpawnRegistry::SPAWN_GRAPH SPAWN_GRAPH;
	// names are not unique; one vector per name.
	typedef xr_map<shared_str, xr_vector<ALife::_SPAWN_ID> > NAME_INDEX;
	// same name on the same level within this distance = the same object
	const float SAME_OBJECT_TOLERANCE = 1.f;

	struct STemplateIds
	{
		xr_set<ALife::_STORY_ID> story_ids;
		xr_set<ALife::_SPAWN_STORY_ID> spawn_story_ids;
		ALife::_SPAWN_ID next_spawn_id{};
	};

	// Name index and id bookkeeping (taken story / spawn-story ids, next spawn id), built once over the templates
	// as loaded (before any edit), so a removed template's id is never reused and every edit sees a consistent view.
	struct STemplates
	{
		SPAWN_GRAPH& spawns;
		NAME_INDEX index;
		STemplateIds ids;

		explicit STemplates(SPAWN_GRAPH& spawns);
	};

	bool find_template(const SPAWN_GRAPH& spawns, const NAME_INDEX& index, LPCSTR name, LPCSTR match_position, ALife::_SPAWN_ID& spawn_id, string256& reason);
	bool same_object_present(const SPAWN_GRAPH& spawns, const NAME_INDEX& index, const CGameGraph& graph, LPCSTR name, GameGraph::_LEVEL_ID level_id, const Fvector& position, ALife::_SPAWN_ID* match = nullptr);
	void remove_template(STemplates& templates, LPCSTR name, ALife::_SPAWN_ID spawn_id);
	bool claim_id(xr_set<u32>& taken, u32 id);
	void release_id(xr_set<u32>& taken, u32 id);
	bool reassign_id(xr_set<u32>& taken, u32& field, u32 new_value);
	CSE_Abstract* create_from_packet(NET_Packet& packet, string256& reason);
	// Checks a decoded record before placement: refuses creatures, non-alife and nested objects and runs out of spawn
	// ids; a level changer also gets its destination re-resolved (resolve_destination). Returns 1 with out set to the
	// object, 0 failed (reason) with out null, -1 the changer's destination level is not in the graph (reason = level name).
	int prepare_template(const STemplateIds& ids, CSE_Abstract* object, const CGameGraph& graph, CSE_ALifeDynamicObject*& out, string256& reason);
	void register_template(STemplates& templates, CSE_ALifeDynamicObject* dynamic_object, LPCSTR source);
	// Approximate placement: nearest graph vertex of the level and its AI node; the engine snaps the node to
	// the real position once the level is loaded (CSE_ALifeDynamicObject::synchronize_location).
	bool relocate(CSE_ALifeObject& object, const CGameGraph& graph, GameGraph::_LEVEL_ID level_id, const Fvector& position, string256& reason);
	// Destination ids of a level changer belong to the author's graph; re-resolve them from level name and point.
	// 1 resolved, 0 failed (reason), -1 the destination level is not in the graph (reason = level name)
	int resolve_destination(CSE_ALifeLevelChanger& changer, const CGameGraph& graph, string256& reason);
}
