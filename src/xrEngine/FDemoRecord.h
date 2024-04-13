#if !defined(AFX_FDEMORECORD_H__D7638760_FB61_11D3_B4E3_4854E82A090D__INCLUDED_)
#define AFX_FDEMORECORD_H__D7638760_FB61_11D3_B4E3_4854E82A090D__INCLUDED_

#pragma once

#include "iinputreceiver.h"
#include "effector.h"

class ENGINE_API CDemoRecord :
	public CEffectorCam,
	public IInputReceiver,
	public pureRender
{
private:
	static struct force_position
	{
		bool set_position;
		Fvector p;
	} g_position;

	static struct force_direction
	{
		bool set_direction;
		Fvector d;
	} g_direction;

	int iCount;
	IWriter* file;
	Fvector m_HPB;
	Fvector m_Position;
	Fvector m_Actor_Position;
	Fvector m_Starting_Position;
	Fmatrix m_Camera;
	u32 m_Stage;

	Fvector m_vT;
	Fvector m_vR;
	Fvector m_vVelocity;
	Fvector m_vAngularVelocity;

	BOOL m_bMakeCubeMap;
	BOOL m_bMakeScreenshot;
	int m_iLMScreenshotFragment;
	BOOL m_bMakeLevelMap;
	BOOL return_ctrl_inputs;
	BOOL m_CameraBoundaryEnabled;

	float m_fSpeed0;
	float m_fSpeed1;
	float m_fSpeed2;
	float m_fSpeed3;
	float m_fAngSpeed0;
	float m_fAngSpeed1;
	float m_fAngSpeed2;
	float m_fAngSpeed3;
	float m_fCameraBoundary;
	float m_fGroundPosition;

	BOOL isInputBlocked;
	xr_unordered_set<CDemoRecord*>* pDemoRecords;

	void MakeCubeMapFace(Fvector& D, Fvector& N);
	void MakeLevelMapProcess();
	void MakeScreenshotFace();
	void RecordKey();
	void MakeCubemap();
	void MakeScreenshot();
	void MakeLevelMapScreenshot(BOOL bHQ);
public:
	CDemoRecord(const char* name, float life_time = 60 * 60 * 1000, BOOL return_ctrl_inputs = 0);
	CDemoRecord(const char* name, xr_unordered_set<CDemoRecord*>* pDemoRecords, BOOL isInputBlocked = 0, float life_time = 60 * 60 * 1000, BOOL return_ctrl_inputs = 0);
	virtual ~CDemoRecord();

	virtual void IR_OnKeyboardPress(int dik);
	virtual void IR_OnKeyboardRelease(int dik);
	virtual void IR_OnKeyboardHold(int dik);
	virtual void IR_OnMouseMove(int dx, int dy);
	virtual void IR_OnMouseHold(int btn);
	virtual void IR_OnMousePress(int btn);
	virtual void IR_OnMouseRelease(int btn);
	void StopDemo();
	void EnableReturnCtrlInputs();
	void SetCameraBoundary(float boundary);
	virtual BOOL ProcessCam(SCamEffectorInfo& info);
	static void SetGlobalPosition(const Fvector& p) { g_position.p.set(p), g_position.set_position = true; }
	static void GetGlobalPosition(Fvector& p) { p.set(g_position.p); }
	static void SetGlobalDirection(const Fvector& d) { g_direction.d.set(d), g_direction.set_direction = true; }
	static void GetGlobalDirection(Fvector& d) { d.set(g_direction.d); }
	BOOL m_b_redirect_input_to_level;
	virtual void OnRender();
};

#endif // !defined(AFX_FDEMORECORD_H__D7638760_FB61_11D3_B4E3_4854E82A090D__INCLUDED_)
