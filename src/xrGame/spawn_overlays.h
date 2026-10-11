////////////////////////////////////////////////////////////////////////////
//	Module 		: spawn_overlays.h
//	Created 	: 16.09.2026
//	Description : ltx overlays applied on top of the loaded all.spawn
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "alife_spawn_registry.h"
#include "spawn_packs.h"

class CInifile;
class CPatrolPathStorage;

namespace spawn_overlays
{
	// Parses $game_config$\spawn_overlays.ltx (and its DLTX mods) once and opens the level packs; apply_graph,
	// apply_objects, add_paths and spawn_guid apply what it found. One instance per CALifeSpawnRegistry::load.
	class CSpawnOverlays
	{
	private:
		CInifile* m_ini;
		float m_link_tolerance;
		bool m_sync_offsets;
		CSpawnPacks m_packs;
		// [level_cut] levels present in the graph, resolved by apply_graph; ids never change afterwards
		xr_vector<GameGraph::_LEVEL_ID> m_cut_levels;

	public:
		explicit CSpawnOverlays(LPCSTR base_spawn_name);
		~CSpawnOverlays();

		// Appends the levels of packs, syncs level offsets to game.ltx [levelNN], applies [graph_links] /
		// [graph_unlinks], then cuts the [level_cut] levels off every other level. Returns a graph over buffer
		// (xr_malloc, caller owns) or nullptr when the base graph is unchanged.
		CGameGraph* apply_graph(const CGameGraph& base, void*& buffer);
		// Applies [spawn_replace] to the base templates, adds the records of $game_spawn$\<level_name>\*.spawn
		// fragments (SDK level.spawn format) as templates of that level and the pack records the base lacks, then
		// applies [spawn_remove] and [spawn_patch@<name>] to base and added templates alike, then removes the
		// templates on [level_cut] levels and the level changers leading to them. graph must already be the
		// assembled one.
		void apply_objects(CALifeSpawnRegistry::SPAWN_GRAPH& spawns, const CGameGraph& graph);
		// Adds the paths of $game_spawn$\<level_name>\*.game fragments (SDK level.game format) and of packs whose
		// names storage lacks, remapped to graph, applies [path_remove], then erases the paths on [level_cut] levels.
		void add_paths(CPatrolPathStorage& storage, const CGameGraph& graph);
		// Spawn GUID of base plus every contributing pack.
		void spawn_guid(const xrGUID& base, xrGUID& result) const;
		// The save being loaded carries save_guid; a pack with that own GUID has its ids translated (CSpawnPacks).
		void legacy_save(const xrGUID& save_guid);
		bool take_legacy(xr_vector<u16>& vertices, xr_vector<u16>& spawn_ids);
	};
}
