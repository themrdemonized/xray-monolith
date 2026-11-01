////////////////////////////////////////////////////////////////////////////
//	Module 		: ai_debug_draw.h
//	Description : DEBUG_DRAW visualisations of NPC perception, memory, aiming and firing
//
//	Console (all under DEBUG_DRAW):
//		ai_draw_aim					head / target direction, gun line, aim point and its source
//		ai_draw_fire				fire() yaw cone, brain / weapon state, every bullet coloured by enemy visibility
//		ai_draw_memory				sight / sound / hit records vs. the target's real position, shared-record state
//		ai_draw_sound				sounds heard by NPCs: power vs. threshold, occlusion, fake hits
//		ai_draw_info				detection meter, view range, vision trace result, vision config
//		ai_dbg_visibility_log		console log of line-of-sight / visible_now transitions and blind shots
//		ai_draw_nearby_only			0 = draw for every NPC, 1 = only NPCs within ai_draw_nearby_distance of the camera
//		ai_draw_nearby_distance		radius for ai_draw_nearby_only (metres)
//	ai_draw_nearby_only also applies to ai_dbg_frustum, ai_dbg_vision and ai_draw_visibility_rays.
////////////////////////////////////////////////////////////////////////////

#pragma once

#if defined(DEBUG) || defined(DEBUG_DRAW)

class CObject;
class CAI_Stalker;
class CCustomMonster;

// psAI_Flags has no free bits left, so these live in their own flag set
#define aiDrawAim					(1<<0)
#define aiDrawFire					(1<<1)
#define aiDrawMemory				(1<<2)
#define aiDrawSound					(1<<3)
#define aiDrawInfo					(1<<4)
#define aiDrawNearbyOnly			(1<<5)
#define aiDbgVisibilityLog			(1<<6)

extern Flags32 psAI_DrawFlags;
extern float psAI_DrawNearbyDistance;

namespace ai_debug_draw
{
	enum EAimSource
	{
		eAimSourceNone = 0,			// sight type does not aim at a point (path / direction / cover / search ...)
		eAimSourceHeld,				// fire-object sight, target not in the raw vision list: head target is NOT updated
		eAimSourceLiveBone,			// fire-object sight, target in the raw vision list: live aim bone (+ lead)
		eAimSourceVisPoint,			// fire-object sight, target in the raw vision list: last vision ray point (+ lead)
		eAimSourceObjectCenter,		// object sight (no fire): live object position at centre height
		eAimSourcePosition,			// position sight: explicit point, usually the remembered position
	};

	// general mode: always true; nearby mode: within ai_draw_nearby_distance of the camera
	bool npc_passes_filter(const CObject* npc);

	// capture hooks, cheap no-ops while the matching console flags are off
	void on_aim(CAI_Stalker* npc, u32 sight_type, EAimSource source, const Fvector& point, const Fvector& lead);
	void on_fire_decision(CAI_Stalker* npc, float yaw_difference, bool fire_requested);
	void on_shot(CObject* shooter, const Fvector& position, const Fvector& direction);
	void on_sound(CCustomMonster* npc, const CObject* source, int sound_type, const Fvector& position, float power,
	              float threshold, bool fake_hit);
	// the position CSoundMemoryManager actually stored for the sound being processed (called before on_sound)
	void on_sound_remembered(CCustomMonster* npc, const Fvector& remembered_position);
	void on_visual_refresh(const void* shared_list, u16 object_id, u16 refresher_id);
	// whether CVisualMemoryManager::get_visible_value used the lua override (recorded during the AI update)
	void on_visible_value_source(bool lua_override);

	// detection-range arc (used by ai_dbg_frustum)
	void draw_detection_arc(CCustomMonster* npc);

	bool world_to_screen(const Fvector& position, float& x, float& y);

	// called from CLevel::OnRender before debug_renderer().render()
	// NOTE: runs while the parallel Lua GC (lua_parallel_gc) may be stepping on the device thread:
	//       nothing reachable from render() may touch the Lua state (no functors, no best_weapon(), ...)
	void render();
}

#endif // DEBUG || DEBUG_DRAW
