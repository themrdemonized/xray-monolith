#include "stdafx.h"
#include "r4.h"
#include "../xrRender/ResourceManager.h"
#include "../xrRender/fbasicvisual.h"
#include "../../xrEngine/fmesh.h"
#include "../../xrEngine/xrLevel.h"
#include "../../xrEngine/x_ray.h"
#include "../../xrEngine/IGame_Persistent.h"
#include "../../xrCore/stream_reader.h"

#include "../xrRender/dxRenderDeviceRender.h"

#include "../xrRenderDX10/dx10BufferUtils.h"
#include "../xrRenderDX10/3DFluid/dx103DFluidVolume.h"

#include "../xrRender/FHierrarhyVisual.h"

#pragma warning(push)
#pragma warning(disable:4995)
#include <malloc.h>
#pragma warning(pop)

namespace
{
shared_str cached_level_name;
bool release_cached_level = false;
}

void CRender::level_Load(IReader* fs)
{
	CTimer level_timer;
	level_timer.Start();
	R_ASSERT(0!=g_pGameLevel);
	if (b_loaded)
	{
		if (cached_level_name.equal(g_pGameLevel->name()))
		{
			Msg("* [LEVEL CACHE] R4 hit: %s", cached_level_name.c_str());
			Wallmarks->clear();
			pLastSector = nullptr;
			vLastCameraPos.set(0.f, 0.f, 0.f);
			return;
		}

		Msg("* [LEVEL CACHE] R4 miss: %s -> %s", cached_level_name.c_str(), g_pGameLevel->name().c_str());
		release_cached_level = true;
		level_Unload();
		release_cached_level = false;
	}
	R_ASSERT(!b_loaded);

	// Begin
	pApp->LoadBegin();
	dxRenderDeviceRender::Instance().Resources->DeferredLoad(TRUE);
	IReader* chunk;

	// Shaders
	//	g_pGamePersistent->LoadTitle		("st_loading_shaders");
	g_pGamePersistent->LoadTitle();
	struct shader_description
	{
		xr_string shader;
		xr_string textures;
	};
	xr_vector<shader_description> unique_shaders;
	xr_vector<u32> shader_indices;
	{
		chunk = fs->open_chunk(fsL_SHADERS);
		R_ASSERT2(chunk, "Level doesn't builded correctly.");
		u32 count = chunk->r_u32();
		Shaders.resize(count);
		shader_indices.assign(count, u32(-1));
		xr_map<xr_string, u32> unique_indices;
		for (u32 i = 0; i < count; i++) // skip first shader as "reserved" one
		{
			string512 n_sh, n_tlist;
			LPCSTR n = LPCSTR(chunk->pointer());
			chunk->skip_stringZ();
			if (0 == n[0]) continue;
			xr_strcpy(n_sh, n);
			LPSTR delim = strchr(n_sh, '/');
			*delim = 0;
			xr_strcpy(n_tlist, delim + 1);

			xr_string key = n_sh;
			key += '\n';
			key += n_tlist;
			auto existing = unique_indices.find(key);
			if (existing != unique_indices.end())
			{
				shader_indices[i] = existing->second;
				continue;
			}

			shader_indices[i] = static_cast<u32>(unique_shaders.size());
			unique_indices.emplace(std::move(key), shader_indices[i]);
			unique_shaders.push_back({n_sh, n_tlist});
		}
		chunk->close();
	}

	CTimer shader_timer;
	shader_timer.Start();
	xr_vector<ref_shader> compiled_shaders(unique_shaders.size());
	xr_vector<u32> lua_shaders;
	xr_vector<u32> cpp_shaders;
	for (u32 i = 0; i < unique_shaders.size(); ++i)
		(dxRenderDeviceRender::Instance().Resources->_lua_HasShader(unique_shaders[i].shader.c_str()) ? lua_shaders : cpp_shaders)
			.push_back(i);

	xr_task_group lua_shader_load_task;
	lua_shader_load_task.run([&]()
	{
		for (u32 i : lua_shaders)
			compiled_shaders[i] = dxRenderDeviceRender::Instance().Resources->CreateLevelShader(
				unique_shaders[i].shader.c_str(), unique_shaders[i].textures.c_str());
	});
	xr_task_group cpp_shader_load_tasks;
	auto start_cpp_shader_load = [&]()
	{
		lua_shader_load_task.wait();
		const u32 task_count = _min(4u, static_cast<u32>(cpp_shaders.size()));
		for (u32 task = 0; task < task_count; ++task)
			cpp_shader_load_tasks.run([&, task, task_count]()
			{
				for (u32 j = task; j < cpp_shaders.size(); j += task_count)
				{
					u32 i = cpp_shaders[j];
					compiled_shaders[i] = dxRenderDeviceRender::Instance().Resources->CreateLevelShader(
						unique_shaders[i].shader.c_str(), unique_shaders[i].textures.c_str());
				}
			});
	};
	auto finish_shader_load = [&]()
	{
		cpp_shader_load_tasks.wait();
		for (u32 i = 0; i < shader_indices.size(); ++i)
			if (shader_indices[i] != u32(-1))
				Shaders[i] = compiled_shaders[shader_indices[i]];
		Msg("* [LEVEL LOAD] R4 shaders: %d ms (%d unique, %d lua, %d cpp)", shader_timer.GetElapsed_ms(),
			static_cast<u32>(unique_shaders.size()), static_cast<u32>(lua_shaders.size()),
			static_cast<u32>(cpp_shaders.size()));
	};

	// Components
	Wallmarks = xr_new<CWallmarksEngine>();
	Details = xr_new<CDetailManager>();
	xr_task_group early_environment_tasks;
	early_environment_tasks.run([this]()
	{
		CTimer timer;
		timer.Start();
		Load3DFluid();
		Msg("* [LEVEL LOAD] R4 fluid: %d ms", timer.GetElapsed_ms());
	});
	early_environment_tasks.run([this]()
	{
		CTimer timer;
		timer.Start();
		HOM.Load();
		Msg("* [LEVEL LOAD] R4 HOM: %d ms", timer.GetElapsed_ms());
	});

	if (!g_dedicated_server)
	{
		// VB,IB,SWI
		//		g_pGamePersistent->LoadTitle("st_loading_geometry");
		g_pGamePersistent->LoadTitle();
		CTimer geometry_timer;
		geometry_timer.Start();
		xr_task_group geometry_load_tasks;
		geometry_load_tasks.run([this]()
		{
			CTimer timer;
			timer.Start();
			CStreamReader* geom = FS.rs_open("$level$", "level.geom");
			R_ASSERT2(geom, "level.geom");
			LoadBuffers(geom,FALSE);
			LoadSWIs(geom);
			FS.r_close(geom);
			Msg("* [LEVEL LOAD] R4 level.geom: %d ms", timer.GetElapsed_ms());
		});

		//...and alternate/fast geometry
		geometry_load_tasks.run([this]()
		{
			CTimer timer;
			timer.Start();
			CStreamReader* geom = FS.rs_open("$level$", "level.geomx");
			R_ASSERT2(geom, "level.geomX");
			LoadBuffers(geom,TRUE);
			FS.r_close(geom);
			Msg("* [LEVEL LOAD] R4 level.geomx: %d ms", timer.GetElapsed_ms());
		});
		start_cpp_shader_load();
		geometry_load_tasks.wait();
		Msg("* [LEVEL LOAD] R4 geometry barrier: %d ms", geometry_timer.GetElapsed_ms());
		finish_shader_load();

		xr_task_group visual_load_tasks;
		visual_load_tasks.run([this]()
		{
			CTimer timer;
			timer.Start();
			Details->Load();
			Msg("* [LEVEL LOAD] R4 details: %d ms", timer.GetElapsed_ms());
		});

		// Visuals
		//		g_pGamePersistent->LoadTitle("st_loading_spatial_db");
		g_pGamePersistent->LoadTitle();
		IReader* visuals = fs->open_chunk(fsL_VISUALS);
		visual_load_tasks.run([this, visuals]()
		{
			CTimer timer;
			timer.Start();
			LoadVisuals(visuals);
			visuals->close();
			Msg("* [LEVEL LOAD] R4 visuals: %d ms", timer.GetElapsed_ms());
		});

		visual_load_tasks.wait();
	}
	else
	{
		start_cpp_shader_load();
		finish_shader_load();
	}

	// Sectors
	//	g_pGamePersistent->LoadTitle("st_loading_sectors_portals");
	g_pGamePersistent->LoadTitle();
	CTimer sectors_timer;
	sectors_timer.Start();
	LoadSectors(fs);
	Msg("* [LEVEL LOAD] R4 sectors: %d ms", sectors_timer.GetElapsed_ms());

	early_environment_tasks.wait();
	xr_task_group environment_load_tasks;

	// Lights
	// pApp->LoadTitle			("Loading lights...");
	environment_load_tasks.run([this, fs]()
	{
		CTimer timer;
		timer.Start();
		LoadLights(fs);
		Msg("* [LEVEL LOAD] R4 lights: %d ms", timer.GetElapsed_ms());
	});
	environment_load_tasks.wait();

	// End
	pApp->LoadEnd();

	// signal loaded
	b_loaded = TRUE;
	cached_level_name = g_pGameLevel->name();
	Msg("* [LEVEL LOAD] R4 total: %d ms", level_timer.GetElapsed_ms());
}

void CRender::level_Unload()
{
	if (0 == g_pGameLevel) return;
	if (!b_loaded) return;
	if (!release_cached_level)
	{
		Msg("* [LEVEL CACHE] R4 retained: %s", cached_level_name.c_str());
		return;
	}

	dxRenderDeviceRender::Instance().Resources->WaitForTextureLoads();

	GMBase.clear();
	GMRainWet.clear();
	for (sun::cascade& cascade : m_sun_cascades)
		cascade.GMCascade.clear();

	u32 I;

	// HOM
	HOM.Unload();

	//*** Details
	Details->Unload();

	//*** Sectors
	// 1.
	xr_delete(rmPortals);
	pLastSector = 0;
	pOutdoorSector = 0;
	vLastCameraPos.set(0, 0, 0);
	// 2.
	for (I = 0; I < Sectors.size(); I++) xr_delete(Sectors[I]);
	Sectors.clear();
	// 3.
	for (I = 0; I < Portals.size(); I++) xr_delete(Portals[I]);
	Portals.clear();

	//*** Lights
	// Glows.Unload			();
	Lights.Unload();

	//*** Visuals
	for (I = 0; I < Visuals.size(); I++)
	{
		Visuals[I]->Release();
		xr_delete(Visuals[I]);
	}
	Visuals.clear();

	//*** SWI
	for (I = 0; I < SWIs.size(); I++)xr_free(SWIs[I].sw);
	SWIs.clear();

	//*** VB/IB
	for (I = 0; I < nVB.size(); I++) _RELEASE(nVB[I]);
	for (I = 0; I < xVB.size(); I++) _RELEASE(xVB[I]);
	nVB.clear();
	xVB.clear();
	for (I = 0; I < nIB.size(); I++) _RELEASE(nIB[I]);
	for (I = 0; I < xIB.size(); I++) _RELEASE(xIB[I]);
	nIB.clear();
	xIB.clear();
	nDC.clear();
	xDC.clear();

	//*** Components
	xr_delete(Details);
	xr_delete(Wallmarks);

	//*** Shaders
	Shaders.clear_and_free();

	const bool clearResources = psDeviceFlags2.test(rsClearAllResources);
	if (psDeviceFlags2.test(rsClearModels) || clearResources)
	{
		Models->ClearPool(true);
		Visuals.clear_and_free();
		if (clearResources)
		{
			dxRenderDeviceRender::Instance().Resources->UnloadAllTexturesOnLevelUnload();
			dxRenderDeviceRender::Instance().ResourcesDestroyNecessaryTextures();
			dxRenderDeviceRender::Instance().Resources->Evict();
		}
		dxRenderDeviceRender::Instance().Resources->Dump(false);
		//static int unload_counter = 0;
		//Msg("The Level Unloaded.======================== %d", ++unload_counter);
	}

	b_loaded = FALSE;
	cached_level_name = nullptr;
}

void CRender::LoadBuffers(CStreamReader* base_fs, BOOL _alternative)
{
	R_ASSERT2(base_fs, "Could not load geometry. File not found.");
	dxRenderDeviceRender::Instance().Resources->Evict();
	//	u32	dwUsage					= D3DUSAGE_WRITEONLY;

	xr_vector<VertexDeclarator>& _DC = _alternative ? xDC : nDC;
	xr_vector<ID3DVertexBuffer*>& _VB = _alternative ? xVB : nVB;
	xr_vector<ID3DIndexBuffer*>& _IB = _alternative ? xIB : nIB;
	xr_task_group buffer_creation_tasks;
	xr_vector<xr_vector<u8>> vertex_data;
	xr_vector<xr_vector<u8>> index_data;

	// Vertex buffers
	{
		// Use DX9-style declarators
		CStreamReader* fs = base_fs->open_chunk(fsL_VB);
		R_ASSERT2(fs, "Could not load geometry. File 'level.geom?' corrupted.");
		u32 count = fs->r_u32();
		_DC.resize(count);
		_VB.resize(count);
		vertex_data.resize(count);
		u32 bufferSize = (MAXD3DDECLLENGTH + 1) * sizeof(D3DVERTEXELEMENT9);
		D3DVERTEXELEMENT9* dcl = (D3DVERTEXELEMENT9*)_alloca(bufferSize);
		for (u32 i = 0; i < count; i++)
		{
			// decl
			//			D3DVERTEXELEMENT9*	dcl		= (D3DVERTEXELEMENT9*) fs().pointer();
			fs->r(dcl, bufferSize);
			fs->advance(-(int)bufferSize);

			u32 dcl_len = D3DXGetDeclLength(dcl) + 1;
			_DC[i].resize(dcl_len);
			fs->r(_DC[i].begin(), dcl_len * sizeof(D3DVERTEXELEMENT9));

			// count, size
			u32 vCount = fs->r_u32();
			u32 vSize = D3DXGetDeclVertexSize(dcl, 0);
			Msg("* [Loading VB] %d verts, %d Kb", vCount, (vCount * vSize) / 1024);

			// Create and fill
			//BYTE*	pData		= 0;
			//R_CHK				(HW.pDevice->CreateVertexBuffer(vCount*vSize,dwUsage,0,D3DPOOL_MANAGED,&_VB[i],0));
			//R_CHK				(_VB[i]->Lock(0,0,(void**)&pData,0));
			//			CopyMemory			(pData,fs().pointer(),vCount*vSize);
			//fs->r				(pData,vCount*vSize);
			//_VB[i]->Unlock		();
			//	TODO: DX10: Check fragmentation.
			//	Check if buffer is less then 2048 kb
			vertex_data[i].resize(vCount * vSize);
			fs->r(vertex_data[i].data(), vertex_data[i].size());

			//			fs->advance			(vCount*vSize);
		}
		fs->close();

		for (u32 i = 0; i < count; ++i)
			buffer_creation_tasks.run([&, i]()
			{
				dx10BufferUtils::CreateVertexBuffer(&_VB[i], vertex_data[i].data(), static_cast<UINT>(vertex_data[i].size()));
			});
	}

	// Index buffers
	{
		CStreamReader* fs = base_fs->open_chunk(fsL_IB);
		u32 count = fs->r_u32();
		_IB.resize(count);
		index_data.resize(count);
		for (u32 i = 0; i < count; i++)
		{
			u32 iCount = fs->r_u32();
			Msg("* [Loading IB] %d indices, %d Kb", iCount, (iCount * 2) / 1024);

			// Create and fill
			//BYTE*	pData		= 0;
			//R_CHK				(HW.pDevice->CreateIndexBuffer(iCount*2,dwUsage,D3DFMT_INDEX16,D3DPOOL_MANAGED,&_IB[i],0));
			//R_CHK				(_IB[i]->Lock(0,0,(void**)&pData,0));
			//			CopyMemory			(pData,fs().pointer(),iCount*2);
			//fs->r				(pData,iCount*2);
			//_IB[i]->Unlock		();

			//	TODO: DX10: Check fragmentation.
			//	Check if buffer is less then 2048 kb
			index_data[i].resize(iCount * 2);
			fs->r(index_data[i].data(), index_data[i].size());

			//			fs().advance		(iCount*2);
		}
		fs->close();

		for (u32 i = 0; i < count; ++i)
			buffer_creation_tasks.run([&, i]()
			{
				dx10BufferUtils::CreateIndexBuffer(&_IB[i], index_data[i].data(), static_cast<UINT>(index_data[i].size()));
			});
	}

	buffer_creation_tasks.wait();
}

void CRender::LoadVisuals(IReader* fs)
{
	IReader* chunk = 0;
	u32 index = 0;
	dxRender_Visual* V = 0;
	ogf_header H;

	while ((chunk = fs->open_chunk(index)) != 0)
	{
		chunk->r_chunk_safe(OGF_HEADER, &H, sizeof(H));
		V = Models->Instance_Create(H.type);
		V->Load(0, chunk, 0);
		Visuals.push_back(V);

		chunk->close();
		index++;
	}
}

void CRender::LoadLights(IReader* fs)
{
	// lights
	Lights.Load(fs);
	Lights.LoadHemi();
}

struct b_portal
{
	u16 sector_front;
	u16 sector_back;
	svector<Fvector, 6> vertices;
};

void CRender::LoadSectors(IReader* fs)
{
	// allocate memory for portals
	u32 size = fs->find_chunk(fsL_PORTALS);
	R_ASSERT(0==size%sizeof(b_portal));
	u32 count = size / sizeof(b_portal);
	Portals.resize(count);
	for (u32 c = 0; c < count; c++)
		Portals[c] = xr_new<CPortal>();

	// load sectors
	IReader* S = fs->open_chunk(fsL_SECTORS);
	for (u32 i = 0; ; i++)
	{
		IReader* P = S->open_chunk(i);
		if (0 == P) break;

		CSector* __S = xr_new<CSector>();
		__S->load(*P);
		Sectors.push_back(__S);

		P->close();
	}
	S->close();

	// load portals
	if (count)
	{
		CDB::Collector CL;
		fs->find_chunk(fsL_PORTALS);
		for (u32 i = 0; i < count; i++)
		{
			b_portal P;
			fs->r(&P, sizeof(P));
			CPortal* __P = (CPortal*)Portals[i];
			__P->Setup(P.vertices.begin(), P.vertices.size(),
			           (CSector*)getSector(P.sector_front),
			           (CSector*)getSector(P.sector_back));
			for (u32 j = 2; j < P.vertices.size(); j++)
				CL.add_face_packed_D(
					P.vertices[0], P.vertices[j - 1], P.vertices[j],
					u32(i)
				);
		}
		if (CL.getTS() < 2)
		{
			Fvector v1, v2, v3;
			v1.set(-20000.f, -20000.f, -20000.f);
			v2.set(-20001.f, -20001.f, -20001.f);
			v3.set(-20002.f, -20002.f, -20002.f);
			CL.add_face_packed_D(v1, v2, v3, 0);
		}

		// build portal model
		rmPortals = xr_new<CDB::MODEL>();
		rmPortals->build(CL.getV(), int(CL.getVS()), CL.getT(), int(CL.getTS()));
	}
	else
	{
		rmPortals = 0;
	}

	// debug
	//	for (int d=0; d<Sectors.size(); d++)
	//		Sectors[d]->DebugDump	();

	pLastSector = 0;

	// Search for default sector - assume "default" or "outdoor" sector is the largest one
	//. hack: need to know real outdoor sector
	CSector* largest_sector = 0;
	float largest_sector_vol = 0;
	for (u32 s = 0; s < Sectors.size(); s++)
	{
		CSector* S = (CSector*)Sectors[s];
		dxRender_Visual* V = S->root();
		float vol = V->vis.box.getvolume();
		if (vol > largest_sector_vol)
		{
			largest_sector_vol = vol;
			largest_sector = S;
		}
	}
	pOutdoorSector = largest_sector;
}

void CRender::LoadSWIs(CStreamReader* base_fs)
{
	// allocate memory for portals
	if (base_fs->find_chunk(fsL_SWIS))
	{
		CStreamReader* fs = base_fs->open_chunk(fsL_SWIS);
		u32 item_count = fs->r_u32();

		xr_vector<FSlideWindowItem>::iterator it = SWIs.begin();
		xr_vector<FSlideWindowItem>::iterator it_e = SWIs.end();

		for (; it != it_e; ++it)
			xr_free((*it).sw);

		SWIs.clear_not_free();

		SWIs.resize(item_count);
		for (u32 c = 0; c < item_count; c++)
		{
			FSlideWindowItem& swi = SWIs[c];
			swi.reserved[0] = fs->r_u32();
			swi.reserved[1] = fs->r_u32();
			swi.reserved[2] = fs->r_u32();
			swi.reserved[3] = fs->r_u32();
			swi.count = fs->r_u32();
			VERIFY(NULL==swi.sw);
			swi.sw = xr_alloc<FSlideWindow>(swi.count);
			fs->r(swi.sw, sizeof(FSlideWindow) * swi.count);
		}
		fs->close();
	}
}

void CRender::Load3DFluid()
{
	if (!RImplementation.o.volumetricfog)
		return;

	string_path fn_game;
	if (FS.exist(fn_game, "$level$", "level.fog_vol"))
	{
		IReader* F = FS.r_open(fn_game);
		u16 version = F->r_u16();

		if (version == 3)
		{
			u32 cnt = F->r_u32();
			for (u32 i = 0; i < cnt; ++i)
			{
				dx103DFluidVolume* pVolume = xr_new<dx103DFluidVolume>();
				pVolume->Load("", F, 0);

				//	Attach to sector's static geometry
				CSector* pSector = (CSector*)detectSector(pVolume->getVisData().sphere.P);
				//	3DFluid volume must be in render sector
				VERIFY(pSector);

				dxRender_Visual* pRoot = pSector->root();
				//	Sector must have root
				VERIFY(pRoot);
				VERIFY(pRoot->getType() == MT_HIERRARHY);

				((FHierrarhyVisual*)pRoot)->children.push_back(pVolume);
			}
		}

		FS.r_close(F);
	}
}
