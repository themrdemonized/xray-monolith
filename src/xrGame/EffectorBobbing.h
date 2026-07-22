#ifndef _EFFECTOR_BOBBING_H
#define _EFFECTOR_BOBBING_H
#pragma once

#include "CameraEffector.h"
#include "../xrEngine/cameramanager.h"

class CEffectorBobbing : public CEffectorCam
{
	float fTime;
	Fvector vAngleAmplitude;
	float fYAmplitude;
	float fSpeed;

	u32 dwMState;
	float fReminderFactor;
	bool is_limping;
	bool m_bZoomMode;

	float m_fAmplitudeRun;
	float m_fAmplitudeWalk;
	float m_fAmplitudeLimp;

	float m_fSpeedRun;
	float m_fSpeedWalk;
	float m_fSpeedLimp;

public:
	CEffectorBobbing();
	virtual ~CEffectorBobbing();
	virtual BOOL ProcessCam(SCamEffectorInfo& info);
	void SetState(u32 st, bool limping, bool ZoomMode);
};

#endif //_EFFECTOR_BOBBING_H

// demonized: First Person Death Cam Effector
class CFPCamEffector : public CEffectorCam
{
public:
	Fvector m_Position;
	Fvector m_HPB;
	Fmatrix m_Camera;
	bool hudEnabled = false;
	unsigned int m_customSmoothing; // 0 - use FPDeath smoothing params, no custom smoothing
	float m_fov; // <=0 no override, mirrors CAnimatorCamEffector fov
	bool m_exclusive; // true pushes the effector to the back of the list for exclusive positioning
	bool m_releasing; // smoothed handoff to the base pose then self-remove
	virtual void ema(Fvector& current, Fvector& target, unsigned int steps);

public:
	CFPCamEffector();
	virtual BOOL ProcessCam(SCamEffectorInfo& info);
	virtual bool AbsolutePositioning() { return m_exclusive; }
	void SetFov(float val) { m_fov = val; }

};
