// HOM.h: interface for the CHOM class.
//
//////////////////////////////////////////////////////////////////////
#pragma once

#include "../../xrEngine/IGame_Persistent.h"

class occTri;

class CHOM
#ifdef DEBUG
	: public pureRender
#endif
{
private:
	xrXRC xrc;
	CDB::MODEL* m_pModel;
	occTri* m_pTris;
	BOOL bEnabled;
	Fmatrix m_xform;
	Fmatrix m_xform_01;
#ifdef DEBUG
	u32						tris_in_frame_visible	;
	u32						tris_in_frame			;
#endif

	xr_atomic_u32 MT_frame_rendered;
	xrCriticalSection m_mt_render_guard;

	void Render_DB(CFrustum& base);
public:
	struct StaticData
	{
		CDB::MODEL* model = nullptr;
		occTri* tris = nullptr;
		BOOL enabled = FALSE;
		~StaticData();
	};

	void Load();
	void Prepare(StaticData& data);
	void Prepare(LPCSTR canonical_level_path, StaticData& data);
	void Unload();
	void Suspend(StaticData& data);
	void Resume(StaticData& data);
	void Render(CFrustum& base);
	void Render_ZB();
	//	void					Debug		();

	void occlude(Fbox2& space)
	{
	}

	void Disable();
	void Enable();

	void __stdcall MT_RENDER();

	BOOL visible(vis_data& vis);
	BOOL visible(Fbox3& B);
	BOOL visible(Fsphere& S);
	BOOL visible(sPoly& P);
	BOOL visible(Fbox2& B, float depth); // viewport-space (0..1)

	CHOM();
	~CHOM();

#ifdef DEBUG
	virtual void			OnRender	();
			void			stats		();
#endif
};
