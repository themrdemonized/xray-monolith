////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_storage_manager.cpp
//	Created 	: 25.12.2002
//  Modified 	: 12.05.2004
//	Author		: Dmitriy Iassenev
//	Description : ALife Simulator storage manager
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "alife_storage_manager.h"
#include "alife_simulator_header.h"
#include "alife_time_manager.h"
#include "alife_spawn_registry.h"
#include "alife_object_registry.h"
#include "alife_graph_registry.h"
#include "alife_group_registry.h"
#include "alife_registry_container.h"
#include "xrserver.h"
#include "level.h"
#include "../xrEngine/x_ray.h"
#include "saved_game_wrapper.h"
#include "string_table.h"
#include "../xrEngine/igame_persistent.h"
#include "autosave_manager.h"
#include "../xrEngine/fmesh.h"
#include "../xrEngine/Render.h"
#include "../Include/xrRender/RenderDeviceRender.h"
//Alundaio
#ifdef ENGINE_LUA_ALIFE_STORAGE_MANAGER_CALLBACKS
#include "pch_script.h"
#include "../../xrServerEntities/script_engine.h"
#endif
//-Alundaio

extern XRCORE_API string_path g_bug_report_file;

using namespace ALife;
#ifdef ENGINE_LUA_ALIFE_STORAGE_MANAGER_CALLBACKS
 //Alundaio
#endif

extern string_path g_last_saved_game;

namespace
{
struct prepared_save
{
	NativeLoadExecutor::Batch batch;
	xr_task_group fallback_task;
	xr_vector<u8> data;
	xr_string name;
	string_path file_name{};
	bool active = false;
	bool valid = false;

	void wait()
	{
		if (batch.Valid())
			NativeLoadExecutor::Instance().Wait(batch);
		fallback_task.wait();
	}
} g_prepared_save;

void cleanup_prepared_save()
{
	try
	{
		g_prepared_save.wait();
	}
	catch (...)
	{
	}
	g_prepared_save.data.clear();
	g_prepared_save.name.clear();
	g_prepared_save.file_name[0] = 0;
	g_prepared_save.active = false;
	g_prepared_save.valid = false;
	g_prepared_save.batch = {};
}

struct prepared_visual_manifest
{
	xr_string actual_visual;
	xr_string ltx_visual;
	xr_string visual;
	xr_vector<xr_string> textures;
};

struct prepared_level_object_resources
{
	NativeLoadExecutor::Batch batch;
	xr_task_group fallback_tasks;
	xr_vector<prepared_visual_manifest> manifests;
	xr_string level_path;
	u32 object_count = 0;
	u32 visual_count = 0;
	bool active = false;

	void wait()
	{
		if (batch.Valid())
			NativeLoadExecutor::Instance().Wait(batch);
		fallback_tasks.wait();
	}
} g_prepared_level_object_resources;

xr_string normalize_visual_name(LPCSTR source)
{
	if (!source || !source[0])
		return {};

	string_path name;
	xr_strcpy(name, source);
	xr_strlwr(name);
	for (LPSTR i = name; *i; ++i)
		if (*i == '/')
			*i = '\\';

	LPSTR extension = strext(name);
	if (extension && !xr_strcmp(extension, ".ogf"))
		*extension = 0;
	return name;
}

bool resolve_visual_file(LPCSTR source, const xr_string& level_path, xr_string* resolved = nullptr)
{
	const xr_string visual = normalize_visual_name(source);
	if (visual.empty())
		return false;

	string_path file_name;
	xr_strcpy(file_name, visual.c_str());
	if (!strext(file_name))
		xr_strcat(file_name, ".ogf");

	xr_string path = level_path;
	path += file_name;
	if (!FS.exist(path.c_str()))
	{
		string_path mesh_path;
		if (!FS.exist(mesh_path, "$game_meshes$", file_name))
			return false;
		path = mesh_path;
	}

	if (resolved)
		*resolved = std::move(path);
	return true;
}
}

void CALifeStorageManager::start_current_level_object_resources(CSE_ALifeCreatureActor* actor)
{
	cleanup_current_level_object_resources();

	if (g_dedicated_server || !Device.m_pRender || !actor ||
		!ai().game_graph().valid_vertex_id(actor->m_tGraphID))
	{
		return;
	}

	const GameGraph::_LEVEL_ID level_id = ai().game_graph().vertex(actor->m_tGraphID)->level_id();
	prepared_level_object_resources& prepared = g_prepared_level_object_resources;
	prepared.level_path = FS.get_path("$game_levels$")->m_Path;
	prepared.level_path += *ai().game_graph().header().level(level_id).name();
	prepared.level_path += "\\";

	xr_set<xr_string> unique_visuals;
	prepared.manifests.reserve(objects().objects().size());
	for (const auto& item : objects().objects())
	{
		CSE_ALifeDynamicObject* object = item.second;
		if (!object->m_bOnline || !ai().game_graph().valid_vertex_id(object->m_tGraphID) ||
			ai().game_graph().vertex(object->m_tGraphID)->level_id() != level_id)
		{
			continue;
		}

		++prepared.object_count;
		xr_string ltx_visual;
		if (pSettings->section_exist(object->s_name.c_str()) &&
			pSettings->line_exist(object->s_name.c_str(), "visual"))
		{
			ltx_visual = normalize_visual_name(pSettings->r_string(object->s_name.c_str(), "visual"));
		}

		xr_string actual_visual;
		if (CSE_Visual* visual = object->visual())
		{
			LPCSTR name = visual->get_visual();
			if (name)
				actual_visual = normalize_visual_name(name);
		}

		if (actual_visual.empty() && ltx_visual.empty())
			continue;
		xr_string key = actual_visual;
		key += '\n';
		key += ltx_visual;
		if (unique_visuals.insert(std::move(key)).second)
			prepared.manifests.push_back({std::move(actual_visual), std::move(ltx_visual), {}, {}});
	}

	prepared.visual_count = static_cast<u32>(prepared.manifests.size());
	prepared.active = true;
	NativeLoadExecutor& executor = NativeLoadExecutor::Instance();
	prepared.batch = executor.BeginBatch(executor.CurrentGeneration());
	for (u32 index = 0; index < prepared.manifests.size(); ++index)
	{
		auto scan = [index]()
		{
			prepared_level_object_resources& prepared = g_prepared_level_object_resources;
			prepared_visual_manifest& manifest = prepared.manifests[index];
			if (resolve_visual_file(manifest.actual_visual.c_str(), prepared.level_path))
				manifest.visual = manifest.actual_visual;
			else if (resolve_visual_file(manifest.ltx_visual.c_str(), prepared.level_path))
				manifest.visual = manifest.ltx_visual;
			if (manifest.visual.empty())
				return;
			::Render->model_CollectTextures(manifest.visual.c_str(), prepared.level_path.c_str(), manifest.textures);
			for (const xr_string& texture : manifest.textures)
				Device.m_pRender->ResourcesPrefetchCreateTexture(texture.c_str(), prepared.level_path.c_str());
		};
		if (prepared.batch.Valid())
			executor.Submit(prepared.batch, NativeLoadPriority::Spawn, std::move(scan));
		else
			prepared.fallback_tasks.run(std::move(scan));
	}
}

void CALifeStorageManager::finish_current_level_object_resources()
{
	prepared_level_object_resources& prepared = g_prepared_level_object_resources;
	if (!prepared.active)
		return;

	prepared.wait();

	xr_set<xr_string> textures;
	for (const prepared_visual_manifest& manifest : prepared.manifests)
		textures.insert(manifest.textures.begin(), manifest.textures.end());

	Msg("* [object-resources] objects=%u visuals=%u textures=%u", prepared.object_count,
		prepared.visual_count, static_cast<u32>(textures.size()));
	cleanup_current_level_object_resources();
}

void CALifeStorageManager::cleanup_current_level_object_resources()
{
	prepared_level_object_resources& prepared = g_prepared_level_object_resources;
	try
	{
		prepared.wait();
	}
	catch (...)
	{
	}
	prepared.manifests.clear();
	prepared.level_path.clear();
	prepared.object_count = 0;
	prepared.visual_count = 0;
	prepared.active = false;
	prepared.batch = {};
}

void CALifeStorageManager::prepare_load(LPCSTR save_name)
{
	cleanup_prepared_save();
	g_prepared_save.name = save_name;
	g_prepared_save.active = true;
	g_prepared_save.valid = false;
	CSavedGameWrapper::saved_game_full_name(save_name, g_prepared_save.file_name);
	NativeLoadExecutor& executor = NativeLoadExecutor::Instance();
	g_prepared_save.batch = executor.BeginBatch(executor.CurrentGeneration());
	auto prepare = []()
	{
		IReader* stream = FS.r_open(g_prepared_save.file_name);
		if (!stream || !CSavedGameWrapper::valid_saved_game(*stream))
		{
			if (stream)
				FS.r_close(stream);
			return;
		}

		u32 source_count = stream->r_u32();
		g_prepared_save.data.resize(source_count);
		rtc_decompress(g_prepared_save.data.data(), source_count, stream->pointer(), stream->length() - 3 * sizeof(u32));
		FS.r_close(stream);
		g_prepared_save.valid = true;
	};
	if (g_prepared_save.batch.Valid())
		executor.Submit(g_prepared_save.batch, NativeLoadPriority::Spawn, std::move(prepare));
	else
		g_prepared_save.fallback_task.run(std::move(prepare));
}

CALifeStorageManager::~CALifeStorageManager()
{
	cleanup_current_level_object_resources();
	cleanup_prepared_save();
	*g_last_saved_game = 0;
}

void CALifeStorageManager::save(LPCSTR save_name_no_check, bool update_name)
{
	PROF_EVENT();
	LPCSTR game_saves_path = FS.get_path("$game_saves$")->m_Path;

	string_path save_name;
	strncpy_s(save_name, sizeof(save_name), save_name_no_check,
	          sizeof(save_name) - 5 - xr_strlen(SAVE_EXTENSION) - xr_strlen(game_saves_path));

	xr_strcpy(g_last_saved_game, save_name);

	string_path save;
	xr_strcpy(save, m_save_name);
	if (save_name)
	{
		strconcat(sizeof(m_save_name), m_save_name, save_name, SAVE_EXTENSION);
	}
	else
	{
		if (!xr_strlen(m_save_name))
		{
			Log("There is no file name specified!");
			return;
		}
	}

	//Alundaio: To get the savegame fname to make our own custom save states
#ifdef ENGINE_LUA_ALIFE_STORAGE_MANAGER_CALLBACKS
	::luabind::functor<void> funct1;
	if (ai().script_engine().functor("alife_storage_manager.CALifeStorageManager_before_save", funct1))
		funct1((LPCSTR)m_save_name);
#endif
	//-Alundaio

	u32 source_count;
	u32 dest_count;
	void* dest_data;
	{
		CMemoryWriter stream;
		header().save(stream);
		time_manager().save(stream);
		spawns().save(stream);
		objects().save(stream);
		registry().save(stream);

		source_count = stream.tell();
		void* source_data = stream.pointer();
		dest_count = rtc_csize(source_count);
		dest_data = xr_malloc(dest_count);
		dest_count = rtc_compress(dest_data, dest_count, source_data, source_count);
	}

	string_path temp;
	FS.update_path(temp, "$game_saves$", m_save_name);
	IWriter* writer = FS.w_open(temp);
	writer->w_u32(u32(-1));
	writer->w_u32(ALIFE_VERSION);

	writer->w_u32(source_count);
	writer->w(dest_data, dest_count);
	xr_free(dest_data);
	FS.w_close(writer);
#ifdef DEBUG
	Msg							("* Game %s is successfully saved to file '%s' (%d bytes compressed to %d)",m_save_name,temp,source_count,dest_count + 4);
#else // DEBUG
	Msg("* Game %s is successfully saved to file '%s'", m_save_name, temp);
#endif // DEBUG

	//Alundaio: To get the savegame fname to make our own custom save states
#ifdef ENGINE_LUA_ALIFE_STORAGE_MANAGER_CALLBACKS
	::luabind::functor<void> funct2;
	if (ai().script_engine().functor("alife_storage_manager.CALifeStorageManager_save", funct2))
		funct2((LPCSTR)m_save_name);
#endif
	//-Alundaio

	if (!update_name)
		xr_strcpy(m_save_name, save);
}

void CALifeStorageManager::load(void* buffer, const u32& buffer_size, LPCSTR file_name)
{
	//Alundaio: So we can get the fname to make our own custom save states
#ifdef ENGINE_LUA_ALIFE_STORAGE_MANAGER_CALLBACKS
	::luabind::functor<void> funct;
	if (ai().script_engine().functor("alife_storage_manager.CALifeStorageManager_load", funct))
		funct(file_name);
#endif
	//-Alundaio

	IReader source(buffer, buffer_size);
	header().load(source);
	time_manager().load(source);
	spawns().load(source, file_name);
	graph().on_load();
	objects().load(source);

	VERIFY(can_register_objects());
	can_register_objects(false);
	CALifeObjectRegistry::OBJECT_REGISTRY::iterator B = objects().objects().begin();
	CALifeObjectRegistry::OBJECT_REGISTRY::iterator E = objects().objects().end();
	CALifeObjectRegistry::OBJECT_REGISTRY::iterator I;
	CSE_ALifeCreatureActor* current_actor = nullptr;
	for (I = B; I != E; ++I)
	{
		CSE_ALifeCreatureActor* actor = smart_cast<CSE_ALifeCreatureActor*>((*I).second);
		if (actor)
		{
			graph().prepare_current_level(actor);
			current_actor = actor;
			break;
		}
	}
	VERIFY(I != E);
	try
	{
		start_current_level_object_resources(current_actor);

		for (I = B; I != E; ++I)
		{
			ALife::_OBJECT_ID id = (*I).second->ID;
			(*I).second->ID = server().PerformIDgen(id);
			VERIFY(id == (*I).second->ID);
			register_object((*I).second, false);
		}

		registry().load(source);

		can_register_objects(true);

		for (I = B; I != E; ++I)
			(*I).second->on_register();
		finish_current_level_object_resources();
	}
	catch (...)
	{
		cleanup_current_level_object_resources();
		throw;
	}

	if (!g_pGameLevel)
		return;

	Level().autosave_manager().on_game_loaded();
}

bool CALifeStorageManager::load(LPCSTR save_name_no_check)
{
	LPCSTR game_saves_path = FS.get_path("$game_saves$")->m_Path;

	string_path save_name;
	strncpy_s(save_name, sizeof(save_name), save_name_no_check,
	          sizeof(save_name) - 5 - xr_strlen(SAVE_EXTENSION) - xr_strlen(game_saves_path));

	CTimer timer;
	timer.Start();

	string_path save;
	xr_strcpy(save, m_save_name);
	if (!save_name)
	{
		if (!xr_strlen(m_save_name))
			R_ASSERT2(false, "There is no file name specified!");
	}
	else
	{
		strconcat(sizeof(m_save_name), m_save_name, save_name, SAVE_EXTENSION);
	}
	string_path file_name;
	FS.update_path(file_name, "$game_saves$", m_save_name);

	xr_strcpy(g_last_saved_game, save_name);
	xr_strcpy(g_bug_report_file, file_name);

	const bool prepared = g_prepared_save.active && g_prepared_save.name == save_name;
	IReader* stream = prepared ? nullptr : FS.r_open(file_name);
	if (!prepared && !stream)
	{
		Msg("* Cannot find saved game %s", file_name);
		xr_strcpy(m_save_name, save);
		return (false);
	}

	if (!prepared)
		CHECK_OR_EXIT(CSavedGameWrapper::valid_saved_game(*stream),
		              make_string("%s\nSaved game version mismatch or saved game is corrupted",file_name));
	/*
		string512					temp;
		strconcat					(sizeof(temp),temp,CStringTable().translate("st_loading_saved_game").c_str()," \"",save_name,SAVE_EXTENSION,"\"");
		g_pGamePersistent->LoadTitle(temp);
	*/
	g_pGamePersistent->LoadTitle();

	unload();
	reload(m_section);

	if (prepared)
	{
		g_prepared_save.wait();
		CHECK_OR_EXIT(g_prepared_save.valid,
		              make_string("%s\nSaved game version mismatch or saved game is corrupted",file_name));
		load(g_prepared_save.data.data(), g_prepared_save.data.size(), file_name);
		cleanup_prepared_save();
	}
	else
	{
		u32 source_count = stream->r_u32();
		void* source_data = xr_malloc(source_count);
		rtc_decompress(source_data, source_count, stream->pointer(), stream->length() - 3 * sizeof(u32));
		FS.r_close(stream);
		load(source_data, source_count, file_name);
		xr_free(source_data);
	}

	groups().on_after_game_load();

	VERIFY(graph().actor());

	Msg("* Game %s is successfully loaded from file '%s' (%.3fs)", save_name, file_name, timer.GetElapsed_sec());

	return (true);
}

void CALifeStorageManager::save(NET_Packet& net_packet)
{
	PROF_EVENT();
	prepare_objects_for_save();

	shared_str game_name;
	net_packet.r_stringZ(game_name);
	save(*game_name, !!net_packet.r_u8());
}

void CALifeStorageManager::prepare_objects_for_save()
{
	PROF_EVENT();
	Level().ClientSend();
	Level().ClientSave();
}
