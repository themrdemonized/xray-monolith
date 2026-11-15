////////////////////////////////////////////////////////////////////////////
//	Module 		: spawn_overlays.cpp
//	Created 	: 16.09.2026
//	Description : ltx overlays applied on top of the loaded all.spawn
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "spawn_overlays.h"
#include "spawn_templates.h"
#include "game_graph_builder.h"
#include "patrol_path_storage.h"
#include "restriction_space.h"

using namespace spawn_overlays;

namespace
{
	const LPCSTR OVERLAY_FILE = "spawn_overlays.ltx";
	const LPCSTR SECTION_SETTINGS = "settings";
	const LPCSTR SECTION_LINKS = "graph_links";
	const LPCSTR SECTION_UNLINKS = "graph_unlinks";
	const LPCSTR SECTION_REMOVE = "spawn_remove";
	const LPCSTR SECTION_REPLACE = "spawn_replace";
	const LPCSTR SECTION_PATH_REMOVE = "path_remove";
	const LPCSTR SECTION_LEVEL_CUT = "level_cut";
	const LPCSTR PATCH_PREFIX = "spawn_patch@";
	const LPCSTR ADD_PREFIX = "spawn_add@";
	const float DEFAULT_LINK_TOLERANCE = 2.f;

	bool overlay_path(string_path& file_name)
	{
		FS.update_path(file_name, "$game_config$", OVERLAY_FILE);
		return !!FS.exist(file_name);
	}

	bool overlay_file(string_path& file_name)
	{
		if (overlay_path(file_name))
			return true;
		Msg("* [spawn_overlays] %s not found: graph_links/graph_unlinks/spawn_remove/spawn_add/spawn_patch are off; level packs, spawns\\<level>\\*.spawn and *.game fragments, offset sync and patrol_paths.ltx 'level =' sections still apply", OVERLAY_FILE);
		return false;
	}

	// "level_name" or "level_name:x,y,z" (level point)
	struct SEndpoint
	{
		string256 level_name;
		bool has_point;
		Fvector point;
	};

	bool parse_endpoint(LPCSTR text, SEndpoint& result)
	{
		result.has_point = false;
		if (!text)
			return false;

		string256 temp;
		xr_strcpy(temp, text);
		_Trim(temp);
		LPSTR colon = strchr(temp, ':');
		if (colon)
		{
			*colon = 0;
			if (3 != sscanf(colon + 1, "%f,%f,%f", &result.point.x, &result.point.y, &result.point.z))
				return false;
			result.has_point = true;
			_Trim(temp);
		}
		if (!temp[0])
			return false;

		xr_strcpy(result.level_name, temp);
		return true;
	}

	struct SLevelOffset
	{
		GameGraph::_LEVEL_ID level_id;
		Fvector offset;
	};

	// Levels whose [levelNN] offset in game.ltx differs from the offset compiled into the graph header.
	void collect_level_offsets(const GameGraph::CHeader& header, xr_vector<SLevelOffset>& result)
	{
		if (!pGameIni->section_exist("levels"))
			return;

		LPCSTR section, dummy;
		for (int i = 0; pGameIni->r_line("levels", i, &section, &dummy); ++i)
		{
			if (!pGameIni->line_exist(section, "name") || !pGameIni->line_exist(section, "offset"))
				continue;

			const LPCSTR name = pGameIni->r_string(section, "name");
			if (!name || !*name)
				continue;
			const GameGraph::SLevel* level = header.level(name, true);
			if (!level)
				continue;

			SLevelOffset entry{};
			entry.level_id = level->id();
			entry.offset = pGameIni->r_fvector3(section, "offset");
			if (!entry.offset.similar(level->offset()))
				result.push_back(entry);
		}
	}

	// Names of the game.ltx [levels] entries; on a duplicate name the first section wins, as in the pack reader.
	xr_vector<shared_str> listed_level_names()
	{
		xr_vector<shared_str> result;
		if (!pGameIni->section_exist("levels"))
			return result;

		LPCSTR section, dummy;
		for (int i = 0; pGameIni->r_line("levels", i, &section, &dummy); ++i)
		{
			if (!pGameIni->line_exist(section, "name"))
				continue;
			const LPCSTR name = pGameIni->r_string(section, "name");
			if (!name || !*name)
				continue;
			const shared_str key(name);
			if (std::find(result.begin(), result.end(), key) == result.end())
				result.push_back(key);
		}
		return result;
	}

	// game.ltx [levels] is the level set; the base spawn and the packs must agree with it.
	void check_level_list(const GameGraph::CHeader& base, const CSpawnPacks& packs)
	{
		const xr_vector<shared_str> listed = listed_level_names();

		for (const auto& level : base.levels())
		{
			if (std::find(listed.begin(), listed.end(), level.second.name()) == listed.end())
				Msg("! [spawn_overlays] game.ltx: level '%s' is in the base spawn but has no [levelNN] section under [levels]", *level.second.name());
		}
		for (const shared_str& name : listed)
		{
			if (!base.level(*name, true) && !packs.carries(*name))
				Msg("! [spawn_overlays] game.ltx: level '%s' is listed under [levels] but neither the base spawn nor any pack provides it", *name);
		}
	}

	void apply_level_offsets(CGameGraphBuilder& builder, const xr_vector<SLevelOffset>& offsets)
	{
		for (auto [level_id, offset] : offsets)
		{
			const GameGraph::SLevel& level = builder.header().level(level_id);
			const Fvector before = level.offset();
			const u32 reweighted = builder.set_level_offset(level_id, offset);
			Msg("* [spawn_overlays] level_offsets: %s moved [%.1f,%.1f,%.1f] -> [%.1f,%.1f,%.1f], %d edges reweighted (game.ltx [levelNN] offset)", *level.name(), VPUSH(before), VPUSH(offset), reweighted);
		}
	}

	bool section_has_lines(const CInifile& ini, LPCSTR section)
	{
		return ini.section_exist(section) && !ini.r_section(section).Data.empty();
	}

	void matching_sections(CInifile& ini, const LPCSTR name, xr_vector<LPCSTR>& result)
	{
		const size_t length = xr_strlen(name);
		for (const CInifile::Sect& sect : ini.sections())
		{
			const LPCSTR section = sect.Name.c_str();
			if (0 == strncmp(section, name, length) && (section[length] == 0 || section[length] == '@') && !sect.Data.empty())
				result.push_back(section);
		}
	}

	LPCSTR line_source(CInifile& ini, LPCSTR section, LPCSTR key)
	{
		LPCSTR source = ini.DLTX_getFilenameOfLine(section, key);
		return source ? source : OVERLAY_FILE;
	}

	LPCSTR section_source(CInifile& ini, const LPCSTR section)
	{
		LPCSTR key, value;
		if (ini.r_line(section, 0, &key, &value))
			return line_source(ini, section, key);
		return OVERLAY_FILE;
	}

	void log_skip(LPCSTR source, LPCSTR section, LPCSTR key, LPCSTR reason)
	{
		Msg("! [spawn_overlays] %s [%s] %s: %s", source, section, key, reason);
	}

	void log_no_level(const LPCSTR source, const LPCSTR section, const LPCSTR key, const LPCSTR level_name, const LPCSTR other_level_name = nullptr)
	{
		if (other_level_name)
			Msg("- [spawn_overlays] %s [%s] %s: levels '%s' and '%s' are not installed, skipped", source, section, key, level_name, other_level_name);
		else
			Msg("- [spawn_overlays] %s [%s] %s: level '%s' is not installed, skipped", source, section, key, level_name);
	}

	bool setting_set(CInifile& ini, const LPCSTR key)
	{
		if (!ini.line_exist(SECTION_SETTINGS, key))
			return false;
		const LPCSTR value = ini.r_string(SECTION_SETTINGS, key);
		if (value && *value)
			return true;
		log_skip(line_source(ini, SECTION_SETTINGS, key), SECTION_SETTINGS, key, "empty value, kept the default");
		return false;
	}

	// 1 resolved, 0 failed (reason), -1 the level is not in the graph (reason = level name)
	int resolve_vertex(const CGameGraphBuilder& builder, const SEndpoint& endpoint, const float tolerance, u32& vertex_id, string256& reason)
	{
		const GameGraph::SLevel* level = builder.level(endpoint.level_name);
		if (!level)
		{
			xr_strcpy(reason, endpoint.level_name);
			return -1;
		}

		if (float distance; !builder.find_vertex(level->id(), endpoint.point, tolerance, vertex_id, distance))
		{
			if (vertex_id == u32(-1))
				xr_sprintf(reason, "level '%s' has no vertices", endpoint.level_name);
			else
				xr_sprintf(reason, "no vertex of '%s' within %.1f m of [%.1f,%.1f,%.1f] (nearest %.1f m away)", endpoint.level_name, tolerance, VPUSH(endpoint.point), distance);
			return 0;
		}
		return 1;
	}

	bool resolve_endpoints(const CGameGraphBuilder& builder, const SEndpoint& a, const SEndpoint& b, const float tolerance, u32& vertex_a, u32& vertex_b,
		const LPCSTR source, const LPCSTR section, const LPCSTR key)
	{
		string256 reason_a, reason_b;
		const int result_a = resolve_vertex(builder, a, tolerance, vertex_a, reason_a);
		const int result_b = resolve_vertex(builder, b, tolerance, vertex_b, reason_b);
		if (result_a == 1 && result_b == 1)
			return true;
		if (result_a < 0 && result_b < 0)
			log_no_level(source, section, key, reason_a, reason_b);
		else if (result_a < 0 || result_b < 0)
			log_no_level(source, section, key, result_a < 0 ? reason_a : reason_b);
		else
			log_skip(source, section, key, result_a == 1 ? reason_b : reason_a);
		return false;
	}

	void apply_links(CInifile& ini, const LPCSTR section, CGameGraphBuilder& builder, const float tolerance, u32& added)
	{
		LPCSTR key, value;
		for (int i = 0; ini.r_line(section, i, &key, &value); ++i)
		{
			const LPCSTR source = line_source(ini, section, key);
			SEndpoint a{}, b{};
			if (!parse_endpoint(key, a) || !parse_endpoint(value, b) || !a.has_point || !b.has_point)
			{
				log_skip(source, section, key, "expected 'level:x,y,z = level:x,y,z'");
				continue;
			}

			u32 vertex_a, vertex_b;
			if (!resolve_endpoints(builder, a, b, tolerance, vertex_a, vertex_b, source, section, key))
				continue;
			string256 reason;
			if (vertex_a == vertex_b)
			{
				xr_sprintf(reason, "both endpoints resolve to vertex %d", vertex_a);
				log_skip(source, section, key, reason);
				continue;
			}

			const bool had_ab = builder.has_edge(vertex_a, vertex_b);
			const bool had_ba = builder.has_edge(vertex_b, vertex_a);
			if (had_ab && had_ba)
			{
				Msg("* [spawn_overlays] %s: %s(%d) <-> %s(%d) already linked (%s)", section, a.level_name, vertex_a, b.level_name, vertex_b, source);
				continue;
			}
			if (!had_ab && !builder.add_edge(vertex_a, vertex_b))
			{
				xr_sprintf(reason, "vertex %d already has 255 edges", vertex_a);
				log_skip(source, section, key, reason);
				continue;
			}
			if (!had_ba && !builder.add_edge(vertex_b, vertex_a))
			{
				if (!had_ab)
					builder.remove_edge(vertex_a, vertex_b);
				xr_sprintf(reason, "vertex %d already has 255 edges", vertex_b);
				log_skip(source, section, key, reason);
				continue;
			}

			added += u32(!had_ab) + u32(!had_ba);
			const float distance = builder.vertex(vertex_a).data.game_point().distance_to(builder.vertex(vertex_b).data.game_point());
			Msg("* [spawn_overlays] %s: %s(%d) <-> %s(%d) d=%.1f (%s)", section, a.level_name, vertex_a, b.level_name, vertex_b, distance, source);
		}
	}

	void apply_unlinks(CInifile& ini, const LPCSTR section, CGameGraphBuilder& builder, const float tolerance, u32& removed)
	{
		LPCSTR key, value;
		for (int i = 0; ini.r_line(section, i, &key, &value); ++i)
		{
			const LPCSTR source = line_source(ini, section, key);
			SEndpoint a{}, b{};
			if (!parse_endpoint(key, a) || !parse_endpoint(value, b) || a.has_point != b.has_point)
			{
				log_skip(source, section, key, "expected 'level = level' or 'level:x,y,z = level:x,y,z'");
				continue;
			}

			u32 count;
			if (a.has_point)
			{
				u32 vertex_a, vertex_b;
				if (!resolve_endpoints(builder, a, b, tolerance, vertex_a, vertex_b, source, section, key))
					continue;
				count = builder.remove_edge(vertex_a, vertex_b) + builder.remove_edge(vertex_b, vertex_a);
				Msg("* [spawn_overlays] %s: %s(%d) <-> %s(%d) removed %d edges (%s)", section, a.level_name, vertex_a, b.level_name, vertex_b, count, source);
			}
			else
			{
				const GameGraph::SLevel* level_a = builder.level(a.level_name);
				const GameGraph::SLevel* level_b = builder.level(b.level_name);
				if (!level_a || !level_b)
				{
					if (!level_a && !level_b)
						log_no_level(source, section, key, a.level_name, b.level_name);
					else
						log_no_level(source, section, key, level_a ? b.level_name : a.level_name);
					continue;
				}
				if (level_a->id() == level_b->id())
				{
					log_skip(source, section, key, "both sides are the same level; that would remove every edge on it, use points to remove one edge");
					continue;
				}
				count = builder.remove_level_edges(level_a->id(), level_b->id());
				Msg("* [spawn_overlays] %s: %s <-> %s removed %d edges (%s)", section, a.level_name, b.level_name, count, source);
			}
			removed += count;
		}
	}

	void unescape_custom_data(const LPCSTR source, xr_string& result)
	{
		result.clear();
		for (LPCSTR p = source; *p; ++p)
		{
			if (p[0] == '\\' && p[1] == 'n')
			{
				result += '\n';
				++p;
				continue;
			}
			if (p[0] == '\\' && p[1] == '\\')
			{
				result += '\\';
				++p;
				continue;
			}
			result += *p;
		}
	}

	void set_custom_data(CSE_Abstract& object, const LPCSTR value)
	{
		object.m_ini_string = value;
		xr_delete(object.m_ini_file);
	}

	// x, y, z; result is untouched unless all three parse.
	bool read_vector(CInifile& ini, const LPCSTR section, const LPCSTR key, Fvector& result)
	{
		Fvector parsed;
		if (3 != sscanf(ini.r_string(section, key), "%f,%f,%f", &parsed.x, &parsed.y, &parsed.z))
			return false;
		result = parsed;
		return true;
	}

	// direction, custom data, story ids and flags, shared by [spawn_patch@] and [spawn_add@]. ids: the sets the
	// object's ids are claimed in, nullptr for an object not registered yet (register_template claims them).
	// 1 applied, 0 skipped (logged), -1 not one of these keys.
	int apply_object_key(CInifile& ini, const LPCSTR section, const LPCSTR key, const LPCSTR value, CSE_Abstract& object, STemplateIds* ids)
	{
		auto* alife_object = smart_cast<CSE_ALifeObject*>(&object);
		if (!xr_strcmp(key, "direction"))
		{
			if (!read_vector(ini, section, key, object.o_Angle))
			{
				log_skip(line_source(ini, section, key), section, key, "must be x, y, z; ignored");
				return 0;
			}
		}
		else if (!xr_strcmp(key, "custom_data"))
		{
			xr_string text;
			unescape_custom_data(value, text);
			set_custom_data(object, text.c_str());
		}
		else if (!xr_strcmp(key, "custom_data_cfg"))
		{
			string_path text;
			xr_sprintf(text, "[logic]\ncfg = %s", value);
			set_custom_data(object, text);
		}
		else if (!xr_strcmp(key, "story_id") && alife_object)
		{
			const auto id = ALife::_STORY_ID(ini.r_s32(section, key));
			if (!ids)
				alife_object->m_story_id = id;
			else if (!reassign_id(ids->story_ids, alife_object->m_story_id, id))
			{
				log_skip(line_source(ini, section, key), section, key, "story_id is already taken");
				return 0;
			}
		}
		else if (!xr_strcmp(key, "spawn_story_id") && alife_object)
		{
			const auto id = ALife::_SPAWN_STORY_ID(ini.r_s32(section, key));
			if (!ids)
				alife_object->m_spawn_story_id = id;
			else if (!reassign_id(ids->spawn_story_ids, alife_object->m_spawn_story_id, id))
			{
				log_skip(line_source(ini, section, key), section, key, "spawn_story_id is already taken");
				return 0;
			}
		}
		else if (!xr_strcmp(key, "object_flags") && alife_object)
			alife_object->m_flags.flags = u32(strtoul(value, nullptr, 0));
		else
			return -1;
		return 1;
	}

	// [spawn_remove] and [spawn_replace] share the syntax (name [= match_position]); the latter runs before the
	// fragments and packs so their copy of the object is taken instead of the base one.
	void apply_removes(CInifile& ini, const LPCSTR section, STemplates& templates, u32& removed)
	{
		if (!section_has_lines(ini, section))
			return;

		LPCSTR key, value;
		for (int i = 0; ini.r_line(section, i, &key, &value); ++i)
		{
			const LPCSTR source = line_source(ini, section, key);
			ALife::_SPAWN_ID spawn_id;
			string256 reason;
			if (!find_template(templates.spawns, templates.index, key, value, spawn_id, reason))
			{
				log_skip(source, section, key, reason);
				continue;
			}

			CSE_Abstract& object = templates.spawns.vertex(spawn_id)->data()->object();
			const CSE_ALifeObject* alife_object = smart_cast<CSE_ALifeObject*>(&object);
			if (alife_object && alife_object->m_spawn_story_id != INVALID_SPAWN_STORY_ID)
				Msg("! [spawn_overlays] %s [%s] %s: template has spawn_story_id %d, scripts spawning it by story id will fail", source, section, key, alife_object->m_spawn_story_id);

			Msg("* [spawn_overlays] %s: %s (%s, spawn id %d) (%s)", section, key, *object.s_name, spawn_id, source);
			remove_template(templates, key, spawn_id);
			++removed;
		}
	}

	void apply_path_removes(CInifile& ini, CPatrolPathStorage& storage, u32& removed)
	{
		if (!section_has_lines(ini, SECTION_PATH_REMOVE))
			return;

		LPCSTR key, value;
		for (int i = 0; ini.r_line(SECTION_PATH_REMOVE, i, &key, &value); ++i)
		{
			const LPCSTR source = line_source(ini, SECTION_PATH_REMOVE, key);
			if (!storage.path(key, true))
			{
				log_skip(source, SECTION_PATH_REMOVE, key, "no path with that name");
				continue;
			}
			Msg("* [spawn_overlays] %s: %s (%s)", SECTION_PATH_REMOVE, key, source);
			storage.erase(key);
			++removed;
		}
	}

	bool destination_key(const LPCSTR key)
	{
		return !xr_strcmp(key, "dest_level") || !xr_strcmp(key, "dest_position") || !xr_strcmp(key, "dest_direction");
	}

	bool has_value(CInifile& ini, const LPCSTR section, const LPCSTR key)
	{
		if (!ini.line_exist(section, key))
			return false;
		const LPCSTR value = ini.r_string(section, key);
		return value && *value;
	}

	// dest_level / dest_position / dest_direction of a level changer; returns the keys applied. A dest_level that is
	// not installed or comes without dest_position skips all three.
	u32 apply_destination(CInifile& ini, const LPCSTR section, CSE_Abstract& object, const CGameGraph& graph, const LPCSTR name)
	{
		const bool has_level = has_value(ini, section, "dest_level");
		const bool has_position = has_value(ini, section, "dest_position");
		const bool has_direction = has_value(ini, section, "dest_direction");
		if (!has_level && !has_position && !has_direction)
			return 0;
		const bool empty_level = ini.line_exist(section, "dest_level") && !has_level;
		if (empty_level)
			return 0;
		auto* changer = smart_cast<CSE_ALifeLevelChanger*>(&object);
		if (!changer)
		{
			const LPCSTR key = has_level ? "dest_level" : has_position ? "dest_position" : "dest_direction";
			log_skip(line_source(ini, section, key), section, key, "not a level changer");
			return 0;
		}

		shared_str level_name = changer->m_caLevelToChange;
		if (has_level)
		{
			const LPCSTR source = line_source(ini, section, "dest_level");
			const LPCSTR value = ini.r_string(section, "dest_level");
			if (!graph.header().level(value, true))
			{
				log_no_level(source, section, "dest_level", value);
				return 0;
			}
			if (!has_position)
			{
				log_skip(source, section, "dest_level", "needs a dest_position key, coordinates do not carry over between levels");
				return 0;
			}
			level_name = value;
		}

		u32 applied = 0;
		if (has_direction)
		{
			changer->m_tAngles = ini.r_fvector3(section, "dest_direction");
			++applied;
		}
		if (!has_position)
			return applied;

		const shared_str previous_level = changer->m_caLevelToChange;
		const Fvector previous_position = changer->m_tNextPosition;
		changer->m_caLevelToChange = level_name;
		changer->m_tNextPosition = ini.r_fvector3(section, "dest_position");
		string256 reason;
		const LPCSTR source = line_source(ini, section, "dest_position");
		const int result = resolve_destination(*changer, graph, reason);
		if (result != 1)
		{
			changer->m_caLevelToChange = previous_level;
			changer->m_tNextPosition = previous_position;
			if (result < 0)
				log_no_level(source, section, "dest_position", reason);
			else
				log_skip(source, section, "dest_position", reason);
			return applied;
		}
		Msg("* [spawn_overlays] spawn_patch: %s leads to %s(%d) [%.1f,%.1f,%.1f]", name, *level_name, changer->m_tNextGraphID, VPUSH(changer->m_tNextPosition));
		return applied + 1;
	}

	bool apply_patch(CInifile& ini, const LPCSTR section, CSE_Abstract& object, const CGameGraph& graph, const LPCSTR name, STemplateIds& ids)
	{
		auto* alife_object = smart_cast<CSE_ALifeObject*>(&object);
		const LPCSTR summary_source = section_source(ini, section);

		if (alife_object && !graph.valid_vertex_id(alife_object->m_tGraphID))
		{
			log_skip(summary_source, section, name, "template has an invalid graph vertex");
			return false;
		}

		bool relocated = false;
		Fvector position{};
		GameGraph::_LEVEL_ID level_id = alife_object ? graph.vertex(alife_object->m_tGraphID)->level_id() : 0;
		u32 applied = 0;

		if (ini.line_exist(section, "level"))
		{
			const LPCSTR source = line_source(ini, section, "level");
			const LPCSTR level_name = ini.r_string(section, "level");
			if (!level_name || !*level_name)
			{
				log_skip(source, section, "level", "missing value");
				return false;
			}
			const GameGraph::SLevel* level = graph.header().level(level_name, true);
			if (!level)
			{
				log_no_level(source, section, "level", level_name);
				return false;
			}
			if (!ini.line_exist(section, "position"))
			{
				log_skip(source, section, "level", "needs a position key, coordinates do not carry over between levels");
				return false;
			}
			level_id = level->id();
		}

		LPCSTR key, value;
		for (int i = 0; ini.r_line(section, i, &key, &value); ++i)
		{
			const LPCSTR source = line_source(ini, section, key);
			if (!value)
			{
				log_skip(source, section, key, "missing value");
				continue;
			}

			if (!xr_strcmp(key, "match_position") || !xr_strcmp(key, "level") || destination_key(key))
				continue;
			if (!xr_strcmp(key, "position"))
			{
				position = ini.r_fvector3(section, key);
				relocated = true;
				continue;
			}
			const int result = apply_object_key(ini, section, key, value, object, &ids);
			if (result < 0)
				log_skip(source, section, key, "unknown key");
			else if (result > 0)
				++applied;
		}

		if (relocated)
		{
			string256 reason;
			const LPCSTR source = line_source(ini, section, "position");
			if (!alife_object)
				log_skip(source, section, "position", "template is not an alife object");
			else if (!relocate(*alife_object, graph, level_id, position, reason))
				log_skip(source, section, "position", reason);
			else
			{
				++applied;
				Msg("* [spawn_overlays] spawn_patch: %s moved to %s(%d) [%.1f,%.1f,%.1f]", name, *graph.header().level(level_id).name(), alife_object->m_tGraphID, VPUSH(position));
			}
		}

		applied += apply_destination(ini, section, object, graph, name);

		if (applied)
			Msg("* [spawn_overlays] spawn_patch: %s (%s) %d keys applied (%s)", name, *object.s_name, applied, summary_source);
		return applied != 0;
	}

	void apply_patches(CInifile& ini, STemplates& templates, const CGameGraph& graph, u32& patched)
	{
		const size_t prefix_length = xr_strlen(PATCH_PREFIX);
		const CInifile::Root& sections = ini.sections();
		for (const auto& [Name, Data] : sections)
		{
			const LPCSTR section = Name.c_str();
			if (0 != strncmp(section, PATCH_PREFIX, prefix_length))
				continue;

			const LPCSTR name = section + prefix_length;
			const LPCSTR match_position = ini.line_exist(section, "match_position") ? ini.r_string(section, "match_position") : nullptr;
			ALife::_SPAWN_ID spawn_id;
			string256 reason;
			if (!find_template(templates.spawns, templates.index, name, match_position, spawn_id, reason))
			{
				log_skip(section_source(ini, section), section, name, reason);
				continue;
			}

			if (apply_patch(ini, section, templates.spawns.vertex(spawn_id)->data()->object(), graph, name, templates.ids))
				++patched;
		}
	}

	float changer_radius(const CSE_ALifeLevelChanger& changer)
	{
		float radius = 0.f;
		for (const CShapeData::shape_def& shape : changer.shapes)
		{
			if (shape.type == CShapeData::cfSphere)
				radius = _max(radius, shape.data.sphere.R);
			else
				radius = _max(radius, 0.5f * _max(shape.data.box.i.magnitude(), _max(shape.data.box.j.magnitude(), shape.data.box.k.magnitude())));
		}
		return radius;
	}

	void report_overlapping_changers(const STemplates& templates, const CGameGraph& graph)
	{
		struct SChanger
		{
			const CSE_ALifeLevelChanger* changer;
			GameGraph::_LEVEL_ID level_id;
			float radius;
			LPCSTR source;
		};
		xr_vector<SChanger> changers;
		for (const auto& [spawn_id, vertex] : templates.spawns.vertices())
		{
			const auto* changer = smart_cast<const CSE_ALifeLevelChanger*>(&vertex->data()->object());
			if (changer && graph.valid_vertex_id(changer->m_tGraphID))
			{
				const auto found = templates.sources.find(spawn_id);
				changers.push_back({changer, graph.vertex(changer->m_tGraphID)->level_id(), changer_radius(*changer), found == templates.sources.end() ? "base" : *found->second});
			}
		}

		for (size_t i = 0; i < changers.size(); ++i)
		{
			for (size_t j = i + 1; j < changers.size(); ++j)
			{
				const SChanger& a = changers[i];
				const SChanger& b = changers[j];
				if (a.level_id == b.level_id && a.changer->m_caLevelToChange != b.changer->m_caLevelToChange && a.changer->o_Position.distance_to(b.changer->o_Position) < a.radius + b.radius)
					Msg("! [spawn_overlays] %s: level changers %s (-> %s, %s) and %s (-> %s, %s) overlap", *graph.header().level(a.level_id).name(), a.changer->name_replace(), *a.changer->m_caLevelToChange, a.source, b.changer->name_replace(), *b.changer->m_caLevelToChange, b.source);
			}
		}
	}

	void apply_level_cuts(CInifile& ini, CGameGraphBuilder& builder, xr_vector<GameGraph::_LEVEL_ID>& cut_levels, u32& removed)
	{
		LPCSTR key, value;
		for (int i = 0; ini.r_line(SECTION_LEVEL_CUT, i, &key, &value); ++i)
		{
			const LPCSTR source = line_source(ini, SECTION_LEVEL_CUT, key);
			const GameGraph::SLevel* level = builder.level(key);
			if (!level)
			{
				Msg("- [spawn_overlays] %s [%s] %s: not in the graph, nothing to cut", source, SECTION_LEVEL_CUT, key);
				continue;
			}
			if (std::find(cut_levels.begin(), cut_levels.end(), level->id()) != cut_levels.end())
				continue;
			cut_levels.push_back(level->id());
			const u32 count = builder.isolate_level(level->id());
			removed += count;
			Msg("* [spawn_overlays] %s: %s cut off, -%d edges (%s)", SECTION_LEVEL_CUT, key, count, source);
		}
	}

	int cut_level_index(const xr_vector<GameGraph::_LEVEL_ID>& cut_levels, const GameGraph::_LEVEL_ID level_id)
	{
		const auto found = std::find(cut_levels.begin(), cut_levels.end(), level_id);
		return found == cut_levels.end() ? -1 : int(found - cut_levels.begin());
	}

	// Engine story_id / spawn_story_id and Anomaly's custom-data [story_object] story_id of a removed template.
	void log_cut_story(const LPCSTR level_name, CSE_ALifeObject& object)
	{
		CInifile& ini = object.spawn_ini();
		const LPCSTR story_name = ini.line_exist("story_object", "story_id") ? ini.r_string("story_object", "story_id") : nullptr;
		if (object.m_story_id == INVALID_STORY_ID && object.m_spawn_story_id == INVALID_SPAWN_STORY_ID && !story_name)
			return;
		Msg("! [spawn_overlays] %s %s: removed %s (%s) with story_id %d, spawn_story_id %d, story_object '%s' - scripts using it will fail", SECTION_LEVEL_CUT, level_name, object.name_replace(), *object.s_name, object.m_story_id, object.m_spawn_story_id, story_name ? story_name : "");
	}

	void cut_objects(STemplates& templates, const CGameGraph& graph, const xr_vector<GameGraph::_LEVEL_ID>& cut_levels, u32& removed)
	{
		struct SDoomed
		{
			ALife::_SPAWN_ID spawn_id;
			u32 level;
		};
		xr_vector<SDoomed> doomed;
		xr_vector<u32> on_level, changers;
		on_level.assign(cut_levels.size(), 0);
		changers.assign(cut_levels.size(), 0);

		for (auto& I : templates.spawns.vertices())
		{
			auto* object = smart_cast<CSE_ALifeObject*>(&I.second->data()->object());
			if (!object)
				continue;
			int index = graph.valid_vertex_id(object->m_tGraphID) ? cut_level_index(cut_levels, graph.vertex(object->m_tGraphID)->level_id()) : -1;
			if (index >= 0)
			{
				if (smart_cast<CSE_ALifeCreatureActor*>(object))
				{
					Msg("! [spawn_overlays] %s %s: the actor template %s is on this level, kept", SECTION_LEVEL_CUT, *graph.header().level(cut_levels[index]).name(), object->name_replace());
					continue;
				}
				++on_level[index];
			}
			else if (const auto* changer = smart_cast<CSE_ALifeLevelChanger*>(object))
			{
				const GameGraph::SLevel* destination = graph.header().level(*changer->m_caLevelToChange, true);
				index = destination ? cut_level_index(cut_levels, destination->id()) : -1;
				if (index < 0)
					continue;
				++changers[index];
			}
			else
				continue;
			doomed.push_back({I.first, u32(index)});
		}

		for (const SDoomed& entry : doomed)
		{
			auto* object = smart_cast<CSE_ALifeObject*>(&templates.spawns.vertex(entry.spawn_id)->data()->object());
			log_cut_story(*graph.header().level(cut_levels[entry.level]).name(), *object);
			string256 name;
			xr_strcpy(name, object->name_replace());
			remove_template(templates, name, entry.spawn_id);
		}
		removed += doomed.size();

		for (u32 i = 0, n = cut_levels.size(); i < n; ++i)
			Msg("* [spawn_overlays] %s %s: -%d templates on it, -%d level changers into it", SECTION_LEVEL_CUT, *graph.header().level(cut_levels[i]).name(), on_level[i], changers[i]);
	}

	struct SRetired
	{
		ALife::_SPAWN_ID spawn_id;
		shared_str name;
		shared_str section;
		shared_str story_object;
		ALife::_STORY_ID story_id;
		ALife::_SPAWN_STORY_ID spawn_story_id;
		// the rebuilt level the template stood on, or led into
		GameGraph::_LEVEL_ID level;
		bool changer;
	};

	bool is_rebuilt(const xr_vector<GameGraph::_LEVEL_ID>& rebuilt, const GameGraph::_LEVEL_ID level_id)
	{
		return std::find(rebuilt.begin(), rebuilt.end(), level_id) != rebuilt.end();
	}

	// Base templates on a rebuilt level and base level changers into one; the rebuilding pack brings its own copies.
	// The actor template is kept and placed by its position.
	void retire_rebuilt(STemplates& templates, const CGameGraph& graph, const xr_vector<GameGraph::_LEVEL_ID>& rebuilt, xr_vector<SRetired>& retired)
	{
		for (auto& I : templates.spawns.vertices())
		{
			auto* object = smart_cast<CSE_ALifeObject*>(&I.second->data()->object());
			if (!object)
				continue;
			auto* changer = smart_cast<CSE_ALifeLevelChanger*>(object);
			SRetired entry{};
			const bool placed = graph.valid_vertex_id(object->m_tGraphID);
			const GameGraph::_LEVEL_ID object_level = placed ? graph.vertex(object->m_tGraphID)->level_id() : GameGraph::_LEVEL_ID(-1);
			if (placed && is_rebuilt(rebuilt, object_level))
			{
				entry.level = object_level;
				if (smart_cast<CSE_ALifeCreatureActor*>(object))
				{
					const LPCSTR level_name = *graph.header().level(object_level).name();
					string256 reason;
					if (!relocate(*object, graph, object_level, object->o_Position, reason))
						Msg("! [spawn_overlays] rebuilt level '%s': the actor template %s cannot be placed: %s", level_name, object->name_replace(), reason);
					else
						Msg("! [spawn_overlays] rebuilt level '%s': the actor template %s is on it, kept and placed by its position", level_name, object->name_replace());
					continue;
				}
			}
			else if (changer)
			{
				const GameGraph::SLevel* destination = graph.header().level(*changer->m_caLevelToChange, true);
				if (!destination || !is_rebuilt(rebuilt, destination->id()))
					continue;
				entry.level = destination->id();
			}
			else
				continue;

			CInifile& ini = object->spawn_ini();
			entry.name = object->name_replace();
			entry.section = object->s_name;
			entry.story_object = ini.line_exist("story_object", "story_id") ? ini.r_string("story_object", "story_id") : nullptr;
			entry.story_id = object->m_story_id;
			entry.spawn_story_id = object->m_spawn_story_id;
			entry.changer = !!changer;
			entry.spawn_id = I.first;
			retired.push_back(entry);
		}

		for (const SRetired& entry : retired)
			remove_template(templates, *entry.name, entry.spawn_id);
	}

	// After the packs: a retired level changer without a copy by name, and a retired story object whose story ids
	// no template holds now or whose story_object has no copy by name. Names listed in [spawn_remove] (ini may be
	// null) are not reported.
	void report_retired(const CInifile* ini, const STemplates& templates, const CGameGraph& graph, const xr_vector<GameGraph::_LEVEL_ID>& rebuilt, const xr_vector<SRetired>& retired)
	{
		for (const GameGraph::_LEVEL_ID level_id : rebuilt)
		{
			const LPCSTR level_name = *graph.header().level(level_id).name();
			u32 count = 0, missing = 0;
			for (const SRetired& entry : retired)
			{
				if (entry.level != level_id)
					continue;
				++count;
				if (ini && ini->line_exist(SECTION_REMOVE, *entry.name))
					continue;
				const auto copy = templates.index.find(entry.name);
				const bool copied = copy != templates.index.end() && !copy->second.empty();
				missing += u32(!copied);
				if (entry.changer)
				{
					if (!copied)
						Msg("! [spawn_overlays] rebuilt level '%s': level changer %s is not in the pack, removed", level_name, *entry.name);
				}
				else if ((entry.story_id != INVALID_STORY_ID && !templates.ids.story_ids.count(entry.story_id))
					|| (entry.spawn_story_id != INVALID_SPAWN_STORY_ID && !templates.ids.spawn_story_ids.count(entry.spawn_story_id))
					|| (entry.story_object.size() && !copied))
					Msg("! [spawn_overlays] rebuilt level '%s': %s (%s) with story_id %d, spawn_story_id %d, story_object '%s' is not in the pack, removed - scripts using it will fail", level_name, *entry.name, *entry.section, entry.story_id, entry.spawn_story_id, entry.story_object.size() ? *entry.story_object : "");
			}
			Msg("* [spawn_overlays] rebuilt level '%s': -%d base templates on it and level changers into it, taken from the pack; %d not in the pack", level_name, count, missing);
		}
	}

	const LPCSTR FRAGMENT_MASK = "*.spawn";

	// One level.spawn chunk = one raw M_SPAWN packet (no size prefix, no M_UPDATE part).
	CSE_Abstract* read_record(IReader& chunk, string256& reason)
	{
		if (u32(chunk.length()) > NET_PacketSizeLimit)
		{
			xr_sprintf(reason, "record of %d bytes exceeds the packet limit", chunk.length());
			return nullptr;
		}
		if (chunk.length() == 0)
		{
			xr_strcpy(reason, "empty spawn packet");
			return nullptr;
		}

		NET_Packet packet;
		packet.B.count = chunk.length();
		chunk.r(packet.B.data, packet.B.count);

		return create_from_packet(packet, reason);
	}

	// Refused/relocated/registered as a new template of level_id; takes ownership on success (1), leaves it to
	// the caller otherwise.
	int add_template(STemplates& templates, CSE_Abstract* object, const CGameGraph& graph, const GameGraph::_LEVEL_ID level_id, const LPCSTR source, string256& reason)
	{
		CSE_ALifeDynamicObject* dynamic_object;
		const int result = prepare_template(templates.ids, object, graph, dynamic_object, reason);
		if (result != 1)
			return result;

		const Fvector position = dynamic_object->o_Position;
		if (!relocate(*dynamic_object, graph, level_id, position, reason))
			return 0;

		register_template(templates, dynamic_object, source);
		return 1;
	}

	const LPCSTR DEFAULT_CHANGER_HINT = "level_changer_invitation";
	const LPCSTR CHANGER_KEYS[] = {"dest_level", "dest_position", "dest_direction", "silent_mode", "hint"};

	bool add_key(const LPCSTR key)
	{
		static const LPCSTR keys[] = {"section", "level", "position", "box", "sphere", "restrictor_type"};
		for (const LPCSTR known : keys)
			if (!xr_strcmp(key, known))
				return true;
		for (const LPCSTR known : CHANGER_KEYS)
			if (!xr_strcmp(key, known))
				return true;
		return false;
	}

	// The value of a key [spawn_add@] cannot do without; nullptr (logged) when it is missing or empty.
	LPCSTR required_value(CInifile& ini, const LPCSTR section, const LPCSTR key)
	{
		if (has_value(ini, section, key))
			return ini.r_string(section, key);
		log_skip(ini.line_exist(section, key) ? line_source(ini, section, key) : section_source(ini, section), section, key, "missing, section skipped");
		return nullptr;
	}

	// A required key holding x, y, z; false (logged) when it is missing or does not parse.
	bool required_vector(CInifile& ini, const LPCSTR section, const LPCSTR key, Fvector& result)
	{
		if (!required_value(ini, section, key))
			return false;
		if (read_vector(ini, section, key, result))
			return true;
		log_skip(line_source(ini, section, key), section, key, "must be x, y, z; section skipped");
		return false;
	}

	bool parse_shape(CInifile& ini, const LPCSTR section, const LPCSTR key, CShapeData::shape_def& shape)
	{
		const LPCSTR value = ini.r_string(section, key);
		if (!xr_strcmp(key, "box"))
		{
			Fvector size;
			if (!read_vector(ini, section, key, size) || size.x <= 0.f || size.y <= 0.f || size.z <= 0.f)
			{
				log_skip(line_source(ini, section, key), section, key, "must be width, height, depth, all above 0; section skipped");
				return false;
			}
			shape.type = CShapeData::cfBox;
			shape.data.box.identity();
			shape.data.box.i.set(size.x, 0.f, 0.f);
			shape.data.box.j.set(0.f, size.y, 0.f);
			shape.data.box.k.set(0.f, 0.f, size.z);
			return true;
		}
		float radius;
		if (1 != sscanf(value, "%f", &radius) || radius <= 0.f)
		{
			log_skip(line_source(ini, section, key), section, key, "must be a radius above 0; section skipped");
			return false;
		}
		shape.type = CShapeData::cfSphere;
		shape.data.sphere.P.set(0.f, 0.f, 0.f);
		shape.data.sphere.R = radius;
		return true;
	}

	void set_changer_hint(CSE_ALifeLevelChanger& changer, const LPCSTR hint)
	{
		NET_Packet packet;
		packet.write_start();
		changer.CSE_ALifeLevelChanger::STATE_Write(packet);
		packet.w_u8(1);
		packet.w_stringZ(hint);
		packet.w_u16(u16(1 + xr_strlen(hint) + 1));
		packet.read_start();
		changer.STATE_Read(packet, u16(packet.B.count));
	}

	LPCSTR template_level_name(const CSE_Abstract& object, const CGameGraph& graph)
	{
		const auto* alife_object = smart_cast<const CSE_ALifeObject*>(&object);
		if (!alife_object || !graph.valid_vertex_id(alife_object->m_tGraphID))
			return "?";
		return *graph.header().level(graph.vertex(alife_object->m_tGraphID)->level_id()).name();
	}

	bool apply_shape(CInifile& ini, const LPCSTR section, CSE_Abstract& object)
	{
		const bool has_box = has_value(ini, section, "box");
		const bool has_sphere = has_value(ini, section, "sphere");
		if (auto* shaped = smart_cast<CSE_Shape*>(&object))
		{
			if (has_box == has_sphere)
			{
				log_skip(section_source(ini, section), section, "box", has_box ? "box and sphere are both given, section skipped" : "a shaped object needs box or sphere, section skipped");
				return false;
			}
			CShapeData::shape_def shape;
			if (!parse_shape(ini, section, has_box ? "box" : "sphere", shape))
				return false;
			shaped->shapes.clear();
			shaped->shapes.push_back(shape);
			return true;
		}
		if (has_box)
			log_skip(line_source(ini, section, "box"), section, "box", "not a shaped object, ignored");
		if (has_sphere)
			log_skip(line_source(ini, section, "sphere"), section, "sphere", "not a shaped object, ignored");
		return true;
	}

	void apply_restrictor_type(CInifile& ini, const LPCSTR section, CSE_Abstract& object)
	{
		if (!has_value(ini, section, "restrictor_type"))
			return;
		const LPCSTR source = line_source(ini, section, "restrictor_type");
		auto* restrictor = smart_cast<CSE_ALifeSpaceRestrictor*>(&object);
		if (!restrictor)
		{
			log_skip(source, section, "restrictor_type", "not a space restrictor, ignored");
			return;
		}

		static const std::pair<LPCSTR, u8> types[] = {
			{"default_none", RestrictionSpace::eDefaultRestrictorTypeNone},
			{"default_out", RestrictionSpace::eDefaultRestrictorTypeOut},
			{"default_in", RestrictionSpace::eDefaultRestrictorTypeIn},
			{"none", RestrictionSpace::eRestrictorTypeNone},
			{"in", RestrictionSpace::eRestrictorTypeIn},
			{"out", RestrictionSpace::eRestrictorTypeOut},
		};
		const LPCSTR value = ini.r_string(section, "restrictor_type");
		for (const auto& [name, id] : types)
		{
			if (!xr_strcmp(value, name))
			{
				restrictor->m_space_restrictor_type = id;
				return;
			}
		}
		log_skip(source, section, "restrictor_type", "must be default_none, default_out, default_in, none, in or out; ignored");
	}

	bool apply_changer(CInifile& ini, const LPCSTR section, CSE_Abstract& object)
	{
		auto* changer = smart_cast<CSE_ALifeLevelChanger*>(&object);
		if (!changer)
		{
			for (const LPCSTR key : CHANGER_KEYS)
				if (has_value(ini, section, key))
					log_skip(line_source(ini, section, key), section, key, "not a level changer, ignored");
			return true;
		}

		Fvector dest_position;
		const LPCSTR dest_level = required_value(ini, section, "dest_level");
		if (!dest_level || !required_vector(ini, section, "dest_position", dest_position))
			return false;

		changer->m_caLevelToChange = dest_level;
		changer->m_tNextPosition = dest_position;
		if (has_value(ini, section, "dest_direction"))
		{
			Fvector dest_direction;
			if (read_vector(ini, section, "dest_direction", dest_direction))
				changer->m_tAngles = dest_direction;
			else
				log_skip(line_source(ini, section, "dest_direction"), section, "dest_direction", "must be x, y, z; ignored");
		}
		if (has_value(ini, section, "silent_mode"))
			changer->m_bSilentMode = ini.r_bool(section, "silent_mode");
		set_changer_hint(*changer, has_value(ini, section, "hint") ? ini.r_string(section, "hint") : DEFAULT_CHANGER_HINT);
		return true;
	}

	bool apply_add(CInifile& ini, const LPCSTR section, const LPCSTR name, STemplates& templates, const CGameGraph& graph)
	{
		const LPCSTR source = section_source(ini, section);
		if (!*name)
		{
			log_skip(source, section, "", "the section needs an object name after spawn_add@, skipped");
			return false;
		}
		const LPCSTR object_section = required_value(ini, section, "section");
		const LPCSTR level_name = required_value(ini, section, "level");
		Fvector position;
		const bool has_position = required_vector(ini, section, "position", position);
		if (!object_section || !level_name || !has_position)
			return false;

		const GameGraph::SLevel* level = graph.header().level(level_name, true);
		if (!level)
		{
			log_no_level(line_source(ini, section, "level"), section, "level", level_name);
			return false;
		}

		if (same_object_present(templates.spawns, templates.index, graph, name, level->id(), position))
			return false;
		if (const auto found = templates.index.find(shared_str(name)); found != templates.index.end() && !found->second.empty())
		{
			const CSE_Abstract& other = templates.spawns.vertex(found->second.front())->data()->object();
			string256 reason;
			xr_sprintf(reason, "name already used by %s on %s [%.1f,%.1f,%.1f], skipped", other.name_replace(), template_level_name(other, graph), VPUSH(other.o_Position));
			log_skip(source, section, name, reason);
			return false;
		}

		if (!pSettings->section_exist(object_section) || !pSettings->line_exist(object_section, "class"))
		{
			log_skip(line_source(ini, section, "section"), section, "section", "not an object section of system.ltx, section skipped");
			return false;
		}
		CSE_Abstract* object = F_entity_Create(object_section);
		if (!object)
		{
			log_skip(line_source(ini, section, "section"), section, "section", "no server object for this class, section skipped");
			return false;
		}
		const auto refuse = [&]() {
			F_entity_Destroy(object);
			return false;
		};
		object->s_RP = 0xff;
		object->m_wVersion = SPAWN_VERSION;
		object->set_name_replace(name);
		object->o_Position = position;

		if (!apply_shape(ini, section, *object) || !apply_changer(ini, section, *object))
			return refuse();

		apply_restrictor_type(ini, section, *object);

		LPCSTR key, value;
		for (int i = 0; ini.r_line(section, i, &key, &value); ++i)
		{
			if (!value || !*value)
			{
				log_skip(line_source(ini, section, key), section, key, "missing value");
				continue;
			}
			if (add_key(key))
				continue;
			if (apply_object_key(ini, section, key, value, *object, nullptr) < 0)
				log_skip(line_source(ini, section, key), section, key, "unknown key");
		}

		string256 reason;
		if (const int result = add_template(templates, object, graph, level->id(), source, reason); result != 1)
		{
			if (result < 0)
				log_no_level(line_source(ini, section, "dest_level"), section, "dest_level", reason);
			else
				log_skip(source, section, name, reason);
			return refuse();
		}

		string256 destination = "";
		if (const auto* changer = smart_cast<CSE_ALifeLevelChanger*>(object))
			xr_sprintf(destination, " leads to %s(%d) [%.1f,%.1f,%.1f]", *changer->m_caLevelToChange, changer->m_tNextGraphID, VPUSH(changer->m_tNextPosition));
		Msg("* [spawn_overlays] spawn_add: %s (%s) on %s(%d) [%.1f,%.1f,%.1f]%s (%s)", name, object_section, level_name, smart_cast<CSE_ALifeObject*>(object)->m_tGraphID, VPUSH(position), destination, source);
		return true;
	}

	void apply_adds(CInifile& ini, STemplates& templates, const CGameGraph& graph, u32& added)
	{
		const size_t prefix_length = xr_strlen(ADD_PREFIX);
		for (const auto& [Name, Data] : ini.sections())
		{
			const LPCSTR section = Name.c_str();
			if (0 == strncmp(section, ADD_PREFIX, prefix_length) && apply_add(ini, section, section + prefix_length, templates, graph))
				++added;
		}
	}

	const GameGraph::SLevel* fragment_level(const LPCSTR relative, const CGameGraph& graph, string256& level_name)
	{
		const LPCSTR separator = strchr(relative, '\\');
		if (!separator || strchr(separator + 1, '\\'))
			return nullptr;

		strncpy_s(level_name, relative, size_t(separator - relative));
		const GameGraph::SLevel* level = graph.header().level(level_name, true);
		if (!level)
			Msg("- [spawn_overlays] spawns\\%s: level '%s' is not installed, skipped", relative, level_name);
		return level;
	}

	void apply_fragments(STemplates& templates, const CGameGraph& graph, u32& added)
	{
		FS_FileSet files;
		FS.file_list(files, "$game_spawn$", FS_ListFiles, FRAGMENT_MASK);

		for (const auto& I : files)
		{
			const LPCSTR relative = I.name.c_str();
			string256 level_name;
			const GameGraph::SLevel* level = fragment_level(relative, graph, level_name);
			if (!level)
				continue;

			IReader* file = FS.r_open("$game_spawn$", relative);
			if (!file)
			{
				Msg("! [spawn_overlays] spawns\\%s: cannot open", relative);
				continue;
			}

			u32 file_added = 0, present = 0, skipped = 0, graph_points = 0;
			u32 chunk_id;
			for (IReader* chunk = file->open_chunk_iterator(chunk_id); chunk; chunk = file->open_chunk_iterator(chunk_id, chunk))
			{
				string256 reason;
				CSE_Abstract* object = read_record(*chunk, reason);
				if (!object)
				{
					Msg("! [spawn_overlays] spawns\\%s #%d: %s", relative, chunk_id, reason);
					++skipped;
					continue;
				}
				if (smart_cast<CSE_ALifeGraphPoint*>(object))
				{
					F_entity_Destroy(object);
					++graph_points;
					continue;
				}
				if (same_object_present(templates.spawns, templates.index, graph, object->name_replace(), level->id(), object->o_Position))
				{
					F_entity_Destroy(object);
					++present;
					continue;
				}
				const int result = add_template(templates, object, graph, level->id(), relative, reason);
				if (result != 1)
				{
					if (result < 0)
						Msg("- [spawn_overlays] spawns\\%s: %s is on level '%s' which is not installed, skipped", relative, object->name_replace(), reason);
					else
						Msg("! [spawn_overlays] spawns\\%s: %s (%s): %s", relative, object->name_replace(), *object->s_name, reason);
					F_entity_Destroy(object);
					++skipped;
					continue;
				}
				++file_added;
			}
			FS.r_close(file);

			added += file_added;
			Msg("* [spawn_overlays] spawns\\%s: +%d templates on %s, %d already present, %d skipped, %d graph points ignored", relative, file_added, level_name, present, skipped, graph_points);
		}
	}

	const LPCSTR PATH_FRAGMENT_MASK = "*.game";

	// SDK level.game files: spawns\<level>\<anything>.game. A path whose name storage already has is skipped
	// silently, so a whole-level export adds only the level's new paths.
	void apply_path_fragments(CPatrolPathStorage& storage, const CGameGraph& graph, u32& added)
	{
		FS_FileSet files;
		FS.file_list(files, "$game_spawn$", FS_ListFiles, PATH_FRAGMENT_MASK);

		for (const auto& I : files)
		{
			const LPCSTR relative = I.name.c_str();
			string256 level_name;
			const GameGraph::SLevel* level = fragment_level(relative, graph, level_name);
			if (!level)
				continue;

			IReader* file = FS.r_open("$game_spawn$", relative);
			if (!file)
			{
				Msg("! [spawn_overlays] spawns\\%s: cannot open", relative);
				continue;
			}
			CPatrolPathStorage source;
			const u32 skipped = source.load_fragment(graph, level->id(), *file, relative);
			FS.r_close(file);

			CPatrolPathStorage::MOVED_PATHS moved;
			storage.merge(source, moved);
			added += moved.size();
			Msg("* [spawn_overlays] spawns\\%s: +%d paths on %s, %d already present, %d skipped", relative, moved.size(), level_name, source.patrol_paths().size(), skipped);
		}
	}
}

const xr_vector<shared_str>& spawn_overlays::level_names()
{
	static xr_vector<shared_str> names;
	static bool built = false;
	if (built)
		return names;
	built = true;

	names = listed_level_names();
	u32 cut = 0;
	string_path file_name;
	if (overlay_path(file_name))
	{
		CInifile ini(file_name, TRUE);
		const bool cuts = section_has_lines(ini, SECTION_LEVEL_CUT);
		LPCSTR key, value;
		for (int i = 0; cuts && ini.r_line(SECTION_LEVEL_CUT, i, &key, &value); ++i)
		{
			const auto found = std::find(names.begin(), names.end(), shared_str(key));
			if (found == names.end())
				continue;
			names.erase(found);
			++cut;
		}
	}
	std::sort(names.begin(), names.end(), [](const shared_str& a, const shared_str& b) { return xr_strcmp(a, b) < 0; });
	Msg("* [spawn_overlays] level list: %d levels (%d cut)", names.size(), cut);
	return names;
}

CSpawnOverlays::CSpawnOverlays(const LPCSTR base_spawn_name)
{
	string_path file_name;
	m_ini = overlay_file(file_name) ? xr_new<CInifile>(file_name, TRUE) : nullptr;

	m_sync_offsets = true;
	if (m_ini && setting_set(*m_ini, "sync_level_offsets"))
		m_sync_offsets = !!m_ini->r_bool(SECTION_SETTINGS, "sync_level_offsets");

	m_link_tolerance = DEFAULT_LINK_TOLERANCE;
	if (m_ini && setting_set(*m_ini, "link_tolerance"))
		m_link_tolerance = m_ini->r_float(SECTION_SETTINGS, "link_tolerance");

	m_packs.open(base_spawn_name);
}

CSpawnOverlays::~CSpawnOverlays()
{
	xr_delete(m_ini);
}

CGameGraph* CSpawnOverlays::apply_graph(const CGameGraph& base, void*& buffer)
{
	check_level_list(base.header(), m_packs);

	xr_vector<SLevelOffset> offsets;
	if (m_sync_offsets)
		collect_level_offsets(base.header(), offsets);

	xr_vector<LPCSTR> link_sections, unlink_sections;
	if (m_ini)
	{
		matching_sections(*m_ini, SECTION_LINKS, link_sections);
		matching_sections(*m_ini, SECTION_UNLINKS, unlink_sections);
	}
	const bool cuts = m_ini && section_has_lines(*m_ini, SECTION_LEVEL_CUT);
	if (offsets.empty() && link_sections.empty() && unlink_sections.empty() && m_packs.empty() && !cuts)
		return nullptr;

	xr_vector<shared_str> cut_names;
	LPCSTR key, value;
	for (int i = 0; cuts && m_ini->r_line(SECTION_LEVEL_CUT, i, &key, &value); ++i)
		cut_names.push_back(key);

	CGameGraphBuilder builder(base);
	u32 added = 0, removed = 0;
	// offsets before the packs: the sync reweights the base edges of a moved level, pack edges keep their weights
	apply_level_offsets(builder, offsets);
	const u32 levels = m_packs.append_levels(builder, m_link_tolerance, cut_names);
	for (const LPCSTR section : unlink_sections)
		apply_unlinks(*m_ini, section, builder, m_link_tolerance, removed);
	for (const LPCSTR section : link_sections)
		apply_links(*m_ini, section, builder, m_link_tolerance, added);
	if (cuts)
		apply_level_cuts(*m_ini, builder, m_cut_levels, removed);

	if (!builder.dirty())
		return nullptr;

	Msg("* [spawn_overlays] graph: +%d levels, %d levels moved, +%d edges, -%d edges, %d vertices", levels, offsets.size(), added, removed, builder.vertex_count());
	return builder.build(buffer);
}

void CSpawnOverlays::apply_objects(CALifeSpawnRegistry::SPAWN_GRAPH& spawns, const CGameGraph& graph)
{
	STemplates templates(spawns);

	u32 removed = 0, patched = 0, added = 0;
	const xr_vector<GameGraph::_LEVEL_ID> rebuilt = m_packs.rebuilt_levels();
	xr_vector<SRetired> retired;
	if (m_ini)
		apply_removes(*m_ini, SECTION_REPLACE, templates, removed);
	if (!rebuilt.empty())
		retire_rebuilt(templates, graph, rebuilt, retired);
	apply_fragments(templates, graph, added);
	added += m_packs.add_objects(templates, graph);
	if (m_ini)
	{
		apply_removes(*m_ini, SECTION_REMOVE, templates, removed);
		apply_adds(*m_ini, templates, graph, added);
	}
	report_retired(m_ini, templates, graph, rebuilt, retired);
	removed += retired.size();
	if (m_ini)
		apply_patches(*m_ini, templates, graph, patched);
	if (!m_cut_levels.empty())
		cut_objects(templates, graph, m_cut_levels, removed);
	m_packs.report_drift(templates, graph);
	report_overlapping_changers(templates, graph);

	if (removed || patched || added)
		Msg("* [spawn_overlays] objects: -%d templates, %d patched, +%d added, %d remaining", removed, patched, added, spawns.vertex_count());
}

void CSpawnOverlays::add_paths(CPatrolPathStorage& storage, const CGameGraph& graph)
{
	u32 fragment_paths = 0;
	apply_path_fragments(storage, graph, fragment_paths);
	if (fragment_paths)
		Msg("* [spawn_overlays] paths: +%d from spawns\\<level>\\*.game fragments", fragment_paths);
	if (const u32 added = m_packs.add_paths(storage, graph))
		Msg("* [spawn_overlays] paths: +%d from packs", added);
	u32 removed = 0;
	if (m_ini)
		apply_path_removes(*m_ini, storage, removed);
	if (removed)
		Msg("* [spawn_overlays] paths: -%d removed", removed);
	for (const GameGraph::_LEVEL_ID level_id : m_cut_levels)
		Msg("* [spawn_overlays] %s %s: -%d paths", SECTION_LEVEL_CUT, *graph.header().level(level_id).name(), storage.erase_level(graph, level_id));
}

void CSpawnOverlays::spawn_guid(const xrGUID& base, xrGUID& result) const
{
	m_packs.finish(base, result);
}

void CSpawnOverlays::legacy_save(const xrGUID& save_guid)
{
	m_packs.legacy_save(save_guid);
}

bool CSpawnOverlays::take_legacy(xr_vector<u16>& vertices, xr_vector<u16>& spawn_ids)
{
	return m_packs.take_legacy(vertices, spawn_ids);
}
