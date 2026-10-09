////////////////////////////////////////////////////////////////////////////
//	Module 		: spawn_packs.cpp
//	Created 	: 16.09.2026
//	Description : Level packs: extra all.spawn files whose new levels, objects and paths join the base spawn
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "spawn_packs.h"
#include "game_graph.h"
#include "game_graph_builder.h"
#include "patrol_path.h"
#include "patrol_path_storage.h"
#include "xrmessages.h"

extern LPCSTR LEVEL_GRAPH_NAME;

namespace
{
	using namespace spawn_overlays;

	const LPCSTR PACK_MASK = "*.spawn";
	// Packet version whose layout peek_graph_vertex reads.
	const u16 PEEKED_SPAWN_VERSION = 128;
	static_assert(SPAWN_VERSION == PEEKED_SPAWN_VERSION, "peek_graph_vertex mirrors Spawn_Read for version 128; update both");
	static_assert(XRAI_CURRENT_VERSION == 10, "CGameGraphBuilder::readable_version lists the all.spawn layouts the pack readers walk; update it");

	// The level.ai versions the CLevelGraph constructor accepts; keep in sync with it.
	bool level_ai_loadable(const u32 version)
	{
		return version == XRAI_CURRENT_VERSION || version == XRAI_LARGE_VERSION;
	}

	void close_pack(CSpawnPacks::SPack* pack)
	{
		xr_delete(pack->graph);
		if (pack->graph_chunk)
			pack->graph_chunk->close();
		if (pack->file)
			FS.r_close(pack->file);
		xr_delete(pack);
	}

	const LPCSTR AUTO_LEVEL_ID = "auto";

	enum class EListedId
	{
		missing,
		number,
		automatic,
	};

	EListedId listed_id_of(const LPCSTR section, u32& listed_id)
	{
		if (!pGameIni->line_exist(section, "id"))
			return EListedId::missing;
		const LPCSTR value = pGameIni->r_string(section, "id");
		if (value && !xr_strcmp(value, AUTO_LEVEL_ID))
			return EListedId::automatic;
		listed_id = pGameIni->r_u32(section, "id");
		return EListedId::number;
	}

	LPCSTR listed_section(const LPCSTR level_name)
	{
		if (!pGameIni->section_exist("levels"))
			return nullptr;

		LPCSTR section, dummy;
		for (int i = 0; pGameIni->r_line("levels", i, &section, &dummy); ++i)
		{
			if (!pGameIni->line_exist(section, "name"))
				continue;
			const LPCSTR name = pGameIni->r_string(section, "name");
			if (name && *name && !xr_strcmp(name, level_name))
				return section;
		}
		return nullptr;
	}

	bool listed_level(const LPCSTR level_name, Fvector& offset, u32& listed_id, EListedId& id_state)
	{
		const LPCSTR section = listed_section(level_name);
		if (!section)
			return false;
		id_state = listed_id_of(section, listed_id);
		if (pGameIni->line_exist(section, "offset"))
			offset = pGameIni->r_fvector3(section, "offset");
		return true;
	}

	// Level ids for the game.ltx [levels] entries with id = auto whose level the base graph lacks. Reserved: every
	// base id and every numeric id of every entry, installed or not. The auto levels, sorted by name, each take the
	// highest id at or below 254 left free.
	void allocate_auto_ids(const GameGraph::CHeader& base, xr_map<shared_str, GameGraph::_LEVEL_ID>& result)
	{
		if (!pGameIni->section_exist("levels"))
			return;

		const u32 invalid = u32(GameGraph::_LEVEL_ID(-1));
		bool reserved[u32(GameGraph::_LEVEL_ID(-1)) + 1] = {};
		reserved[0] = true;
		for (const auto& level : base.levels())
			reserved[level.first] = true;

		xr_vector<shared_str> automatic;
		LPCSTR section, dummy;
		for (int i = 0; pGameIni->r_line("levels", i, &section, &dummy); ++i)
		{
			if (!pGameIni->line_exist(section, "name"))
				continue;
			const LPCSTR name = pGameIni->r_string(section, "name");
			if (!name || !*name || xr_strcmp(listed_section(name), section))
				continue;
			u32 listed_id = 0;
			switch (listed_id_of(section, listed_id))
			{
			case EListedId::number:
				if (listed_id < invalid)
					reserved[listed_id] = true;
				break;
			case EListedId::automatic:
				if (!base.level(name, true))
					automatic.push_back(name);
				break;
			default:
				break;
			}
		}

		std::sort(automatic.begin(), automatic.end(), [](const shared_str& a, const shared_str& b) { return xr_strcmp(a, b) < 0; });
		u32 next = invalid - 1;
		for (const auto& name : automatic)
		{
			while (next && reserved[next])
				--next;
			if (!next)
			{
				Msg("! [spawn_overlays] game.ltx: level '%s' has id = auto but no level id is left", *name);
				continue;
			}
			reserved[next] = true;
			result[name] = GameGraph::_LEVEL_ID(next);
			Msg("* [spawn_overlays] game.ltx: level '%s' id = auto -> %d", *name, next);
		}
	}

	bool has_bytes(NET_Packet& packet, const u32 count)
	{
		return packet.r_elapsed() >= count;
	}

	// Graph vertex of a spawn packet positioned right after o_Position, read from the bytes without
	// constructing the entity: the same fields CSE_Abstract::Spawn_Read walks, then m_tGraphID, the first field of
	// CSE_ALifeObject::STATE_Read. Lets a record the base already has be rejected without F_entity_Create (a Lua
	// constructor for script classes). Only the PEEKED_SPAWN_VERSION layout is known here: u32(-1) for an older
	// packet version (the caller then constructs the entity, Spawn_Read has the legacy branches) or one too short to
	// hold it.
	u32 peek_graph_vertex(NET_Packet& packet)
	{
		if (!has_bytes(packet, sizeof(Fvector)))
			return u32(-1);
		Fvector angle;
		packet.r_vec3(angle);
		if (!has_bytes(packet, 4 * sizeof(u16)))
			return u32(-1);
		packet.r_advance(4 * sizeof(u16)); // RespawnTime, ID, ID_Parent, ID_Phantom
		if (!has_bytes(packet, 2 * sizeof(u16)))
			return u32(-1);
		const u16 flags = packet.r_u16();
		const u16 version = packet.r_u16();
		if (!(flags & M_SPAWN_VERSION) || version != PEEKED_SPAWN_VERSION)
			return u32(-1);
		if (!has_bytes(packet, 2 * sizeof(u16) + sizeof(u16)))
			return u32(-1);
		packet.r_advance(2 * sizeof(u16)); // game type, script version
		const u16 client_data_size = packet.r_u16();
		if (!has_bytes(packet, client_data_size + 2 * sizeof(u16) + sizeof(u16)))
			return u32(-1);
		packet.r_advance(client_data_size); // client data
		packet.r_advance(2 * sizeof(u16)); // spawn id, state size
		return packet.r_u16(); // CSE_ALifeObject::m_tGraphID, first field of every alife object's state
	}

	LPCSTR pack_level_name(const CSpawnPacks::SPack& pack, u32 pack_vertex_id)
	{
		if (pack_vertex_id >= pack.vertex_map.size())
			return nullptr;
		const auto pack_level = pack.graph->header().levels().find(pack.graph->vertex(pack_vertex_id)->level_id());
		if (pack_level == pack.graph->header().levels().end())
			return nullptr;
		return *pack_level->second.name();
	}

	// Whether this object is already registered (base or an earlier pack): never on a level this pack appended,
	// otherwise the same name on the same level within same_object_present's tolerance.
	bool pack_object_present(const CSpawnPacks::SPack& pack, u32 pack_vertex_id, const SPAWN_GRAPH& spawns, const NAME_INDEX& known_index, const CGameGraph& graph, const LPCSTR name, const Fvector& position, ALife::_SPAWN_ID& match)
	{
		if (pack_vertex_id >= pack.vertex_map.size())
			return false;
		const GameGraph::_LEVEL_ID pack_level_id = pack.graph->vertex(pack_vertex_id)->level_id();
		for (const auto& level : pack.appended)
			if (level.pack_id == pack_level_id)
				return false;
		const LPCSTR level_name = pack_level_name(pack, pack_vertex_id);
		const GameGraph::SLevel* level = level_name ? graph.header().level(level_name, true) : nullptr;
		return level && same_object_present(spawns, known_index, graph, name, level->id(), position, &match);
	}

	// Records a registered object on a level the pack did not append that looks like a base template the pack's
	// build changed (SPack::drift). Templates with an id below known_end are the ones registered before this pack.
	void note_drift(CSpawnPacks::SPack& pack, const STemplates& templates, const ALife::_SPAWN_ID known_end, const NAME_INDEX& known_index, const CGameGraph& graph, const CSE_ALifeDynamicObject& object)
	{
		const GameGraph::_LEVEL_ID level_id = graph.vertex(object.m_tGraphID)->level_id();

		ALife::_SPAWN_ID nearest = ALife::_SPAWN_ID(-1);
		float best = SAME_OBJECT_TOLERANCE;
		for (auto& I : templates.spawns.vertices())
		{
			if (I.first >= known_end)
				continue;
			const CSE_ALifeObject* other = smart_cast<CSE_ALifeObject*>(&I.second->data()->object());
			if (!other || other->s_name != object.s_name || !graph.valid_vertex_id(other->m_tGraphID) || graph.vertex(other->m_tGraphID)->level_id() != level_id)
				continue;
			const float distance = other->o_Position.distance_to(object.o_Position);
			if (distance > best)
				continue;
			best = distance;
			nearest = I.first;
		}
		if (nearest != ALife::_SPAWN_ID(-1))
			pack.drift.push_back({object.m_tSpawnID, nearest, best, false});

		const auto found = known_index.find(shared_str(object.name_replace()));
		if (found == known_index.end())
			return;
		for (const auto candidate : found->second)
		{
			const CSE_ALifeObject* other = smart_cast<CSE_ALifeObject*>(&templates.spawns.vertex(candidate)->data()->object());
			if (!other || !graph.valid_vertex_id(other->m_tGraphID) || graph.vertex(other->m_tGraphID)->level_id() == level_id)
				continue;
			pack.drift.push_back({object.m_tSpawnID, candidate, 0.f, true});
			return;
		}
	}

	// Sub-chunk 1 of an all.spawn record: u16 size + M_UPDATE packet, applied to object if present.
	void read_update(IReader& record, CSE_Abstract& object)
	{
		IReader* chunk = record.open_chunk(1);
		if (!chunk)
			return;

		const u32 update_size = chunk->r_u16();
		if (update_size <= NET_PacketSizeLimit && int(update_size) <= chunk->elapsed())
		{
			NET_Packet update;
			update.B.count = update_size;
			chunk->r(update.B.data, update_size);
			u16 id;
			update.r_begin(id);
			if (id == M_UPDATE)
				object.UPDATE_Read(update);
		}
		chunk->close();
	}

	// Vertex of a pack record/point on the assembled graph: the mapped vertex for an appended or substituted
	// level, nearest vertex by position for a level the base already has. 1 ok, 0 failed (reason), -1 the level
	// is not in the graph (reason = level name).
	int resolve_vertex(const CSpawnPacks::SPack& pack, u32 pack_vertex_id, const Fvector& position, const CGameGraph& graph, u32& vertex_id, float& distance, string256& reason)
	{
		if (pack_vertex_id >= pack.vertex_map.size())
		{
			xr_strcpy(reason, "vertex is outside the pack graph");
			return 0;
		}
		if (pack.vertex_map[pack_vertex_id] != u32(-1))
		{
			vertex_id = pack.vertex_map[pack_vertex_id];
			distance = 0.f;
			return 1;
		}

		const LPCSTR level_name = pack_level_name(pack, pack_vertex_id);
		if (!level_name)
		{
			xr_strcpy(reason, "vertex references an unknown level in the pack graph");
			return 0;
		}
		const GameGraph::SLevel* level = graph.header().level(level_name, true);
		if (!level)
		{
			xr_strcpy(reason, level_name);
			return -1;
		}
		if (!graph.nearest_vertex(level->id(), position, vertex_id, distance))
		{
			xr_sprintf(reason, "level '%s' has no graph vertices", level_name);
			return 0;
		}
		return 1;
	}

	// One all.spawn record: sub-chunk 0 = u16 size + M_SPAWN packet, sub-chunk 1 = u16 size + M_UPDATE packet.
	// Sets present (match = that template) for an object already registered (base or an earlier pack) and no_level
	// when the record's own level is not (yet) in the assembled graph. Both are decided from the packet's name,
	// position and graph vertex (peek_graph_vertex) before F_entity_Create, so a record that is not going to be
	// added is never instantiated: for a script class that means its Lua constructor and STATE_Read, which may read
	// the mod's configs.
	CSE_Abstract* read_pack_record(IReader& record, const CSpawnPacks::SPack& pack, const SPAWN_GRAPH& spawns, const NAME_INDEX& known_index, const CGameGraph& graph, bool& present, ALife::_SPAWN_ID& match, bool& no_level, string256& name, string256& reason)
	{
		present = false;
		no_level = false;
		name[0] = 0;
		IReader* chunk = record.open_chunk(0);
		if (!chunk)
		{
			xr_strcpy(reason, "no spawn packet");
			return nullptr;
		}
		const u32 size = chunk->r_u16();
		if (size > NET_PacketSizeLimit || int(size) > chunk->elapsed())
		{
			chunk->close();
			xr_sprintf(reason, "spawn packet of %d bytes is corrupt", size);
			return nullptr;
		}
		NET_Packet packet;
		packet.B.count = size;
		chunk->r(packet.B.data, size);
		chunk->close();

		u16 id;
		packet.r_begin(id);
		if (id != M_SPAWN)
		{
			xr_strcpy(reason, "not an M_SPAWN packet");
			return nullptr;
		}
		string64 section;
		packet.r_stringZ_s(section);
		packet.r_stringZ_s(name);
		packet.r_u8();
		packet.r_u8();
		Fvector position;
		packet.r_vec3(position);
		const u32 pack_vertex_id = peek_graph_vertex(packet);
		if (pack_vertex_id != u32(-1) && pack_vertex_id < pack.vertex_map.size())
		{
			const LPCSTR level_name = pack_level_name(pack, pack_vertex_id);
			if (level_name && !graph.header().level(level_name, true))
			{
				no_level = true;
				xr_strcpy(reason, level_name);
				return nullptr;
			}
			if (pack_object_present(pack, pack_vertex_id, spawns, known_index, graph, name, position, match))
			{
				present = true;
				return nullptr;
			}
		}

		string256 create_reason;
		CSE_Abstract* object = create_from_packet(packet, create_reason);
		if (!object)
		{
			xr_sprintf(reason, "%s: %s", name, create_reason);
			return nullptr;
		}
		if (pack_vertex_id == u32(-1))
		{
			const CSE_ALifeObject* alife_object = smart_cast<CSE_ALifeObject*>(object);
			if (alife_object && pack_object_present(pack, alife_object->m_tGraphID, spawns, known_index, graph, name, position, match))
			{
				F_entity_Destroy(object);
				present = true;
				return nullptr;
			}
		}

		read_update(record, *object);
		return object;
	}

	CSpawnPacks::SPack* open_pack(const LPCSTR name)
	{
		IReader* file = FS.r_open("$game_spawn$", name);
		if (!file)
		{
			Msg("! [spawn_overlays] pack %s: cannot open", name);
			return nullptr;
		}

		IReader* chunk = file->open_chunk(0);
		if (!chunk)
		{
			Msg("! [spawn_overlays] pack %s: not a spawn file (no header chunk), skipped", name);
			FS.r_close(file);
			return nullptr;
		}
		const u32 version = chunk->r_u32();
		xrGUID guid{};
		chunk->r(&guid, sizeof(guid));
		chunk->close();
		if (!CGameGraphBuilder::readable_version(version))
		{
			Msg("! [spawn_overlays] pack %s: spawn version %d is not readable, skipped", name, version);
			FS.r_close(file);
			return nullptr;
		}

		chunk = file->open_chunk(4);
		CGameGraph* graph = chunk ? CGameGraphBuilder::open_graph(*chunk) : nullptr;
		if (!graph)
		{
			Msg("! [spawn_overlays] pack %s: no game graph of a readable version, skipped", name);
			if (chunk)
				chunk->close();
			FS.r_close(file);
			return nullptr;
		}

		auto* pack = xr_new<CSpawnPacks::SPack>();
		pack->file_name = name;
		pack->file = file;
		pack->graph_chunk = chunk;
		pack->guid = guid;
		pack->graph = graph;
		return pack;
	}

	// A point off the AI map (level vertex id -1) carries whatever game vertex id xrAI left in it; it is used by
	// position only and is kept as is, like the base spawn's own.
	int remap_path(const CSpawnPacks::SPack& pack, CPatrolPath& path, const CGameGraph& graph, u32& off_mesh, string256& reason)
	{
		for (auto& I : path.vertices())
		{
			CPatrolPoint& point = I.second->data();
			// level_graph = nullptr: safe to call before the level is loaded (see CPatrolPath::approximate_level).
			if (point.level_vertex_id(nullptr, nullptr, &graph) == u32(-1))
			{
				++off_mesh;
				continue;
			}
			const GameGraph::_GRAPH_ID pack_vertex_id = point.game_vertex_id(nullptr, nullptr, &graph);

			const bool exact = pack_vertex_id < pack.vertex_map.size() && pack.vertex_map[pack_vertex_id] != u32(-1);
			u32 vertex_id;
			float distance;
			const int result = resolve_vertex(pack, pack_vertex_id, point.position(), graph, vertex_id, distance, reason);
			if (result != 1)
				return result;

			point.relocate(graph, GameGraph::_GRAPH_ID(vertex_id), !exact);
		}
		return 1;
	}

	int place_pack_object(CSE_ALifeDynamicObject& object, const CSpawnPacks::SPack& pack, const CGameGraph& graph, string256& reason)
	{
		const u32 pack_vertex_id = object.m_tGraphID;
		const bool exact = pack_vertex_id < pack.vertex_map.size() && pack.vertex_map[pack_vertex_id] != u32(-1);

		u32 vertex_id;
		float distance;
		const int result = resolve_vertex(pack, pack_vertex_id, object.o_Position, graph, vertex_id, distance, reason);
		if (result != 1)
			return result;

		object.m_tGraphID = GameGraph::_GRAPH_ID(vertex_id);
		if (!exact)
		{
			object.m_tNodeID = graph.vertex(vertex_id)->level_vertex_id();
			object.m_fDistance = distance;
		}
		return 1;
	}

	// pack.legacy_vertices: the mapped vertex for an appended or substituted level, else the nearest vertex by level
	// point on the level of the same name (distance 0 for the build the base has).
	void build_legacy_vertices(CSpawnPacks::SPack& pack, const CGameGraph& graph)
	{
		const u32 count = pack.graph->header().vertex_count();
		pack.legacy_vertices.assign(count, u16(-1));
		u32 moved = 0, missing = 0;
		for (u32 pack_vertex_id = 0; pack_vertex_id < count; ++pack_vertex_id)
		{
			if (pack_vertex_id < pack.vertex_map.size() && pack.vertex_map[pack_vertex_id] != u32(-1))
			{
				pack.legacy_vertices[pack_vertex_id] = u16(pack.vertex_map[pack_vertex_id]);
				continue;
			}
			const LPCSTR level_name = pack_level_name(pack, pack_vertex_id);
			const GameGraph::SLevel* level = level_name ? graph.header().level(level_name, true) : nullptr;
			u32 vertex_id;
			float distance;
			if (!level || !graph.nearest_vertex(level->id(), pack.graph->vertex(pack_vertex_id)->level_point(), vertex_id, distance))
			{
				++missing;
				continue;
			}
			pack.legacy_vertices[pack_vertex_id] = u16(vertex_id);
			if (distance > 0.f)
				++moved;
		}
		Msg("* [spawn_overlays] pack %s: legacy save: %d vertices mapped, %d to the nearest vertex of another build, %d without a level", *pack.file_name, count - missing, moved, missing);
	}
}

namespace spawn_overlays
{
CSpawnPacks::~CSpawnPacks()
{
	for (auto* pack : m_packs)
		close_pack(pack);
	m_packs.clear();
}

void CSpawnPacks::open(const LPCSTR base_spawn_name)
{
	string_path base_file;
	xr_sprintf(base_file, "%s.spawn", base_spawn_name);
	strlwr(base_file);

	FS_FileSet files;
	FS.file_list(files, "$game_spawn$", FS_ListFiles, PACK_MASK);
	for (const auto& file : files)
	{
		const LPCSTR name = file.name.c_str();
		if (strchr(name, '\\') || !xr_strcmp(name, base_file))
			continue;

		SPack* pack = open_pack(name);
		if (!pack)
			continue;

		m_packs.push_back(pack);
		Msg("* [spawn_overlays] pack %s: %d levels, %d vertices", name, pack->graph->header().level_count(), pack->graph->header().vertex_count());
	}
}

bool CSpawnPacks::installed_level(const LPCSTR level_name, xrGUID& guid, u32& version)
{
	const shared_str key(level_name);
	if (const auto cached = m_installed_levels.find(key); cached != m_installed_levels.end())
	{
		guid = cached->second.guid;
		version = cached->second.version;
		return true;
	}

	string_path relative, file_name;
	xr_sprintf(relative, "%s\\%s", level_name, LEVEL_GRAPH_NAME);
	if (!FS.exist(file_name, "$game_levels$", relative))
		return false;
	IReader* file = FS.r_open(file_name);
	if (!file)
		return false;
	const bool ok = file->length() >= int(sizeof(hdrNODES));
	if (ok)
	{
		const auto* header = static_cast<const hdrNODES*>(file->pointer());
		guid = header->guid;
		version = header->version;
	}
	FS.r_close(file);
	if (ok)
		m_installed_levels[key] = {guid, version};
	return ok;
}

bool CSpawnPacks::cut(const LPCSTR level_name) const
{
	return std::find(m_cut_levels.begin(), m_cut_levels.end(), shared_str(level_name)) != m_cut_levels.end();
}

u32 CSpawnPacks::append_levels(CGameGraphBuilder& builder, float tolerance, const xr_vector<shared_str>& cut_levels)
{
	m_cut_levels = cut_levels;
	xr_map<shared_str, GameGraph::_LEVEL_ID> auto_ids;
	allocate_auto_ids(builder.header(), auto_ids);
	u32 changed = 0;
	for (auto* entry : m_packs)
	{
		SPack& pack = *entry;
		const LPCSTR file_name = *pack.file_name;
		pack.vertex_map.assign(pack.graph->header().vertex_count(), u32(-1));

		auto I = pack.graph->header().levels().begin();
		auto E = pack.graph->header().levels().end();
		for (; I != E; ++I)
		{
			const GameGraph::SLevel& level = I->second;
			const LPCSTR level_name = *level.name();
			const GameGraph::SLevel* present = builder.level(level_name);
			if (present && present->guid() == level.guid())
				continue;
			if (!present && cut(level_name))
			{
				Msg("* [spawn_overlays] pack %s: level '%s' is in [level_cut], not added", file_name, level_name);
				++pack.missing_levels;
				continue;
			}

			xrGUID installed{};
			u32 ai_version = 0;
			if (!installed_level(level_name, installed, ai_version))
			{
				if (!present)
				{
					Msg("- [spawn_overlays] pack %s: level '%s' is not installed (no levels\\%s\\level.ai), skipped", file_name, level_name, level_name);
					++pack.missing_levels;
				}
				continue;
			}
			if (!level_ai_loadable(ai_version))
			{
				if (!present)
				{
					Msg("! [spawn_overlays] pack %s: level '%s' has a version %d level.ai, which this engine cannot load, skipped", file_name, level_name, ai_version);
					++pack.missing_levels;
				}
				continue;
			}
			if (installed != level.guid())
			{
				if (!present)
				{
					Msg("! [spawn_overlays] pack %s: level '%s' is another build than the installed level.ai, skipped", file_name, level_name);
					++pack.missing_levels;
				}
				continue;
			}

			string256 reason;
			if (present)
			{
				GameGraph::_LEVEL_ID level_id;
				if (!builder.substitute_level(*pack.graph, level.id(), pack.vertex_map, level_id, reason))
				{
					Msg("! [spawn_overlays] pack %s: level '%s' matches the installed level.ai but the graph's build does not; cannot substitute: %s", file_name, level_name, reason);
					continue;
				}
				pack.substituted.push_back(level_id);
				++changed;
				Msg("* [spawn_overlays] pack %s: level '%s' (id %d) rebuilt from the pack to match the installed level.ai", file_name, level_name, level_id);
				continue;
			}

			Fvector offset = level.offset();
			u32 listed_id = 0;
			EListedId id_state = EListedId::missing;
			if (!listed_level(level_name, offset, listed_id, id_state))
			{
				Msg("! [spawn_overlays] pack %s: level '%s' has no [levelNN] section in game.ltx [levels], skipped", file_name, level_name);
				++pack.missing_levels;
				continue;
			}
			if (id_state == EListedId::missing)
			{
				Msg("! [spawn_overlays] pack %s: level '%s' has no id in its game.ltx [levelNN] section, skipped", file_name, level_name);
				++pack.missing_levels;
				continue;
			}
			const bool automatic = id_state == EListedId::automatic;
			if (automatic)
			{
				const auto allocated = auto_ids.find(shared_str(level_name));
				if (allocated == auto_ids.end())
				{
					Msg("! [spawn_overlays] pack %s: level '%s' has id = auto in game.ltx and no level id is left, skipped", file_name, level_name);
					++pack.missing_levels;
					continue;
				}
				listed_id = allocated->second;
			}
			else if (listed_id == 0 || listed_id >= u32(GameGraph::_LEVEL_ID(-1)))
			{
				Msg("! [spawn_overlays] pack %s: level '%s' has id %d in game.ltx, outside 1..%d, skipped", file_name, level_name, listed_id, u32(GameGraph::_LEVEL_ID(-1)) - 1);
				++pack.missing_levels;
				continue;
			}
			const GameGraph::_LEVEL_ID level_id = GameGraph::_LEVEL_ID(listed_id);
			const LPCSTR id_source = automatic ? "auto" : "from game.ltx";

			const u32 before = builder.vertex_count();
			if (!builder.append_level(*pack.graph, level.id(), level_id, offset, pack.vertex_map, reason))
			{
				Msg("! [spawn_overlays] pack %s: level '%s' (id %d %s): %s, skipped", file_name, level_name, level_id, id_source, reason);
				++pack.missing_levels;
				continue;
			}
			pack.appended.push_back({level.id(), level_id});
			++changed;
			if (level_id != level.id())
				Msg("* [spawn_overlays] pack %s: level '%s' id %d %s (%d in the pack) +%d vertices at [%.1f,%.1f,%.1f]", file_name, level_name, level_id, id_source, level.id(), builder.vertex_count() - before, VPUSH(offset));
			else
				Msg("* [spawn_overlays] pack %s: level '%s' (id %d) +%d vertices at [%.1f,%.1f,%.1f]", file_name, level_name, level_id, builder.vertex_count() - before, VPUSH(offset));
		}

		if (pack.appended.empty() && pack.substituted.empty())
			continue;
		u32 no_level_edges = 0, unresolved = 0;
		pack.edges = builder.append_edges(*pack.graph, pack.vertex_map, tolerance, no_level_edges, unresolved);
		if (no_level_edges)
			Msg("- [spawn_overlays] pack %s: %d edges lead to levels not installed, skipped", file_name, no_level_edges);
		if (unresolved)
			Msg("! [spawn_overlays] pack %s: %d edges could not be resolved (no vertex within %.1f m)", file_name, unresolved, tolerance);
	}
	return changed;
}

bool CSpawnPacks::substituted(const GameGraph::_LEVEL_ID level_id) const
{
	for (const auto* pack : m_packs)
	{
		const xr_vector<GameGraph::_LEVEL_ID>& levels = pack->substituted;
		if (std::find(levels.begin(), levels.end(), level_id) != levels.end())
			return true;
	}
	return false;
}

void CSpawnPacks::approximate_substituted(SPAWN_GRAPH& spawns, const CGameGraph& graph) const
{
	u32 count = 0, changers = 0;
	auto I = spawns.vertices().begin();
	auto E = spawns.vertices().end();
	for (; I != E; ++I)
	{
		auto* object = smart_cast<CSE_ALifeObject*>(&I->second->data()->object());
		if (!object)
			continue;
		if (graph.valid_vertex_id(object->m_tGraphID) && substituted(graph.vertex(object->m_tGraphID)->level_id()))
		{
			object->m_tNodeID = graph.vertex(object->m_tGraphID)->level_vertex_id();
			++count;
		}

		auto* changer = smart_cast<CSE_ALifeLevelChanger*>(object);
		if (!changer)
			continue;
		const GameGraph::SLevel* destination = graph.header().level(*changer->m_caLevelToChange, true);
		if (!destination || !substituted(destination->id()))
			continue;
		if (string256 reason; resolve_destination(*changer, graph, reason) != 1)
			Msg("! [spawn_overlays] %s: destination on a rebuilt level: %s", changer->name_replace(), reason);
		else
			++changers;
	}
	if (count || changers)
		Msg("* [spawn_overlays] objects: %d templates on rebuilt levels will re-snap to the new AI map, %d level changer destinations re-resolved", count, changers);
}

u32 CSpawnPacks::add_objects(STemplates& templates, const CGameGraph& graph)
{
	approximate_substituted(templates.spawns, graph);

	u32 added = 0;
	for (auto* entry : m_packs)
	{
		SPack& pack = *entry;
		const LPCSTR file_name = *pack.file_name;
		IReader* records = pack.file->open_chunk(1);
		IReader* vertices = records ? records->open_chunk(1) : nullptr;
		if (!vertices)
		{
			Msg("! [spawn_overlays] pack %s: no spawn records", file_name);
			if (records)
				records->close();
			continue;
		}

		const NAME_INDEX known_index = templates.index;
		const ALife::_SPAWN_ID known_end = templates.ids.next_spawn_id;

		if (pack.legacy)
		{
			build_legacy_vertices(pack, graph);
			IReader* header = records->open_chunk(0);
			pack.legacy_spawn_ids.assign(header ? header->r_u32() : 0, u16(-1));
			if (header)
				header->close();
		}

		u32 pack_added = 0, present_count = 0, skipped = 0, no_level = 0;
		u32 vertex_id;
		for (IReader* vertex = vertices->open_chunk_iterator(vertex_id); vertex; vertex = vertices->open_chunk_iterator(vertex_id, vertex))
		{
			IReader* record = vertex->open_chunk(1);
			if (!record)
				continue;
			ALife::_SPAWN_ID pack_spawn_id = ALife::_SPAWN_ID(-1);
			if (pack.legacy)
			{
				if (IReader* id_chunk = vertex->open_chunk(0))
				{
					pack_spawn_id = id_chunk->r_u16();
					id_chunk->close();
				}
			}
			string256 name, reason;
			bool present = false, level_missing = false;
			auto match = ALife::_SPAWN_ID(-1);
			CSE_Abstract* object = read_pack_record(*record, pack, templates.spawns, known_index, graph, present, match, level_missing, name, reason);
			record->close();
			if (!object)
			{
				if (present)
				{
					++present_count;
					if (pack_spawn_id < pack.legacy_spawn_ids.size())
						pack.legacy_spawn_ids[pack_spawn_id] = match;
				}
				else if (level_missing && cut(reason))
					++pack.cut_records;
				else if (level_missing)
				{
					Msg("- [spawn_overlays] pack %s: %s is on level '%s' which is not installed, skipped", file_name, name, reason);
					++no_level;
				}
				else
				{
					Msg("! [spawn_overlays] pack %s: %s", file_name, reason);
					++skipped;
				}
				continue;
			}

			CSE_ALifeDynamicObject* dynamic_object;
			int result = prepare_template(templates.ids, object, graph, dynamic_object, reason);
			if (result == 1)
				result = place_pack_object(*dynamic_object, pack, graph, reason);
			if (result != 1)
			{
				if (result < 0 && cut(reason))
					++pack.cut_records;
				else if (result < 0)
				{
					Msg("- [spawn_overlays] pack %s: %s is on level '%s' which is not installed, skipped", file_name, object->name_replace(), reason);
					++no_level;
				}
				else
				{
					Msg("! [spawn_overlays] pack %s: %s (%s): %s", file_name, object->name_replace(), *object->s_name, reason);
					++skipped;
				}
				F_entity_Destroy(object);
				continue;
			}

			register_template(templates, dynamic_object, file_name);
			const GameGraph::_LEVEL_ID object_level = graph.vertex(dynamic_object->m_tGraphID)->level_id();
			if (std::none_of(pack.appended.begin(), pack.appended.end(), [object_level](const SPack::SAppendedLevel& level) { return level.id == object_level; }))
				note_drift(pack, templates, known_end, known_index, graph, *dynamic_object);
			++pack_added;
			if (pack_spawn_id < pack.legacy_spawn_ids.size())
				pack.legacy_spawn_ids[pack_spawn_id] = dynamic_object->m_tSpawnID;
		}
		vertices->close();
		records->close();

		pack.objects = pack_added;
		pack.present = present_count;
		pack.no_level = no_level;
		pack.skipped = skipped;
		added += pack_added;
	}
	return added;
}

void CSpawnPacks::report_drift(const STemplates& templates, const CGameGraph& graph) const
{
	for (const auto* pack : m_packs)
		for (const SPack::SDrift& drift : pack->drift)
		{
			const auto* added = templates.spawns.vertex(drift.added);
			const auto* base = templates.spawns.vertex(drift.base);
			if (!added || !base)
				continue;
			const CSE_Abstract& object = added->data()->object();
			const CSE_Abstract& other = base->data()->object();
			const auto level_of = [&](const CSE_Abstract& o) { return *graph.header().level(graph.vertex(smart_cast<const CSE_ALifeObject*>(&o)->m_tGraphID)->level_id()).name(); };
			const LPCSTR level_name = level_of(object);
			if (drift.moved)
				Msg("! [spawn_overlays] pack %s: %s added on %s; the base has it on %s - moved? list it in [spawn_replace] if so", *pack->file_name, object.name_replace(), level_name, level_of(other));
			else
				Msg("! [spawn_overlays] pack %s: %s (%s) added %.1f m from %s on %s - the same object renamed? remove the old name in [spawn_remove] if so", *pack->file_name, object.name_replace(), *object.s_name, drift.distance, other.name_replace(), level_name);
		}
}

u32 CSpawnPacks::add_paths(CPatrolPathStorage& storage, const CGameGraph& graph)
{
	u32 total = 0;
	for (auto* entry : m_packs)
	{
		SPack& pack = *entry;
		const LPCSTR file_name = *pack.file_name;
		for (const GameGraph::_LEVEL_ID level_id : pack.substituted)
			storage.approximate_level(graph, level_id);

		IReader* chunk = pack.file->open_chunk(3);
		if (!chunk)
			continue;

		CPatrolPathStorage source;
		source.load(*chunk);
		chunk->close();

		CPatrolPathStorage::MOVED_PATHS moved;
		storage.merge(source, moved);

		u32 added = 0;
		for (auto& i : moved)
		{
			const shared_str& name = i.first;
			CPatrolPath& path = *i.second;
			string256 reason;
			const int result = remap_path(pack, path, graph, pack.off_mesh_points, reason);
			if (result == 1)
			{
				++added;
				continue;
			}
			if (result < 0 && cut(reason))
				++pack.cut_paths;
			else if (result < 0)
			{
				Msg("- [spawn_overlays] pack %s: path %s is on level '%s' which is not installed, skipped", file_name, *name, reason);
				++pack.no_level_paths;
			}
			else
				Msg("! [spawn_overlays] pack %s: path %s: %s, skipped", file_name, *name, reason);
			storage.erase(name);
		}
		pack.paths = added;
		total += added;
	}
	return total;
}

void CSpawnPacks::finish(const xrGUID& base_guid, xrGUID& result) const
{
	result = base_guid;
	for (auto* entry : m_packs)
	{
		SPack& pack = *entry;
		const LPCSTR file_name = *pack.file_name;
		const bool changes_levels = !pack.appended.empty() || !pack.substituted.empty();
		const bool has_content = changes_levels || pack.objects || pack.paths;
		if (pack.cut_records || pack.cut_paths)
			Msg("* [spawn_overlays] pack %s: %d records and %d paths on or into [level_cut] levels skipped", file_name, pack.cut_records, pack.cut_paths);
		if (!has_content)
		{
			if (pack.missing_levels || pack.no_level || pack.no_level_paths)
				Msg("- [spawn_overlays] pack %s: %d levels not installed or cut, %d records and %d paths on missing levels skipped", file_name, pack.missing_levels, pack.no_level, pack.no_level_paths);
			else if (!pack.cut_records && !pack.cut_paths)
				Msg("* [spawn_overlays] pack %s: nothing new, ignored", file_name);
			continue;
		}
		Msg("* [spawn_overlays] pack %s: +%d levels, %d rebuilt, +%d edges, +%d objects (%d in base, %d refused, %d on levels not installed), +%d paths (%d points off the AI map kept)%s", file_name, pack.appended.size(), pack.substituted.size(), pack.edges, pack.objects, pack.present, pack.skipped, pack.no_level, pack.paths, pack.off_mesh_points, changes_levels ? "" : " (save-compatible, does not change the spawn GUID)");
		if (!changes_levels)
			continue;

		const u32 name_hash = crc32(file_name, xr_strlen(file_name));
		u32 pack_hash = crc32(&pack.guid, sizeof(pack.guid), name_hash);
		// a level id taken from game.ltx instead of the pack joins the hash, so renumbering it refuses saves
		for (const auto& level : pack.appended)
		{
			if (level.id == level.pack_id)
				continue;
			const shared_str& level_name = pack.graph->header().level(level.pack_id).name();
			pack_hash = crc32(*level_name, level_name.size(), pack_hash);
			pack_hash = crc32(&level.id, sizeof(level.id), pack_hash);
		}
		if (pack.missing_levels)
			for (const auto& level : pack.appended)
			{
				const xrGUID& level_guid = pack.graph->header().level(level.pack_id).guid();
				pack_hash = crc32(&level_guid, sizeof(level_guid), pack_hash);
			}
		result.g[0] ^= (u64(name_hash) << 32) | u64(pack_hash);
		result.g[1] ^= (u64(pack_hash) << 32) | u64(crc32(&pack.guid, sizeof(pack.guid), ~name_hash));
	}
}

void CSpawnPacks::legacy_save(const xrGUID& save_guid)
{
	for (auto* pack : m_packs)
		if (pack->guid == save_guid)
		{
			pack->legacy = true;
			Msg("* [spawn_overlays] pack %s: the saved game was made with this all.spawn as the base spawn, its ids will be translated", *pack->file_name);
			return;
		}
}

bool CSpawnPacks::take_legacy(xr_vector<u16>& vertices, xr_vector<u16>& spawn_ids)
{
	for (auto* pack : m_packs)
	{
		if (!pack->legacy)
			continue;
		if (pack->missing_levels)
		{
			Msg("! [spawn_overlays] pack %s: %d of its levels are not in the graph, the saved game cannot be translated", *pack->file_name, pack->missing_levels);
			return false;
		}
		vertices.swap(pack->legacy_vertices);
		spawn_ids.swap(pack->legacy_spawn_ids);
		return true;
	}
	return false;
}
}
