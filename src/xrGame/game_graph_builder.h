////////////////////////////////////////////////////////////////////////////
//	Module 		: game_graph_builder.h
//	Created 	: 16.09.2026
//	Description : Editable copy of a game graph that re-serializes into a spawn-chunk layout
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "game_graph.h"

class CGameGraphBuilder
{
public:
	struct SEdge
	{
		GameGraph::_GRAPH_ID vertex_id;
		float distance;
	};

	struct SVertex
	{
		GameGraph::CVertex data;
		xr_vector<SEdge> edges;
		xr_vector<GameGraph::CLevelPoint> points;
	};

	// One level's embedded cross table (u32 size + header + cells); vertex_map != nullptr means the cells are
	// pack-numbered and get remapped on build.
	struct SCrossTable
	{
		const u32* data;
		const xr_vector<u32>* vertex_map;
	};

private:
	GameGraph::CHeader m_header;
	xr_vector<SVertex> m_vertices;
	u32* m_cross_tables;
	xr_map<GameGraph::_LEVEL_ID, SCrossTable> m_level_cross_tables;
	u32 m_edge_count;
	bool m_dirty;
	bool m_appended;

private:
	static const u32* level_cross_table(const CGameGraph& graph, GameGraph::_LEVEL_ID level_id);
	static const u32* validate_cross_table(const CGameGraph& pack, GameGraph::_LEVEL_ID pack_level_id, string256& reason);
	static void clear_vertex_map(const CGameGraph& pack, GameGraph::_LEVEL_ID pack_level_id, xr_vector<u32>& vertex_map);

public:
	static bool readable_version(u32 version);
	static CGameGraph* open_graph(IReader& stream);

	explicit CGameGraphBuilder(const CGameGraph& base);
	IC const GameGraph::CHeader& header() const { return m_header; }
	IC u32 vertex_count() const { return m_vertices.size(); }
	IC const SVertex& vertex(u32 vertex_id) const { return m_vertices[vertex_id]; }
	IC bool dirty() const { return m_dirty; }

	const GameGraph::SLevel* level(LPCSTR level_name) const;
	bool find_vertex(GameGraph::_LEVEL_ID level_id, const Fvector& level_point, float tolerance, u32& vertex_id, float& distance) const;
	bool has_edge(u32 from, u32 to) const;
	bool add_edge(u32 from, u32 to);
	bool add_edge(u32 from, u32 to, float distance);
	u32 remove_edge(u32 from, u32 to);
	u32 remove_level_edges(GameGraph::_LEVEL_ID level_a, GameGraph::_LEVEL_ID level_b);
	// Removes every edge between a vertex of level_id and a vertex of another level, both directions.
	u32 isolate_level(GameGraph::_LEVEL_ID level_id);
	u32 set_level_offset(GameGraph::_LEVEL_ID level_id, const Fvector& offset);

	// Appends pack level pack_level_id as level_id (name, section, guid from the pack header, offset as given) with
	// its vertices, death points and cross table. vertex_map is sized to the pack and receives the new ids of the
	// appended vertices (u32(-1) elsewhere); edges are added by append_edges. Needs the pack and vertex_map alive
	// until build() called.
	bool append_level(const CGameGraph& pack, GameGraph::_LEVEL_ID pack_level_id, GameGraph::_LEVEL_ID level_id, const Fvector& offset, xr_vector<u32>& vertex_map, string256& reason);
	// Replaces the internals of a level the graph already has (same name, same graph points) with the pack's
	// build of it: AI nodes, death points, intra-level edges, cross table and level GUID. Vertex ids and
	// cross-level edges stay. vertex_map as in append_level; level_id receives the level's id in this graph.
	bool substitute_level(const CGameGraph& pack, GameGraph::_LEVEL_ID pack_level_id, xr_vector<u32>& vertex_map, GameGraph::_LEVEL_ID& level_id, string256& reason);
	// Adds the pack edges that touch an appended vertex, with the pack's weights. An endpoint on a level the base
	// already has is the nearest base vertex of that level to its level point. Returns edges added; no_level
	// counts edges to a level not installed, unresolved counts the rest.
	u32 append_edges(const CGameGraph& pack, const xr_vector<u32>& vertex_map, float tolerance, u32& no_level, u32& unresolved);

	// Lays vertices, edges and death points out into buffer (xr_malloc, caller owns) and returns a graph
	// over it. Without appended levels the graph keeps using the base cross tables; with them the cross tables
	// are copied into the same buffer, remapped, with the base graph GUID.
	CGameGraph* build(void*& buffer) const;
};
