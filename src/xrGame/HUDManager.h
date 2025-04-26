#pragma once

#include "../xrEngine/CustomHUD.h"
#include "HitMarker.h"
#include "HUDTarget.h"
#include "GamePersistent.h"

class CHUDTarget;
class CUIGameCustom;

struct SPickParam
{
	collide::ray_defs defs;
	collide::rq_result result;
	float barrel_dist;
	bool barrel_blocked;
	Fmatrix barrel_matrix;
	float power;
	u32 pass;

	SPickParam() :
		defs(collide::ray_defs(Fvector(), Fvector(), 0.f, CDB::OPT_CULL, collide::rqtBoth)),
		result(collide::rq_result().set(NULL, 0.f, 0)),
		barrel_dist(0.f),
		barrel_blocked(false),
		barrel_matrix(Fmatrix().identity()),
		power(1.f),
		pass(0)
	{
	}

	void InitPick()
	{
		defs.start.set(0, 0, 0);
		defs.dir.set(0, 0, 0);
		defs.range = g_pGamePersistent->Environment().CurrentEnv->far_plane * 0.99f;
		barrel_blocked = false;
		barrel_dist = 0.f;
		barrel_matrix.identity();
	}

	void CameraPick()
	{
		InitPick();
		defs.start = Device.vCameraPosition;
		defs.dir = Device.vCameraDirection;
	}
};

class CHUDManager :
	public CCustomHUD
{
	friend class CUI;
private:
	//.	CUI*					pUI;
	CUIGameCustom* pUIGame;
	CHitMarker HitMarker;
	CHUDTarget* m_pHUDTarget;
	bool b_online;
	SPickParam PP;
	collide::rq_results RQR;
public:
	CHUDManager();
	virtual ~CHUDManager();
	virtual void OnFrame();
	virtual void OnEvent(EVENT E, u64 P1, u64 P2);

	virtual void Render_First();
	virtual void Render_Last();

	virtual void RenderUI();

	//.				CUI*		GetUI				(){return pUI;}
	CUIGameCustom* GetGameUI()
	{
		return pUIGame;
	}

	void HitMarked(int idx, float power, const Fvector& dir);
	bool AddGrenade_ForMark(CGrenade* grn);
	void Update_GrenadeView(Fvector& pos_actor);
	void net_Relcase(CObject* obj);


	bool FireposActive();
	bool DoPick(SPickParam& pp);
	SPickParam& GetPick() { return PP; }
	collide::rq_result& GetRQ() { return GetPick().result; }

	//устанвка внешнего вида прицела в зависимости от текущей дисперсии
	void SetCrosshairDisp(float dispf, float disps = 0.f);
#ifdef DEBUG
    void					SetFirstBulletCrosshairDisp(float fbdispf);
#endif
	void ShowCrosshair(bool show);

	void SetHitmarkType(LPCSTR tex_name);
	void SetGrenadeMarkType(LPCSTR tex_name);

	virtual void OnScreenResolutionChanged();
	virtual void Load();
	virtual void OnDisconnected();
	virtual void OnConnected();

	virtual void RenderActiveItemUI();
	virtual void RenderCamAttachedUI();
	virtual bool RenderActiveItemUIQuery();
	virtual bool RenderCamAttachedUIQuery();

	//Lain: added
	void SetRenderable(bool renderable)
	{
		psHUD_Flags.set(HUD_DRAW_RT2, renderable);
	}

	void Render_R1_Attachment_UI();
};

IC CHUDManager& HUD()
{
	return *((CHUDManager*)g_hud);
}
