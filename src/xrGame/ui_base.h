#pragma once

class CUICursor;
class CUIGameCustom;

#include "ui_defs.h"


class CDeviceResetNotifier : public pureDeviceReset
{
public:
	CDeviceResetNotifier() { Device.seqDeviceReset.Add(this,REG_PRIORITY_NORMAL); };
	virtual ~CDeviceResetNotifier() { Device.seqDeviceReset.Remove(this); };

	virtual void OnDeviceReset()
	{
	};
};

struct CFontManager : public pureDeviceReset
{
	CFontManager();
	~CFontManager();

	typedef xr_vector<CGameFont**> FONTS_VEC;
	typedef FONTS_VEC::iterator FONTS_VEC_IT;
	FONTS_VEC m_all_fonts;
	void Render();

	// hud font
	CGameFont* pFontMedium;
	CGameFont* pFontDI;

	CGameFont* pFontArial14;
	CGameFont* pFontGraffiti19Russian;
	CGameFont* pFontGraffiti22Russian;
	CGameFont* pFontLetterica16Russian;
	CGameFont* pFontLetterica18Russian;
	CGameFont* pFontGraffiti32Russian;
	CGameFont* pFontGraffiti50Russian;
	CGameFont* pFontLetterica25;
	CGameFont* pFontStat;

	void InitializeFonts();
	void InitializeFont(CGameFont*& F, LPCSTR section, u32 flags = 0);
	LPCSTR GetFontTexName(LPCSTR section);

	virtual void OnDeviceReset();
};


class ui_core : public CDeviceResetNotifier
{
	C2DFrustum m_2DFrustum;
	C2DFrustum m_2DFrustumPP;
	C2DFrustum m_FrustumLIT;

	bool m_bPostprocess;

	CFontManager* m_pFontManager;
	CUICursor* m_pUICursor;

	Fvector2 m_pp_scale_;
	Fvector2 m_scale_;
	Fvector2* m_current_scale;

	// Clip-frustum stack: base is the screen frustum, tighter convex polys push on top (twin of m_Scissors).
	xr_stack<const C2DFrustum*> m_clip_stack;

public:
	xr_stack<Frect> m_Scissors;

	ui_core();
	~ui_core();
	CFontManager& Font() { return *m_pFontManager; }
	CUICursor& GetUICursor() { return *m_pUICursor; }

	IC float ClientToScreenScaledX(float left) const { return left * m_current_scale->x; };
	IC float ClientToScreenScaledY(float top) const { return top * m_current_scale->y; };
	void ClientToScreenScaled(Fvector2& dest, float left, float top) const;
	void ClientToScreenScaled(Fvector2& src_and_dest) const;
	void ClientToScreenScaledWidth(float& src_and_dest) const;
	void ClientToScreenScaledHeight(float& src_and_dest) const;
	void AlignPixel(float& src_and_dest) const;

	const C2DFrustum& ScreenFrustum() const { return (m_bPostprocess) ? m_2DFrustumPP : m_2DFrustum; }
	C2DFrustum& ScreenFrustumLIT() { return m_FrustumLIT; }
	const C2DFrustum& ActiveClipFrustum() const { return m_clip_stack.empty() ? ScreenFrustum() : *m_clip_stack.top(); }
	void PushClipFrustum(const C2DFrustum* f) { m_clip_stack.push(f); }
	void PopClipFrustum() { m_clip_stack.pop(); }
	bool HasCustomClip() const { return !m_clip_stack.empty(); }
	void PushScissor(const Frect& r, bool overlapped = false);
	void PopScissor();

	void pp_start();
	void pp_stop();
	void RenderFont();

	virtual void OnDeviceReset();
	static bool is_widescreen();
	static u8 screenmode();
	static float get_current_kx();
	shared_str get_xml_name(LPCSTR fn);

	IUIRender::ePointType m_currentPointType;
};


extern CUICursor& GetUICursor();
extern ui_core& UI();
extern CUIGameCustom* CurrentGameUI();
