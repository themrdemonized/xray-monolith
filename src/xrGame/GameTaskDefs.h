#pragma once
#include "alife_abstract_registry.h"
#include "alife_registry_version.h"
#include "../xrServerEntities/alife_space.h"

enum ETaskState
{
	eTaskStateFail = 0,
	eTaskStateInProgress,
	eTaskStateCompleted,
	eTaskStateDummy = u16(-1)
};

// all task has `storyline`-type now (10.10.2008)(sea)
enum ETaskType
{
	eTaskTypeStoryline = 0,
	eTaskTypeAdditional,
	//	eTaskTypeInsignificant,
	//	eTaskTypeCount,
	eTaskTypeDummy = u16(-1)
};


extern shared_str g_active_task_id;
class CGameTask;

struct SGameTaskKey : public IPureSerializeObject<IReader, IWriter>, public IPureDestroyableObject
{
	shared_str task_id;
	CGameTask* game_task;

	SGameTaskKey(const shared_str& t_id): task_id(t_id), game_task(NULL)
	{
	};

	SGameTaskKey(): task_id(NULL), game_task(NULL)
	{
	};


	virtual void save(IWriter& stream);
	virtual void load(IReader& stream);
	virtual void destroy();
};

DEFINE_VECTOR(SGameTaskKey, vGameTasks, vGameTasks_it);

struct CGameTaskRegistry : public CALifeAbstractRegistry<ALife::_OBJECT_ID, vGameTasks>
{
	virtual void save(IWriter& stream)
	{
		CALifeAbstractRegistry<ALife::_OBJECT_ID, vGameTasks>::save(stream);
		save_data(g_active_task_id, stream);
	};

	virtual void load(IReader& stream)
	{
		load_object_ids(stream, alife_registry_load_version() >= ALIFE_VERSION_WIDE_IDS);
		load_data(g_active_task_id, stream);
	};
};
