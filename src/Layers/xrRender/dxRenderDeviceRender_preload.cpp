#include "stdafx.h"
#include "dxRenderDeviceRender.h"
#include "ResourceManager.h"
#include "../../xrEngine/xrLevel.h"

// Level preload (console: level_preload, see xrGame/Level_preload.cpp).
// The materials of a level that is about to be loaded are created during play, a few per frame,
// and held until that load has found them. A background task reads their names from the level file.
// Everything here except that task runs on the main thread.

extern xr_task_group textures_load_tasks;
extern xr_atomic_u32 textures_loading;
void fix_texture_name(LPSTR fn);

namespace
{
struct SMaterialPreload
{
	string_path folder; // of the level, with the trailing separator; the key of one not released
	string_path current; // folder of the level loaded when it was asked for
	xr_vector<shared_str> materials; // "shader/texture,texture,...", filled by the task
	u32 next = 0; // the next material to create
	xr_vector<ref_shader> held;
	bool released = false; // for good: a level asked for again is prepared anew
	u32 frames = 0;
	float spent_ms = 0.f;
	float worst_ms = 0.f;
	xr_atomic_bool read{false};
	xr_task_group task;
};
xr_vector<SMaterialPreload*> preloads;

// Textures are kept by name, and a texture is looked for in the folder of the loaded level first.
// A texture that either level has in its own folder (lightmaps and level_lods have the same names on
// every level) would be created from the wrong file now and kept by the load of the next level.
// Materials that use one are left to that load, and so are animated textures (.seq), whose frames
// are looked up the same way, and names too long for CResourceManager::_ParseList.
bool uses_level_texture(const SMaterialPreload& preload, LPCSTR textures)
{
	string512 list;
	xr_strcpy(list, textures);
	for (LPSTR name = list, next; name; name = next)
	{
		next = strchr(name, ',');
		if (next)
			*next++ = 0;
		if (!name[0])
		{
			if (textures[0]) // an empty entry in a list: _ParseList passes it on as a name
				return true;
			continue;
		}
		if (xr_strlen(name) >= 128)
			return true;
		strlwr(name); // as _ParseList
		fix_texture_name(name);
		string_path file;
		for (LPCSTR folder : {preload.folder, preload.current})
		{
			if (FS.exist(strconcat(sizeof(file), file, folder, name, ".dds")))
				return true;
		}
		if (FS.exist(file, "$game_textures$", name, ".seq"))
			return true;
	}
	return false;
}

void read_materials(SMaterialPreload* preload)
{
	string_path file;
	IReader* stream = FS.r_open(strconcat(sizeof(file), file, preload->folder, "level"));
	if (!stream)
		return;
	// The chunk is looked for here: IReader::find_chunk checks the bounds in debug builds only, and a
	// damaged file is for the load to report. A compressed one (CFS_CompressMark) is left to the load
	// too: _decompressLZ is not reentrant, and the materials of two levels can be read at once.
	while (stream->elapsed() >= 8)
	{
		const u32 type = stream->r_u32();
		const u32 size = stream->r_u32();
		if (size > u32(stream->elapsed()))
			break;
		if (type != fsL_SHADERS)
		{
			stream->advance(int(size));
			continue;
		}
		IReader chunk(stream->pointer(), int(size));
		const u32 count = chunk.elapsed() >= 4 ? chunk.r_u32() : 0;
		for (u32 i = 0; i < count && chunk.elapsed() > 0; ++i)
		{
			LPCSTR name = LPCSTR(chunk.pointer());
			const size_t length = strnlen(name, chunk.elapsed());
			if (length == size_t(chunk.elapsed()))
				break; // not terminated: a damaged file, the load reports it
			chunk.advance(int(length) + 1);
			// The shader name must fit CResourceManager::_lua_HasShader
			LPCSTR textures = strchr(name, '/');
			if (textures && textures > name && textures - name < 256 && length < sizeof(string512) && !uses_level_texture(*preload, textures + 1))
				preload->materials.push_back(name);
		}
		break;
	}
	FS.r_close(stream);
}

// Frees released levels. A texture load task holds a plain pointer to its texture (_CreateTexture,
// CTexture::apply_load), so the textures created here are let go only while no load is queued, or
// right after waiting for the queue: a load queued since is for a texture not created here.
void free_released(bool waited = false)
{
	if (textures_loading && !waited)
		return;
	for (u32 i = 0; i < preloads.size();)
	{
		SMaterialPreload* preload = preloads[i];
		if (preload->released && preload->read)
		{
			preload->task.wait(); // has finished: read is set last
			xr_delete(preload);
			preloads.erase(preloads.begin() + i);
		}
		else
			++i;
	}
}
}

void dxRenderDeviceRender::ResourcesPreloadLevel(LPCSTR level_folder)
{
	for (SMaterialPreload* it : preloads)
		if (!it->released && 0 == xr_strcmp(it->folder, level_folder))
			return;
	SMaterialPreload* preload = xr_new<SMaterialPreload>();
	xr_strcpy(preload->folder, level_folder);
	FS.update_path(preload->current, "$level$", "");
	preloads.push_back(preload);
	preload->task.run([preload]()
	{
		read_materials(preload);
		preload->read = true;
	});
}

void dxRenderDeviceRender::ResourcesPreloadUpdate(float budget_ms)
{
	free_released();
	for (SMaterialPreload* preload : preloads)
	{
		if (preload->released || !preload->read || preload->next == preload->materials.size())
			continue;
#if !defined(USE_DX10) && !defined(USE_DX11)
		if (FAILED(HW.pDevice->TestCooperativeLevel())) // lost: CRenderDevice::Begin draws nothing either
			return;
#endif
		CTimer timer;
		timer.Start();
		do
		{
			string512 shader;
			xr_strcpy(shader, *preload->materials[preload->next++]);
			LPSTR textures = strchr(shader, '/'); // read_materials kept only names that have it
			*textures++ = 0;
			LPCSTR name = shader;
#if RENDER == R_R1
			// As CRender::level_Load: on R1 'default' with a single texture is drawn with def_vertex
			if (0 == xr_strcmp(name, "default") && !strchr(textures, ','))
				name = "def_shaders\\def_vertex";
#endif
			const float started = timer.GetElapsed_sec();
			// A shader that is not in the library is for the load of this level to report, not for
			// walking past its level changer (CResourceManager::Create stops the game on DX9).
			if (Resources->_lua_HasShader(name) || Resources->_FindBlender(name))
				preload->held.push_back(Resources->Create(name, textures));
			preload->worst_ms = std::max(preload->worst_ms, (timer.GetElapsed_sec() - started) * 1000.f);
		}
		while (preload->next < preload->materials.size() && timer.GetElapsed_sec() * 1000.f < budget_ms);
		preload->spent_ms += timer.GetElapsed_sec() * 1000.f;
		++preload->frames;
		if (preload->next == preload->materials.size())
			Msg("* level preload: %s ready, %u materials over %u frames, %.0f ms of work in total, slowest material %.1f ms",
			    preload->folder, u32(preload->held.size()), preload->frames, preload->spent_ms, preload->worst_ms);
		break; // one level per frame
	}
}

void dxRenderDeviceRender::ResourcesPreloadRelease(LPCSTR level_folder)
{
	for (SMaterialPreload* preload : preloads)
		if (!level_folder || 0 == xr_strcmp(preload->folder, level_folder))
			preload->released = true;
	free_released();
}

void dxRenderDeviceRender::ResourcesPreloadWait()
{
	if (preloads.empty())
		return;
	for (SMaterialPreload* preload : preloads)
		preload->task.wait();
	textures_load_tasks.wait();
	free_released(true);
}
