////////////////////////////////////////////////////////////////////////////
//	Module 		: game_graph_builder.cpp
//	Created 	: 16.09.2026
//	Description : Editable copy of a game graph that re-serializes into a spawn-chunk layout
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "game_graph_builder.h"

bool CGameGraphBuilder::readable_version(const u32 version)
{
	return version == XRAI_CURRENT_VERSION || version == XRAI_LARGE_VERSION;
}

CGameGraph* CGameGraphBuilder::open_graph(IReader& stream)
{
	GameGraph::CHeader header;
	header.load(&stream);
	if (!readable_version(header.version()))
		return nullptr;
	auto* vertices = static_cast<GameGraph::CVertex*>(stream.pointer());
	u8* cursor = reinterpret_cast<u8*>(vertices + header.vertex_count());
	cursor += header.edge_count() * sizeof(GameGraph::CEdge);
	auto* tables = reinterpret_cast<u32*>(reinterpret_cast<GameGraph::CLevelPoint*>(cursor) + header.death_point_count());
	return xr_new<CGameGraph>(header, vertices, tables);
}

CGameGraphBuilder::CGameGraphBuilder(const CGameGraph& base)
{
	m_header = base.header();
	m_cross_tables = base.m_cross_tables;
	m_edge_count = 0;
	m_dirty = false;
	m_appended = false;

	{
		auto I = base.header().levels().begin();
		auto E = base.header().levels().end();
		for (; I != E; ++I)
		{
			SCrossTable entry{};
			entry.data = level_cross_table(base, (*I).first);
			entry.vertex_map = nullptr;
			m_level_cross_tables[I->first] = entry;
		}
	}

	const u32 vertex_count = base.header().vertex_count();
	m_vertices.resize(vertex_count);
	for (u32 i = 0; i < vertex_count; ++i)
	{
		const GameGraph::CVertex* source = base.vertex(i);
		SVertex& vertex = m_vertices[i];
		vertex.data = *source;

		CGameGraph::const_iterator e_begin, e_end;
		base.begin(i, e_begin, e_end);
		vertex.edges.reserve(e_end - e_begin);
		for (const auto* e = e_begin; e != e_end; ++e)
		{
			SEdge edge{};
			edge.vertex_id = e->vertex_id();
			edge.distance = e->distance();
			vertex.edges.push_back(edge);
		}
		m_edge_count += vertex.edges.size();

		CGameGraph::const_spawn_iterator p_begin, p_end;
		base.begin_spawn(i, p_begin, p_end);
		vertex.points.assign(p_begin, p_end);
	}
	VERIFY(m_edge_count == base.header().edge_count());
}

const GameGraph::SLevel* CGameGraphBuilder::level(LPCSTR level_name) const
{
	return m_header.level(level_name, true);
}

bool CGameGraphBuilder::find_vertex(GameGraph::_LEVEL_ID level_id, const Fvector& level_point, float tolerance, u32& vertex_id, float& distance) const
{
	float best = flt_max;
	vertex_id = u32(-1);
	for (u32 i = 0, n = m_vertices.size(); i < n; ++i)
	{
		const GameGraph::CVertex& vertex = m_vertices[i].data;
		if (vertex.level_id() != level_id)
			continue;
		const float current = vertex.level_point().distance_to(level_point);
		if (current >= best)
			continue;
		best = current;
		vertex_id = i;
	}
	distance = best;
	return vertex_id != u32(-1) && best <= tolerance;
}

bool CGameGraphBuilder::has_edge(u32 from, u32 to) const
{
	for (const SEdge& edge : m_vertices[from].edges)
		if (edge.vertex_id == to)
			return true;
	return false;
}

bool CGameGraphBuilder::add_edge(u32 from, u32 to)
{
	const float distance = m_vertices[from].data.game_point().distance_to(m_vertices[to].data.game_point());
	return add_edge(from, to, distance);
}

bool CGameGraphBuilder::add_edge(u32 from, u32 to, const float distance)
{
	SVertex& vertex = m_vertices[from];
	if (vertex.edges.size() >= 255)
		return false;

	SEdge edge{};
	edge.vertex_id = static_cast<GameGraph::_GRAPH_ID>(to);
	edge.distance = distance;
	vertex.edges.push_back(edge);
	++m_edge_count;
	m_dirty = true;
	return true;
}

u32 CGameGraphBuilder::remove_edge(u32 from, u32 to)
{
	xr_vector<SEdge>& edges = m_vertices[from].edges;
	u32 removed = 0;
	for (u32 i = 0; i < edges.size();)
	{
		if (edges[i].vertex_id != to)
		{
			++i;
			continue;
		}
		edges.erase(edges.begin() + i);
		++removed;
	}
	m_edge_count -= removed;
	if (removed)
		m_dirty = true;
	return removed;
}

u32 CGameGraphBuilder::remove_level_edges(const GameGraph::_LEVEL_ID level_a, const GameGraph::_LEVEL_ID level_b)
{
	u32 removed = 0;
	for (SVertex& vertex : m_vertices)
	{
		const GameGraph::_LEVEL_ID from = vertex.data.level_id();
		if (from != level_a && from != level_b)
			continue;
		const GameGraph::_LEVEL_ID other = (from == level_a) ? level_b : level_a;
		xr_vector<SEdge>& edges = vertex.edges;
		for (u32 j = 0; j < edges.size();)
		{
			if (m_vertices[edges[j].vertex_id].data.level_id() != other)
			{
				++j;
				continue;
			}
			edges.erase(edges.begin() + j);
			++removed;
		}
	}
	m_edge_count -= removed;
	if (removed)
		m_dirty = true;
	return removed;
}

u32 CGameGraphBuilder::isolate_level(const GameGraph::_LEVEL_ID level_id)
{
	u32 removed = 0;
	for (SVertex& vertex : m_vertices)
	{
		const bool from_level = vertex.data.level_id() == level_id;
		xr_vector<SEdge>& edges = vertex.edges;
		for (u32 j = 0; j < edges.size();)
		{
			if (from_level == (m_vertices[edges[j].vertex_id].data.level_id() == level_id))
			{
				++j;
				continue;
			}
			edges.erase(edges.begin() + j);
			++removed;
		}
	}
	m_edge_count -= removed;
	if (removed)
		m_dirty = true;
	return removed;
}

u32 CGameGraphBuilder::set_level_offset(const GameGraph::_LEVEL_ID level_id, const Fvector& offset)
{
	const auto level = m_header.m_levels.find(level_id);
	VERIFY(level != m_header.m_levels.end());

	Fvector delta;
	delta.sub(offset, level->second.m_offset);
	level->second.m_offset = offset;

	for (SVertex& vertex : m_vertices)
		if (vertex.data.level_id() == level_id)
			vertex.data.tGlobalPoint.add(delta);

	u32 reweighted = 0;
	for (SVertex& vertex : m_vertices)
	{
		const bool from_level = vertex.data.level_id() == level_id;
		for (u32 j = 0, m = vertex.edges.size(); j < m; ++j)
		{
			const SVertex& target = m_vertices[vertex.edges[j].vertex_id];
			if (from_level == (target.data.level_id() == level_id))
				continue;
			vertex.edges[j].distance = vertex.data.game_point().distance_to(target.data.game_point());
			++reweighted;
		}
	}

	m_dirty = true;
	return reweighted;
}

const u32* CGameGraphBuilder::level_cross_table(const CGameGraph& graph, const GameGraph::_LEVEL_ID level_id)
{
	const u32* table = graph.m_cross_tables;
	auto I = graph.header().levels().begin();
	auto E = graph.header().levels().end();
	for (; I != E; ++I)
	{
		if (I->first == level_id)
			return table;
		table = reinterpret_cast<const u32*>(reinterpret_cast<const u8*>(table) + *table);
	}
	return nullptr;
}

void CGameGraphBuilder::clear_vertex_map(const CGameGraph& pack, const GameGraph::_LEVEL_ID pack_level_id, xr_vector<u32>& vertex_map)
{
	for (u32 i = 0, n = pack.header().vertex_count(); i < n; ++i)
		if (pack.vertex(i)->level_id() == pack_level_id)
			vertex_map[i] = u32(-1);
}

const u32* CGameGraphBuilder::validate_cross_table(const CGameGraph& pack, const GameGraph::_LEVEL_ID pack_level_id, string256& reason)
{
	const GameGraph::SLevel& source = pack.header().level(pack_level_id);
	const u32* table = level_cross_table(pack, pack_level_id);
	if (!table)
	{
		xr_strcpy(reason, "no cross table in the pack");
		return nullptr;
	}
	const auto* table_header = reinterpret_cast<const CGameLevelCrossTable::CHeader*>(table + 1);
	if (!readable_version(table_header->version()))
	{
		xr_sprintf(reason, "cross table version %d is not readable", table_header->version());
		return nullptr;
	}
	if (u64(*table) < sizeof(u32) + sizeof(CGameLevelCrossTable::CHeader) + u64(table_header->level_vertex_count()) * sizeof(CGameLevelCrossTable::CCell))
	{
		xr_sprintf(reason, "cross table of %d bytes is too short for %d AI nodes", *table, table_header->level_vertex_count());
		return nullptr;
	}
	if (table_header->level_guid() != source.guid())
	{
		xr_strcpy(reason, "cross table does not match the level");
		return nullptr;
	}
	const auto* cells = reinterpret_cast<const CGameLevelCrossTable::CCell*>(table_header + 1);
	for (u32 i = 0, n = table_header->level_vertex_count(); i < n; ++i)
	{
		const u32 gvid = cells[i].game_vertex_id();
		if (gvid >= pack.header().vertex_count() || pack.vertex(gvid)->level_id() != pack_level_id)
		{
			xr_sprintf(reason, "cross table cell %d points outside the level", i);
			return nullptr;
		}
	}
	return table;
}

bool CGameGraphBuilder::append_level(const CGameGraph& pack, const GameGraph::_LEVEL_ID pack_level_id, const GameGraph::_LEVEL_ID level_id, const Fvector& offset, xr_vector<u32>& vertex_map, string256& reason)
{
	const GameGraph::SLevel& source = pack.header().level(pack_level_id);
	if (m_header.m_levels.find(level_id) != m_header.m_levels.end())
	{
		xr_sprintf(reason, "level id %d is already used by '%s'", level_id, *m_header.level(level_id).name());
		return false;
	}

	const u32* table = validate_cross_table(pack, pack_level_id, reason);
	if (!table)
		return false;

	const u32 pack_count = pack.header().vertex_count();
	if (vertex_map.size() != pack_count)
		vertex_map.assign(pack_count, u32(-1));

	const u32 before = m_vertices.size();
	u32 appended = 0;
	for (u32 i = 0; i < pack_count; ++i)
	{
		const GameGraph::CVertex* vertex = pack.vertex(i);
		if (vertex->level_id() != pack_level_id)
			continue;
		if (m_vertices.size() >= u32(GameGraph::_GRAPH_ID(-1)))
		{
			m_vertices.resize(before);
			clear_vertex_map(pack, pack_level_id, vertex_map);
			xr_strcpy(reason, "game graph vertex limit reached");
			return false;
		}

		SVertex copy;
		copy.data = *vertex;
		copy.data.tLevelID = level_id;
		copy.data.tGlobalPoint.add(vertex->level_point(), offset);
		CGameGraph::const_spawn_iterator p_begin, p_end;
		pack.begin_spawn(i, p_begin, p_end);
		copy.points.assign(p_begin, p_end);

		vertex_map[i] = m_vertices.size();
		m_vertices.push_back(copy);
		++appended;
	}
	if (!appended)
	{
		xr_strcpy(reason, "level has no graph vertices");
		return false;
	}

	GameGraph::SLevel level = source;
	level.m_id = level_id;
	level.m_offset = offset;
	m_header.m_levels.insert(std::make_pair(level.id(), level));

	SCrossTable entry{};
	entry.data = table;
	entry.vertex_map = &vertex_map;
	m_level_cross_tables[level.id()] = entry;
	m_appended = true;
	m_dirty = true;
	return true;
}

bool CGameGraphBuilder::substitute_level(const CGameGraph& pack, const GameGraph::_LEVEL_ID pack_level_id, xr_vector<u32>& vertex_map, GameGraph::_LEVEL_ID& level_id, string256& reason)
{
	const GameGraph::SLevel& source = pack.header().level(pack_level_id);
	auto target = m_header.m_levels.begin();
	for (; target != m_header.m_levels.end(); ++target)
		if (!xr_strcmp(target->second.name(), source.name()))
			break;
	if (target == m_header.m_levels.end())
	{
		xr_strcpy(reason, "level is not in the graph");
		return false;
	}
	level_id = target->first;

	const u32* table = validate_cross_table(pack, pack_level_id, reason);
	if (!table)
		return false;

	const u32 pack_count = pack.header().vertex_count();
	if (vertex_map.size() != pack_count)
		vertex_map.assign(pack_count, u32(-1));

	xr_vector<u32> ours;
	for (u32 i = 0, n = m_vertices.size(); i < n; ++i)
		if (m_vertices[i].data.level_id() == level_id)
			ours.push_back(i);
	xr_vector<bool> taken;
	taken.assign(ours.size(), false);
	u32 matched = 0;
	for (u32 i = 0; i < pack_count; ++i)
	{
		const GameGraph::CVertex* vertex = pack.vertex(i);
		if (vertex->level_id() != pack_level_id)
			continue;
		u32 found = u32(-1);
		for (u32 j = 0, n = ours.size(); j < n; ++j)
		{
			if (taken[j] || !m_vertices[ours[j]].data.level_point().similar(vertex->level_point(), .05f))
				continue;
			found = j;
			break;
		}
		if (found == u32(-1))
		{
			clear_vertex_map(pack, pack_level_id, vertex_map);
			xr_sprintf(reason, "graph points differ (pack vertex at [%.1f,%.1f,%.1f] has no counterpart)", VPUSH(vertex->level_point()));
			return false;
		}
		taken[found] = true;
		vertex_map[i] = ours[found];
		++matched;
	}
	if (matched != ours.size())
	{
		clear_vertex_map(pack, pack_level_id, vertex_map);
		xr_sprintf(reason, "graph points differ (%d in the pack, %d in the graph)", matched, ours.size());
		return false;
	}

	// AI nodes, death points and intra-level edges come from the pack; cross-level edges stay
	u32 intra_removed = 0;
	for (const u32 our : ours)
	{
		SVertex& vertex = m_vertices[our];
		for (u32 k = 0; k < vertex.edges.size();)
		{
			if (m_vertices[vertex.edges[k].vertex_id].data.level_id() != level_id)
			{
				++k;
				continue;
			}
			vertex.edges.erase(vertex.edges.begin() + k);
			++intra_removed;
		}
	}
	m_edge_count -= intra_removed;

	for (u32 i = 0; i < pack_count; ++i)
	{
		if (pack.vertex(i)->level_id() != pack_level_id)
			continue;
		const GameGraph::CVertex* vertex = pack.vertex(i);
		SVertex& target_vertex = m_vertices[vertex_map[i]];
		target_vertex.data.tNodeID = vertex->level_vertex_id();
		CGameGraph::const_spawn_iterator p_begin, p_end;
		pack.begin_spawn(i, p_begin, p_end);
		target_vertex.points.assign(p_begin, p_end);

		CGameGraph::const_iterator e, e_end;
		pack.begin(i, e, e_end);
		for (; e != e_end; ++e)
		{
			if (pack.vertex(e->vertex_id())->level_id() != pack_level_id)
				continue;
			add_edge(vertex_map[i], vertex_map[e->vertex_id()], e->distance());
		}
	}

	(*target).second.m_guid = source.guid();
	SCrossTable entry{};
	entry.data = table;
	entry.vertex_map = &vertex_map;
	m_level_cross_tables[level_id] = entry;
	m_appended = true;
	m_dirty = true;
	return true;
}

u32 CGameGraphBuilder::append_edges(const CGameGraph& pack, const xr_vector<u32>& vertex_map, const float tolerance, u32& no_level, u32& unresolved)
{
	u32 added = 0;
	for (u32 i = 0, n = pack.header().vertex_count(); i < n; ++i)
	{
		CGameGraph::const_iterator e, e_end;
		pack.begin(i, e, e_end);
		for (; e != e_end; ++e)
		{
			if (e->vertex_id() >= vertex_map.size())
			{
				++unresolved;
				continue;
			}
			const u32 from = vertex_map[i];
			const u32 to = vertex_map[e->vertex_id()];
			if (from == u32(-1) && to == u32(-1))
				continue;

			if (from != u32(-1) && to != u32(-1))
			{
				if (has_edge(from, to))
					continue;
				if (!add_edge(from, to, e->distance()))
				{
					++unresolved;
					continue;
				}
				++added;
				continue;
			}

			const u32 foreign = (from == u32(-1)) ? i : e->vertex_id();
			const GameGraph::CVertex* vertex = pack.vertex(foreign);
			auto pack_level = pack.header().levels().find(vertex->level_id());
			if (pack_level == pack.header().levels().end())
			{
				++unresolved;
				continue;
			}
			const GameGraph::SLevel* level = m_header.level(*pack_level->second.name(), true);
			if (!level)
			{
				++no_level;
				continue;
			}
			u32 resolved;
			float distance;
			if (!find_vertex(level->id(), vertex->level_point(), tolerance, resolved, distance))
			{
				++unresolved;
				continue;
			}
			const u32 a = (from == u32(-1)) ? resolved : from;
			const u32 b = (to == u32(-1)) ? resolved : to;
			if (has_edge(a, b))
				continue;
			if (!add_edge(a, b, e->distance()))
			{
				++unresolved;
				continue;
			}
			++added;
		}
	}
	return added;
}

CGameGraph* CGameGraphBuilder::build(void*& buffer) const
{
	const u32 vertex_count = m_vertices.size();
	u32 point_count = 0;
	for (u32 i = 0; i < vertex_count; ++i)
		point_count += m_vertices[i].points.size();

	const u32 vertices_size = vertex_count * sizeof(GameGraph::CVertex);
	const u32 edges_size = m_edge_count * sizeof(GameGraph::CEdge);
	const u32 points_size = point_count * sizeof(GameGraph::CLevelPoint);

	u32 tables_size = 0;
	if (m_appended)
	{
		auto I = m_header.m_levels.begin();
		auto E = m_header.m_levels.end();
		for (; I != E; ++I)
			tables_size += *m_level_cross_tables.find(I->first)->second.data;
	}

	buffer = xr_malloc(vertices_size + edges_size + points_size + tables_size);
	auto* vertices = static_cast<GameGraph::CVertex*>(buffer);
	auto* edges = reinterpret_cast<GameGraph::CEdge*>(static_cast<u8*>(buffer) + vertices_size);
	auto* points = reinterpret_cast<GameGraph::CLevelPoint*>(static_cast<u8*>(buffer) + vertices_size + edges_size);

	u32 edge_offset = vertices_size;
	u32 point_offset = vertices_size + edges_size;
	for (u32 i = 0; i < vertex_count; ++i)
	{
		const SVertex& source = m_vertices[i];
		GameGraph::CVertex& vertex = vertices[i];
		vertex = source.data;
		vertex.dwEdgeOffset = edge_offset;
		vertex.dwPointOffset = point_offset;
		vertex.tNeighbourCount = u8(source.edges.size());
		vertex.tDeathPointCount = u8(source.points.size());

		for (u32 j = 0, n = source.edges.size(); j < n; ++j, ++edges)
		{
			edges->m_vertex_id = source.edges[j].vertex_id;
			edges->m_path_distance = source.edges[j].distance;
		}
		edge_offset += source.edges.size() * sizeof(GameGraph::CEdge);

		for (u32 j = 0, n = source.points.size(); j < n; ++j, ++points)
			*points = source.points[j];
		point_offset += source.points.size() * sizeof(GameGraph::CLevelPoint);
	}

	GameGraph::CHeader header = m_header;
	header.m_vertex_count = static_cast<GameGraph::_GRAPH_ID>(vertex_count);
	header.m_edge_count = m_edge_count;
	header.m_death_point_count = point_count;

	if (!m_appended)
		return xr_new<CGameGraph>(header, vertices, m_cross_tables);

	auto* tables = reinterpret_cast<u32*>(static_cast<u8*>(buffer) + vertices_size + edges_size + points_size);
	auto* cursor = reinterpret_cast<u8*>(tables);
	auto I = m_header.m_levels.begin();
	auto E = m_header.m_levels.end();
	for (; I != E; ++I)
	{
		const SCrossTable& source = m_level_cross_tables.find(I->first)->second;
		const u32 size = *source.data;
		Memory.mem_copy(cursor, source.data, size);
		if (source.vertex_map)
		{
			auto* table_header = reinterpret_cast<CGameLevelCrossTable::CHeader*>(cursor + sizeof(u32));
			table_header->dwVersion = m_header.version();
			table_header->m_game_guid = m_header.m_guid;
			auto* cells = reinterpret_cast<CGameLevelCrossTable::CCell*>(table_header + 1);
			for (u32 i = 0, n = table_header->level_vertex_count(); i < n; ++i)
				cells[i].tGraphIndex = GameGraph::_GRAPH_ID((*source.vertex_map)[cells[i].tGraphIndex]);
		}
		cursor += size;
	}
	return xr_new<CGameGraph>(header, vertices, tables);
}
