////////////////////////////////////////////////////////////////////////////
//	Module 		: patrol_path_storage.cpp
//	Created 	: 15.06.2004
//  Modified 	: 15.06.2004
//	Author		: Dmitriy Iassenev
//	Description : Patrol path storage
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "patrol_path_storage.h"
#include "patrol_path.h"
#include "patrol_point.h"
#include "levelgamedef.h"
#include "game_graph.h"
#include "level_graph.h"

CPatrolPathStorage::~CPatrolPathStorage()
{
	delete_data(m_registry);
}

void CPatrolPathStorage::load_raw(const CLevelGraph* level_graph, const CGameLevelCrossTable* cross, const CGameGraph* game_graph, IReader& stream)
{
	IReader* chunk = stream.open_chunk(WAY_PATROLPATH_CHUNK);

	if (!chunk)
		return;

	u32 chunk_iterator;
	for (IReader* sub_chunk = chunk->open_chunk_iterator(chunk_iterator); sub_chunk; sub_chunk = chunk->open_chunk_iterator(chunk_iterator, sub_chunk))
	{
		R_ASSERT(sub_chunk->find_chunk(WAYOBJECT_CHUNK_VERSION));
		R_ASSERT(sub_chunk->r_u16() == WAYOBJECT_VERSION);
		R_ASSERT(sub_chunk->find_chunk(WAYOBJECT_CHUNK_NAME));

		shared_str patrol_name;
		sub_chunk->r_stringZ(patrol_name);
		const_iterator I = m_registry.find(patrol_name);
		VERIFY3(I == m_registry.end(), "Duplicated patrol path found", *patrol_name);
		m_registry.insert(
			std::make_pair(
				patrol_name,
				&xr_new<CPatrolPath>(patrol_name)->load_raw(level_graph, cross, game_graph, *sub_chunk)
			)
		);
	}

	chunk->close();
}

u32 CPatrolPathStorage::load_fragment(const CGameGraph& graph, const GameGraph::_LEVEL_ID level_id, IReader& stream, const LPCSTR source)
{
	IReader* chunk = stream.open_chunk(WAY_PATROLPATH_CHUNK);
	if (!chunk)
		return 0;

	u32 skipped = 0;
	u32 chunk_iterator;
	for (IReader* sub_chunk = chunk->open_chunk_iterator(chunk_iterator); sub_chunk; sub_chunk = chunk->open_chunk_iterator(chunk_iterator, sub_chunk))
	{
		if (!sub_chunk->find_chunk(WAYOBJECT_CHUNK_NAME))
		{
			Msg("! [spawn_overlays] spawns\\%s path #%d: no name, skipped", source, chunk_iterator);
			++skipped;
			continue;
		}
		shared_str patrol_name;
		sub_chunk->r_stringZ(patrol_name);

		if (!sub_chunk->find_chunk(WAYOBJECT_CHUNK_VERSION) || sub_chunk->r_u16() != WAYOBJECT_VERSION)
		{
			Msg("! [spawn_overlays] spawns\\%s path %s: not a version %d way object, skipped", source, *patrol_name, WAYOBJECT_VERSION);
			++skipped;
			continue;
		}
		if (m_registry.find(patrol_name) != m_registry.end())
		{
			Msg("! [spawn_overlays] spawns\\%s path %s: named twice in the file, skipped", source, *patrol_name);
			++skipped;
			continue;
		}

		auto* patrol_path = xr_new<CPatrolPath>(patrol_name);
		if (string256 reason; !patrol_path->load_fragment(graph, level_id, *sub_chunk, reason))
		{
			Msg("! [spawn_overlays] spawns\\%s path %s: %s, skipped", source, *patrol_name, reason);
			xr_delete(patrol_path);
			++skipped;
			continue;
		}
		m_registry.insert(std::make_pair(patrol_name, patrol_path));
	}

	chunk->close();
	return skipped;
}

void CPatrolPathStorage::load(IReader& stream)
{
	IReader* chunk;

	chunk = stream.open_chunk(0);
	u32 size = chunk->r_u32();
	chunk->close();

	m_registry.clear();

	PATROL_REGISTRY::value_type pair;

	chunk = stream.open_chunk(1);
	for (u32 i = 0; i < size; ++i)
	{
		IReader* chunk1;
		chunk1 = chunk->open_chunk(i);

		IReader* chunk2;
		chunk2 = chunk1->open_chunk(0);
		load_data(pair.first, *chunk2);
		chunk2->close();

		chunk2 = chunk1->open_chunk(1);
		load_data(pair.second, *chunk2);
		chunk2->close();

		chunk1->close();

		const_iterator I = m_registry.find(pair.first);
		VERIFY3(I == m_registry.end(), "Duplicated patrol path found ", *pair.first);

#ifdef DEBUG
		pair.second->name(pair.first);
#endif

		m_registry.insert(pair);
	}

	chunk->close();
}

void CPatrolPathStorage::load_from_config(const CGameGraph* game_graph)
{
	// Open patrol_paths.ltx
	string_path fname;
	FS.update_path(fname, "$game_config$", "patrol_paths.ltx");
	auto* ini_paths = xr_new<CInifile>(fname, TRUE);

	Msg("[PP] %s initialized", fname);

	u32 level_sections = 0;

	// Iterate sections. Each section is a unique patrol path
	const CInifile::Root& paths = ini_paths->sections();
	for (const auto& path : paths)
	{
		// Get patrol path name
		LPCSTR patrol_name = path.Name.c_str();

		Msg("[PP] Reading section %s", patrol_name);

		// A section that cannot be loaded is skipped and logged, never fatal.
		const bool overlay = ini_paths->line_exist(patrol_name, "level");
		const LPCSTR source_key = overlay ? "level" : "points";
		const LPCSTR source = ini_paths->line_exist(patrol_name, source_key) ? ini_paths->DLTX_getFilenameOfLine(patrol_name, source_key) : nullptr;
		const LPCSTR file = source ? source : "patrol_paths.ltx";

		// 'level = <name>': points may carry no vertex ids, they are approximated on that level
		const GameGraph::SLevel* level = nullptr;
		if (overlay)
		{
			const LPCSTR level_name = ini_paths->r_string(patrol_name, "level");
			if (!level_name || !*level_name)
			{
				Msg("! [spawn_overlays] %s [%s]: 'level' is empty, skipped", file, patrol_name);
				continue;
			}
			if (!game_graph)
			{
				Msg("! [spawn_overlays] %s [%s]: no game graph (no ALife), skipped", file, patrol_name);
				continue;
			}
			level = game_graph->header().level(level_name, true);
			if (!level)
			{
				Msg("- [spawn_overlays] %s [%s]: level '%s' is not installed, skipped", file, patrol_name, level_name);
				continue;
			}
		}

		if (m_registry.find(patrol_name) != m_registry.end())
		{
			if (!overlay)
			{
				Msg("! [spawn_overlays] %s [%s]: a compiled path has this name, skipped", file, patrol_name);
				continue;
			}
			Msg("* [spawn_overlays] %s [%s]: replaces the compiled path of that name", file, patrol_name);
			erase(patrol_name);
		}

		auto* patrol_path = xr_new<CPatrolPath>(patrol_name);
		if (string256 reason; !patrol_path->load_from_config(ini_paths, patrol_name, game_graph, level, reason))
		{
			Msg("! [spawn_overlays] %s [%s]: %s, skipped", file, patrol_name, reason);
			xr_delete(patrol_path);
			continue;
		}

		// Insert in registry
		PATROL_REGISTRY::value_type pair = std::make_pair(patrol_name, patrol_path);
		m_registry.insert(pair);

		if (level)
			++level_sections;
	}

	Msg("[PP] Done reading patrol paths from configs");
	if (level_sections)
		Msg("* [spawn_overlays] paths: +%d from patrol_paths.ltx 'level =' sections", level_sections);

	xr_delete(ini_paths);
}

void CPatrolPathStorage::resolve_level(const CLevelGraph* level_graph, const CGameLevelCrossTable* cross, const CGameGraph* game_graph)
{
	u32 resolved = 0;
	for (auto& I : m_registry)
		resolved += I.second->resolve(level_graph, cross, game_graph);
	if (resolved)
		Msg("* [spawn_overlays] patrol paths: %d points snapped to the AI map of %s", resolved, *game_graph->header().level(level_graph->level_id()).name());
}

void CPatrolPathStorage::merge(CPatrolPathStorage& source, MOVED_PATHS& moved)
{
	moved.clear();
	for (u32 i = 0; i < source.m_registry.size();)
	{
		const auto I = source.m_registry.begin() + i;
		const shared_str name = I->first;
		CPatrolPath* path = I->second;
		if (m_registry.find(name) != m_registry.end())
		{
			++i;
			continue;
		}

		m_registry.insert(std::make_pair(name, path));
		source.m_registry.erase(I);
		moved.push_back(std::make_pair(name, path));
	}
}

void CPatrolPathStorage::erase(const shared_str& name)
{
	const auto I = m_registry.find(name);
	if (I == m_registry.end())
		return;
	xr_delete(I->second);
	m_registry.erase(I);
}

void CPatrolPathStorage::approximate_level(const CGameGraph& graph, const GameGraph::_LEVEL_ID level_id)
{
	u32 marked = 0;
	for (auto& I : m_registry)
		marked += I.second->approximate_level(graph, level_id);
	Msg("* [spawn_overlays] patrol paths: %d points on %s will be re-snapped to its new AI map", marked, *graph.header().level(level_id).name());
}

u32 CPatrolPathStorage::erase_level(const CGameGraph& graph, const GameGraph::_LEVEL_ID level_id)
{
	u32 erased = 0;
	for (u32 i = 0; i < m_registry.size();)
	{
		const auto I = m_registry.begin() + i;
		if (!I->second->on_level(graph, level_id))
		{
			++i;
			continue;
		}
		xr_delete(I->second);
		m_registry.erase(I);
		++erased;
	}
	return erased;
}

void CPatrolPathStorage::save(IWriter& stream)
{
	stream.open_chunk(0);
	stream.w_u32(m_registry.size());
	stream.close_chunk();

	stream.open_chunk(1);

	PATROL_REGISTRY::iterator I = m_registry.begin();
	PATROL_REGISTRY::iterator E = m_registry.end();
	for (int i = 0; I != E; ++I, ++i)
	{
		stream.open_chunk(i);

		stream.open_chunk(0);
		save_data((*I).first, stream);
		stream.close_chunk();

		stream.open_chunk(1);
		save_data((*I).second, stream);
		stream.close_chunk();

		stream.close_chunk();
	}

	stream.close_chunk();
}
