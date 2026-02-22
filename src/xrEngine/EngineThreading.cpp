#include "stdafx.h"
#include "EngineThreading.h"
#include "CustomHUD.h"
#include "IGame_Persistent.h"
#include "IGame_Level.h"
#include "Rain.h"
#include "../../xrCDB/ISpatial.h"
#include "../../xrCDB/Frustum.h"
#include "Render.h"
#include "Irenderable.h"
#include "../../Include/xrRender/Kinematics.h"

BOOL mt_Scheduler = TRUE;

void XRay::Engine::PreRenderThread()
{
	PROF_THREAD("Secondary Task 1");

	{
		PROF_EVENT("seqParallelRender");
		for (auto& it : Device.seqParallelRender)
			it();
	}

	if (g_pGamePersistent && g_pGamePersistent->pEnvironment && g_pGamePersistent->pEnvironment->eff_Rain)
	{
		PROF_EVENT("CEffect_Rain::UpdateItems");
		g_pGamePersistent->pEnvironment->eff_Rain->UpdateItems();
	}

	if (Device.ParticleWorkerCallback)
	{
		PROF_EVENT("Process Particles");
		Device.ParticleWorkerCallback();
	}
}

struct SpatialSnapshot
{
	ISpatialShared ptr;
	Fvector P;
	float R;
	u32 type;
	IRenderable* renderable;
};
void XRay::Engine::CalculateBonesThread()
{
	PROF_THREAD("Secondary Task 3");

	PROF_EVENT("CalculateBones");

	if (!g_SpatialSpace) return;
	if (Device.Paused()) return;
	if (!psDeviceFlags.test(rsDrawDynamic)) return;
	if (!g_pGameLevel || !g_pGameLevel->bReady) return;
	if (!g_pGameLevel->CurrentEntity()) return;

	static CFrustum ViewBase;
	ViewBase.CreateFromMatrix(Device.mFullTransform_saved, FRUSTUM_P_LRTB | FRUSTUM_P_FAR);

	static xr_vector<ISpatialShared> spatials = {};
	g_SpatialSpace->q_sphere(
		spatials,
		ISpatial_DB::O_ORDERED,
		STYPE_RENDERABLE + STYPE_RENDERABLESHADOW + STYPE_PARTICLE + STYPE_LIGHTSOURCE,
		Device.vCameraPosition_saved,
		g_pGamePersistent->Environment().CurrentEnv->fog_distance
	);

	static xr_vector<SpatialSnapshot> spatialsSnapshot;
	spatialsSnapshot.clear();
	{
		xrSRWLockGuard g(g_SpatialSpace->db_lock);
		for (ISpatialShared& spatial : spatials)
		{
			if (!spatial)
				continue;

			if (!ViewBase.testSphere_dirty(spatial->spatial.sphere.P, spatial->spatial.sphere.R))
			{
				if (Device.vCameraPosition_saved.distance_to_sqr(spatial->spatial.sphere.P) > 62500.f)//250 m
					continue;
			}
				
			spatial->spatial_updatesector();
			spatialsSnapshot.push_back({ spatial, spatial->spatial.sphere.P, spatial->spatial.sphere.R, spatial->spatial.type, spatial->dcast_Renderable() });
		}
	}

	static auto sortFunc = [](const SpatialSnapshot& _1, const SpatialSnapshot& _2)
	{
		return _1.P.distance_to_sqr(Device.vCameraPosition_saved) < _2.P.distance_to_sqr(Device.vCameraPosition_saved);
	};
	std::sort(spatialsSnapshot.begin(), spatialsSnapshot.end(), sortFunc);

	for (const auto& spatial : spatialsSnapshot)
	{
		if (spatial.renderable && spatial.renderable->renderable.visual && (spatial.type & (STYPE_RENDERABLE | STYPE_RENDERABLESHADOW)))
		{
			IKinematics* pKin = spatial.renderable->renderable.visual->dcast_PKinematics();
			if (pKin)
				pKin->CalculateBones(TRUE);
		}
	}
}

extern BOOL psLua_ParallelGC;
int psLua_ParallelGC_CallAmount = 25;
void XRay::Engine::GameThread()
{
	PROF_THREAD("Secondary Task 2")
		
	// we has granted permission to execute
	if (g_hud)
	{
		PROF_EVENT("g_hud OnFrameMT");
		g_hud->OnFrameMT();
	}

	if (g_pGameLevel && g_pGameLevel->bReady)
	{
		PROF_EVENT("SoundEvent_Dispatch");
		g_pGameLevel->SoundEvent_Dispatch();
	}

	if (!Device.Paused())
	{
		if (mt_Scheduler)
		{
			PROF_EVENT("Sheduler Deferred");
			::Engine.Sheduler.UpdateDeferred();
			::Engine.Sheduler.UpdateFinalize();
		}
	}

	{
		PROF_EVENT("seqParallel");
		for (u32 pit = 0; pit < Device.seqParallel.size(); pit++)
			Device.seqParallel[pit]();
		Device.seqParallel.clear();
	}

	{
		PROF_EVENT("seqFrameMT");
		Device.seqFrameMT.Process(rp_Frame);
	}

	// demonized: While Renderer prepares frame and GPU renders it, use time opportunity to repeatedly call Lua GC with small step value
	// Reduces stutters since less work will be done in main GC step or no work at all
	{
		PROF_EVENT("seqLuaGC");
		if (psLua_ParallelGC && Device.LuaGC)
		{
			// Do at least once
			do
			{
				Device.LuaGCCount++;
				if (Device.LuaGC(false) == 1) // 1 informs that GC cycle is complete
				{
					Device.LuaGCDone = true;
					break;
				}

			} while (Device.isRendering && Device.LuaGCCount < psLua_ParallelGC_CallAmount);
		}
	}
}
