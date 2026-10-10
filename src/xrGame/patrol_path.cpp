////////////////////////////////////////////////////////////////////////////
//	Module 		: patrol_path.cpp
//	Created 	: 15.06.2004
//  Modified 	: 15.06.2004
//	Author		: Dmitriy Iassenev
//	Description : Patrol path
////////////////////////////////////////////////////////////////////////////

#include <regex>
#include "stdafx.h"
#include "patrol_path.h"
#include "levelgamedef.h"
#include "game_graph.h"
#include "../xrCore/mezz_stringbuffer.h"

LPCSTR TEST_PATROL_PATH_NAME = "val_dogs_nest4_centre";

CPatrolPath::CPatrolPath(shared_str name)
{
#ifdef DEBUG
	m_name = name;
#endif
}

CPatrolPath& CPatrolPath::load_raw(const CLevelGraph* level_graph, const CGameLevelCrossTable* cross, const CGameGraph* game_graph, IReader& stream)
{
	R_ASSERT(stream.find_chunk(WAYOBJECT_CHUNK_POINTS));
	u32 vertex_count = stream.r_u16();
	for (u32 i = 0; i < vertex_count; ++i)
	{
		add_vertex(CPatrolPoint(this).load_raw(level_graph, cross, game_graph, stream), i);
	}

	R_ASSERT(stream.find_chunk(WAYOBJECT_CHUNK_LINKS));
	u32 edge_count = stream.r_u16();
	for (u32 i = 0; i < edge_count; ++i)
	{
		u16 vertex0 = stream.r_u16();
		u16 vertex1 = stream.r_u16();
		float probability = stream.r_float();
		add_edge(vertex0, vertex1, probability);
	}

	return (*this);
}

bool CPatrolPath::load_from_config(const CInifile* ini_paths, const LPCSTR patrol_name, const CGameGraph* game_graph, const GameGraph::SLevel* level, string256& reason)
{
	if (!ini_paths->line_exist(patrol_name, "points"))
	{
		xr_strcpy(reason, "missing key 'points'");
		return false;
	}
	LPCSTR points_csv = ini_paths->r_string(patrol_name, "points");
	std::vector<std::string> points = splitStringMulti(points_csv, ",", false, true);

	// Keep track of name <-> vertex_id association
	std::map<shared_str, u32> vertex_ids_by_name;

	// Add patrol points
	for (int idx = 0; idx < points.size(); idx++)
	{
		LPCSTR point_name = points[idx].c_str();
		Msg("[PP] Reading point %s", point_name);
		CPatrolPoint point(this);
		if (!point.load_from_config(ini_paths, patrol_name, point_name, game_graph, level, reason))
			return false;
		add_vertex(point, idx);
		vertex_ids_by_name.emplace(point_name, idx);
	}

	// Link patrol points
	for (int idx = 0; idx < points.size(); idx++)
	{
		LPCSTR point_name = points[idx].c_str();

		Msg("[PP] Linking point for %s", point_name);

		// Verify if point has links (links are optional)
		xr_string links_csv_key = FormatString("%s:%s", point_name, "links");
		if (!ini_paths->line_exist(patrol_name, links_csv_key.c_str()))
		{
			continue;
		}

		// Get list of vertices to link to
		LPCSTR links_csv = ini_paths->r_string(patrol_name, links_csv_key.c_str());
		std::vector<std::string> links = splitStringMulti(links_csv, ",", false, true);

		for (const std::string& link : links)
		{
			// Link current point to target points
			std::pair<u16, float> link_info;
			if (!parse_point_link(link, vertex_ids_by_name, link_info, reason))
				return false;
			add_edge(idx, link_info.first, link_info.second);
			Msg("[PP] Linked %d to %d with a probability of %f", link_info.first, idx, link_info.second);
		}
	}

	return true;
}

bool CPatrolPath::load_fragment(const CGameGraph& graph, const GameGraph::_LEVEL_ID level_id, IReader& stream, string256& reason)
{
	if (!stream.find_chunk(WAYOBJECT_CHUNK_POINTS))
	{
		xr_strcpy(reason, "no points chunk");
		return false;
	}
	const u32 vertex_count = stream.r_u16();
	for (u32 i = 0; i < vertex_count; ++i)
	{
		CPatrolPoint point(this);
		point.load_raw(nullptr, nullptr, nullptr, stream);
		u32 nearest;
		float distance;
		if (!graph.nearest_vertex(level_id, point.position(), nearest, distance))
		{
			xr_strcpy(reason, "the level has no game vertices");
			return false;
		}
		point.relocate(graph, GameGraph::_GRAPH_ID(nearest), true);
		add_vertex(point, i);
	}

	if (!stream.find_chunk(WAYOBJECT_CHUNK_LINKS))
	{
		xr_strcpy(reason, "no links chunk");
		return false;
	}
	const u32 edge_count = stream.r_u16();
	for (u32 i = 0; i < edge_count; ++i)
	{
		const u16 vertex0 = stream.r_u16();
		const u16 vertex1 = stream.r_u16();
		const float probability = stream.r_float();
		if (vertex0 >= vertex_count || vertex1 >= vertex_count)
		{
			xr_sprintf(reason, "link %d -> %d names a point past the last one (%d)", vertex0, vertex1, vertex_count);
			return false;
		}
		add_edge(vertex0, vertex1, probability);
	}
	return true;
}

u32 CPatrolPath::resolve(const CLevelGraph* level_graph, const CGameLevelCrossTable* cross, const CGameGraph* game_graph)
{
	u32 resolved = 0;
	for (auto& I : vertices())
		resolved += u32(I.second->data().resolve(level_graph, cross, game_graph));
	return resolved;
}

u32 CPatrolPath::approximate_level(const CGameGraph& graph, const GameGraph::_LEVEL_ID level_id)
{
	u32 marked = 0;
	for (auto& I : vertices())
	{
		CPatrolPoint& point = I.second->data();
		// level_graph = nullptr: this runs before the level is loaded, ai().level_graph() is not valid yet.
		const GameGraph::_GRAPH_ID vertex_id = point.game_vertex_id(nullptr, nullptr, &graph);
		if (!graph.valid_vertex_id(vertex_id) || graph.vertex(vertex_id)->level_id() != level_id)
			continue;
		point.relocate(graph, vertex_id, true);
		++marked;
	}
	return marked;
}

bool CPatrolPath::on_level(const CGameGraph& graph, const GameGraph::_LEVEL_ID level_id) const
{
	for (const auto& I : vertices())
	{
		const CPatrolPoint& point = I.second->data();
		if (point.level_vertex_id(nullptr, nullptr, &graph) == u32(-1))
			continue;
		const GameGraph::_GRAPH_ID vertex_id = point.game_vertex_id(nullptr, nullptr, &graph);
		return graph.valid_vertex_id(vertex_id) && graph.vertex(vertex_id)->level_id() == level_id;
	}
	return false;
}

bool CPatrolPath::parse_point_link(const std::string& link, const std::map<shared_str, u32>& vertex_ids_by_name, std::pair<u16, float>& result, string256& reason)
{
	Msg("[PP] Linking %s", link.c_str());

	const std::regex pattern(R"((\w+)\((\d+)\))");
	std::smatch matches;

	if (!std::regex_search(link, matches, pattern))
	{
		xr_sprintf(reason, "link '%s': expected <point>(<probability>)", link.c_str());
		return false;
	}

	const std::string target = matches[1].str();
	float prob = std::stof(matches[2].str());

	const auto I = vertex_ids_by_name.find(target.c_str());
	if (I == vertex_ids_by_name.end())
	{
		xr_sprintf(reason, "link '%s': point '%s' is not in 'points'", link.c_str(), target.c_str());
		return false;
	}

	result = std::make_pair(u16(I->second), prob);
	return true;
}

CPatrolPath::~CPatrolPath()
{
}

#ifdef DEBUG
void CPatrolPath::load(IReader &stream)
{
	inherited::load	(stream);
	vertex_iterator	I = vertices().begin();
	vertex_iterator	E = vertices().end();
	for ( ; I != E; ++I)
		(*I).second->data().path(this);
}
#endif
