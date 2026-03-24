////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_update_manager.h
//	Created 	: 25.12.2002
//  Modified 	: 12.05.2004
//	Author		: Dmitriy Iassenev
//	Description : ALife Simulator update manager
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "alife_switch_manager.h"
#include "alife_surge_manager.h"
#include "alife_storage_manager.h"
#include "../xrEngine/pure.h"
#include "../xrEngine/CameraDefs.h"

namespace RestrictionSpace
{
	enum ERestrictorTypes;
}

class CActor;

class CALifeUpdateManager :
	public CALifeSwitchManager,
	public CALifeSurgeManager,
	public CALifeStorageManager,
	public ISheduled,
	public pureFrame,
	public pureRender
{
private:
	bool m_first_time;

	// Per-frame time skip state
	bool            m_time_skip_active;
	u32             m_time_skip_step_ms;
	u32             m_time_skip_remaining;
	u32             m_time_skip_total_ms;
	u32             m_time_skip_step_idx;
	u32             m_time_skip_saved_opu;
	CActor*         m_time_skip_actor;
	EEffectorPPType m_time_skip_pp_type;
	u32             m_time_skip_start_ms;

protected:
	u64 m_max_process_time;
	float m_update_monster_factor;
	u32 m_objects_per_update;
	bool m_changing_level;

public:
	void __stdcall update();

protected:
	void new_game(LPCSTR save_name);
	void init_ef_storage() const;
	virtual void reload(LPCSTR section);

public:
	CALifeUpdateManager(xrServer* server, LPCSTR section);
	virtual ~CALifeUpdateManager();
	virtual shared_str shedule_Name() const { return shared_str("alife_simulator"); };
	virtual float shedule_Scale();
	virtual void shedule_Update(u32 dt);
	virtual bool shedule_Needed() { return true; };
	void update_switch();
	void update_scheduled(bool init_ef = true);
	void time_skip_begin  (u32 total_ms, u32 step_ms);
private:
	bool time_skip_tick   ();
	void time_skip_finish ();
public:
	virtual void OnFrame  ();
	virtual void OnRender ();
	u32  time_skip_step   () const { return m_time_skip_step_idx; }
	u32  time_skip_total  () const { return m_time_skip_step_ms ? (m_time_skip_total_ms + m_time_skip_step_ms - 1) / m_time_skip_step_ms : 0; }
	bool time_skip_active () const { return m_time_skip_active; }
	void load(LPCSTR game_name = 0, bool no_assert = false, bool new_only = false);
	bool load_game(LPCSTR game_name, bool no_assert = false);
	IC float update_monster_factor() const;
	bool change_level(NET_Packet& net_packet);
	void set_process_time(int microseconds);
	void objects_per_update(const u32& objects_per_update);
	void set_switch_online(ALife::_OBJECT_ID id, bool value);
	void set_switch_offline(ALife::_OBJECT_ID id, bool value);
	void set_interactive(ALife::_OBJECT_ID id, bool value);
	void jump_to_level(LPCSTR level_name) const;
	void teleport_object(ALife::_OBJECT_ID id, GameGraph::_GRAPH_ID game_vertex_id, u32 level_vertex_id,
	                     const Fvector& position);
	void add_restriction(ALife::_OBJECT_ID id, ALife::_OBJECT_ID restriction_id,
	                     const RestrictionSpace::ERestrictorTypes& restriction_type);
	void remove_restriction(ALife::_OBJECT_ID id, ALife::_OBJECT_ID restriction_id,
	                        const RestrictionSpace::ERestrictorTypes& restriction_type);
	void remove_all_restrictions(ALife::_OBJECT_ID id, const RestrictionSpace::ERestrictorTypes& restriction_type);
};

#include "alife_update_manager_inline.h"
