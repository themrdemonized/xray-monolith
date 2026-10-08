////////////////////////////////////////////////////////////////////////////
//	Module 		: spawn_packs.h
//	Created 	: 16.09.2026
//	Description : Level packs: extra all.spawn files whose new levels, objects and paths join the base spawn
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "spawn_templates.h"

class CGameGraphBuilder;
class CPatrolPathStorage;

namespace spawn_overlays
{
	// A pack is $game_spawn$\<name>.spawn at the folder root, any name but the base spawn's. Only what the base
	// lacks is taken from it: levels absent from the base header, records and paths whose names are new. Levels
	// are gated per pack (append_levels); records and paths are gated per record/path against the assembled
	// graph (add_objects, add_paths) - a pack with some levels not installed still contributes everything it can
	// on the levels that are.
	class CSpawnPacks
	{
	public:
		struct SPack
		{
			shared_str file_name;
			IReader* file = nullptr;
			IReader* graph_chunk = nullptr;
			xrGUID guid{};
			CGameGraph* graph = nullptr;
			// pack graph vertex id -> assembled vertex id; u32(-1) for vertices of levels the base already has
			xr_vector<u32> vertex_map;
			struct SAppendedLevel
			{
				GameGraph::_LEVEL_ID pack_id; // in the pack graph (straight from .spawn ile)
				GameGraph::_LEVEL_ID id; // in the assembled graph: the game.ltx id, or the one allocated for id = auto
			};
			xr_vector<SAppendedLevel> appended;
			// levels the graph already had whose build this pack replaced (they match the installed level.ai)
			xr_vector<GameGraph::_LEVEL_ID> substituted;
			// levels this pack could not append or substitute (not installed, wrong build, no [levelNN] section or
			// id, id outside 1..254 or already used, no id left for id = auto, vertex limit reached).
			u32 missing_levels = 0;
			u32 edges = 0;
			u32 objects = 0;
			// objects already in the base (or an earlier pack) / on a level not installed / refused, of
			// pack.objects' pass over the records
			u32 present = 0;
			u32 no_level = 0;
			u32 paths = 0;
			// paths on a level not installed
			u32 no_level_paths = 0;
			// records and paths on, or leading to, a [level_cut] level the pack brings; counted, not logged one by one
			u32 cut_records = 0;
			u32 cut_paths = 0;
			// path points off the AI map (level vertex id -1), kept as the pack wrote them
			u32 off_mesh_points = 0;
			u32 skipped = 0;
			// the save being loaded was made with this pack's own all.spawn (legacy_save); its ids are translated
			// by the tables below (take_legacy)
			bool legacy = false;
			// pack graph vertex id -> assembled vertex id, u16(-1) for a level the assembled graph lacks
			xr_vector<u16> legacy_vertices;
			// pack spawn id -> assembled spawn id, u16(-1) for a record that was not added
			xr_vector<u16> legacy_spawn_ids;
			// a record added on a level the pack did not append that looks like a base template the pack's
			// build renamed (same section within 1 m) or moved (same name, another level); reported by
			// report_drift once removes and patches have run, if the base template is still there
			struct SDrift
			{
				ALife::_SPAWN_ID added;
				ALife::_SPAWN_ID base;
				float distance;
				bool moved;
			};
			xr_vector<SDrift> drift;
		};
		typedef xr_vector<SPack*> PACKS;

	private:
		struct SInstalledLevel
		{
			xrGUID guid;
			u32 version;
		};
		PACKS m_packs;
		xr_map<shared_str, SInstalledLevel> m_installed_levels;
		xr_vector<shared_str> m_cut_levels;

	private:
		bool cut(LPCSTR level_name) const;
		// GUID and version of the installed $game_levels$\<level_name>\level.ai; false when there is none.
		bool installed_level(LPCSTR level_name, xrGUID& guid, u32& version);
		bool substituted(GameGraph::_LEVEL_ID level_id) const;
		void approximate_substituted(SPAWN_GRAPH& spawns, const CGameGraph& graph) const;

	public:
		~CSpawnPacks();
		void open(LPCSTR base_spawn_name);
		IC bool empty() const { return m_packs.empty(); }
		// Appends the levels the base lacks and the edges that touch them; a level already present whose build
		// does not match the installed level.ai is substituted by a pack's build that does. Returns the levels
		// appended plus substituted. A level the graph lacks that is named in cut_levels is not appended.
		u32 append_levels(CGameGraphBuilder& builder, float tolerance, const xr_vector<shared_str>& cut_levels);
		// Adds the pack records templates lacks, gated per record against the assembled graph.
		u32 add_objects(STemplates& templates, const CGameGraph& graph);
		// Logs the add_objects records that still look like a renamed or moved base template.
		void report_drift(const STemplates& templates, const CGameGraph& graph) const;
		// Adds the pack paths whose names storage lacks, remapped to graph.
		u32 add_paths(CPatrolPathStorage& storage, const CGameGraph& graph);
		// Spawn GUID of base_guid plus every contributing pack; logs each pack's summary.
		void finish(const xrGUID& base_guid, xrGUID& result) const;
		// Marks the pack whose own spawn GUID is save_guid: the save was made with that all.spawn as the base
		// spawn and its ids need translating. Call before append_levels.
		void legacy_save(const xrGUID& save_guid);
		// Hands out the marked pack's id tables once every level of the pack is in the assembled graph;
		// false otherwise (logged) or when no pack was marked.
		bool take_legacy(xr_vector<u16>& vertices, xr_vector<u16>& spawn_ids);
	};
}
