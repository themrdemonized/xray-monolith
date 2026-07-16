////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_storage_manager.h
//	Created 	: 25.12.2002
//  Modified 	: 12.05.2004
//	Author		: Dmitriy Iassenev
//	Description : ALife Simulator storage manager
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "alife_simulator_base.h"

class NET_Packet;
class CSE_ALifeCreatureActor;

class CALifeStorageManager : public virtual CALifeSimulatorBase
{
	friend class CALifeUpdatePredicate;
protected:
	typedef CALifeSimulatorBase inherited;

protected:
	string_path m_save_name;
	LPCSTR m_section;
	void start_current_level_object_resources(CSE_ALifeCreatureActor* actor);
	void finish_current_level_object_resources();
	void cleanup_current_level_object_resources();

private:
	void prepare_objects_for_save();
	void load(void* buffer, const u32& buffer_size, LPCSTR file_name);

public:
	IC CALifeStorageManager(xrServer* server, LPCSTR section);
	virtual ~CALifeStorageManager();
	static void prepare_load(LPCSTR save_name);
	bool load(LPCSTR save_name = 0);
	void save(LPCSTR save_name = 0, bool update_name = true);
	void save(NET_Packet& net_packet);
};

#include "alife_storage_manager_inline.h"
