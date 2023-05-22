#include "stdafx.h"
#include "EffectorBobbing.h"


#include "actor.h"
#include "actor_defs.h"


#define BOBBING_SECT "bobbing_effector"

#define CROUCH_FACTOR	0.75f
#define SPEED_REMINDER	5.f


//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CEffectorBobbing::CEffectorBobbing() : CEffectorCam(eCEBobbing, 10000.f)
{
	fYAmplitude = 0.0f;
	fSpeed = 0.0f;
	dwMState = 0;
	m_bZoomMode = false;
	fTime = 0;
	fReminderFactor = 0;
	is_limping = false;

	m_fAmplitudeRun = pSettings->r_float(BOBBING_SECT, "run_amplitude");
	m_fAmplitudeWalk = pSettings->r_float(BOBBING_SECT, "walk_amplitude");
	m_fAmplitudeLimp = pSettings->r_float(BOBBING_SECT, "limp_amplitude");

	m_fSpeedRun = pSettings->r_float(BOBBING_SECT, "run_speed");
	m_fSpeedWalk = pSettings->r_float(BOBBING_SECT, "walk_speed");
	m_fSpeedLimp = pSettings->r_float(BOBBING_SECT, "limp_speed");
}

CEffectorBobbing::~CEffectorBobbing()
{
}

void CEffectorBobbing::SetState(u32 mstate, bool limping, bool ZoomMode)
{
	dwMState = mstate;
	is_limping = limping;
	m_bZoomMode = ZoomMode;
}

extern float g_head_bob_factor;
BOOL CEffectorBobbing::ProcessCam(SCamEffectorInfo& info)
{
	fTime += Device.fTimeDelta;
	if (dwMState & ACTOR_DEFS::mcAnyMove)
	{
		if (fReminderFactor < 1.f) fReminderFactor += SPEED_REMINDER * Device.fTimeDelta;
		else fReminderFactor = 1.f;
	}
	else
	{
		if (fReminderFactor > 0.f) fReminderFactor -= SPEED_REMINDER * Device.fTimeDelta;
		else fReminderFactor = 0.f;
	}
	if (!fsimilar(fReminderFactor, 0))
	{
		Fmatrix M;
		M.identity();
		M.j.set(info.n);
		M.k.set(info.d);
		M.i.crossproduct(info.n, info.d);
		M.c.set(info.p);

		// apply footstep bobbing effect
		Fvector dangle;
		float k = ((dwMState & ACTOR_DEFS::mcCrouch) ? CROUCH_FACTOR : 1.f);

		float A, ST;

		if (isActorAccelerated(dwMState, m_bZoomMode))
		{
			A = m_fAmplitudeRun * k;
			ST = m_fSpeedRun * fTime * k;
		}
		else if (is_limping)
		{
			A = m_fAmplitudeLimp * k;
			ST = m_fSpeedLimp * fTime * k;
		}
		else
		{
			A = m_fAmplitudeWalk * k;
			ST = m_fSpeedWalk * fTime * k;
		}

		float _sinA = _abs(_sin(ST) * A) * fReminderFactor * g_head_bob_factor;
		float _cosA = _cos(ST) * A * fReminderFactor * g_head_bob_factor;

		info.p.y += _sinA;
		dangle.x = _cosA;
		dangle.z = _cosA;
		dangle.y = _sinA;

		Fmatrix R;
		R.setHPB(dangle.x, dangle.y, dangle.z);

		Fmatrix mR;
		mR.mul(M, R);

		info.d.set(mR.k);
		info.n.set(mR.j);
	}
	//	else{
	//		fTime		= 0;
	//	}
	return TRUE;
}

// demonized: First Person Death (Cam Effector, can be used in scripts any time to set custom position and direction)
CFPCamEffector::CFPCamEffector() : CEffectorCam(eCEUser, INT_MAX) {
	m_Camera.identity();
	m_Camera.setHPB(0, 0, 0);
	m_HPB.set(0, 0, 0);
	m_Position.set(0, 0, 0);
	m_customSmoothing = 0;
}

// EMA smoothing for changing values, frame independent
int firstPersonDeathPositionSmoothing = 6;
int firstPersonDeathDirectionSmoothing = 12;

void CFPCamEffector::ema(Fvector &current, Fvector &target, unsigned int steps) {
	float smoothing_alpha = 2.0 / (steps + 1);
	float delta = Device.dwTimeDelta;

	if (fis_zero(current.x) && fis_zero(current.y) && fis_zero(current.z)) {
		current.x = target.x;
		current.y = target.y;
		current.z = target.z;
		return;
	}

	current.x = current.x + min(1.f, smoothing_alpha * (delta / steps)) * (target.x - current.x);
	current.y = current.y + min(1.f, smoothing_alpha * (delta / steps)) * (target.y - current.y);
	current.z = current.z + min(1.f, smoothing_alpha * (delta / steps)) * (target.z - current.z);
}

BOOL CFPCamEffector::ProcessCam(SCamEffectorInfo& info)
{
	// Set target camera
	Fmatrix temp;
	temp.identity().setHPB(m_HPB.x, m_HPB.y, m_HPB.z).translate_over(m_Position);

	// Smooth out transition between current camera and target
	if (m_customSmoothing) {
		ema(m_Camera.j, temp.j, m_customSmoothing);
		ema(m_Camera.k, temp.k, m_customSmoothing);
		ema(m_Camera.c, temp.c, m_customSmoothing);
	} else {
		ema(m_Camera.j, temp.j, firstPersonDeathDirectionSmoothing);
		ema(m_Camera.k, temp.k, firstPersonDeathDirectionSmoothing);
		ema(m_Camera.c, temp.c, firstPersonDeathPositionSmoothing);
	}

	// update camera
	info.n.set(m_Camera.j);
	info.d.set(m_Camera.k);
	info.p.set(m_Camera.c);
	return TRUE;
}