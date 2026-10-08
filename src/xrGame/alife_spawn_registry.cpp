////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_spawn_registry.cpp
//	Created 	: 15.01.2003
//  Modified 	: 12.05.2004
//	Author		: Dmitriy Iassenev
//	Description : ALife spawn registry
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "alife_spawn_registry.h"
#include "object_broker.h"
#include "game_base.h"
#include "ai_space.h"
#include "game_graph.h"
#include "level_graph.h"
#include "spawn_overlays.h"
#include "patrol_path_storage.h"

#pragma warning(push)
#pragma warning(disable:4995)
#include <malloc.h>
#pragma warning(pop)

CALifeSpawnRegistry::CALifeSpawnRegistry(LPCSTR section)
{
	m_spawn_name = "";
	seed(u32(CPU::QPC() & 0xffffffff));
	m_game_graph = 0;
	m_graph_buffer = nullptr;
	m_chunk = 0;
	m_file = 0;
}

CALifeSpawnRegistry::~CALifeSpawnRegistry()
{
	xr_delete(m_game_graph);
	xr_free(m_graph_buffer);
	m_chunk->close();
	FS.r_close(m_file);
}

void CALifeSpawnRegistry::save(IWriter& memory_stream)
{
	Msg("* Saving spawns...");
	memory_stream.open_chunk(SPAWN_CHUNK_DATA);

	memory_stream.open_chunk(0);
	memory_stream.w_stringZ(m_spawn_name);
	memory_stream.w(&header().guid(), sizeof(header().guid()));
	// current level id and name, so the save-slot UI needs no game graph (level packs renumber vertices)
	if (ai().get_level_graph())
	{
		const GameGraph::_LEVEL_ID level_id = ai().level_graph().level_id();
		memory_stream.w_u8(level_id);
		memory_stream.w_stringZ(ai().game_graph().header().level(level_id).name());
	}
	memory_stream.close_chunk();

	memory_stream.open_chunk(1);
	save_updates(memory_stream);
	memory_stream.close_chunk();

	memory_stream.close_chunk();
}

void CALifeSpawnRegistry::save_spawn(IWriter& stream)
{
	stream.open_chunk(0);
	stream.w_u32(header().version());
	stream.w(&header().guid(), sizeof(header().guid()));
	stream.w(&header().graph_guid(), sizeof(header().graph_guid()));
	stream.w_u32(m_spawns.vertex_count());
	stream.w_u32(m_game_graph->header().level_count());
	stream.close_chunk();

	stream.open_chunk(1);
	m_spawns.save(stream);
	stream.close_chunk();

	stream.open_chunk(2);
	save_data(m_artefact_spawn_positions, stream);
	stream.close_chunk();

	stream.open_chunk(3);
	ai().m_patrol_path_storage->save(stream);
	stream.close_chunk();

	stream.open_chunk(4);
	m_game_graph->save(stream);
	stream.close_chunk();
}

void CALifeSpawnRegistry::load(IReader& file_stream, LPCSTR game_name)
{
	R_ASSERT(FS.exist(game_name));

	IReader *chunk, *chunk0;
	Msg("* Loading spawn registry...");
	R_ASSERT2(file_stream.find_chunk(SPAWN_CHUNK_DATA), "Cannot find chunk SPAWN_CHUNK_DATA!");
	chunk0 = file_stream.open_chunk(SPAWN_CHUNK_DATA);

	xrGUID guid;
	chunk = chunk0->open_chunk(0);
	VERIFY(chunk);
	chunk->r_stringZ(m_spawn_name);
	chunk->r(&guid, sizeof(guid));
	chunk->close();

	string_path file_name;
	bool file_exists = !!FS.exist(file_name, "$game_spawn$", *m_spawn_name, ".spawn");
	R_ASSERT3(file_exists, "Can't find spawn file:", *m_spawn_name);

	VERIFY(!m_file);
	m_file = FS.r_open(file_name);
	load(*m_file, &guid);

	chunk0->close();
}

void CALifeSpawnRegistry::load(LPCSTR spawn_name)
{
	Msg("* Loading spawn registry...");
	m_spawn_name = spawn_name;
	string_path file_name;
	R_ASSERT3(FS.exist(file_name, "$game_spawn$", *m_spawn_name, ".spawn"), "Can't find spawn file:", *m_spawn_name);

	VERIFY(!m_file);
	m_file = FS.r_open(file_name);
	load(*m_file);
}

struct dummy
{
	int count;
	lua_State* state;
	int ref;
};

static bool ignore_save_incompatibility()
{
	return (!!strstr(Core.Params, "-ignore_save_incompatibility"));
}

void CALifeSpawnRegistry::load(IReader& file_stream, xrGUID* save_guid)
{
	IReader* chunk;
	chunk = file_stream.open_chunk(0);
	m_header.load(*chunk);
	chunk->close();

	chunk = file_stream.open_chunk(1);
	m_spawns.load(*chunk);
	chunk->close();

#if 0
	SPAWN_GRAPH::vertex_iterator			I = m_spawns.vertices().begin();
	SPAWN_GRAPH::vertex_iterator			E = m_spawns.vertices().end();
	for ( ; I != E; ++I) {
		::luabind::wrap_base		*base = smart_cast<::luabind::wrap_base*>(&(*I).second->data()->object());
		if (!base)
			continue;

		if (xr_strcmp((*I).second->data()->object().name_replace(),"rostok_stalker_outfit"))
			continue;

		dummy					*_dummy = (dummy*)((void*)base->m_self.m_impl);
		lua_State				**_state = &_dummy->state;
		Msg						("0x%08x",*(int*)&_state);
		break;
	}
#endif

	chunk = file_stream.open_chunk(2);
	load_data(m_artefact_spawn_positions, *chunk);
	chunk->close();

	VERIFY(!m_chunk);
	m_chunk = file_stream.open_chunk(4);
	R_ASSERT2(m_chunk, "Spawn version mismatch - REBUILD SPAWN!");

	VERIFY(!m_game_graph);
	m_game_graph = xr_new<CGameGraph>(*m_chunk);

	spawn_overlays::CSpawnOverlays overlays(*m_spawn_name);
	if (save_guid)
		overlays.legacy_save(*save_guid);

	VERIFY(!m_graph_buffer);
	CGameGraph* overlaid = overlays.apply_graph(*m_game_graph, m_graph_buffer);
	if (overlaid)
	{
		xr_delete(m_game_graph);
		m_game_graph = overlaid;
	}
	ai().game_graph(m_game_graph);

	R_ASSERT2((header().graph_guid() == ai().game_graph().header().guid()) || ignore_save_incompatibility(),
	          "Spawn doesn't correspond to the graph : REBUILD SPAWN!");

	overlays.apply_objects(m_spawns, *m_game_graph);

	// after the graph: patrol_paths.ltx 'level =' sections resolve their points on it
	chunk = file_stream.open_chunk(3);
	R_ASSERT2(chunk, "Spawn version mismatch - REBUILD SPAWN!");
	ai().patrol_path_storage(*chunk);
	if (ai().m_patrol_path_storage)
		overlays.add_paths(*ai().m_patrol_path_storage, *m_game_graph);
	chunk->close();

	xrGUID guid{};
	overlays.spawn_guid(header().guid(), guid);
	m_header.set_guid(guid);
	const bool legacy = save_guid && !(*save_guid == header().guid()) && overlays.take_legacy(m_legacy_vertices, m_legacy_spawn_ids);
	R_ASSERT2(!save_guid || (*save_guid == header().guid()) || legacy || ignore_save_incompatibility(),
	          "Saved game doesn't correspond to the spawn : DELETE SAVED GAME!");

	build_story_spawns();

	build_root_spawns();

	Msg("* %d spawn points are successfully loaded", m_spawns.vertex_count());
}

void CALifeSpawnRegistry::remap_legacy(const ALife::D_OBJECT_P_MAP& objects)
{
	if (m_legacy_vertices.empty())
		return;

	const auto vertex = [&](GameGraph::_GRAPH_ID& id, const CSE_ALifeDynamicObject& object)
	{
		R_ASSERT3(id < m_legacy_vertices.size() && m_legacy_vertices[id] != u16(-1), "Saved object has no vertex in the assembled graph", object.name_replace());
		id = m_legacy_vertices[id];
	};
	for (const auto& I : objects)
	{
		CSE_ALifeDynamicObject& object = *I.second;
		vertex(object.m_tGraphID, object);
		object.m_tSpawnID = object.m_tSpawnID < m_legacy_spawn_ids.size() ? m_legacy_spawn_ids[object.m_tSpawnID] : ALife::_SPAWN_ID(-1);
		if (auto* changer = smart_cast<CSE_ALifeLevelChanger*>(&object))
			vertex(changer->m_tNextGraphID, object);
		if (auto* monster = smart_cast<CSE_ALifeMonsterAbstract*>(&object))
		{
			if (monster->m_tNextGraphID < m_legacy_vertices.size())
				vertex(monster->m_tNextGraphID, object);
			if (monster->m_tPrevGraphID < m_legacy_vertices.size())
				vertex(monster->m_tPrevGraphID, object);
		}
	}
	Msg("* [spawn_overlays] %d saved objects translated to the assembled spawn", objects.size());
	m_legacy_vertices.clear();
	m_legacy_spawn_ids.clear();
}

void CALifeSpawnRegistry::save_updates(IWriter& stream)
{
	SPAWN_GRAPH::vertex_iterator I = m_spawns.vertices().begin();
	SPAWN_GRAPH::vertex_iterator E = m_spawns.vertices().end();
	for (; I != E; ++I)
	{
		stream.open_chunk((*I).second->vertex_id());
		(*I).second->data()->save_update(stream);
		stream.close_chunk();
	}
}

void CALifeSpawnRegistry::load_updates(IReader& stream)
{
	u32 vertex_id;
	for (IReader* chunk = stream.open_chunk_iterator(vertex_id); chunk; chunk = stream.open_chunk_iterator(
		     vertex_id, chunk))
	{
		VERIFY(u32(ALife::_SPAWN_ID(-1)) > vertex_id);
		const SPAWN_GRAPH::CVertex* vertex = m_spawns.vertex(ALife::_SPAWN_ID(vertex_id));
		VERIFY(vertex);
		vertex->data()->load_update(*chunk);
	}
}

void CALifeSpawnRegistry::build_root_spawns()
{
	m_temp0.clear();
	m_temp1.clear();

	{
		SPAWN_GRAPH::const_vertex_iterator I = m_spawns.vertices().begin();
		SPAWN_GRAPH::const_vertex_iterator E = m_spawns.vertices().end();
		for (; I != E; ++I)
			m_temp0.push_back((*I).second->vertex_id());
	}

	{
		SPAWN_GRAPH::const_vertex_iterator I = m_spawns.vertices().begin();
		SPAWN_GRAPH::const_vertex_iterator E = m_spawns.vertices().end();
		for (; I != E; ++I)
		{
			SPAWN_GRAPH::const_iterator i = (*I).second->edges().begin();
			SPAWN_GRAPH::const_iterator e = (*I).second->edges().end();
			for (; i != e; ++i)
				m_temp1.push_back((*i).vertex_id());
		}
	}

	process_spawns(m_temp0);
	process_spawns(m_temp1);

	m_spawn_roots.resize(m_temp0.size() + m_temp1.size());
	xr_vector<ALife::_SPAWN_ID>::iterator I = std::set_difference(
		m_temp0.begin(),
		m_temp0.end(),
		m_temp1.begin(),
		m_temp1.end(),
		m_spawn_roots.begin()
	);

	m_spawn_roots.erase(I, m_spawn_roots.end());
}

void CALifeSpawnRegistry::build_story_spawns()
{
	SPAWN_GRAPH::const_vertex_iterator I = m_spawns.vertices().begin();
	SPAWN_GRAPH::const_vertex_iterator E = m_spawns.vertices().end();
	for (; I != E; ++I)
	{
		CSE_ALifeObject* object = smart_cast<CSE_ALifeObject*>(&(*I).second->data()->object());
		VERIFY(object);
		if (object->m_spawn_story_id == INVALID_SPAWN_STORY_ID)
			continue;

		m_spawn_story_ids.insert(std::make_pair(object->m_spawn_story_id, (*I).first));
	}
}
