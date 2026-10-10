////////////////////////////////////////////////////////////////////////////
//	Module 		: patrol_point.cpp
//	Created 	: 15.06.2004
//  Modified 	: 15.06.2004
//	Author		: Dmitriy Iassenev
//	Description : Patrol point
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "patrol_point.h"
#include "level_graph.h"
#include "level_graph.h"
#include "game_level_cross_table.h"
#include "game_graph.h"
#include "object_broker.h"

#ifdef XRGAME_EXPORTS
#	include "ai_space.h"
#endif

#ifdef DEBUG
#	include "patrol_path.h"
#endif

CPatrolPoint::CPatrolPoint(const CPatrolPath* path)
{
	m_approximate = false;
#ifdef DEBUG
	m_path = path;
	m_initialized = false;
#endif
}

#ifdef DEBUG
void CPatrolPoint::verify_vertex_id(const CLevelGraph *level_graph, const CGameLevelCrossTable *cross, const CGameGraph *game_graph) const
{
	if (!level_graph)
		return;

	if (level_graph->valid_vertex_id(m_level_vertex_id)) {
		return;
	}

	VERIFY(m_path);
	string1024 temp;
	xr_sprintf(temp, "\n! Patrol point %s in path %s is not on the level graph vertex!", *m_name, *m_path->m_name);
	THROW2(level_graph->valid_vertex_id(m_level_vertex_id),temp);
}
#endif

IC void CPatrolPoint::correct_position(const CLevelGraph* level_graph, const CGameLevelCrossTable* cross, const CGameGraph* game_graph)
{
	if (!level_graph || !level_graph->valid_vertex_position(position()) || !level_graph->valid_vertex_id(m_level_vertex_id))
		return;

	if (!level_graph->inside(level_vertex_id(level_graph, cross, game_graph), position()))
		m_position = level_graph->vertex_position(level_vertex_id(level_graph, cross, game_graph));

	m_game_vertex_id = cross->vertex(level_vertex_id(level_graph, cross, game_graph)).game_vertex_id();
}

CPatrolPoint::CPatrolPoint(const CLevelGraph* level_graph, const CGameLevelCrossTable* cross, const CGameGraph* game_graph, const CPatrolPath* path, const Fvector& position, u32 level_vertex_id, u32 flags, shared_str name)
{
#ifdef DEBUG
	VERIFY(path);
	m_path = path;
#endif

	m_position = position;
	m_level_vertex_id = level_vertex_id;
	m_flags = flags;
	m_name = name;
	m_approximate = false;

#ifdef DEBUG
	m_initialized = true;
#endif

	correct_position(level_graph, cross, game_graph);
}

CPatrolPoint& CPatrolPoint::load_raw(const CLevelGraph* level_graph, const CGameLevelCrossTable* cross, const CGameGraph* game_graph, IReader& stream)
{
	stream.r_fvector3(m_position);
	m_flags = stream.r_u32();
	stream.r_stringZ(m_name);

	if (level_graph && level_graph->valid_vertex_position(m_position))
	{
		Fvector position = m_position;
		position.y += .15f;
		m_level_vertex_id = level_graph->vertex_id(position);
}
	else
	{
		m_level_vertex_id = u32(-1);
	}

#ifdef DEBUG
	m_initialized = true;
#endif

	correct_position(level_graph, cross, game_graph);
	return (*this);
}

bool CPatrolPoint::load_from_config(const CInifile* ini_paths, const LPCSTR patrol_name, const LPCSTR point_name, const CGameGraph* game_graph, const GameGraph::SLevel* level, string256& reason)
{
	const xr_string point_name_key = FormatString("%s:%s", point_name, "name");
	if (!ini_paths->line_exist(patrol_name, point_name_key.c_str()))
	{
		xr_sprintf(reason, "point %s: missing key 'name'", point_name);
		return false;
	}
	m_name = ini_paths->r_string(patrol_name, point_name_key.c_str());

	const xr_string point_position_key = FormatString("%s:%s", point_name, "position");
	if (!ini_paths->line_exist(patrol_name, point_position_key.c_str()))
	{
		xr_sprintf(reason, "point %s: missing key 'position'", point_name);
		return false;
	}
	m_position = ini_paths->r_fvector3(patrol_name, point_position_key.c_str());

	const xr_string point_lvid_key = FormatString("%s:%s", point_name, "level_vertex_id");
	const xr_string point_gvid_key = FormatString("%s:%s", point_name, "game_vertex_id");
	const bool baked = ini_paths->line_exist(patrol_name, point_lvid_key.c_str()) && ini_paths->line_exist(patrol_name, point_gvid_key.c_str());
	if (baked)
	{
		m_level_vertex_id = ini_paths->r_u32(patrol_name, point_lvid_key.c_str());
		m_game_vertex_id = ini_paths->r_u16(patrol_name, point_gvid_key.c_str());
	}
	else if (!level)
	{
		xr_sprintf(reason, "point %s: missing key 'level_vertex_id' or 'game_vertex_id' (section has no 'level')", point_name);
		return false;
	}
	else
	{
		u32 nearest;
		float distance;
		if (!game_graph->nearest_vertex(level->id(), m_position, nearest, distance))
		{
			xr_sprintf(reason, "point %s: level '%s' has no game vertices", point_name, *level->name());
			return false;
		}
		relocate(*game_graph, GameGraph::_GRAPH_ID(nearest), true);
	}

	const xr_string point_flags_key = FormatString("%s:%s", point_name, "flags");
	if (ini_paths->line_exist(patrol_name, point_flags_key.c_str()))
	{
		m_flags = ini_paths->r_u32(patrol_name, point_flags_key.c_str());
	}

#ifdef DEBUG
	m_initialized = true;
#endif

	return true;
}

bool CPatrolPoint::resolve(const CLevelGraph* level_graph, const CGameLevelCrossTable* cross, const CGameGraph* game_graph)
{
	if (!m_approximate || game_graph->vertex(m_game_vertex_id)->level_id() != level_graph->level_id())
		return false;

	m_approximate = false;
	if (!level_graph->valid_vertex_position(m_position))
	{
		Msg("! [spawn_overlays] patrol point %s at [%.1f,%.1f,%.1f] is outside the AI map, nearest game vertex kept", *m_name, VPUSH(m_position));
		return false;
	}

	Fvector position = m_position;
	position.y += .15f;
	const u32 level_vertex_id = level_graph->vertex_id(position);
	if (!level_graph->valid_vertex_id(level_vertex_id))
	{
		Msg("! [spawn_overlays] patrol point %s at [%.1f,%.1f,%.1f] has no AI node, nearest game vertex kept", *m_name, VPUSH(m_position));
		return false;
	}

	m_level_vertex_id = level_vertex_id;
	correct_position(level_graph, cross, game_graph);
	return true;
}

void CPatrolPoint::relocate(const CGameGraph& graph, const GameGraph::_GRAPH_ID vertex_id, const bool approximate)
{
	m_game_vertex_id = vertex_id;
	m_approximate = approximate;
	if (approximate)
		m_level_vertex_id = graph.vertex(vertex_id)->level_vertex_id();
}

void CPatrolPoint::load(IReader& stream)
{
	load_data(m_name, stream);
	load_data(m_position, stream);
	load_data(m_flags, stream);
	load_data(m_level_vertex_id, stream);
	load_data(m_game_vertex_id, stream);

#ifdef DEBUG
	m_initialized = true;
#endif
}

void CPatrolPoint::save(IWriter& stream)
{
	save_data(m_name, stream);
	save_data(m_position, stream);
	save_data(m_flags, stream);
	save_data(m_level_vertex_id, stream);
	save_data(m_game_vertex_id, stream);
}

#ifdef XRGAME_EXPORTS
const u32& CPatrolPoint::level_vertex_id() const
{
	if (ai().game_graph().vertex(m_game_vertex_id)->level_id() == ai().level_graph().level_id())
		return (level_vertex_id(&ai().level_graph(), &ai().cross_table(), &ai().game_graph()));

	return (m_level_vertex_id);
}

const GameGraph::_GRAPH_ID& CPatrolPoint::game_vertex_id() const
{
	CGameGraph::CVertex const* vertex = ai().game_graph().vertex(m_game_vertex_id);

#ifdef DEBUG
	VERIFY2(
		vertex,
		make_string(
			"invalid game vertex id[%d] (level_vertex_id[%d]) for patrol point[%s] in path[%s] in position[%f][%f][%f]",
			m_game_vertex_id,
			m_level_vertex_id,
			m_name.c_str(),
			m_path->m_name.c_str(),
			VPUSH(m_position)
		)
	);
#endif

	if (vertex->level_id() == ai().level_graph().level_id())
		return (game_vertex_id(&ai().level_graph(), &ai().cross_table(), &ai().game_graph()));

	return (m_game_vertex_id);
}
#endif
