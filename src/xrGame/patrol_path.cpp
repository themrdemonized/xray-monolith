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

CPatrolPath& CPatrolPath::load_from_config(CInifile* ini_paths, LPCSTR patrol_name)
{
	R_ASSERT3(ini_paths->line_exist(patrol_name, "points"), "Missing key 'points' in patrol path", patrol_name);
	xr_string points_csv = ini_paths->r_string(patrol_name, "points");
	xr_vector<xr_string> points = points_csv.SplitStringMulti(",", false, true);

	// Keep track of name <-> vertex_id association
	xr_map<shared_str, u32> vertex_ids_by_name;

	// Add patrol points
	for (int idx = 0; idx < points.size(); idx++)
	{
		LPCSTR point_name = points[idx].c_str();
		Msg("[PP] Reading point %s", point_name);
		add_vertex(CPatrolPoint(this).load_from_config(ini_paths, patrol_name, point_name), idx);
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
		xr_string links_csv = ini_paths->r_string(patrol_name, links_csv_key.c_str());
		xr_vector<xr_string> links = links_csv.SplitStringMulti(",", false, true);

		for (xr_string link : links)
		{
			// Link current point to target points
			xr_pair<u32, float> link_info = parse_point_link(patrol_name, link, vertex_ids_by_name);
			add_edge(idx, link_info.first, link_info.second);
			Msg("[PP] Linked %d to %d with a probability of %f", link_info.first, idx, link_info.second);
		}
	}

	vertex_ids_by_name.clear();

	return (*this);
}

xr_pair<u32, float> CPatrolPath::parse_point_link(LPCSTR patrol_name, xr_string link, xr_map<shared_str, u32> vertex_ids_by_name)
{
	Msg("[PP] Linking %s", link.c_str());

    std::regex pattern("(\\w+)\\((\\d+)\\)");
    std::smatch matches;

	bool matched = std::regex_search(link, matches, pattern);
	R_ASSERT4(matched, "Bad format for patrol path link", patrol_name, link.c_str());
	
	xr_string target = matches[1].str().c_str();
	float prob = std::stof(matches[2].str());

	auto I = vertex_ids_by_name.find(target.c_str());
	R_ASSERT4(I != vertex_ids_by_name.end(), "Patrol point link target does not exist", patrol_name, target.c_str());

	return { (*I).second, prob };
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
