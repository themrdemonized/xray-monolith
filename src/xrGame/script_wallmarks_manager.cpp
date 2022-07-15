#pragma once
#include "stdafx.h"
#include "script_wallmarks_manager.h"
#include "Level.h"
#include "material_manager.h"
#include "../Include/xrRender/RenderVisual.h"

ScriptWallmarksArray::ScriptWallmarksArray(LPCSTR section)
{
	m_wallmarks = xr_new<FactoryPtr<IWallMarkArray>>();
	m_section = section;

	R_ASSERT2(pSettings->section_exist(section), make_string("[ScriptWallmarksManager] Can't find section '%s'", m_section));
	LPCSTR wallmarks_string = READ_IF_EXISTS(pSettings, r_string, m_section, "wallmarks", nullptr);
	R_ASSERT2(wallmarks_string, make_string("[ScriptWallmarksManager] Can't find 'wallmarks' in section '%s'", m_section));

	string256 tmp;
	int cnt = _GetItemCount(wallmarks_string);

	for (int k = 0; k < cnt; ++k)
		(*m_wallmarks)->AppendMark(_GetItem(wallmarks_string, k, tmp));
}

ScriptWallmarksManager::ScriptWallmarksManager() {};

ScriptWallmarksManager::~ScriptWallmarksManager()
{
	for (auto i : m_script_wallmarks)
	{
		xr_delete(i->m_wallmarks);
		xr_delete(i);
	}
};

IWallMarkArray* ScriptWallmarksManager::FindSection(LPCSTR section)
{
	for (auto i : m_script_wallmarks)
	{
		if (!xr_strcmp(i->m_section, section))
			return &**i->m_wallmarks;
	}

	ScriptWallmarksArray* wm = xr_new<ScriptWallmarksArray>(section);
	m_script_wallmarks.push_back(wm);
	return &**wm->m_wallmarks;
}

void ScriptWallmarksManager::PlaceWallmark(Fvector dir, Fvector start_pos,
	float trace_dist, float wallmark_size, LPCSTR section,
	CScriptGameObject* ignore_obj, float ttl)
{
	PlaceWallmark(dir, start_pos, trace_dist, wallmark_size, section, ignore_obj, ttl, true);
}

void ScriptWallmarksManager::PlaceWallmark(Fvector dir, Fvector start_pos,
	float trace_dist, float wallmark_size, LPCSTR section,
	CScriptGameObject* ignore_obj, float ttl, bool random_rotation)
{
	collide::rq_result result;
	BOOL reach_wall =
		Level().ObjectSpace.RayPick(
			start_pos,
			dir,
			trace_dist,
			collide::rqtBoth,
			result,
			ignore_obj ? &ignore_obj->object() : nullptr
		)
		&&
		!result.O;

	if (reach_wall)
	{
		CDB::TRI* pTri = Level().ObjectSpace.GetStaticTris() + result.element;
		SGameMtl* pMaterial = GMLib.GetMaterialByIdx(pTri->material);

		if (!pMaterial->Flags.is(SGameMtl::flSuppressWallmarks))
		{
			Fvector* pVerts = Level().ObjectSpace.GetStaticVerts();
			Fvector end_point;
			end_point.set(0, 0, 0);
			end_point.mad(start_pos, dir, result.range);

			::Render->add_StaticWallmark(FindSection(section), end_point, wallmark_size, pTri, pVerts, ttl, true, random_rotation);
		}
	}
}

void ScriptWallmarksManager::PlaceSkeletonWallmark(CScriptGameObject* obj, LPCSTR section, 
	Fvector start, Fvector dir, float size, float ttl)
{
	if (!obj)
	{
		Msg("[ScriptWallmarksManager] object is null!");
		return;
	}

	CObject* object = &obj->object();
	Render->add_SkeletonWallmark(&object->renderable.xform, object->Visual()->dcast_PKinematics(), FindSection(section), start,
		dir, size, ttl, true);
}

