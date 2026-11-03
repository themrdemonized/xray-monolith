////////////////////////////////////////////////////////////////////////////
//	Module 		: ai_debug_draw.cpp
//	Description : DEBUG_DRAW visualisations of NPC perception, memory, aiming and firing
//	              (see ai_debug_draw.h for the console commands)
////////////////////////////////////////////////////////////////////////////

#include "pch_script.h"
#include "ai_debug_draw.h"

#if defined(DEBUG) || defined(DEBUG_DRAW)

#include "ai/stalker/ai_stalker.h"
#include "CustomMonster.h"
#include "ai_monster_space.h"
#include "memory_manager.h"
#include "memory_space.h"
#include "visual_memory_manager.h"
#include "sound_memory_manager.h"
#include "hit_memory_manager.h"
#include "enemy_manager.h"
#include "agent_manager.h"
#include "agent_member_manager.h"
#include "stalker_planner.h"
#include "action_planner_action.h"
#include "object_handler.h"
#include "object_handler_planner.h"
#include "object_handler_space.h"
#include "sight_manager_space.h"
#include "stalker_movement_manager_smart_cover.h"
#include "inventory.h"
#include "weapon.h"
#include "actor.h"
#include "level.h"
#include "debug_renderer.h"
#include "ui_base.h"
#include "ai_sounds.h"
#include "../xrEngine/GameFont.h"

Flags32 psAI_DrawFlags = {0};
float psAI_DrawNearbyDistance = 25.f;

extern float g_ai_aim_fire_angle;
extern float g_ai_vision_speed_boost;
extern float g_ai_fire_range_extension;
extern float g_ai_fire_max_height_diff;
extern float g_ai_fire_min_dist;
extern u32 g_ai_fire_make_sense_interval;

namespace ai_debug_draw
{
namespace
{
	u32 const shot_life_time = 2500;	// ms a bullet ray stays on screen
	u32 const sound_life_time = 2000;	// ms a heard sound stays on screen
	u32 const aim_stale_time = 1000;	// ms after which the recorded aim point is not drawn
	u32 const max_events = 512;			// cap for stored shots / sounds
	float const text_height = 14.f;
	float const text_anchor_height = 2.6f;	// text block bottom, metres above the NPC origin (ai_dbg_vision is at 2.5)

	u32 const color_white = 0xffffffff;
	u32 const color_light = 0xffc8c8c8;
	u32 const color_grey = 0xff808080;
	u32 const color_red = 0xffff2020;
	u32 const color_green = 0xff20ff20;
	u32 const color_yellow = 0xffffff00;
	u32 const color_orange = 0xffff9000;
	u32 const color_blue = 0xff4080ff;
	u32 const color_cyan = 0xff00ffff;
	u32 const color_magenta = 0xffff00ff;

	enum EShotVisibility
	{
		eShotNoEnemy = 0,
		eShotSeen,		// visible_right_now(enemy)
		eShotGrace,		// visible_now(enemy) only: still_visible_time grace or a squad-shared record
		eShotUnseen,	// not even visible_now
	};

	struct SNpcState
	{
		// ai_draw_aim
		u32 aim_time;
		u32 sight_type;
		EAimSource aim_source;
		Fvector aim_point;
		Fvector aim_lead;
		// ai_draw_fire
		u32 fire_time;
		float fire_yaw_difference;
		bool fire_requested;
		u32 shots_seen;
		u32 shots_grace;
		u32 shots_unseen;
		// ai_dbg_visibility_log
		bool log_initialized;
		u16 log_target;
		bool log_right_now;
		bool log_now;
		u32 log_lost_time;
		u32 log_grace_shots;
		u32 log_unseen_shots;

		SNpcState()
		{
			aim_time = 0;
			sight_type = u32(-1);
			aim_source = eAimSourceNone;
			aim_point.set(0.f, 0.f, 0.f);
			aim_lead.set(0.f, 0.f, 0.f);
			fire_time = 0;
			fire_yaw_difference = 0.f;
			fire_requested = false;
			shots_seen = 0;
			shots_grace = 0;
			shots_unseen = 0;
			log_initialized = false;
			log_target = u16(-1);
			log_right_now = false;
			log_now = false;
			log_lost_time = 0;
			log_grace_shots = 0;
			log_unseen_shots = 0;
		}
	};

	struct SShot
	{
		u16 npc;
		Fvector from;
		Fvector to;
		bool hit_static;
		EShotVisibility visibility;
		u32 time;
	};

	struct SSound
	{
		u16 npc;
		Fvector from;
		Fvector to;
		shared_str source_name;
		u32 type;
		float power;
		float threshold;
		float occlusion;
		bool fake_hit;
		bool remembered;			// a sound record was written for this sound
		Fvector remembered_position;	// what was written into it
		u32 time;
	};

	// filled by on_sound_remembered during CSoundMemoryManager::feel_sound_new, consumed by on_sound
	struct SPendingRemember
	{
		u16 npc;
		Fvector position;
		bool valid;
	};

	struct SRefresh
	{
		u16 refresher;
		u32 time;
	};

	typedef xr_map<u16, SNpcState> NPC_STATES;
	typedef std::pair<const void*, u16> REFRESH_KEY;
	typedef xr_map<REFRESH_KEY, SRefresh> REFRESHES;

	NPC_STATES g_npcs;
	xr_vector<SShot> g_shots;
	xr_vector<SSound> g_sounds;
	REFRESHES g_refreshes;
	SPendingRemember g_pending_remember;	// static storage: zero-initialised, valid == false

	IC u32 now()
	{
		return (Device.dwTimeGlobal);
	}

	IC char sign(bool value)
	{
		return (value ? '+' : '-');
	}

	IC SNpcState& state(const CObject* npc)
	{
		return (g_npcs[npc->ID()]);
	}

	IC SNpcState* find_state(const CObject* npc)
	{
		NPC_STATES::iterator I = g_npcs.find(npc->ID());
		return ((I == g_npcs.end()) ? 0 : &(*I).second);
	}

	IC CDebugRenderer& renderer()
	{
		return (Level().debug_renderer());
	}

	IC void line(const Fvector& from, const Fvector& to, u32 color)
	{
		renderer().draw_line(Fidentity, from, to, color, false);
	}

	IC void box(const Fvector& center, float half_size, u32 color)
	{
		renderer().draw_aabb(center, half_size, half_size, half_size, color, false);
	}

	u32 count_bits(u64 value)
	{
		u32 result = 0;
		for (; value; value &= value - 1)
			++result;
		return (result);
	}

	// first static hit along the ray, or the ray end
	Fvector trace_static(const Fvector& from, const Fvector& direction, float range, bool& hit, CObject* ignore)
	{
		collide::rq_result result;
		hit = !!Level().ObjectSpace.RayPick(from, direction, range, collide::rqtStatic, result, ignore);
		return (Fvector().mad(from, direction, hit ? result.range : range));
	}

	// the target an NPC is reasoning about: its selected enemy, otherwise the actor
	const CEntityAlive* target_of(CAI_Stalker* npc)
	{
		const CEntityAlive* enemy = npc->memory().enemy().selected();
		if (enemy)
			return (enemy);
		CActor* actor = Actor();
		return (actor);
	}

	// the position memory records store for an object (see CObjectParams::fill)
	Fvector memory_position(const CGameObject* object)
	{
		Fvector result;
		object->Center(result);
		result.x = object->Position().x;
		result.z = object->Position().z;
		return (result);
	}

	const Feel::Vision::feel_visible_Item* feel_item(CCustomMonster* npc, const CObject* object)
	{
		xr_vector<Feel::Vision::feel_visible_Item>::const_iterator I = npc->feel_visible.begin();
		xr_vector<Feel::Vision::feel_visible_Item>::const_iterator E = npc->feel_visible.end();
		for (; I != E; ++I)
			if ((*I).O == object)
				return (&*I);
		return (0);
	}

	LPCSTR trace_result_name(u8 result)
	{
		switch (result)
		{
		case Feel::dbg_trace_none: return ("not traced yet");
		case Feel::dbg_trace_visible: return ("clear");
		case Feel::dbg_trace_attenuated: return ("through semi-transparent");
		case Feel::dbg_trace_near: return ("point blank");
		case Feel::dbg_trace_blocked: return ("BLOCKED");
		case Feel::dbg_trace_blocked_cached_tri: return ("BLOCKED (cached triangle)");
		case Feel::dbg_trace_blocked_object: return ("BLOCKED (visible-for-AI object)");
		case Feel::dbg_trace_no_cform: return ("target has no cform");
		}
		return ("?");
	}

	u32 trace_result_color(u8 result)
	{
		switch (result)
		{
		case Feel::dbg_trace_visible:
		case Feel::dbg_trace_near: return (color_green);
		case Feel::dbg_trace_attenuated: return (color_yellow);
		case Feel::dbg_trace_blocked:
		case Feel::dbg_trace_blocked_cached_tri: return (color_red);
		case Feel::dbg_trace_blocked_object: return (color_orange);
		}
		return (color_grey);
	}

	LPCSTR sight_type_name(u32 sight_type)
	{
		switch (sight_type)
		{
		case SightManager::eSightTypeCurrentDirection: return ("current direction");
		case SightManager::eSightTypePathDirection: return ("path direction");
		case SightManager::eSightTypeDirection: return ("direction");
		case SightManager::eSightTypePosition: return ("position");
		case SightManager::eSightTypeObject: return ("object");
		case SightManager::eSightTypeCover: return ("cover");
		case SightManager::eSightTypeSearch: return ("search");
		case SightManager::eSightTypeLookOver: return ("look over");
		case SightManager::eSightTypeCoverLookOver: return ("cover look over");
		case SightManager::eSightTypeFireObject: return ("fire object");
		case SightManager::eSightTypeFirePosition: return ("fire position");
		case SightManager::eSightTypeAnimationDirection: return ("animation direction");
		}
		return ("?");
	}

	LPCSTR aim_source_name(EAimSource source)
	{
		switch (source)
		{
		case eAimSourceNone: return ("no aim point");
		case eAimSourceHeld: return ("HELD (target not in raw vision list)");
		case eAimSourceLiveBone: return ("LIVE aim bone");
		case eAimSourceVisPoint: return ("vision ray point");
		case eAimSourceObjectCenter: return ("LIVE object position");
		case eAimSourcePosition: return ("explicit / remembered position");
		}
		return ("?");
	}

	u32 aim_source_color(EAimSource source)
	{
		switch (source)
		{
		case eAimSourceHeld: return (color_orange);
		case eAimSourceLiveBone:
		case eAimSourceObjectCenter: return (color_yellow);
		case eAimSourceVisPoint: return (color_green);
		case eAimSourcePosition: return (color_blue);
		}
		return (color_grey);
	}

	void sound_type_name(u32 type, LPSTR result, u32 result_size)
	{
		struct SName
		{
			u32 mask;
			LPCSTR name;
		};

		static SName const categories[] = {
			{u32(SOUND_TYPE_WEAPON), "weapon"}, {u32(SOUND_TYPE_ITEM), "item"}, {u32(SOUND_TYPE_MONSTER), "npc"},
			{u32(SOUND_TYPE_ANOMALY), "anomaly"}, {u32(SOUND_TYPE_WORLD), "world"},
		};
		static SName const kinds[] = {
			{u32(SOUND_TYPE_SHOOTING), "shot"}, {u32(SOUND_TYPE_EMPTY_CLICKING), "empty click"},
			{u32(SOUND_TYPE_BULLET_HIT), "bullet hit"}, {u32(SOUND_TYPE_RECHARGING), "reload"},
			{u32(SOUND_TYPE_STEP), "step"}, {u32(SOUND_TYPE_TALKING), "talk"}, {u32(SOUND_TYPE_INJURING), "injured"},
			{u32(SOUND_TYPE_DYING), "dying"}, {u32(SOUND_TYPE_ATTACKING), "attack"}, {u32(SOUND_TYPE_EATING), "eating"},
			{u32(SOUND_TYPE_PICKING_UP), "pick up"}, {u32(SOUND_TYPE_DROPPING), "drop"},
			{u32(SOUND_TYPE_HIDING), "hide"}, {u32(SOUND_TYPE_TAKING), "take"}, {u32(SOUND_TYPE_USING), "use"},
			{u32(SOUND_TYPE_OBJECT_BREAKING), "breaking"}, {u32(SOUND_TYPE_OBJECT_COLLIDING), "colliding"},
			{u32(SOUND_TYPE_OBJECT_EXPLODING), "explosion"}, {u32(SOUND_TYPE_AMBIENT), "ambient"},
			{u32(SOUND_TYPE_IDLE), "idle"},
		};

		LPCSTR category = "sound";
		for (u32 i = 0; i < sizeof(categories) / sizeof(categories[0]); ++i)
			if (type & categories[i].mask)
			{
				category = categories[i].name;
				break;
			}

		LPCSTR kind = "";
		for (u32 i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i)
			if (type & kinds[i].mask)
			{
				kind = kinds[i].name;
				break;
			}

		xr_sprintf(result, result_size, "%s%s%s", category, *kind ? " " : "", kind);
	}

	// recorded by on_visible_value_source during the AI update; render must not query the script engine
	bool g_visible_value_source_known = false;
	bool g_visible_value_lua = false;

	// CAI_Stalker::fire_make_sense() without the Lua call: the original ends with best_weapon(), which runs
	// the _g.update_best_weapon script hook and must not be reached from render. The active item's type is
	// used instead (fire() switches to best_weapon() anyway).
	bool fire_make_sense_no_lua(CAI_Stalker* npc)
	{
		const CEntityAlive* enemy = npc->memory().enemy().selected();
		if (!enemy)
			return (false);

		float const pick_distance = npc->pick_distance();
		if ((pick_distance + g_ai_fire_range_extension) < npc->Position().distance_to(enemy->Position()))
			return (false);

		if (_abs(npc->Position().y - enemy->Position().y) > g_ai_fire_max_height_diff)
			return (false);

		if (pick_distance < g_ai_fire_min_dist)
			return (false);

		if (npc->memory().visual().visible_right_now(enemy))
			return (true);

		u32 const last_time_seen = npc->memory().visual().visible_object_time_last_seen(enemy);
		if (last_time_seen == u32(-1))
			return (false);

		if (now() > last_time_seen + g_ai_fire_make_sense_interval)
			return (false);

		CInventoryItem* item = npc->inventory().ActiveItem();
		if (!item)
			return (false);

		switch (item->object().ef_weapon_type())
		{
		case 6:
		case 7:
		case 8:
		case 10:
			return (true);
		}
		return (false);
	}

	template <typename planner_type>
	void planner_path(planner_type& planner, LPSTR result, u32 result_size, u32 depth)
	{
		if ((depth > 6) || !planner.initialized())
			return;

		LPCSTR const name = planner.action2string(planner.current_action_id());
		if (result[0])
			xr_strcat(result, result_size, " > ");
		xr_strcat(result, result_size, name ? name : "?");

		CScriptActionPlannerAction* const sub_planner = smart_cast<CScriptActionPlannerAction*>(&planner.current_action());
		if (sub_planner)
			planner_path(*sub_planner, result, result_size, depth + 1);
	}

	char property_sign(const CPropertyStorage& storage, CPropertyStorage::_condition_type id)
	{
		CPropertyStorage::CConditionStorage::const_iterator I = std::find(storage.m_storage.begin(),
		                                                                   storage.m_storage.end(), id);
		if (I == storage.m_storage.end())
			return ('?');
		return (sign(!!(*I).m_value));
	}

	class CTextBlock
	{
		struct SLine
		{
			string256 text;
			u32 color;
		};

		xr_vector<SLine> m_lines;

	public:
		void add(u32 color, LPCSTR format, ...)
		{
			m_lines.push_back(SLine());
			SLine& line = m_lines.back();
			line.color = color;
			va_list args;
			va_start(args, format);
			_vsnprintf_s(line.text, sizeof(line.text), _TRUNCATE, format, args);
			va_end(args);
		}

		// lines grow upwards from the anchor
		void draw(const Fvector& anchor) const
		{
			if (m_lines.empty())
				return;

			float x, y;
			if (!world_to_screen(anchor, x, y))
				return;

			CGameFont* font = UI().Font().pFontStat;
			float const old_height = font->GetHeight();
			font->SetHeight(text_height);
			font->OutSet(x, y - font->CurrentHeight_() * float(m_lines.size()));
			xr_vector<SLine>::const_iterator I = m_lines.begin();
			xr_vector<SLine>::const_iterator E = m_lines.end();
			for (; I != E; ++I)
			{
				font->SetColor((*I).color);
				font->OutNext("%s", (*I).text);
			}
			font->SetHeight(old_height);
		}
	};

	void draw_record(const Fvector& remembered, const Fvector& actual, u32 color)
	{
		box(remembered, .12f, color);
		line(remembered, actual, color);
	}

	//////////////////////////////////////////////////////////////////////////
	// ai_draw_info
	//////////////////////////////////////////////////////////////////////////

	void draw_info(CAI_Stalker* npc, CTextBlock& block)
	{
		CVisualMemoryManager& visual = npc->memory().visual();
		const CVisionParameters& params = visual.current_state();
		CAgentMemberManager& members = npc->agent_manager().member();

		block.add(color_white, "%s [%d] rank %d | squad members %d, in combat %d", *npc->cName(), int(npc->ID()),
		          int(npc->Rank()), int(members.members().size()), int(members.combat_members().size()));

		const CEntityAlive* target = target_of(npc);
		if (target)
		{
			float value = 0.f;
			CVisualMemoryManager::NOT_YET_VISIBLES::const_iterator I = visual.not_yet_visible_objects().begin();
			CVisualMemoryManager::NOT_YET_VISIBLES::const_iterator E = visual.not_yet_visible_objects().end();
			for (; I != E; ++I)
				if ((*I).m_object == target)
				{
					value = (*I).m_value;
					break;
				}

			float object_distance = 0.f;
			float const view_distance = visual.object_visible_distance(target, object_distance);
			float const threshold = visual.visibility_threshold();
			bool const instant = (view_distance <= params.m_always_visible_distance + EPS_L);

			block.add(value >= threshold ? color_green : color_yellow, "target %s: detection %.2f / %.2f%s",
			          *target->cName(), value, threshold,
			          instant ? "  INSTANT (view range <= always_visible_distance)" : "");
			block.add(object_distance < view_distance ? color_light : color_red,
			          "  distance %.1fm, view range toward it %.1fm (always_visible_distance %.1fm)",
			          object_distance, view_distance, params.m_always_visible_distance);

			const Feel::Vision::feel_visible_Item* item = feel_item(npc, target);
			if (!item)
				block.add(color_grey, "  vision ray: target outside the view frustum, not traced");
			else
				block.add(trace_result_color(item->dbg_result), "  vision ray: %s, vis %.2f%s, %dms ago",
				          trace_result_name(item->dbg_result), item->dbg_vis, item->dbg_cached ? " (cached)" : "",
				          item->dbg_time ? int(now() - item->dbg_time) : -1);
		}

		block.add(color_grey, "  cfg: still_visible %ums, time_quant %.2f, velocity_factor %.2f, decrease %.2f, transparency %.2f",
		          params.m_still_visible_time, params.m_time_quant, params.m_velocity_factor, params.m_decrease_value,
		          params.m_transparency_threshold);
		block.add(color_grey, "  cfg: eye range %.0fm, fov %.0f, view %.2f..%.2f of range, luminocity_factor %.2f",
		          npc->eye_range, npc->eye_fov, params.m_min_view_distance, params.m_max_view_distance,
		          params.m_luminocity_factor);

		bool const lua_override = g_visible_value_source_known && g_visible_value_lua;
		block.add(lua_override ? color_orange : color_grey,
		          "  speed: ai_vision_speed_boost %.2f, npc vision_speed %.2f, view_distance_factor %.2f | lua get_visible_value: %s",
		          g_ai_vision_speed_boost, visual.vision_speed(), visual.view_distance_factor(),
		          !g_visible_value_source_known ? "unknown yet" : (lua_override ? "OVERRIDES C++" : "not defined"));
	}

	//////////////////////////////////////////////////////////////////////////
	// ai_draw_memory
	//////////////////////////////////////////////////////////////////////////

	void draw_memory(CAI_Stalker* npc, CTextBlock& block)
	{
		const CEntityAlive* target = target_of(npc);
		if (!target)
			return;

		CVisualMemoryManager& visual = npc->memory().visual();
		const CVisionParameters& params = visual.current_state();
		u32 const time = now();
		Fvector const actual = memory_position(target);

		bool const right_now = visual.visible_right_now(target);
		bool const visible_now = visual.visible_now(target);
		block.add(right_now ? color_green : (visible_now ? color_orange : color_red),
		          "sight: visible_right_now %c, visible_now %c%s", sign(right_now), sign(visible_now),
		          (visible_now && !right_now) ? "  << 'visible' without a fresh sighting" : "");

		MemorySpace::CVisibleObject* visible_object = visual.visible_object(target);
		if (visible_object)
		{
			MemorySpace::squad_mask_type const my_mask = npc->agent_manager().member().mask(npc);
			LPCSTR refresher_name = "?";
			bool by_other = false;
			REFRESHES::const_iterator R = g_refreshes.find(REFRESH_KEY(&visual.objects(), target->ID()));
			if (R != g_refreshes.end())
			{
				CObject* refresher = Level().Objects.net_Find((*R).second.refresher);
				refresher_name = refresher ? *refresher->cName() : "<gone>";
				by_other = ((*R).second.refresher != npc->ID());
			}

			int const grace_left = int(visible_object->m_level_time + params.m_still_visible_time) - int(time);
			block.add((by_other && visible_now && !right_now) ? color_red : color_light,
			          "  sight record: age %ums, refreshed by %s%s | my bit %c, %u bits set | grace left %dms",
			          time - visible_object->m_level_time, refresher_name, by_other ? " (SQUADMATE)" : "",
			          sign(visible_object->visible(my_mask)), count_bits(u64(visible_object->m_visible.get())),
			          grace_left > 0 ? grace_left : 0);
			draw_record(visible_object->m_object_params.m_position, actual, color_blue);
		}

		{
			const CSoundMemoryManager::SOUNDS& sounds = npc->memory().sound().objects();
			CSoundMemoryManager::SOUNDS::const_iterator I = sounds.begin();
			CSoundMemoryManager::SOUNDS::const_iterator E = sounds.end();
			for (; I != E; ++I)
			{
				if ((*I).m_object != target)
					continue;

				block.add(color_yellow, "  sound record: age %ums, %.2fm from the real position",
				          time - (*I).m_level_time, (*I).m_object_params.m_position.distance_to(actual));
				draw_record((*I).m_object_params.m_position, actual, color_yellow);
				break;
			}
		}

		{
			const CHitMemoryManager::HITS& hits = npc->memory().hit().objects();
			CHitMemoryManager::HITS::const_iterator I = hits.begin();
			CHitMemoryManager::HITS::const_iterator E = hits.end();
			for (; I != E; ++I)
			{
				if ((*I).m_object != target)
					continue;

				block.add(color_red, "  hit record: age %ums, amount %.2f%s, %.2fm from the real position",
				          time - (*I).m_level_time, (*I).m_amount,
				          fis_zero((*I).m_amount) ? " (fake, from hearing a shot)" : "",
				          (*I).m_object_params.m_position.distance_to(actual));
				draw_record((*I).m_object_params.m_position, actual, color_red);
				break;
			}
		}

		MemorySpace::CMemoryInfo info = npc->memory().memory(target);
		if (info.m_object)
		{
			LPCSTR source = info.m_visual_info ? "sight" : (info.m_sound_info ? "sound" : (info.m_hit_info ? "hit" : "?"));
			block.add(color_white, "  memory() uses the %s record: %.2fm from the real position", source,
			          info.m_object_params.m_position.distance_to(actual));
			box(info.m_object_params.m_position, .25f, color_white);
		}
	}

	//////////////////////////////////////////////////////////////////////////
	// ai_draw_aim
	//////////////////////////////////////////////////////////////////////////

	void draw_aim(CAI_Stalker* npc, CTextBlock& block)
	{
		Fvector const eye = npc->eye_matrix.c;
		const MonsterSpace::SBoneRotation& head = npc->head_orientation();

		Fvector current_direction, target_direction;
		current_direction.setHP(-head.current.yaw, -head.current.pitch);
		target_direction.setHP(-head.target.yaw, -head.target.pitch);
		line(eye, Fvector().mad(eye, current_direction, 1.5f), color_white);
		line(eye, Fvector().mad(eye, target_direction, 1.5f), color_cyan);

		CWeapon* weapon = smart_cast<CWeapon*>(npc->inventory().ActiveItem());
		if (weapon)
		{
			Fvector fire_position, fire_direction;
			npc->g_fireParams(0, fire_position, fire_direction);
			bool hit;
			Fvector const end = trace_static(fire_position, fire_direction, 150.f, hit, npc);
			line(fire_position, end, color_magenta);
			if (hit)
				box(end, .06f, color_magenta);
		}

		SNpcState* state = find_state(npc);
		if (!state || !state->aim_time)
			return;

		u32 const age = now() - state->aim_time;
		block.add(aim_source_color(state->aim_source), "aim: %s sight, %s (%ums ago)", sight_type_name(state->sight_type),
		          aim_source_name(state->aim_source), age);

		const CEntityAlive* target = target_of(npc);
		bool const target_seen = target && npc->memory().visual().visible_right_now(target);
		switch (state->aim_source)
		{
		case eAimSourceHeld:
			block.add(color_orange, "  head target not updated: the gun keeps its last direction");
			break;
		case eAimSourceLiveBone:
		case eAimSourceObjectCenter:
		case eAimSourceVisPoint:
			if (target && !target_seen)
				block.add(color_red, "  aiming at the LIVE target position while visible_right_now is false");
			break;
		}

		if (age > aim_stale_time)
			return;

		switch (state->aim_source)
		{
		case eAimSourceLiveBone:
		case eAimSourceObjectCenter:
		case eAimSourceVisPoint:
		case eAimSourcePosition:
			{
				u32 const color = aim_source_color(state->aim_source);
				box(state->aim_point, .08f, color);
				line(eye, state->aim_point, color);
				if (!state->aim_lead.similar(Fvector().set(0.f, 0.f, 0.f)))
				{
					Fvector const base = Fvector().sub(state->aim_point, state->aim_lead);
					box(base, .04f, color_white);
					line(base, state->aim_point, color_white);
				}
				break;
			}
		}
	}

	//////////////////////////////////////////////////////////////////////////
	// ai_draw_fire
	//////////////////////////////////////////////////////////////////////////

	void draw_fire(CAI_Stalker* npc, CTextBlock& block)
	{
		u32 const time = now();
		const MonsterSpace::SBoneRotation& head = npc->head_orientation();
		const CEntityAlive* enemy = npc->memory().enemy().selected();

		// fire() compares the head yaw with the direction from our origin to the enemy origin (feet level)
		Fvector base = npc->Position();
		base.y += .1f;
		float const length = enemy ? _max(2.f, enemy->Position().distance_to(npc->Position())) : 10.f;
		for (int i = -1; i <= 1; i += 2)
		{
			Fvector edge;
			edge.setHP(-head.current.yaw + float(i) * g_ai_aim_fire_angle, 0.f);
			line(base, Fvector().mad(base, edge, length), color_orange);
		}

		if (enemy)
		{
			Fvector enemy_base = enemy->Position();
			enemy_base.y += .1f;
			Fvector const direction = Fvector().sub(enemy->Position(), npc->Position());
			float yaw, pitch;
			direction.getHP(yaw, pitch);
			bool const inside = angle_difference(-yaw, head.current.yaw) <= g_ai_aim_fire_angle;
			line(base, enemy_base, inside ? color_red : color_grey);
		}

		string512 brain_path;
		brain_path[0] = 0;
		planner_path(npc->brain(), brain_path, sizeof(brain_path), 0);
		block.add(color_white, "brain: %s", brain_path[0] ? brain_path : "-");

		CWeapon* weapon = smart_cast<CWeapon*>(npc->inventory().ActiveItem());
		CObjectHandlerPlanner& handler = npc->CObjectHandler::planner();
		LPCSTR const handler_action = handler.initialized() ? handler.action2string(handler.current_action_id()) : "-";
		char const aimed = weapon
			                   ? property_sign(handler.m_storage,
			                                   handler.uid(weapon->ID(), ObjectHandlerSpace::eWorldPropertyAimed1))
			                   : '-';
		block.add(color_white, "weapon: %s | aimed %c | firing %c", handler_action, aimed,
		          weapon ? sign(weapon->GetState() == CWeapon::eFire) : '-');

		SNpcState* state = find_state(npc);
		if (state && state->fire_time)
			block.add(state->fire_requested ? color_red : color_light, "fire(): %s, yaw diff %.1f / %.1f deg (%ums ago)",
			          state->fire_requested ? "FIRE" : "aim_ready", rad2deg(state->fire_yaw_difference),
			          rad2deg(g_ai_aim_fire_angle), time - state->fire_time);

		if (weapon && enemy)
			block.add(color_light, "can_kill enemy %c, member %c | fire_make_sense %c (active weapon; fire() does not check it)",
			          sign(npc->can_kill_enemy()), sign(npc->can_kill_member()), sign(fire_make_sense_no_lua(npc)));

		if (state)
			block.add(state->shots_unseen ? color_red : (state->shots_grace ? color_orange : color_light),
			          "shots: %u seen, %u grace (visible_now only), %u unseen", state->shots_seen,
			          state->shots_grace, state->shots_unseen);
	}

	void draw_shots()
	{
		xr_vector<SShot>::const_iterator I = g_shots.begin();
		xr_vector<SShot>::const_iterator E = g_shots.end();
		for (; I != E; ++I)
		{
			CObject* npc = Level().Objects.net_Find((*I).npc);
			if (!npc || !npc_passes_filter(npc))
				continue;

			u32 color = color_grey;
			switch ((*I).visibility)
			{
			case eShotSeen: color = color_green;
				break;
			case eShotGrace: color = color_orange;
				break;
			case eShotUnseen: color = color_red;
				break;
			}

			line((*I).from, (*I).to, color);
			if ((*I).hit_static)
				box((*I).to, .05f, color);
		}
	}

	//////////////////////////////////////////////////////////////////////////
	// ai_draw_sound
	//////////////////////////////////////////////////////////////////////////

	void draw_sounds()
	{
		xr_vector<SSound>::const_iterator I = g_sounds.begin();
		xr_vector<SSound>::const_iterator E = g_sounds.end();
		for (; I != E; ++I)
		{
			CObject* npc = Level().Objects.net_Find((*I).npc);
			if (!npc || !npc_passes_filter(npc))
				continue;

			bool const accepted = (*I).power >= (*I).threshold;
			u32 const color = (*I).fake_hit ? color_red : (accepted ? color_green : color_grey);
			line((*I).from, (*I).to, color);
			box((*I).from, .1f, color);

			// what the NPC actually stored: white box, linked to where the sound was heard from
			if ((*I).remembered)
			{
				box((*I).remembered_position, .15f, color_white);
				if ((*I).remembered_position.distance_to((*I).from) > .05f)
					line((*I).from, (*I).remembered_position, color_white);
			}

			string64 type_name;
			sound_type_name((*I).type, type_name, sizeof(type_name));
			CTextBlock label;
			label.add(color, "%s %s: power %.2f / threshold %.2f, occlusion %.2f%s", *(*I).source_name, type_name,
			          (*I).power, (*I).threshold, (*I).occlusion, (*I).fake_hit ? " -> FAKE HIT" : "");
			if ((*I).remembered)
				label.add(color_white, "  remembered at %.2fm from the heard position",
				          (*I).remembered_position.distance_to((*I).from));
			else
				label.add(color_grey, accepted ? "  not remembered (filtered: source visible_now / own item / friend ...)"
				                               : "  not remembered (below threshold)");
			label.draw((*I).from);
		}
	}

	//////////////////////////////////////////////////////////////////////////
	// ai_dbg_visibility_log
	//////////////////////////////////////////////////////////////////////////

	void update_log(CAI_Stalker* npc)
	{
		const CEntityAlive* target = target_of(npc);
		if (!target)
			return;

		CVisualMemoryManager& visual = npc->memory().visual();
		bool const right_now = visual.visible_right_now(target);
		bool const visible_now = visual.visible_now(target);
		SNpcState& s = state(npc);
		u32 const time = now();

		if (!s.log_initialized || (s.log_target != target->ID()))
		{
			s.log_initialized = true;
			s.log_target = target->ID();
			s.log_right_now = right_now;
			s.log_now = visible_now;
			s.log_lost_time = 0;
			s.log_grace_shots = 0;
			s.log_unseen_shots = 0;
			return;
		}

		LPCSTR const npc_name = *npc->cName();
		LPCSTR const target_name = *target->cName();

		if (s.log_right_now && !right_now)
		{
			s.log_lost_time = time;
			s.log_grace_shots = 0;
			s.log_unseen_shots = 0;
			Msg("[ai_vis] %6u %s -> %s: line of sight lost (visible_now %c)", time, npc_name, target_name,
			    sign(visible_now));
		}

		if (s.log_now && !visible_now)
			Msg("[ai_vis] %6u %s -> %s: visible_now cleared %ums after line of sight was lost; shots meanwhile: %u grace, %u unseen",
			    time, npc_name, target_name, s.log_lost_time ? time - s.log_lost_time : 0, s.log_grace_shots,
			    s.log_unseen_shots);

		if (!s.log_now && visible_now)
			Msg("[ai_vis] %6u %s -> %s: visible_now set%s", time, npc_name, target_name,
			    right_now ? "" : " WITHOUT a fresh sighting (squadmate / injected record)");

		if (!s.log_right_now && right_now)
		{
			if (s.log_lost_time)
				Msg("[ai_vis] %6u %s -> %s: line of sight regained after %ums; shots while lost: %u grace, %u unseen",
				    time, npc_name, target_name, time - s.log_lost_time, s.log_grace_shots, s.log_unseen_shots);
			else
				Msg("[ai_vis] %6u %s -> %s: seen (visible_right_now)", time, npc_name, target_name);
			s.log_lost_time = 0;
		}

		s.log_right_now = right_now;
		s.log_now = visible_now;
	}

	//////////////////////////////////////////////////////////////////////////

	void prune()
	{
		u32 const time = now();

		g_shots.erase(std::remove_if(g_shots.begin(), g_shots.end(), [time](const SShot& shot)
		{
			return (shot.time + shot_life_time < time);
		}), g_shots.end());

		g_sounds.erase(std::remove_if(g_sounds.begin(), g_sounds.end(), [time](const SSound& sound)
		{
			return (sound.time + sound_life_time < time);
		}), g_sounds.end());

		static u32 last_prune = 0;
		if (time < last_prune + 2000)
			return;
		last_prune = time;

		for (NPC_STATES::iterator I = g_npcs.begin(); I != g_npcs.end();)
		{
			if (Level().Objects.net_Find((*I).first))
				++I;
			else
				I = g_npcs.erase(I);
		}

		for (REFRESHES::iterator I = g_refreshes.begin(); I != g_refreshes.end();)
		{
			if ((*I).second.time + 60000 < time)
				I = g_refreshes.erase(I);
			else
				++I;
		}
	}
} // namespace

bool npc_passes_filter(const CObject* npc)
{
	if (!npc)
		return (false);

	if (!psAI_DrawFlags.test(aiDrawNearbyOnly))
		return (true);

	return (npc->Position().distance_to_sqr(Device.vCameraPosition) <= _sqr(psAI_DrawNearbyDistance));
}

bool world_to_screen(const Fvector& position, float& x, float& y)
{
	Fvector4 result;
	Device.mFullTransform.transform(result, position);
	if ((result.z < 0.f) || (result.w < 0.f))
		return (false);
	if ((result.x < -1.f) || (result.x > 1.f) || (result.y < -1.f) || (result.y > 1.f))
		return (false);

	x = (1.f + result.x) / 2.f * float(Device.dwWidth);
	y = (1.f - result.y) / 2.f * float(Device.dwHeight);
	return (true);
}

void on_aim(CAI_Stalker* npc, u32 sight_type, EAimSource source, const Fvector& point, const Fvector& lead)
{
	if (!npc || !psAI_DrawFlags.test(aiDrawAim))
		return;

	SNpcState& s = state(npc);
	s.aim_time = now();
	s.sight_type = sight_type;
	s.aim_source = source;
	s.aim_point = point;
	s.aim_lead = lead;
}

void on_fire_decision(CAI_Stalker* npc, float yaw_difference, bool fire_requested)
{
	if (!npc || !psAI_DrawFlags.test(aiDrawFire))
		return;

	SNpcState& s = state(npc);
	s.fire_time = now();
	s.fire_yaw_difference = yaw_difference;
	s.fire_requested = fire_requested;
}

void on_shot(CObject* shooter, const Fvector& position, const Fvector& direction)
{
	if (!psAI_DrawFlags.test(aiDrawFire | aiDbgVisibilityLog))
		return;

	CAI_Stalker* npc = smart_cast<CAI_Stalker*>(shooter);
	if (!npc || !npc->g_Alive())
		return;

	const CEntityAlive* enemy = npc->memory().enemy().selected();
	EShotVisibility visibility = eShotNoEnemy;
	if (enemy)
	{
		if (npc->memory().visual().visible_right_now(enemy))
			visibility = eShotSeen;
		else if (npc->memory().visual().visible_now(enemy))
			visibility = eShotGrace;
		else
			visibility = eShotUnseen;
	}

	SNpcState& s = state(npc);
	switch (visibility)
	{
	case eShotSeen: ++s.shots_seen;
		break;
	case eShotGrace: ++s.shots_grace;
		++s.log_grace_shots;
		break;
	case eShotUnseen: ++s.shots_unseen;
		++s.log_unseen_shots;
		break;
	}

	// every shot fired while the enemy is not even visible_now, with the state that let it happen
	// (runs from the weapon update, not from render: no Lua is touched here either)
	if ((eShotUnseen == visibility) && psAI_DrawFlags.test(aiDbgVisibilityLog) && npc_passes_filter(npc))
	{
		string512 brain_path;
		brain_path[0] = 0;
		planner_path(npc->brain(), brain_path, sizeof(brain_path), 0);

		CObjectHandlerPlanner& handler = npc->CObjectHandler::planner();
		LPCSTR const handler_action = handler.initialized() ? handler.action2string(handler.current_action_id()) : "-";

		u32 const last_seen = npc->memory().visual().visible_object_time_last_seen(enemy);
		u32 const still_visible = npc->memory().visual().current_state().m_still_visible_time;

		Msg("[ai_vis] %6u %s -> %s: UNSEEN shot #%u | last sighting %s%ums ago (still_visible %ums) | brain: %s | weapon: %s | sight: %s, %s",
		    now(), *npc->cName(), *enemy->cName(), s.shots_unseen,
		    (last_seen == u32(-1)) ? "never, " : "", (last_seen == u32(-1)) ? 0 : now() - last_seen, still_visible,
		    brain_path[0] ? brain_path : "-", handler_action, sight_type_name(s.sight_type),
		    aim_source_name(s.aim_source));
	}

	if (!psAI_DrawFlags.test(aiDrawFire))
		return;

	if (g_shots.size() >= max_events)
		g_shots.erase(g_shots.begin());

	SShot shot;
	shot.npc = npc->ID();
	shot.from = position;
	shot.visibility = visibility;
	shot.time = now();
	float const range = enemy ? enemy->Position().distance_to(position) + 30.f : 150.f;
	shot.to = trace_static(position, direction, range, shot.hit_static, npc);
	g_shots.push_back(shot);
}

void on_sound_remembered(CCustomMonster* npc, const Fvector& remembered_position)
{
	if (!npc || !psAI_DrawFlags.test(aiDrawSound))
		return;

	g_pending_remember.npc = npc->ID();
	g_pending_remember.position = remembered_position;
	g_pending_remember.valid = true;
}

void on_sound(CCustomMonster* npc, const CObject* source, int sound_type, const Fvector& position, float power,
              float threshold, bool fake_hit)
{
	// consume the record written for this very sound (if any) before any early-out
	bool const remembered = npc && g_pending_remember.valid && (g_pending_remember.npc == npc->ID());
	Fvector const remembered_position = g_pending_remember.position;
	g_pending_remember.valid = false;

	if (!npc || !psAI_DrawFlags.test(aiDrawSound) || !npc_passes_filter(npc))
		return;

	if (g_sounds.size() >= max_events)
		g_sounds.erase(g_sounds.begin());

	SSound sound;
	sound.npc = npc->ID();
	sound.from = position;
	sound.to = npc->eye_matrix.c;
	if (source)
		sound.source_name = source->cName();
	else
		sound.source_name = "world";
	sound.type = u32(sound_type);
	sound.power = power;
	sound.threshold = threshold;
	// deterministic re-evaluation of the occlusion already folded into power (the engine uses 0.2 jitter)
	sound.occlusion = ::Sound->get_occlusion_to(npc->SpatialComponent->spatial.sphere.P, position, 0.f);
	sound.fake_hit = fake_hit;
	sound.remembered = remembered;
	sound.remembered_position = remembered_position;
	sound.time = now();
	g_sounds.push_back(sound);
}

void on_visible_value_source(bool lua_override)
{
	g_visible_value_source_known = true;
	g_visible_value_lua = lua_override;
}

void on_visual_refresh(const void* shared_list, u16 object_id, u16 refresher_id)
{
	if (!psAI_DrawFlags.test(aiDrawMemory))
		return;

	SRefresh& refresh = g_refreshes[REFRESH_KEY(shared_list, object_id)];
	refresh.refresher = refresher_id;
	refresh.time = now();
}

void draw_detection_arc(CCustomMonster* npc)
{
	if (!npc || !npc->g_Alive())
		return;

	// same math as CVisualMemoryManager::object_visible_distance, sampled across the horizontal fov:
	// the detection meter only fills for targets closer than this curve
	CVisualMemoryManager& visual = npc->memory().visual();
	const CVisionParameters& params = visual.current_state();
	float range = flt_max, fov = flt_max;
	npc->update_range_fov(range, fov, npc->eye_range, deg2rad(npc->eye_fov));
	float const half_fov = fov * .5f;
	if (half_fov <= EPS_L)
		return;

	float const max_distance = range * params.m_max_view_distance;
	float const min_distance = range * params.m_min_view_distance;
	float const factor = visual.view_distance_factor();
	const MonsterSpace::SBoneRotation& head = npc->head_orientation();
	Fvector const eye = npc->eye_matrix.c;

	u32 const segments = 32;
	Fvector previous = eye;
	for (u32 i = 0; i <= segments; ++i)
	{
		float const offset = -half_fov + 2.f * half_fov * float(i) / float(segments);
		float const distance = ((1.f - _abs(offset) / half_fov) * (max_distance - min_distance) + min_distance) *
			factor;
		Fvector direction;
		direction.setHP(-head.current.yaw + offset, -head.current.pitch);
		Fvector const point = Fvector().mad(eye, direction, distance);
		// red where even a point-blank target is detected instantly
		u32 const color = (distance <= params.m_always_visible_distance + EPS_L) ? color_red : color_yellow;
		line(previous, point, color);
		if (i == segments)
			line(point, eye, color_yellow);
		previous = point;
	}
}

void render()
{
	u32 const all = aiDrawAim | aiDrawFire | aiDrawMemory | aiDrawSound | aiDrawInfo | aiDbgVisibilityLog;
	if (!psAI_DrawFlags.test(all))
	{
		g_shots.clear();
		g_sounds.clear();
		return;
	}

	prune();

	bool const draw_any = !!psAI_DrawFlags.test(aiDrawAim | aiDrawFire | aiDrawMemory | aiDrawInfo);
	for (u32 i = 0, n = Level().Objects.o_count(); i < n; ++i)
	{
		CAI_Stalker* npc = smart_cast<CAI_Stalker*>(Level().Objects.o_get_by_iterator(i));
		if (!npc || npc->getDestroy() || !npc->g_Alive())
			continue;

		if (!npc_passes_filter(npc))
			continue;

		if (psAI_DrawFlags.test(aiDbgVisibilityLog))
			update_log(npc);

		if (!draw_any)
			continue;

		CTextBlock block;
		if (psAI_DrawFlags.test(aiDrawInfo))
			draw_info(npc, block);
		if (psAI_DrawFlags.test(aiDrawMemory))
			draw_memory(npc, block);
		if (psAI_DrawFlags.test(aiDrawAim))
			draw_aim(npc, block);
		if (psAI_DrawFlags.test(aiDrawFire))
			draw_fire(npc, block);

		Fvector anchor = npc->Position();
		anchor.y += text_anchor_height;
		block.draw(anchor);
	}

	if (psAI_DrawFlags.test(aiDrawFire))
		draw_shots();

	if (psAI_DrawFlags.test(aiDrawSound))
		draw_sounds();
}
} // namespace ai_debug_draw

#endif // DEBUG || DEBUG_DRAW
