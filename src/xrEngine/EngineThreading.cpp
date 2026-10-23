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
BOOL mt_SchedulerRT = FALSE;

namespace
{
// Stamps a task body into the perf hub's frame record while the hub reads it
struct task_stamp
{
	u64* span;

	task_stamp(u32 task) : span(Device.frame_record_on ? Device.frame_record.task[task] : nullptr)
	{
		if (span)
			span[0] = CPU::QPC();
	}

	~task_stamp()
	{
		if (span)
			span[1] = CPU::QPC();
	}
};
}

void XRay::Engine::PreRenderThread()
{
	PROF_THREAD("Secondary Task 1");
	task_stamp stamp(CRenderDevice::FrameRecord::task_pre_render);

	if (g_pGamePersistent && g_pGamePersistent->pEnvironment && g_pGamePersistent->pEnvironment->eff_Rain)
	{
		PROF_EVENT("CEffect_Rain::UpdateItems");
		g_pGamePersistent->pEnvironment->eff_Rain->UpdateItems();
	}

	if (g_pGamePersistent && Device.ParticleWorkerCallback)
	{
		PROF_EVENT("Process Particles");
		Device.ParticleWorkerCallback();
	}
}

void XRay::Engine::PreRenderPostTransformsThread()
{
    PROF_THREAD("Secondary Task 1.1");
    task_stamp stamp(CRenderDevice::FrameRecord::task_parallel_render);
    {
        PROF_EVENT("seqParallelRender");
        for (auto& it : Device.seqParallelRender)
            it();
    }
}

struct SpatialSnapshot
{
	ISpatialShared ptr;
    IKinematics* pKin;
    float distSq;

    SpatialSnapshot(ISpatialShared _ptr, IKinematics* _pKin, float _distSq) : ptr(_ptr), pKin(_pKin), distSq(_distSq) {};
};
void XRay::Engine::CalculateBonesThread()
{
	PROF_THREAD("Secondary Task 3");
	task_stamp stamp(CRenderDevice::FrameRecord::task_bones);

	PROF_EVENT("CalculateBones");

	if (!g_SpatialSpace) return;
	if (Device.Paused()) return;
	if (!psDeviceFlags.test(rsDrawDynamic)) return;
	if (!g_pGameLevel || !g_pGameLevel->bReady) return;
	if (!g_pGameLevel->CurrentEntity()) return;

	static CFrustum ViewBase;
	ViewBase.CreateFromMatrix(Device.mFullTransform_saved, FRUSTUM_P_LRTB | FRUSTUM_P_FAR);

	static xr_vector<ISpatialShared> spatials = {};
    spatials.clear();
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
		for (ISpatialShared spatial : spatials)
		{
			if (!spatial)
				continue;

            float distSq = Device.vCameraPosition_saved.distance_to_sqr(spatial->spatial.sphere.P);
			if (!ViewBase.testSphere_dirty(spatial->spatial.sphere.P, spatial->spatial.sphere.R))
			{
				if (distSq > 62500.f)//250 m
					continue;
			}
				
			spatial->spatial_updatesector();

            if
            (
                (spatial->spatial.type & (STYPE_PARTICLE | STYPE_LIGHTSOURCE)) ||
                !(spatial->spatial.type & (STYPE_RENDERABLE | STYPE_RENDERABLESHADOW))
            )
                continue;

            auto renderable = spatial->dcast_Renderable();
            if (!(renderable && renderable->renderable.visual))
                continue;

            IKinematics* pKin = renderable->renderable.visual->dcast_PKinematics();
            if (!pKin)
                continue;

			spatialsSnapshot.emplace_back(spatial, pKin, distSq);
		}
	}

	static auto sortFunc = [](const SpatialSnapshot& _1, const SpatialSnapshot& _2) noexcept
	{
		return _1.distSq < _2.distSq;
	};
	std::sort(spatialsSnapshot.begin(), spatialsSnapshot.end(), sortFunc);

    for (const auto& snapshot : spatialsSnapshot)
        snapshot.pKin->CalculateBones(TRUE);
}

extern BOOL psLua_ParallelGC;
int psLua_ParallelGC_CallAmount = 25;
void XRay::Engine::GameThread()
{
	PROF_THREAD("Secondary Task 2")
	task_stamp stamp(CRenderDevice::FrameRecord::task_game);
		
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
            if (mt_SchedulerRT)
            {
                ::Engine.Sheduler.Update();
            }
            else
            {
                ::Engine.Sheduler.UpdateDeferred();
                ::Engine.Sheduler.UpdateFinalize();
            }
		}
	}

	{
		PROF_EVENT("seqParallel");
		for (u32 pit = 0; pit < Device.seqParallel.size(); pit++)
			Device.seqParallel[pit]();
		Device.seqParallel.clear();
	}

    // demonized: While Renderer prepares frame and GPU renders it, use time opportunity to repeatedly call Lua GC with small step value
    // Reduces stutters since less work will be done in main GC step or no work at all
    static auto LuaGC = []()
    {
        PROF_EVENT("seqLuaGC");
        // Do at least once
        do
        {
            PROF_EVENT("LuaGC step");
            Device.LuaGCCount++;
            if (Device.LuaGC() == 1) // 1 informs that GC cycle is complete
            {
                Device.LuaGCDone = true;
                break;
            }

        } while (Device.isRendering && Device.LuaGCCount < psLua_ParallelGC_CallAmount);
    };
    if (psLua_ParallelGC && Device.LuaGC)
        Device.secondary_tasks.run(LuaGC);

	{
		PROF_EVENT("seqFrameMT");
		Device.seqFrameMT.Process(rp_Frame);
	}
}
