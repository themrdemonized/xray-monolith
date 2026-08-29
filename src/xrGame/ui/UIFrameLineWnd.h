#pragma once

#include "UIWindow.h"

class CUIFrameLineWnd : public CUIWindow
{
	typedef CUIWindow inherited;
public:
	CUIFrameLineWnd();
	void InitFrameLineWnd(LPCSTR base_name, Fvector2 pos, Fvector2 size, bool horizontal = true);
	void InitFrameLineWnd(Fvector2 pos, Fvector2 size, bool horizontal = true);
	void InitTexture(LPCSTR tex_name, LPCSTR sh_name = "hud\\default");
	virtual void Draw();

	float GetTextureHeight() const { return m_tex_rect[0].height(); }
	float GetTextureWidth() const { return m_tex_rect[0].width(); }
	void SetTextureColor(u32 cl) { m_texture_color = cl; }
	bool IsHorizontal() { return bHorizontal; }
	void SetHorizontal(bool horiz) { bHorizontal = horiz; }
	// When true, caps are scaled to fit the element -- texture->UI by height, then UI->screen for
	// resolution -- so the caps keep their authored shape. When false the cap width is the texture's own
	// pixel width used as-is on screen (never scaled) while its height stretches to fill the element, so
	// the cap's proportions are not preserved.
	void SetCapScaled(bool b);
protected:
	bool bHorizontal;
	struct SDraw;
	bool inc_pos(Frect& rect, int counter, int i, Fvector2& LTp, Fvector2& RBp, Fvector2& LTt, Fvector2& RBt,
	             const SDraw& d);

	enum
	{
		flFirst = 0,
		// Left or top
		flBack,
		// Center texture
		flSecond,
		// Right or bottom
		flMax
	};

	// Outer texel rows/columns of a slice that are line art rather than content. Content between them keeps a normal stretched UV.
	struct SBorder
	{
		s16 l, t, r, b;
	};

	struct SSpan
	{
		float px_begin, px_end;
		float texel_begin, texel_end;
	};

	// Spans are placed by a cut table: every texel along the axis gets length/span pixels give or take one,
	// so bands and content keep the proportions they have in the art.
	struct SAxis
	{
		SSpan extent;
		float px_origin;
		float length;
		float span;
		int lead_bands, trail_bands;
		float interior_lo, interior_hi;
		bool has_interior;
		int span_count;
	};

	struct SDraw
	{
		float content_scale;
		float band_px;
		bool crisp;
	};

	u32 m_texture_color;
	bool m_bTextureVisible;
	bool m_cap_scaled;
	bool m_has_border;

	// Major is the frameline's length axis (X when horizontal), minor its thickness axis
	float TexMajor(int i) const { return bHorizontal ? m_tex_rect[i].width() : m_tex_rect[i].height(); }
	float TexMinor(int i) const { return bHorizontal ? m_tex_rect[i].height() : m_tex_rect[i].width(); }
	int BandsMajor(int i) const { return bHorizontal ? m_border[i].l + m_border[i].r : m_border[i].t + m_border[i].b; }
	float Major(const Frect& r) const { return bHorizontal ? r.width() : r.height(); }
	float Minor(const Frect& r) const { return bHorizontal ? r.height() : r.width(); }
	float BackTilePx(const SDraw& d) const { return _max(1.0f, TexMajor(flBack) * d.content_scale); }

	void DrawElements();
	void ReadBorder(LPCSTR id, int i);
	float CapExtent(int i, const SDraw& d) const;
	u32 CellCount(int i, const SDraw& d) const;
	bool BuildAxis(SAxis& a, const SSpan& extent, int lead_bands, int trail_bands) const;
	SSpan SpanAt(const SAxis& a, int k) const;
	float CutAt(const SAxis& a, float texel_offset) const;
	float LeadCut(const SAxis& a, int band) const;
	float TrailCut(const SAxis& a, int band) const;
	void DrawSlice(int i, Fvector2 LTp, Fvector2 RBp, Fvector2 LTt, Fvector2 RBt, Fvector2 const& ts, const SDraw& d);

	ui_shader m_shader;
	Frect m_tex_rect [flMax];
	SBorder m_border [flMax];
	shared_str dbg_tex_name;
};
