#include "stdafx.h"
#include "r4.h"
#include "../xrRender/ResourceManager.h"
#include "../xrRender/fbasicvisual.h"
#include "../../xrEngine/fmesh.h"
#include "../../xrEngine/xrLevel.h"
#include "../../xrEngine/x_ray.h"
#include "../../xrEngine/IGame_Persistent.h"
#include "../../xrCore/stream_reader.h"
#include "../../xrCDB/xr_area.h"

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
xr_atomic_u32 g_level_asset_generation{1};

xr_string NormalizeLevelPath(LPCSTR source)
{
	xr_string path = source ? source : "";
	std::transform(path.begin(), path.end(), path.begin(), [](char value)
	{
		return char(tolower(u8(value)));
	});
	if (!path.empty() && path.back() != '\\' && path.back() != '/')
		path += '\\';
	return path;
}

shared_str CurrentLevelCacheKey()
{
	string_path path;
	FS.update_path(path, "$level$", "");
	return shared_str(NormalizeLevelPath(path).c_str());
}

void MixLevelIdentity(u64& identity, u32 value)
{
	identity ^= value;
	identity *= 1099511628211ull;
}

u64 LevelIdentity(LPCSTR canonical_level_path)
{
	static LPCSTR files[] =
	{
		"level", "level.geom", "level.geomx", "level.details", "level.hom", "build.lights", "level.fog_vol"
	};
	u64 identity = 1469598103934665603ull;
	for (LPCSTR name : files)
	{
		xr_string full_path = canonical_level_path ? canonical_level_path : "";
		if (!full_path.empty() && full_path.back() != '\\' && full_path.back() != '/')
			full_path += '\\';
		full_path += name;
		const CLocatorAPI::file* file = FS.exist(full_path.c_str());
		MixLevelIdentity(identity, file ? 1u : 0u);
		if (!file)
			continue;
		MixLevelIdentity(identity, file->crc);
		MixLevelIdentity(identity, file->size_real);
		MixLevelIdentity(identity, file->size_compressed);
		MixLevelIdentity(identity, file->modif);
	}
	// Hash only creation-time options. The raw bitfield contains padding and
	// o.distortion is a per-frame phase flag, so hashing the struct causes false
	// misses for otherwise identical static packages.
	const CRender::_options& o = RImplementation.o;
	const u32 option_values[] =
	{
		o.ssfx_branches, o.ssfx_blood, o.ssfx_rain, o.ssfx_hud_raindrops, o.ssfx_ssr, o.ssfx_terrain,
		o.ssfx_volumetric, o.ssfx_water, o.ssfx_ao, o.ssfx_il, o.ssfx_core, o.ssfx_bloom, o.ssfx_sss,
		o.ssfx_fog, o.ssfx_motionblur, o.ssfx_taa, o.ssfx_motionvectors, o.ssfx_glass, o.bug,
		o.ssao_blur_on, o.ssao_opt_data, o.ssao_half_data, o.ssao_hbao, o.ssao_hdao, o.ssao_ultra,
		o.hbao_vectorized, o.volsize, o.smapsize, o.depth16, o.mrt, o.mrtmixdepth, o.fp16_filter,
		o.fp16_blend, o.albedo_wo, o.HW_smap, o.HW_smap_PCF, o.HW_smap_FETCH4, o.HW_smap_FORMAT,
		o.nvstencil, o.nvdbt, o.nullrt, o.no_ram_textures, o.distortion_enabled, o.sunfilter, o.sunstatic,
		o.sjitter, o.noshadows, o.Tshadows, o.disasm, o.advancedpp, o.volumetricfog, o.dx10_msaa,
		o.dx10_msaa_hybrid, o.dx10_msaa_opt, o.dx10_sm4_1, o.dx10_msaa_alphatest, o.dx10_msaa_samples,
		o.dx10_minmax_sm, o.dx10_minmax_sm_screenarea_threshold, o.dx11_enable_tessellation,
		o.forcegloss, o.forceskinw, o.dx11_hdr10
	};
	for (u32 value : option_values)
		MixLevelIdentity(identity, value);
	MixLevelIdentity(identity, ps_r2_ls_flags.get());
	MixLevelIdentity(identity, ps_r2_ls_flags_ext.get());
	MixLevelIdentity(identity, crc32(&o.forcegloss_v, sizeof(o.forcegloss_v)));
	MixLevelIdentity(identity, dm_current_size);
	MixLevelIdentity(identity, crc32(&ps_current_detail_density, sizeof(ps_current_detail_density)));
	MixLevelIdentity(identity, crc32(&ps_current_detail_height, sizeof(ps_current_detail_height)));
	MixLevelIdentity(identity, g_level_asset_generation.load(std::memory_order_acquire));
	return identity;
}

u64 CurrentLevelIdentity()
{
	const shared_str path = CurrentLevelCacheKey();
	return LevelIdentity(path.c_str());
}

struct PreparedLights
{
	xr_vector<u8> dynamic;
	xr_vector<u8> hemi;
};

struct b_portal
{
	u16 sector_front;
	u16 sector_back;
	svector<Fvector, 6> vertices;
};

void CopyReader(IReader& reader, xr_vector<u8>& data)
{
	data.resize(reader.length());
	if (!data.empty())
		reader.r(data.data(), static_cast<int>(data.size()));
}

void PrepareLevelLights(const xr_string& level_path, PreparedLights& prepared)
{
	xr_string level_name = level_path + "level";
	IReader* level = FS.r_open(level_name.c_str());
	R_ASSERT2(level, level_name.c_str());
	IReader* dynamic = level->open_chunk(fsL_LIGHT_DYNAMIC);
	R_ASSERT(dynamic);
	CopyReader(*dynamic, prepared.dynamic);
	dynamic->close();
	FS.r_close(level);

	xr_string lights_name = level_path + "build.lights";
	if (!FS.exist(lights_name.c_str()))
		return;
	IReader* lights = FS.r_open(lights_name.c_str());
	IReader* hemi = lights->open_chunk(1);
	if (hemi)
	{
		CopyReader(*hemi, prepared.hemi);
		hemi->close();
	}
	FS.r_close(lights);
}

void PrepareLevelChunk(const xr_string& level_path, u32 chunk_id, xr_vector<u8>& data)
{
	xr_string level_name = level_path + "level";
	IReader* level = FS.r_open(level_name.c_str());
	R_ASSERT2(level, level_name.c_str());
	IReader* chunk = level->open_chunk(chunk_id);
	R_ASSERT2(chunk, level_name.c_str());
	CopyReader(*chunk, data);
	chunk->close();
	FS.r_close(level);
}

void PrepareLevelSectors(const xr_string& level_path, xr_vector<u8>& portals,
	xr_vector<xr_vector<u8>>& sectors, CDB::MODEL*& portal_model)
{
	xr_string level_name = level_path + "level";
	IReader* level = FS.r_open(level_name.c_str());
	R_ASSERT2(level, level_name.c_str());

	IReader* portal_chunk = level->open_chunk(fsL_PORTALS);
	if (portal_chunk)
	{
		CopyReader(*portal_chunk, portals);
		portal_chunk->close();
	}
	R_ASSERT(portals.size() % sizeof(b_portal) == 0);

	IReader* sector_chunk = level->open_chunk(fsL_SECTORS);
	R_ASSERT(sector_chunk);
	for (u32 index = 0;; ++index)
	{
		IReader* sector = sector_chunk->open_chunk(index);
		if (!sector)
			break;
		sectors.emplace_back();
		CopyReader(*sector, sectors.back());
		sector->close();
	}
	sector_chunk->close();
	FS.r_close(level);

	const b_portal* source = reinterpret_cast<const b_portal*>(portals.data());
	const u32 portal_count = static_cast<u32>(portals.size() / sizeof(b_portal));
	if (!portal_count)
		return;

	CDB::Collector collector;
	for (u32 index = 0; index < portal_count; ++index)
		for (u32 vertex = 2; vertex < source[index].vertices.size(); ++vertex)
			collector.add_face_packed_D(source[index].vertices[0], source[index].vertices[vertex - 1],
				source[index].vertices[vertex], index);
	if (collector.getTS() < 2)
	{
		Fvector v1, v2, v3;
		v1.set(-20000.f, -20000.f, -20000.f);
		v2.set(-20001.f, -20001.f, -20001.f);
		v3.set(-20002.f, -20002.f, -20002.f);
		collector.add_face_packed_D(v1, v2, v3, 0);
	}
	portal_model = xr_new<CDB::MODEL>();
	portal_model->build(collector.getV(), int(collector.getVS()), collector.getT(), int(collector.getTS()));
}

void PrepareLevelFluids(const xr_string& level_path, xr_vector<dx103DFluidData::PreparedData>& prepared)
{
	prepared.clear();
	xr_string file_name = level_path + "level.fog_vol";
	if (!FS.exist(file_name.c_str()))
		return;

	IReader* reader = FS.r_open(file_name.c_str());
	R_ASSERT2(reader, file_name.c_str());
	const u16 version = reader->r_u16();
	if (version == 3)
	{
		prepared.resize(reader->r_u32());
		for (dx103DFluidData::PreparedData& volume : prepared)
			dx103DFluidVolume::Prepare(reader, volume);
	}
	FS.r_close(reader);
}

void CommitVisualShaderTree(IRenderVisual* visual)
{
	if (!visual)
		return;
	visual->CommitShaderTexture();
	if (xr_vector<IRenderVisual*>* children = visual->get_children())
		for (IRenderVisual* child : *children)
			CommitVisualShaderTree(child);
	if (xr_vector<IRenderVisual*>* children = visual->get_children_invisible())
		for (IRenderVisual* child : *children)
			CommitVisualShaderTree(child);
}

void SuspendVisualShaderTree(IRenderVisual* visual)
{
	if (!visual)
		return;
	visual->SuspendShaderTexture();
	if (xr_vector<IRenderVisual*>* children = visual->get_children())
		for (IRenderVisual* child : *children)
			SuspendVisualShaderTree(child);
	if (xr_vector<IRenderVisual*>* children = visual->get_children_invisible())
		for (IRenderVisual* child : *children)
			SuspendVisualShaderTree(child);
}

bool IsWorkerSafeLevelVisual(u32 type)
{
	return type == MT_NORMAL || type == MT_PROGRESSIVE || type == MT_TREE_ST || type == MT_TREE_PM;
}

}

struct CRender::LevelStaticPackage
{
	shared_str key;
	u64 identity = 0;
	CDB::MODEL* portals_model = nullptr;
	CSector* outdoor_sector = nullptr;
	CDetailManager* details = nullptr;
	CHOM::StaticData hom;
	xr_vector<u8> light_dynamic;
	xr_vector<u8> light_hemi;
	CLight_DB* prepared_lights = nullptr;
	xr_vector<IRender_Portal*> portals;
	xr_vector<IRender_Sector*> sectors;
	xr_vector<FSlideWindowItem> swis;
	xr_vector<ref_shader> shaders;
	xr_vector<VertexDeclarator> normal_declarations;
	xr_vector<VertexDeclarator> fast_declarations;
	xr_vector<ID3DVertexBuffer*> normal_vertex_buffers;
	xr_vector<ID3DVertexBuffer*> fast_vertex_buffers;
	xr_vector<ID3DIndexBuffer*> normal_index_buffers;
	xr_vector<ID3DIndexBuffer*> fast_index_buffers;
	xr_vector<dxRender_Visual*> visuals;
	xr_vector<LevelShaderDescription> shader_descriptions;
	xr_vector<u32> shader_indices;
	xr_vector<ref_shader> cpp_shader_results;
	xr_vector<dx103DFluidData::PreparedData> fluid_descriptors;
	bool normal_geometry_ready = false;
	bool fast_geometry_ready = false;
	xr_vector<u8> visual_data;
	xr_vector<u8> prepared_portals;
	xr_vector<xr_vector<u8>> prepared_sectors;
	CDB::MODEL* prepared_portals_model = nullptr;
	xr_vector<u8> prepared_lights_dynamic;
	xr_vector<u8> prepared_lights_hemi;
	bool shader_recipes_ready = false;
	bool visual_leaves_ready = false;
	bool sectors_ready = false;
	bool details_ready = false;
	bool hom_ready = false;
	bool lights_ready = false;
	bool fluid_descriptors_ready = false;

	~LevelStaticPackage()
	{
		if (details)
		{
			details->Unload();
			xr_delete(details);
		}
		xr_delete(prepared_lights);
		xr_delete(portals_model);
		xr_delete(prepared_portals_model);
		for (IRender_Sector*& sector : sectors)
			xr_delete(sector);
		for (IRender_Portal*& portal : portals)
			xr_delete(portal);
		for (dxRender_Visual*& visual : visuals)
		{
			visual->Release();
			xr_delete(visual);
		}
		for (FSlideWindowItem& swi : swis)
			xr_free(swi.sw);
		for (ID3DVertexBuffer*& buffer : normal_vertex_buffers)
			_RELEASE(buffer);
		for (ID3DVertexBuffer*& buffer : fast_vertex_buffers)
			_RELEASE(buffer);
		for (ID3DIndexBuffer*& buffer : normal_index_buffers)
			_RELEASE(buffer);
		for (ID3DIndexBuffer*& buffer : fast_index_buffers)
			_RELEASE(buffer);
		shaders.clear_and_free();
	}
};

bool CRender::level_StaticCacheReady(LPCSTR canonical_level_path)
{
	const xr_string path = NormalizeLevelPath(canonical_level_path);
	if (path.empty())
		return false;
	const u64 identity = LevelIdentity(path.c_str());
	for (const LevelStaticPackage* package : m_level_cache)
		if (package->key.equal(path.c_str()) && package->identity == identity)
			return true;
	return false;
}

void CRender::WaitLevelPrepare()
{
	NativeLoadExecutor::Batch batch = m_level_prepare_batch;
	m_level_prepare_batch = {};
	if (batch.Valid())
		NativeLoadExecutor::Instance().Wait(batch);
	m_level_prepare_tasks.wait();
}

void CRender::level_Prepare(LPCSTR canonical_level_path)
{
	const xr_string path = NormalizeLevelPath(canonical_level_path);
	if (path.empty() || (b_loaded && m_active_level_key.equal(path.c_str())))
		return;
	NativeLoadExecutor& executor = NativeLoadExecutor::Instance();
	const u64 generation = executor.CurrentGeneration();
	if (m_prepared_level_path.equal(path.c_str()) && m_level_prepare_generation == generation)
		return;
	const u64 identity = LevelIdentity(path.c_str());
	bool static_cache_hit = false;
	for (const LevelStaticPackage* package : m_level_cache)
		if (package->key.equal(path.c_str()) && package->identity == identity)
		{
			static_cache_hit = true;
			break;
		}

	WaitLevelPrepare();
	if (m_prepared_level_geometry)
		dxRenderDeviceRender::Instance().Resources->ReleaseLevelShaderCache(
			m_prepared_level_geometry->key.c_str(), m_prepared_level_geometry->identity);
	xr_delete(m_prepared_level_geometry);
	m_prepared_level_path = path.c_str();
	m_level_prepare_generation = generation;
	m_level_prepare_started_at = Device.TimerAsync();
	m_level_prepare_batch = executor.BeginBatch(executor.CurrentGeneration());
	auto submit = [this, &executor](NativeLoadPriority priority, auto&& work)
	{
		if (m_level_prepare_batch.Valid())
			executor.Submit(m_level_prepare_batch, priority, std::forward<decltype(work)>(work));
		else
			m_level_prepare_tasks.run(std::forward<decltype(work)>(work));
	};
	submit(NativeLoadPriority::Geometry, [path]() { CObjectSpace::PrepareStatic(path.c_str()); });
	if (static_cache_hit)
	{
		Msg("* [LEVEL PREPARE] static cache hit: %s", path.c_str());
		return;
	}
	m_prepared_level_geometry = xr_new<LevelStaticPackage>();
	m_prepared_level_geometry->key = path.c_str();
	m_prepared_level_geometry->identity = identity;
	LevelStaticPackage* geometry = m_prepared_level_geometry;
	const bool detail_recipe_changed = dm_current_size != dm_size ||
		ps_current_detail_density != ps_r__Detail_density || ps_current_detail_height != ps_r__Detail_height;
	if (!g_dedicated_server && (!b_loaded || !detail_recipe_changed))
	{
		CDetailManager::SSwingValue swing[2];
		CDetailManager::SnapshotSwing(swing);
		const CDetailManager::SSwingValue normal_swing = swing[0];
		const CDetailManager::SSwingValue fast_swing = swing[1];
		submit(NativeLoadPriority::Environment, [path, geometry, normal_swing, fast_swing]()
		{
			const CDetailManager::SSwingValue prepared_swing[2] = {normal_swing, fast_swing};
			geometry->details = xr_new<CDetailManager>();
			geometry->details->Load(false, false, path.c_str(), prepared_swing);
			geometry->details_ready = true;
		});
	}
	submit(NativeLoadPriority::Environment, [this, path, geometry]()
	{
		HOM.Prepare(path.c_str(), geometry->hom);
		geometry->hom_ready = true;
	});
	submit(NativeLoadPriority::Environment, [path, geometry]()
	{
		PreparedLights lights;
		PrepareLevelLights(path, lights);
		geometry->prepared_lights_dynamic.swap(lights.dynamic);
		geometry->prepared_lights_hemi.swap(lights.hemi);
		geometry->prepared_lights = xr_new<CLight_DB>();
		geometry->prepared_lights->Prepare(geometry->prepared_lights_dynamic,
			geometry->prepared_lights_hemi);
		geometry->lights_ready = true;
	});
	submit(NativeLoadPriority::Geometry, [this, path, geometry]()
	{
		PrepareLevelChunk(path, fsL_VISUALS, geometry->visual_data);
		auto load_normal = [this, path, geometry]()
		{
			xr_string file_name = path + "level.geom";
			CStreamReader* reader = FS.rs_open(nullptr, file_name.c_str());
			R_ASSERT2(reader, file_name.c_str());
			LoadBuffers(reader, geometry->normal_declarations, geometry->normal_vertex_buffers,
				geometry->normal_index_buffers);
			LoadSWIs(reader, geometry->swis);
			FS.r_close(reader);
		};
		auto load_fast = [this, path, geometry]()
		{
			xr_string file_name = path + "level.geomx";
			CStreamReader* reader = FS.rs_open(nullptr, file_name.c_str());
			R_ASSERT2(reader, file_name.c_str());
			LoadBuffers(reader, geometry->fast_declarations, geometry->fast_vertex_buffers,
				geometry->fast_index_buffers);
			FS.r_close(reader);
		};

		NativeLoadExecutor& executor = NativeLoadExecutor::Instance();
		NativeLoadExecutor::Batch batch = executor.BeginBatch(executor.CurrentGeneration());
		if (batch.Valid())
		{
			executor.Submit(batch, NativeLoadPriority::Geometry, load_normal);
			executor.Submit(batch, NativeLoadPriority::Geometry, load_fast);
			executor.Wait(batch);
		}
		else
		{
			xr_task_group tasks;
			tasks.run(load_normal);
			tasks.run(load_fast);
			tasks.wait();
		}

		geometry->normal_geometry_ready = true;
		geometry->fast_geometry_ready = true;
		VisualGeometrySource source =
		{
			&geometry->normal_declarations, &geometry->fast_declarations,
			&geometry->normal_vertex_buffers, &geometry->fast_vertex_buffers,
			&geometry->normal_index_buffers, &geometry->fast_index_buffers, &geometry->swis
		};
		IReader visuals(geometry->visual_data.data(), static_cast<int>(geometry->visual_data.size()));
		PrepareVisuals(&visuals, geometry->visuals, &source);
		geometry->visual_leaves_ready = true;
	});
	submit(NativeLoadPriority::Environment, [path, geometry]()
	{
		PrepareLevelSectors(path, geometry->prepared_portals, geometry->prepared_sectors,
			geometry->prepared_portals_model);
		geometry->sectors_ready = true;
	});
	submit(NativeLoadPriority::ShaderTexture, [path, geometry, identity]()
	{
		xr_string file_name = path + "level";
		IReader* level = FS.r_open(file_name.c_str());
		R_ASSERT2(level, file_name.c_str());
		IReader* shaders = level->open_chunk(fsL_SHADERS);
		R_ASSERT2(shaders, "Level doesn't builded correctly.");
		const u32 count = shaders->r_u32();
		geometry->shader_indices.assign(count, u32(-1));
		xr_map<xr_string, u32> unique_indices;
		for (u32 i = 0; i < count; ++i)
		{
			string512 shader_name, textures;
			LPCSTR description = LPCSTR(shaders->pointer());
			shaders->skip_stringZ();
			if (!description[0])
				continue;
			xr_strcpy(shader_name, description);
			LPSTR delimiter = strchr(shader_name, '/');
			R_ASSERT(delimiter);
			*delimiter = 0;
			xr_strcpy(textures, delimiter + 1);
			xr_string key = shader_name;
			key += '\n';
			key += textures;
			auto existing = unique_indices.find(key);
			if (existing != unique_indices.end())
			{
				geometry->shader_indices[i] = existing->second;
				continue;
			}
			geometry->shader_indices[i] = static_cast<u32>(geometry->shader_descriptions.size());
			unique_indices.emplace(std::move(key), geometry->shader_indices[i]);
			geometry->shader_descriptions.push_back({shader_name, textures});
		}
		shaders->close();
		FS.r_close(level);

		geometry->cpp_shader_results.resize(geometry->shader_descriptions.size());
		NativeLoadExecutor& shader_executor = NativeLoadExecutor::Instance();
		NativeLoadExecutor::Batch shader_batch = shader_executor.BeginBatch(shader_executor.CurrentGeneration());
		auto compile_shader = [path, geometry, identity](u32 index)
		{
			const LevelShaderDescription& description = geometry->shader_descriptions[index];
			geometry->cpp_shader_results[index] = dxRenderDeviceRender::Instance().Resources->CreateLevelCppShader(
				description.shader.c_str(), description.textures.c_str(), nullptr, nullptr, identity, path.c_str());
		};
		if (shader_batch.Valid())
		{
			for (u32 index = 0; index < geometry->shader_descriptions.size(); ++index)
				shader_executor.Submit(shader_batch, NativeLoadPriority::ShaderTexture,
					[compile_shader, index]() { compile_shader(index); });
			shader_executor.Wait(shader_batch);
		}
		else
			xr_parallel_for(0u, static_cast<u32>(geometry->shader_descriptions.size()), compile_shader);
		geometry->shader_recipes_ready = true;
	});
	const bool prepare_fluids = RImplementation.o.volumetricfog;
	submit(NativeLoadPriority::Environment, [path, geometry, prepare_fluids]()
	{
		if (prepare_fluids)
			PrepareLevelFluids(path, geometry->fluid_descriptors);
		geometry->fluid_descriptors_ready = true;
	});
	Msg("* [LEVEL PREPARE] started: %s", path.c_str());
}

void CRender::level_InvalidateStaticCache()
{
	g_level_asset_generation.fetch_add(1, std::memory_order_acq_rel);
	ReleaseLevelCache();
}

void CRender::level_BeginAsyncLoad()
{
	DiscardPreparedVisuals();
	m_level_shader_descriptions.clear();
	m_level_shader_cpp_results.clear();
	m_level_shader_owner_results.clear();
	m_level_shader_identity = 0;
	m_level_shader_owner_required = false;
	xr_delete(m_level_prepared_hom.model);
	xr_free(m_level_prepared_hom.tris);
	m_level_prepared_hom.enabled = FALSE;
	m_level_prepared_lights_dynamic.clear();
	m_level_prepared_lights_hemi.clear();
	xr_delete(m_level_prepared_lights);
	m_level_prepared_portals.clear();
	m_level_prepared_sectors.clear();
	xr_delete(m_level_prepared_portals_model);
	m_level_owner_reader = nullptr;
	m_level_cache_attach_pending = false;
	m_level_prepared_sectors_ready = false;
	m_level_async_failed.store(false, std::memory_order_release);
}

void CRender::CommitLevelShaderComponentsOwner()
{
	CTimer timer;
	timer.Start();
	if (m_level_async_failed.load(std::memory_order_acquire))
		return;
	if (!m_level_shader_owner_required)
	{
		if (m_level_cache_attach_pending)
		{
			CTimer cache_timer;
			cache_timer.Start();
			HOM.Resume(m_level_prepared_hom);
			g_pGameLevel->ObjectSpace.GetStaticModel()->syncronize();
			CTimer cache_lights_timer;
			cache_lights_timer.Start();
			R_ASSERT(m_level_prepared_lights);
			Lights.Swap(*m_level_prepared_lights);
			Lights.CommitPrepared();
			xr_delete(m_level_prepared_lights);
			Msg("* [LEVEL CACHE] R4 owner lights commit: %d ms", cache_lights_timer.GetElapsed_ms());
			m_active_level_lights_dynamic.swap(m_level_prepared_lights_dynamic);
			m_active_level_lights_hemi.swap(m_level_prepared_lights_hemi);
			if (Details)
				Details->Resume();
			Commit3DFluid();
			m_level_cache_attach_pending = false;
			Msg("* [LEVEL CACHE] R4 owner attach: %d ms", cache_timer.GetElapsed_ms());
		}
		return;
	}

	CResourceManager* resources = dxRenderDeviceRender::Instance().Resources;
	m_level_shader_owner_results.resize(m_level_shader_descriptions.size());
	u32 lua_count = 0;
	for (u32 i = 0; i < m_level_shader_descriptions.size(); ++i)
	{
		const LevelShaderDescription& description = m_level_shader_descriptions[i];
		if (resources->_lua_HasShader(description.shader.c_str()))
		{
			++lua_count;
			m_level_shader_owner_results[i] = resources->CreateLevelShader(
				description.shader.c_str(), description.textures.c_str(), m_level_shader_identity);
		}
		else
		{
			m_level_shader_owner_results[i] = m_level_shader_cpp_results[i];
			if (!m_level_shader_owner_results[i])
				m_level_shader_owner_results[i] = resources->CreateLevelShader(
					description.shader.c_str(), description.textures.c_str(), m_level_shader_identity);
		}
	}
	Msg("* [LEVEL LOAD] R4 owner shader commit: %d ms (%u unique, %u lua)", timer.GetElapsed_ms(),
		static_cast<u32>(m_level_shader_descriptions.size()), lua_count);
	HOM.Resume(m_level_prepared_hom);
	if (Details)
	{
		Details->CommitShaders();
		if (m_level_cache_attach_pending)
			Details->Resume();
		else
			Details->Publish();
	}
	CTimer lights_timer;
	lights_timer.Start();
	R_ASSERT(m_level_prepared_lights);
	Lights.Swap(*m_level_prepared_lights);
	Lights.CommitPrepared();
	xr_delete(m_level_prepared_lights);
	Msg("* [LEVEL LOAD] R4 owner lights commit: %d ms", lights_timer.GetElapsed_ms());
	m_active_level_lights_dynamic.swap(m_level_prepared_lights_dynamic);
	m_active_level_lights_hemi.swap(m_level_prepared_lights_hemi);
	if (m_level_cache_attach_pending)
	{
		g_pGameLevel->ObjectSpace.GetStaticModel()->syncronize();
		Commit3DFluid();
		m_level_cache_attach_pending = false;
	}
}

void CRender::CommitLevelEnvironmentOwner()
{
	if (m_level_async_failed.load(std::memory_order_acquire))
		return;
	R_ASSERT(m_level_owner_reader);
	CTimer environment_timer;
	environment_timer.Start();
	g_pGamePersistent->LoadTitle();
	if (m_level_prepared_visuals_ready)
	{
		R_ASSERT(Visuals.empty());
		IReader visuals(m_level_prepared_visual_data.data(),
			static_cast<int>(m_level_prepared_visual_data.size()));
		LinkPreparedVisuals(&visuals, m_level_prepared_visuals);
		for (dxRender_Visual* visual : m_level_prepared_visuals)
			CommitVisualShaderTree(visual);
		Visuals.swap(m_level_prepared_visuals);
		m_level_prepared_visual_data.clear_and_free();
		m_level_prepared_visuals_ready = false;
	}
	for (dxRender_Visual* visual : Visuals)
		CommitVisualShaderTree(visual);
	if (m_level_prepared_sectors_ready)
		LoadPreparedSectors();
	else
		LoadSectors(m_level_owner_reader);
	g_pGameLevel->ObjectSpace.GetStaticModel()->syncronize();
	Commit3DFluid();
	Msg("* [LEVEL LOAD] R4 owner sectors/fluid commit: %d ms", environment_timer.GetElapsed_ms());
}

void CRender::level_AbortAsyncLoad()
{
	m_level_async_failed.store(true, std::memory_order_release);
	try
	{
		WaitLevelPrepare();
	}
	catch (...)
	{
		// The load path owns and reports the original task failure. Preparation
		// has still been drained, so its private package can now be discarded.
	}
	if (m_prepared_level_geometry)
		dxRenderDeviceRender::Instance().Resources->ReleaseLevelShaderCache(
			m_prepared_level_geometry->key.c_str(), m_prepared_level_geometry->identity);
	xr_delete(m_prepared_level_geometry);
	m_prepared_level_path = nullptr;
	m_level_prepare_generation = 0;
}

void CRender::level_Load(IReader* fs)
{
	CTimer level_timer;
	level_timer.Start();
	R_ASSERT(0!=g_pGameLevel);
	const shared_str level_key = CurrentLevelCacheKey();
	xr_unique_ptr<LevelStaticPackage> prepared_geometry;
	if (m_prepared_level_path.size())
	{
		WaitLevelPrepare();
		if (m_prepared_level_path.equal(level_key))
			Msg("* [LEVEL PREPARE] barrier: %u ms total (%s)", Device.TimerAsync() - m_level_prepare_started_at,
				level_key.c_str());
		prepared_geometry.reset(m_prepared_level_geometry);
		m_prepared_level_geometry = nullptr;
		m_prepared_level_path = nullptr;
		m_level_prepare_generation = 0;
	}
	const bool detail_recipe_changed = dm_current_size != dm_size ||
		ps_current_detail_density != ps_r__Detail_density || ps_current_detail_height != ps_r__Detail_height;
	if (detail_recipe_changed && !b_loaded)
		ReleaseLevelCache();
	const u64 level_identity = CurrentLevelIdentity();
	const bool use_prepared_fluids = prepared_geometry && prepared_geometry->key.equal(level_key) &&
		prepared_geometry->identity == level_identity && prepared_geometry->fluid_descriptors_ready;
	xr_vector<dx103DFluidData::PreparedData> prepared_fluids;
	if (use_prepared_fluids)
		prepared_fluids.swap(prepared_geometry->fluid_descriptors);
	if (b_loaded)
	{
		level_Unload();
		// Cached detail arrays are dimensioned by the currently applied global
		// recipe. Destroy them before CDetailManager applies a new recipe.
		if (detail_recipe_changed)
			ReleaseLevelCache();
	}
	if (RestoreLevelStaticPackage(level_key, level_identity))
	{
		if (prepared_geometry && (!prepared_geometry->key.equal(level_key) ||
			prepared_geometry->identity != level_identity))
		{
			dxRenderDeviceRender::Instance().Resources->ReleaseLevelShaderCache(
				prepared_geometry->key.c_str(), prepared_geometry->identity);
		}
		prepared_geometry.reset();
		CommitLevelShaderComponentsOwner();
		for (u32 i = 0; i < m_active_level_shader_indices.size(); ++i)
			if (m_active_level_shader_indices[i] != u32(-1))
				Shaders[i] = m_level_shader_owner_results[m_active_level_shader_indices[i]];
		for (dxRender_Visual* visual : Visuals)
			CommitVisualShaderTree(visual);
		m_active_level_shader_descriptions = m_level_shader_descriptions;
		m_active_level_shader_cpp_results = m_level_shader_cpp_results;
		Msg("* [LEVEL CACHE] R4 attach: %d ms (%s)", level_timer.GetElapsed_ms(), level_key.c_str());
		return;
	}
	R_ASSERT(!b_loaded);
	const bool use_prepared_geometry = prepared_geometry && prepared_geometry->key.equal(level_key) &&
		prepared_geometry->identity == level_identity && prepared_geometry->normal_geometry_ready &&
		prepared_geometry->fast_geometry_ready;
	const bool use_prepared_shaders = prepared_geometry && prepared_geometry->key.equal(level_key) &&
		prepared_geometry->identity == level_identity && prepared_geometry->shader_recipes_ready;
	const bool use_prepared_visuals = use_prepared_geometry && prepared_geometry->visual_leaves_ready;
	const bool use_prepared_sectors = prepared_geometry && prepared_geometry->key.equal(level_key) &&
		prepared_geometry->identity == level_identity && prepared_geometry->sectors_ready;
	const bool use_prepared_details = prepared_geometry && prepared_geometry->key.equal(level_key) &&
		prepared_geometry->identity == level_identity && prepared_geometry->details_ready;
	const bool use_prepared_hom = prepared_geometry && prepared_geometry->key.equal(level_key) &&
		prepared_geometry->identity == level_identity && prepared_geometry->hom_ready;
	const bool use_prepared_lights = prepared_geometry && prepared_geometry->key.equal(level_key) &&
		prepared_geometry->identity == level_identity && prepared_geometry->lights_ready &&
		prepared_geometry->prepared_lights;
	m_level_fluid_descriptors.swap(prepared_fluids);
	if (!use_prepared_fluids && RImplementation.o.volumetricfog)
		PrepareLevelFluids(level_key.c_str(), m_level_fluid_descriptors);
	xr_vector<LevelShaderDescription> unique_shaders;
	xr_vector<u32> shader_indices;
	CDetailManager* prepared_details = nullptr;
	if (use_prepared_geometry)
	{
		SWIs.swap(prepared_geometry->swis);
		nDC.swap(prepared_geometry->normal_declarations);
		xDC.swap(prepared_geometry->fast_declarations);
		nVB.swap(prepared_geometry->normal_vertex_buffers);
		xVB.swap(prepared_geometry->fast_vertex_buffers);
		nIB.swap(prepared_geometry->normal_index_buffers);
		xIB.swap(prepared_geometry->fast_index_buffers);
		Msg("* [LEVEL PREPARE] geometry committed: %s", level_key.c_str());
	}
	if (use_prepared_shaders)
	{
		unique_shaders.swap(prepared_geometry->shader_descriptions);
		shader_indices.swap(prepared_geometry->shader_indices);
		m_level_shader_cpp_results.swap(prepared_geometry->cpp_shader_results);
		Msg("* [LEVEL PREPARE] C++ shader recipes committed: %u", static_cast<u32>(unique_shaders.size()));
	}
	if (use_prepared_visuals)
	{
		m_level_prepared_visual_data.swap(prepared_geometry->visual_data);
		m_level_prepared_visuals.swap(prepared_geometry->visuals);
		m_level_prepared_visuals_ready = true;
	}
	if (use_prepared_sectors)
	{
		m_level_prepared_portals.swap(prepared_geometry->prepared_portals);
		m_level_prepared_sectors.swap(prepared_geometry->prepared_sectors);
		m_level_prepared_portals_model = prepared_geometry->prepared_portals_model;
		prepared_geometry->prepared_portals_model = nullptr;
		m_level_prepared_sectors_ready = true;
	}
	if (use_prepared_details)
	{
		prepared_details = prepared_geometry->details;
		prepared_geometry->details = nullptr;
	}
	if (use_prepared_hom)
	{
		m_level_prepared_hom.model = prepared_geometry->hom.model;
		m_level_prepared_hom.tris = prepared_geometry->hom.tris;
		m_level_prepared_hom.enabled = prepared_geometry->hom.enabled;
		prepared_geometry->hom.model = nullptr;
		prepared_geometry->hom.tris = nullptr;
		prepared_geometry->hom.enabled = FALSE;
	}
	if (use_prepared_lights)
	{
		m_level_prepared_lights_dynamic.swap(prepared_geometry->prepared_lights_dynamic);
		m_level_prepared_lights_hemi.swap(prepared_geometry->prepared_lights_hemi);
		m_level_prepared_lights = prepared_geometry->prepared_lights;
		prepared_geometry->prepared_lights = nullptr;
	}
	if (prepared_geometry && !use_prepared_shaders)
		dxRenderDeviceRender::Instance().Resources->ReleaseLevelShaderCache(
			prepared_geometry->key.c_str(), prepared_geometry->identity);
	prepared_geometry.reset();

	// Begin
	pApp->LoadBegin();
	dxRenderDeviceRender::Instance().Resources->DeferredLoad(TRUE);
	IReader* chunk;

	// Shaders
	//	g_pGamePersistent->LoadTitle		("st_loading_shaders");
	g_pGamePersistent->LoadTitle();
	if (!use_prepared_shaders)
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
	else
		Shaders.resize(shader_indices.size());

	m_level_shader_descriptions = unique_shaders;
	m_level_shader_identity = level_identity;
	m_level_shader_owner_required = true;
	if (!use_prepared_shaders)
		m_level_shader_cpp_results.resize(unique_shaders.size());

	NativeLoadExecutor& load_executor = NativeLoadExecutor::Instance();
	NativeLoadExecutor::Batch component_batch = load_executor.BeginBatch(load_executor.CurrentGeneration());
	xr_task_group fallback_component_tasks;
	auto submit_component = [&load_executor, &component_batch, &fallback_component_tasks](
		NativeLoadPriority priority, auto&& work)
	{
		if (component_batch.Valid())
			load_executor.Submit(component_batch, priority, std::forward<decltype(work)>(work));
		else
			fallback_component_tasks.run(std::forward<decltype(work)>(work));
	};
	CTimer shader_timer;
	shader_timer.Start();
	if (!use_prepared_shaders)
		for (u32 index = 0; index < unique_shaders.size(); ++index)
			submit_component(NativeLoadPriority::ShaderTexture, [this, &unique_shaders, level_identity, level_key, index]()
			{
				const LevelShaderDescription& description = unique_shaders[index];
				m_level_shader_cpp_results[index] = dxRenderDeviceRender::Instance().Resources->CreateLevelCppShader(
					description.shader.c_str(), description.textures.c_str(), nullptr, nullptr, level_identity,
					level_key.c_str());
			});

	// Components
	Wallmarks = xr_new<CWallmarksEngine>();
	Details = prepared_details ? prepared_details : xr_new<CDetailManager>();
	if (!use_prepared_lights)
	submit_component(NativeLoadPriority::Environment, [this, level_key]()
	{
		CTimer timer;
		timer.Start();
		PreparedLights prepared_lights;
		PrepareLevelLights(level_key.c_str(), prepared_lights);
		m_level_prepared_lights_dynamic.swap(prepared_lights.dynamic);
		m_level_prepared_lights_hemi.swap(prepared_lights.hemi);
		m_level_prepared_lights = xr_new<CLight_DB>();
		m_level_prepared_lights->Prepare(m_level_prepared_lights_dynamic, m_level_prepared_lights_hemi);
		Msg("* [LEVEL LOAD] R4 lights prepare: %d ms", timer.GetElapsed_ms());
	});
	if (!use_prepared_hom)
	submit_component(NativeLoadPriority::Environment, [this, level_key]()
	{
		CTimer timer;
		timer.Start();
		HOM.Prepare(level_key.c_str(), m_level_prepared_hom);
		Msg("* [LEVEL LOAD] R4 HOM: %d ms", timer.GetElapsed_ms());
	});
	if (!g_dedicated_server && !use_prepared_details)
	{
		CDetailManager::SSwingValue swing[2];
		CDetailManager::SnapshotSwing(swing);
		const CDetailManager::SSwingValue normal_swing = swing[0];
		const CDetailManager::SSwingValue fast_swing = swing[1];
		submit_component(NativeLoadPriority::Environment,
			[this, level_key, normal_swing, fast_swing]()
			{
				CTimer timer;
				timer.Start();
				const CDetailManager::SSwingValue prepared_swing[2] = {normal_swing, fast_swing};
				Details->Load(false, false, level_key.c_str(), prepared_swing);
				Msg("* [LEVEL LOAD] R4 details prepare: %d ms", timer.GetElapsed_ms());
			});
	}

	auto commit_prepared_components = [&]()
	{
		if (component_batch.Valid())
			load_executor.Wait(component_batch);
		fallback_component_tasks.wait();
		if (!use_prepared_shaders)
			Msg("* [LEVEL LOAD] R4 C++ shader prepare: %d ms (%u speculative)", shader_timer.GetElapsed_ms(),
				static_cast<u32>(unique_shaders.size()));
		CommitLevelShaderComponentsOwner();
		R_ASSERT2(!m_level_async_failed.load(std::memory_order_acquire), "R4 owner shader/environment commit failed");
		for (u32 i = 0; i < shader_indices.size(); ++i)
			if (shader_indices[i] != u32(-1))
				Shaders[i] = m_level_shader_owner_results[shader_indices[i]];
		m_active_level_shader_descriptions = m_level_shader_descriptions;
		m_active_level_shader_cpp_results = m_level_shader_cpp_results;
		m_active_level_shader_indices = shader_indices;
	};

	if (!g_dedicated_server)
	{
		// VB,IB,SWI
		//		g_pGamePersistent->LoadTitle("st_loading_geometry");
		g_pGamePersistent->LoadTitle();
		CTimer geometry_timer;
		geometry_timer.Start();
		NativeLoadExecutor::Batch geometry_batch = load_executor.BeginBatch(load_executor.CurrentGeneration());
		xr_task_group fallback_geometry_tasks;
		auto submit_geometry = [&load_executor, &geometry_batch, &fallback_geometry_tasks](auto&& work)
		{
			if (geometry_batch.Valid())
				load_executor.Submit(geometry_batch, NativeLoadPriority::Geometry,
					std::forward<decltype(work)>(work));
			else
				fallback_geometry_tasks.run(std::forward<decltype(work)>(work));
		};
		if (!use_prepared_geometry)
		submit_geometry([this, level_key]()
		{
			CTimer timer;
			timer.Start();
			xr_string file_name = xr_string(level_key.c_str()) + "level.geom";
			CStreamReader* geom = FS.rs_open(nullptr, file_name.c_str());
			R_ASSERT2(geom, file_name.c_str());
			LoadBuffers(geom, nDC, nVB, nIB);
			LoadSWIs(geom, SWIs);
			FS.r_close(geom);
			Msg("* [LEVEL LOAD] R4 level.geom: %d ms", timer.GetElapsed_ms());
		});

		//...and alternate/fast geometry
		if (!use_prepared_geometry)
		submit_geometry([this, level_key]()
		{
			CTimer timer;
			timer.Start();
			xr_string file_name = xr_string(level_key.c_str()) + "level.geomx";
			CStreamReader* geom = FS.rs_open(nullptr, file_name.c_str());
			R_ASSERT2(geom, file_name.c_str());
			LoadBuffers(geom, xDC, xVB, xIB);
			FS.r_close(geom);
			Msg("* [LEVEL LOAD] R4 level.geomx: %d ms", timer.GetElapsed_ms());
		});
		commit_prepared_components();
		if (geometry_batch.Valid())
			load_executor.Wait(geometry_batch);
		fallback_geometry_tasks.wait();
		Msg("* [LEVEL LOAD] R4 geometry barrier: %d ms", geometry_timer.GetElapsed_ms());

		// Visuals
		//		g_pGamePersistent->LoadTitle("st_loading_spatial_db");
		g_pGamePersistent->LoadTitle();
		CTimer visuals_timer;
		visuals_timer.Start();
		if (!use_prepared_visuals)
		{
			IReader* visuals = fs->open_chunk(fsL_VISUALS);
			R_ASSERT(visuals);
			CopyReader(*visuals, m_level_prepared_visual_data);
			visuals->close();
			IReader prepared(m_level_prepared_visual_data.data(),
				static_cast<int>(m_level_prepared_visual_data.size()));
			PrepareVisuals(&prepared, m_level_prepared_visuals, nullptr);
			m_level_prepared_visuals_ready = true;
		}
		Msg("* [LEVEL LOAD] R4 visual leaves prepare: %d ms", visuals_timer.GetElapsed_ms());
	}
	else
	{
		commit_prepared_components();
	}

	// Sectors, spatial publication and fluid attachment are owner-thread state.
	m_level_owner_reader = fs;
	CommitLevelEnvironmentOwner();
	R_ASSERT2(!m_level_async_failed.load(std::memory_order_acquire), "R4 owner sectors/fluid commit failed");
	m_level_owner_reader = nullptr;

	// End
	pApp->LoadEnd();

	// signal loaded
	b_loaded = TRUE;
	m_active_level_key = level_key;
	m_active_level_identity = level_identity;
	Msg("* [LEVEL LOAD] R4 total: %d ms", level_timer.GetElapsed_ms());
}

void CRender::level_Unload()
{
	if (!b_loaded) return;

	const bool clear_resources = psDeviceFlags2.test(rsClearAllResources);
	const bool clear_models = psDeviceFlags2.test(rsClearModels) || clear_resources;
	if (!clear_models)
	{
		dxRenderDeviceRender::Instance().Resources->WaitForTextureLoads();
		LevelStaticPackage* package = DetachLevelStaticPackage();
		Msg("* [LEVEL CACHE] R4 retained: %s", package->key.c_str());
		m_level_cache.push_back(package);
		EvictLevelCacheUnderPressure();
		return;
	}

	DestroyActiveLevel();
	ReleaseLevelCache();
	Models->ClearPool(true);
	Visuals.clear_and_free();
	if (clear_resources)
	{
		dxRenderDeviceRender::Instance().Resources->UnloadAllTexturesOnLevelUnload();
		dxRenderDeviceRender::Instance().ResourcesDestroyNecessaryTextures();
		dxRenderDeviceRender::Instance().Resources->Evict();
	}
	dxRenderDeviceRender::Instance().Resources->Dump(false);
}

CRender::LevelStaticPackage* CRender::DetachLevelStaticPackage()
{
	VERIFY(b_loaded);
	LevelStaticPackage* package = xr_new<LevelStaticPackage>();
	package->key = m_active_level_key;
	package->identity = m_active_level_identity;

	GMBase.clear();
	GMRainWet.clear();
	for (sun::cascade& cascade : m_sun_cascades)
		cascade.GMCascade.clear();

	Remove3DFluid();
	package->fluid_descriptors.swap(m_level_fluid_descriptors);
	HOM.Suspend(package->hom);
	if (Details)
	{
		Details->SuspendShaders();
		Details->Suspend();
	}
	Lights.PrepareForCache();
	package->prepared_lights = xr_new<CLight_DB>();
	package->prepared_lights->Swap(Lights);

	package->details = Details;
	Details = nullptr;
	package->light_dynamic.swap(m_active_level_lights_dynamic);
	package->light_hemi.swap(m_active_level_lights_hemi);
	package->portals_model = rmPortals;
	rmPortals = nullptr;
	package->outdoor_sector = pOutdoorSector;
	Portals.swap(package->portals);
	Sectors.swap(package->sectors);
	SWIs.swap(package->swis);
	for (ref_shader& shader : Shaders)
		shader = nullptr;
	Shaders.swap(package->shaders);
	package->shader_descriptions.swap(m_active_level_shader_descriptions);
	package->shader_indices.swap(m_active_level_shader_indices);
	package->cpp_shader_results.swap(m_active_level_shader_cpp_results);
	m_level_shader_descriptions.clear();
	m_level_shader_cpp_results.clear();
	m_level_shader_owner_results.clear();
	nDC.swap(package->normal_declarations);
	xDC.swap(package->fast_declarations);
	nVB.swap(package->normal_vertex_buffers);
	xVB.swap(package->fast_vertex_buffers);
	nIB.swap(package->normal_index_buffers);
	xIB.swap(package->fast_index_buffers);
	for (dxRender_Visual* visual : Visuals)
		SuspendVisualShaderTree(visual);
	Visuals.swap(package->visuals);

	xr_delete(Wallmarks);
	pLastSector = nullptr;
	pOutdoorSector = nullptr;
	vLastCameraPos.set(0.f, 0.f, 0.f);
	b_loaded = FALSE;
	m_active_level_key = nullptr;
	m_active_level_identity = 0;
	return package;
}

bool CRender::RestoreLevelStaticPackage(const shared_str& key, u64 identity)
{
	for (u32 i = 0; i < m_level_cache.size(); ++i)
	{
		LevelStaticPackage* package = m_level_cache[i];
		if (!package->key.equal(key))
			continue;

		m_level_cache.erase(m_level_cache.begin() + i);
		if (package->identity != identity)
		{
			Msg("* [LEVEL CACHE] R4 stale: %s", key.c_str());
			dxRenderDeviceRender::Instance().Resources->ReleaseLevelShaderCache(
				package->key.c_str(), package->identity);
			xr_delete(package);
			return false;
		}

		Details = package->details;
		package->details = nullptr;
		m_level_prepared_lights_dynamic.swap(package->light_dynamic);
		m_level_prepared_lights_hemi.swap(package->light_hemi);
		m_level_prepared_lights = package->prepared_lights;
		package->prepared_lights = nullptr;
		rmPortals = package->portals_model;
		package->portals_model = nullptr;
		pOutdoorSector = package->outdoor_sector;
		Portals.swap(package->portals);
		Sectors.swap(package->sectors);
		SWIs.swap(package->swis);
		Shaders.swap(package->shaders);
		m_level_shader_descriptions.swap(package->shader_descriptions);
		m_active_level_shader_indices.swap(package->shader_indices);
		m_level_shader_cpp_results.swap(package->cpp_shader_results);
		m_level_shader_owner_results.clear();
		m_level_shader_identity = identity;
		m_level_shader_owner_required = true;
		nDC.swap(package->normal_declarations);
		xDC.swap(package->fast_declarations);
		nVB.swap(package->normal_vertex_buffers);
		xVB.swap(package->fast_vertex_buffers);
		nIB.swap(package->normal_index_buffers);
		xIB.swap(package->fast_index_buffers);
		Visuals.swap(package->visuals);
		m_level_fluid_descriptors.swap(package->fluid_descriptors);
		m_level_prepared_hom.model = package->hom.model;
		m_level_prepared_hom.tris = package->hom.tris;
		m_level_prepared_hom.enabled = package->hom.enabled;
		package->hom.model = nullptr;
		package->hom.tris = nullptr;
		package->hom.enabled = FALSE;
		xr_delete(package);

		Wallmarks = xr_new<CWallmarksEngine>();
		pLastSector = nullptr;
		vLastCameraPos.set(0.f, 0.f, 0.f);
		for (dxRender_Visual* visual : Visuals)
		{
			visual->vis.hom_frame = 0;
			visual->vis.hom_tested = 0;
		}

		b_loaded = TRUE;
		m_active_level_key = key;
		m_active_level_identity = identity;
		m_level_cache_attach_pending = true;
		return true;
	}
	return false;
}

void CRender::ReleaseLevelCache()
{
	WaitLevelPrepare();
	if (m_prepared_level_geometry)
		dxRenderDeviceRender::Instance().Resources->ReleaseLevelShaderCache(
			m_prepared_level_geometry->key.c_str(), m_prepared_level_geometry->identity);
	xr_delete(m_prepared_level_geometry);
	m_prepared_level_path = nullptr;
	m_level_prepare_generation = 0;
	xr_delete(m_level_prepared_portals_model);
	m_level_prepared_portals.clear_and_free();
	m_level_prepared_sectors.clear_and_free();
	m_level_prepared_sectors_ready = false;
	for (LevelStaticPackage*& package : m_level_cache)
	{
		dxRenderDeviceRender::Instance().Resources->ReleaseLevelShaderCache(
			package->key.c_str(), package->identity);
		xr_delete(package);
	}
	m_level_cache.clear();
}

void CRender::EvictLevelCacheUnderPressure()
{
	MEMORYSTATUSEX memory = {};
	memory.dwLength = sizeof(memory);
	while (!m_level_cache.empty() && GlobalMemoryStatusEx(&memory))
	{
		const u64 minimum_available = _max(2ull * 1024 * 1024 * 1024, memory.ullTotalPhys * 15 / 100);
		if (memory.ullAvailPhys >= minimum_available)
			break;
		LevelStaticPackage* package = m_level_cache.front();
		Msg("* [LEVEL CACHE] R4 evicted under memory pressure: %s", package->key.c_str());
		m_level_cache.erase(m_level_cache.begin());
		dxRenderDeviceRender::Instance().Resources->ReleaseLevelShaderCache(
			package->key.c_str(), package->identity);
		xr_delete(package);
	}
}

void CRender::DestroyActiveLevel()
{
	if (!b_loaded)
		return;
	dxRenderDeviceRender::Instance().Resources->WaitForTextureLoads();
	LevelStaticPackage* package = DetachLevelStaticPackage();
	dxRenderDeviceRender::Instance().Resources->ReleaseLevelShaderCache(
		package->key.c_str(), package->identity);
	xr_delete(package);
}

void CRender::LoadBuffers(CStreamReader* base_fs, xr_vector<VertexDeclarator>& declarations,
	xr_vector<ID3DVertexBuffer*>& vertex_buffers, xr_vector<ID3DIndexBuffer*>& index_buffers)
{
	R_ASSERT2(base_fs, "Could not load geometry. File not found.");
	//	u32	dwUsage					= D3DUSAGE_WRITEONLY;

	xr_vector<VertexDeclarator>& _DC = declarations;
	xr_vector<ID3DVertexBuffer*>& _VB = vertex_buffers;
	xr_vector<ID3DIndexBuffer*>& _IB = index_buffers;
	NativeLoadExecutor& executor = NativeLoadExecutor::Instance();
	NativeLoadExecutor::Batch buffer_batch = executor.BeginBatch(executor.CurrentGeneration());
	xr_task_group fallback_tasks;
	auto submit = [&executor, &buffer_batch, &fallback_tasks](auto&& work)
	{
		if (buffer_batch.Valid())
			executor.Submit(buffer_batch, NativeLoadPriority::Geometry, std::forward<decltype(work)>(work));
		else
			fallback_tasks.run(std::forward<decltype(work)>(work));
	};
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
			submit([&, i]()
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
			submit([&, i]()
			{
				dx10BufferUtils::CreateIndexBuffer(&_IB[i], index_data[i].data(), static_cast<UINT>(index_data[i].size()));
			});
	}

	if (buffer_batch.Valid())
		executor.Wait(buffer_batch);
	fallback_tasks.wait();
}

void CRender::LoadVisualLeaf(dxRender_Visual* visual, IReader* chunk, const VisualGeometrySource* geometry)
{
	const bool previous_defer = g_defer_visual_shader_creation;
	const VisualGeometrySource* previous_geometry = m_visual_geometry_source;
	g_defer_visual_shader_creation = true;
	m_visual_geometry_source = geometry;
	try
	{
		visual->Load(nullptr, chunk, 0);
	}
	catch (...)
	{
		m_visual_geometry_source = previous_geometry;
		g_defer_visual_shader_creation = previous_defer;
		throw;
	}
	m_visual_geometry_source = previous_geometry;
	g_defer_visual_shader_creation = previous_defer;
}

void CRender::PrepareVisuals(IReader* fs, xr_vector<dxRender_Visual*>& visuals,
	const VisualGeometrySource* geometry)
{
	R_ASSERT(visuals.empty());
	xr_vector<IReader*> chunks;
	xr_vector<ogf_header> headers;
	for (u32 index = 0;; ++index)
	{
		IReader* chunk = fs->open_chunk(index);
		if (!chunk)
			break;
		ogf_header header;
		chunk->r_chunk_safe(OGF_HEADER, &header, sizeof(header));
		chunks.push_back(chunk);
		headers.push_back(header);
	}

	visuals.resize(chunks.size());
	for (u32 index = 0; index < chunks.size(); ++index)
		visuals[index] = Models->Instance_Create(headers[index].type);

	// Every slot exists before loading begins. Leaf visuals own disjoint state
	// and only read immutable shader/geometry registries, so they can load in
	// parallel. Hierarchies link those slots and FLOD reads owner RCache state;
	// keep both in the original index order after the leaf barrier.
	auto load_leaf = [&](u32 index)
	{
		if (!IsWorkerSafeLevelVisual(headers[index].type))
			return;
		LoadVisualLeaf(visuals[index], chunks[index], geometry);
		chunks[index]->close();
		chunks[index] = nullptr;
	};
	NativeLoadExecutor& executor = NativeLoadExecutor::Instance();
	NativeLoadExecutor::Batch visual_batch = executor.BeginBatch(executor.CurrentGeneration());
	try
	{
		if (visual_batch.Valid())
		{
			for (u32 index = 0; index < chunks.size(); ++index)
				executor.Submit(visual_batch, NativeLoadPriority::Geometry,
					[&load_leaf, index]() { load_leaf(index); });
			executor.Wait(visual_batch);
		}
		else
			xr_parallel_for(0u, static_cast<u32>(chunks.size()), load_leaf);
	}
	catch (...)
	{
		for (IReader*& chunk : chunks)
			if (chunk)
				chunk->close();
		throw;
	}
	for (IReader*& chunk : chunks)
		if (chunk)
			chunk->close();
}

void CRender::LinkPreparedVisuals(IReader* fs, xr_vector<dxRender_Visual*>& visuals)
{
	const xr_vector<dxRender_Visual*>* previous_visuals = m_visual_table_source;
	m_visual_table_source = &visuals;
	u32 index = 0;
	try
	{
		for (;; ++index)
		{
			IReader* chunk = fs->open_chunk(index);
			if (!chunk)
				break;
			try
			{
				R_ASSERT(index < visuals.size());
				ogf_header header;
				R_ASSERT(chunk->r_chunk_safe(OGF_HEADER, &header, sizeof(header)));
				R_ASSERT(visuals[index]->getType() == header.type);
				if (header.type == MT_HIERRARHY || header.type == MT_LOD)
					LoadVisualLeaf(visuals[index], chunk, nullptr);
				else if (!IsWorkerSafeLevelVisual(header.type))
					visuals[index]->Load(nullptr, chunk, 0);
			}
			catch (...)
			{
				chunk->close();
				throw;
			}
			chunk->close();
		}
		R_ASSERT(index == visuals.size());
	}
	catch (...)
	{
		m_visual_table_source = previous_visuals;
		throw;
	}
	m_visual_table_source = previous_visuals;
}

void CRender::DiscardPreparedVisuals()
{
	for (dxRender_Visual*& visual : m_level_prepared_visuals)
	{
		visual->Release();
		xr_delete(visual);
	}
	m_level_prepared_visuals.clear_and_free();
	m_level_prepared_visual_data.clear_and_free();
	m_level_prepared_visuals_ready = false;
}

void CRender::LoadLights(IReader* fs)
{
	// lights
	Lights.Load(fs);
	Lights.LoadHemi();
}

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

void CRender::LoadPreparedSectors()
{
	R_ASSERT(m_level_prepared_sectors_ready);
	R_ASSERT(m_level_prepared_portals.size() % sizeof(b_portal) == 0);
	const u32 portal_count = static_cast<u32>(m_level_prepared_portals.size() / sizeof(b_portal));
	Portals.resize(portal_count);
	for (u32 index = 0; index < portal_count; ++index)
		Portals[index] = xr_new<CPortal>();

	for (xr_vector<u8>& bytes : m_level_prepared_sectors)
	{
		IReader reader(bytes.data(), static_cast<int>(bytes.size()));
		CSector* sector = xr_new<CSector>();
		sector->load(reader);
		Sectors.push_back(sector);
	}

	for (u32 index = 0; index < portal_count; ++index)
	{
		b_portal portal;
		CopyMemory(&portal, m_level_prepared_portals.data() + index * sizeof(b_portal), sizeof(portal));
		CPortal* target = static_cast<CPortal*>(Portals[index]);
		target->Setup(portal.vertices.begin(), portal.vertices.size(),
			static_cast<CSector*>(getSector(portal.sector_front)),
			static_cast<CSector*>(getSector(portal.sector_back)));
	}
	rmPortals = m_level_prepared_portals_model;
	m_level_prepared_portals_model = nullptr;
	m_level_prepared_portals.clear_and_free();
	m_level_prepared_sectors.clear_and_free();
	m_level_prepared_sectors_ready = false;

	pLastSector = nullptr;
	CSector* largest_sector = nullptr;
	float largest_sector_volume = 0.f;
	for (IRender_Sector* item : Sectors)
	{
		CSector* sector = static_cast<CSector*>(item);
		const float volume = sector->root()->vis.box.getvolume();
		if (volume > largest_sector_volume)
		{
			largest_sector_volume = volume;
			largest_sector = sector;
		}
	}
	pOutdoorSector = largest_sector;
}

void CRender::LoadSWIs(CStreamReader* base_fs, xr_vector<FSlideWindowItem>& swis)
{
	// allocate memory for portals
	if (base_fs->find_chunk(fsL_SWIS))
	{
		CStreamReader* fs = base_fs->open_chunk(fsL_SWIS);
		u32 item_count = fs->r_u32();

		xr_vector<FSlideWindowItem>::iterator it = swis.begin();
		xr_vector<FSlideWindowItem>::iterator it_e = swis.end();

		for (; it != it_e; ++it)
			xr_free((*it).sw);

		swis.clear_not_free();

		swis.resize(item_count);
		for (u32 c = 0; c < item_count; c++)
		{
			FSlideWindowItem& swi = swis[c];
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

void CRender::Remove3DFluid()
{
	for (dxRender_Visual* visual : Visuals)
	{
		if (visual->getType() != MT_HIERRARHY)
			continue;
		FHierrarhyVisual* hierarchy = static_cast<FHierrarhyVisual*>(visual);
		for (auto child = hierarchy->children.begin(); child != hierarchy->children.end();)
		{
			if ((*child)->getType() != MT_3DFLUIDVOLUME)
			{
				++child;
				continue;
			}
			dxRender_Visual* fluid = static_cast<dxRender_Visual*>(*child);
			fluid->Release();
			xr_delete(fluid);
			child = hierarchy->children.erase(child);
		}
	}
}

void CRender::Commit3DFluid()
{
	if (!RImplementation.o.volumetricfog)
		return;

	for (const dx103DFluidData::PreparedData& prepared : m_level_fluid_descriptors)
	{
		dx103DFluidVolume* pVolume = xr_new<dx103DFluidVolume>();
		pVolume->LoadPrepared(prepared);

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
