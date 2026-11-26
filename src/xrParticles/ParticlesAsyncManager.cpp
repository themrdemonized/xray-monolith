#include "stdafx.h"
#include "ParticlesAsyncManager.h"

#include "../Include/xrRender/ParticleCustom.h"
#include "../Include/xrRender/RenderVisual.h"
#include "../xrEngine/IGame_Persistent.h"

static CParticlesAsync Instance;

void CParticlesAsync::Play()
{
	if (DevicePtr && !Device.ParticleWorkerCallback)
		Device.ParticleWorkerCallback.bind(&CParticlesAsync::Start);

	if (!psDeviceFlags.test(mtParticles))
	{
		Instance.Start();
	}
}

void CParticlesAsync::Start()
{
	Instance.IsStarted = true;

	{
		PROF_EVENT("Particle Update");
        for (intrusive_ptr<CPS_Instance>& particle: g_pGamePersistent->ps_active)
        {
            if (particle->m_bDead)
                continue;

            Instance.UpdateParticle(particle);
        }
    }

    PROF_EVENT("Particle Shedule");
    for (intrusive_ptr<CPS_Instance>& particle : g_pGamePersistent->ps_active)
	{
		particle->Update(Device.dwTimeDelta);
	}

	Instance.IsStarted = false;
}

void CParticlesAsync::Wait()
{
	if (psDeviceFlags.test(mtParticles))
	{
		PROF_EVENT("Particles Wait");
        xrSpinWait w;
		while (Instance.IsStarted)
		{
            w();
		}
	}
}

void CParticlesAsync::ForceUpdate(intrusive_ptr<CPS_Instance> Obj)
{
	if (!Instance.IsStarted)
		return;

	Instance.UpdateParticle(Obj);
}

bool CParticlesAsync::NeedForceUpdate()
{
	return Instance.IsStarted;
}

CParticlesAsync::CParticlesAsync()
{
	if (!DevicePtr)
		return;

	if (g_dedicated_server)
		return;

    Device.ParticleWorkerCallback.bind(&CParticlesAsync::Start);
}

void CParticlesAsync::UpdateParticle(intrusive_ptr<CPS_Instance> particle) const
{
	u32 dt = Device.dwTimeGlobal - particle->dwLastTime;
	IParticleCustom* V = smart_cast<IParticleCustom*>(particle->renderable.visual);

	if (V == nullptr)
		return;

	PROF_EVENT("Particle OnFrame");
	V->OnFrame(dt);

	particle->dwLastTime = Device.dwTimeGlobal;
}
