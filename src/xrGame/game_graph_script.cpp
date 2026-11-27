////////////////////////////////////////////////////////////////////////////
//	Module 		: game_graph_script.cpp
//	Created 	: 02.11.2005
//  Modified 	: 02.11.2005
//	Author		: Dmitriy Iassenev
//	Description : Game graph class script export
////////////////////////////////////////////////////////////////////////////

#include "pch_script.h"
#include "game_graph.h"
#include "ai_space.h"
#include "level_graph.h"
#include "game_level_cross_table.h"
#include "script_engine.h"

using namespace luabind;

const CGameGraph* get_game_graph()
{
	return (&ai().game_graph());
}

const CGameGraph::CHeader* get_header(const CGameGraph* self)
{
	return (&self->header());
}

bool get_accessible1(const CGameGraph* self, const u32& vertex_id)
{
	return (self->accessible(vertex_id));
}

void get_accessible2(const CGameGraph* self, const u32& vertex_id, bool value)
{
	self->accessible(vertex_id, value);
}

Fvector CVertex__level_point(const CGameGraph::CVertex* vertex)
{
	THROW(vertex);
	return (vertex->level_point());
}

Fvector CVertex__game_point(const CGameGraph::CVertex* vertex)
{
	THROW(vertex);
	return (vertex->game_point());
}

GameGraph::LEVEL_MAP const& get_levels(CGameGraph const* graph)
{
	THROW(graph);
	return graph->header().levels();
}

// Level ids of every level sharing at least one edge with level_id, as {[level_id] = true}.
// An unknown level_id yields an empty table.
::luabind::object get_level_neighbours(lua_State* L, CGameGraph const* graph, u32 level_id)
{
	THROW(graph);
	::luabind::object result = ::luabind::newtable(L);
	u32 const vertex_count = graph->header().vertex_count();
	for (u32 vertex_id = 0; vertex_id < vertex_count; ++vertex_id)
	{
		if (graph->vertex(vertex_id)->level_id() != level_id)
			continue;
		CGameGraph::const_iterator i, e;
		graph->begin(vertex_id, i, e);
		for (; i != e; ++i)
		{
			u32 const other = graph->vertex(graph->value(vertex_id, i))->level_id();
			if (other != level_id)
				result[other] = true;
		}
	}
	return result;
}

// Game vertex id of level_name's vertex whose level point is nearest to position; nil when the graph has no such
// level.
::luabind::object get_nearest_vertex(lua_State* L, CGameGraph const* graph, LPCSTR level_name, const Fvector& position)
{
	THROW(graph);
	u32 vertex_id;
	float distance;
	const GameGraph::SLevel* level = graph->header().level(level_name, true);
	if (level && graph->nearest_vertex(level->id(), position, vertex_id, distance))
		return ::luabind::object(L, vertex_id);

	::luabind::object none(L);
	lua_pushnil(L);
	none.set();
	return none;
}

// Nodes are sorted by packed xz (x cell * row_length + z cell). A node outside a window of r cells around position is
// at least r cells away, so the window stops growing once the best distance is below that.
static u32 nearest_level_vertex_3d(const CLevelGraph& level_graph, const Fvector& position)
{
	if (!level_graph.header().vertex_count())
		return u32(-1);

	const float cell_size = level_graph.header().cell_size();
	const Fbox& box = level_graph.header().box();
	const int x0 = iFloor((position.x - box.min.x) / cell_size + .5f);
	const int z0 = iFloor((position.z - box.min.z) / cell_size + .5f);
	const int max_x = int(level_graph.max_x());
	const int max_z = int(level_graph.max_z());
	const u32 row_length = level_graph.row_length();
	CLevelGraph::CVertex* B = level_graph.vertex(u32(0));
	CLevelGraph::CVertex* E = B + level_graph.header().vertex_count();

	u32 best = u32(-1);
	float best_distance_sqr = flt_max; // CLevelGraph::distance(position, vertex) is squared
	for (int radius = 4;; radius *= 4)
	{
		const int x_min = _max(x0 - radius, 0), x_max = _min(x0 + radius, max_x);
		const int z_min = _max(z0 - radius, 0), z_max = _min(z0 + radius, max_z);
		for (int x = x_min; x <= x_max && z_min <= z_max; ++x)
		{
			const u32 row = u32(x) * row_length;
			for (CLevelGraph::CVertex* I = std::lower_bound(B, E, row + u32(z_min)); I != E && I->position().xz() <= row + u32(z_max); ++I)
			{
				const u32 vertex_id = u32(I - B);
				const float d = level_graph.distance(position, vertex_id);
				if (d < best_distance_sqr)
				{
					best_distance_sqr = d;
					best = vertex_id;
				}
			}
		}
		const bool whole_level = x0 - radius <= 0 && x0 + radius >= max_x && z0 - radius <= 0 && z0 + radius >= max_z;
		if (best_distance_sqr < _sqr(radius * cell_size) || whole_level)
			return best;
	}
}

// The cell node alone can be a floor above or below on multi-storey levels.
static u32 nearest_level_vertex(const CLevelGraph& level_graph, const Fvector& position)
{
	constexpr float max_height = 2.f;
	if (level_graph.valid_vertex_position(position))
	{
		u32 cell_vertex_id = level_graph.vertex_id(position);
		if (level_graph.valid_vertex_id(cell_vertex_id) && _abs(level_graph.vertex_plane_y(cell_vertex_id, position.x, position.z) - position.y) <= max_height)
			return cell_vertex_id;
	}
	return nearest_level_vertex_3d(level_graph, position);
}

// Off the current level the ids are the nearest game vertex and its own node; CSE_ALifeDynamicObject::
// synchronize_location re-snaps them to the position when the object goes online. Returns nullptr, or why it failed.
LPCSTR level_vertices(const CGameGraph& graph, LPCSTR level_name, const Fvector& position, u32& level_vertex_id, GameGraph::_GRAPH_ID& game_vertex_id, bool& current_level)
{
	const GameGraph::SLevel* level = graph.header().level(level_name, true);
	if (!level)
		return "is not in the game graph";

	const CLevelGraph* level_graph = ai().get_level_graph();
	current_level = level_graph && level_graph->level_id() == level->id();
	if (current_level)
	{
		level_vertex_id = nearest_level_vertex(*level_graph, position);
		game_vertex_id = ai().cross_table().vertex(level_vertex_id).game_vertex_id();
		return nullptr;
	}

	u32 vertex_id;
	float distance;
	if (!graph.nearest_vertex(level->id(), position, vertex_id, distance))
		return "has no game vertices";
	game_vertex_id = GameGraph::_GRAPH_ID(vertex_id);
	level_vertex_id = graph.vertex(vertex_id)->level_vertex_id();
	return nullptr;
}

::luabind::object get_level_vertices(lua_State* L, CGameGraph const* graph, LPCSTR level_name, const Fvector& position)
{
	THROW(graph);
	u32 level_vertex_id;
	GameGraph::_GRAPH_ID game_vertex_id;
	bool current_level;
	LPCSTR error = level_vertices(*graph, level_name, position, level_vertex_id, game_vertex_id, current_level);
	if (!error)
	{
		::luabind::object result = ::luabind::newtable(L);
		result["level_vertex_id"] = level_vertex_id;
		result["game_vertex_id"] = game_vertex_id;
		return result;
	}

	Msg("! game_graph():level_vertices: level '%s' %s", level_name, error);
	::luabind::object none(L);
	lua_pushnil(L);
	none.set();
	return none;
}

#pragma optimize("s",on)
void CGameGraph::script_register(lua_State* L)
{
	module(L)
	[
		class_<GameGraph::LEVEL_MAP::value_type>("GameGraph__LEVEL_MAP__value_type")
		.def_readonly("id", &GameGraph::LEVEL_MAP::value_type::first)
		.def_readonly("level", &GameGraph::LEVEL_MAP::value_type::second),

		def("game_graph", &get_game_graph),

		class_<CGameGraph>("CGameGraph")
		.def("accessible", &get_accessible1)
		.def("accessible", &get_accessible2)
		.def("valid_vertex_id", &CGameGraph::valid_vertex_id)
		.def("vertex", &CGameGraph::vertex)
		.def("vertex_id", &CGameGraph::vertex_id)
		.def("levels", &get_levels, return_stl_iterator)
		.def("level_neighbours", &get_level_neighbours, raw<1>())
		.def("nearest_vertex", &get_nearest_vertex, raw<1>())
		.def("level_vertices", &get_level_vertices, raw<1>()),

		class_<CVertex>("GameGraph__CVertex")
		.def("level_point", &CVertex__level_point)
		.def("game_point", &CVertex__game_point)
		.def("level_id", &CVertex::level_id)
		.def("level_vertex_id", &CVertex::level_vertex_id)
	];
}
