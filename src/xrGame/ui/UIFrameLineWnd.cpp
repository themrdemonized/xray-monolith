#include "stdafx.h"
#include "UIFrameLineWnd.h"
#include "UITextureMaster.h"

CUIFrameLineWnd::CUIFrameLineWnd()
	: bHorizontal(true),
	  m_bTextureVisible(false),
	  m_cap_scaled(false),
	  m_has_border(false)
{
	m_texture_color = color_argb(255, 255, 255, 255);
	ZeroMemory(m_border, sizeof(m_border));
}

void CUIFrameLineWnd::InitFrameLineWnd(LPCSTR base_name, Fvector2 pos, Fvector2 size, bool horizontal)
{
	InitFrameLineWnd(pos, size, horizontal);
	InitTexture(base_name, "hud\\default");
}

void CUIFrameLineWnd::InitFrameLineWnd(Fvector2 pos, Fvector2 size, bool horizontal)
{
	inherited::SetWndPos(pos);
	inherited::SetWndSize(size);

	bHorizontal = horizontal;
}

void CUIFrameLineWnd::InitTexture(LPCSTR texture, LPCSTR sh_name)
{
	m_bTextureVisible = true;
	dbg_tex_name = texture;
	m_has_border = false;
	ZeroMemory(m_border, sizeof(m_border));
	string256 buf;
	CUITextureMaster::InitTexture(strconcat(sizeof(buf), buf, texture, "_back"), sh_name, m_shader, m_tex_rect[flBack]);
	ReadBorder(buf, flBack);
	CUITextureMaster::InitTexture(strconcat(sizeof(buf), buf, texture, "_b"), sh_name, m_shader, m_tex_rect[flFirst]);
	ReadBorder(buf, flFirst);
	CUITextureMaster::InitTexture(strconcat(sizeof(buf), buf, texture, "_e"), sh_name, m_shader, m_tex_rect[flSecond]);
	ReadBorder(buf, flSecond);
	if (bHorizontal)
	{
		R_ASSERT2(fsimilar(m_tex_rect[flFirst].height(), m_tex_rect[flSecond].height()), texture);
		R_ASSERT2(fsimilar(m_tex_rect[flFirst].height(), m_tex_rect[flBack].height()), texture);
	}
	else
	{
		R_ASSERT2(fsimilar(m_tex_rect[flFirst].width(), m_tex_rect[flSecond].width()), texture);
		R_ASSERT2(fsimilar(m_tex_rect[flFirst].width(), m_tex_rect[flBack].width()), texture);
	}
}

void CUIFrameLineWnd::SetCapScaled(bool b)
{
	m_cap_scaled = b;
}

void CUIFrameLineWnd::ReadBorder(LPCSTR id, int i)
{
	const TEX_INFO info = CUITextureMaster::FindItem(id);
	const int l = info.border_l, t = info.border_t, r = info.border_r, b = info.border_b;
	if (0 == (l | t | r | b))
		return;

	if (l < 0 || t < 0 || r < 0 || b < 0 ||
		float(l + r) > m_tex_rect[i].width() || float(t + b) > m_tex_rect[i].height())
	{
		Msg("! [%s] border_l/t/r/b (%d,%d,%d,%d) do not fit the %dx%d texel slice - ignored",
		    id, l, t, r, b, iFloor(m_tex_rect[i].width()), iFloor(m_tex_rect[i].height()));
		return;
	}

	m_border[i].l = (s16)l;
	m_border[i].t = (s16)t;
	m_border[i].r = (s16)r;
	m_border[i].b = (s16)b;
	m_has_border = true;
}

float CUIFrameLineWnd::CapExtent(int i, const SDraw& d) const
{
	const float total = TexMajor(i);
	const int bands = BandsMajor(i);
	if (!d.crisp || 0 == bands)
		return total * d.content_scale;

	const float interior = total - float(bands);
	return float(bands) * d.band_px + ((interior > 0.0f) ? interior * d.content_scale : 0.0f);
}

u32 CUIFrameLineWnd::CellCount(int i, const SDraw& d) const
{
	if (!d.crisp)
		return 1;
	return u32(m_border[i].l + m_border[i].r + 1) * u32(m_border[i].t + m_border[i].b + 1);
}

float CUIFrameLineWnd::CutAt(const SAxis& a, float texel_offset) const
{
	return float(iFloor(texel_offset * a.length / a.span + 0.5f));
}

float CUIFrameLineWnd::LeadCut(const SAxis& a, int band) const
{
	return _max(CutAt(a, float(band)), float(band));
}

float CUIFrameLineWnd::TrailCut(const SAxis& a, int band) const
{
	return _min(CutAt(a, a.span - float(band)), a.length - float(band));
}

bool CUIFrameLineWnd::BuildAxis(SAxis& a, const SSpan& extent, int lead_bands, int trail_bands) const
{
	const int bands = lead_bands + trail_bands;

	a.extent = extent;
	a.lead_bands = lead_bands;
	a.trail_bands = trail_bands;
	a.px_origin = float(iFloor(extent.px_begin));
	a.length = float(iFloor(extent.px_end)) - a.px_origin;
	a.span = extent.texel_end - extent.texel_begin;

	if (a.span <= EPS_L || float(bands) > a.length || float(bands) > a.span + EPS_L)
		return false;

	a.interior_lo = LeadCut(a, lead_bands);
	a.interior_hi = _max(TrailCut(a, trail_bands), a.interior_lo);
	a.has_interior = (a.span - float(bands)) > EPS_L && a.interior_hi > a.interior_lo;
	a.span_count = bands + (a.has_interior ? 1 : 0);
	return a.span_count > 0;
}

CUIFrameLineWnd::SSpan CUIFrameLineWnd::SpanAt(const SAxis& a, int k) const
{
	SSpan s;
	if (k < a.lead_bands)
	{
		s.px_begin = a.px_origin + LeadCut(a, k);
		s.px_end = a.px_origin + ((k + 1 == a.lead_bands) ? a.interior_lo : LeadCut(a, k + 1));
		s.texel_begin = s.texel_end = a.extent.texel_begin + float(k) + 0.5f;
		return s;
	}

	if (a.has_interior && k == a.lead_bands)
	{
		s.px_begin = a.px_origin + a.interior_lo;
		s.px_end = a.px_origin + a.interior_hi;
		// Half-texel inset on the sides that abut a band. The interior's raw UV sits on a texel boundary,
		// where bilinear returns a blend of the texels either side of the cut -- so the outermost row or
		// column of content comes out tinted with the band colour. Very visible on the _e cap, whose left
		// band is a dark separator against a bright button face. Untouched where there is no band, so the
		// tiling middle keeps its exact 1:1 mapping.
		s.texel_begin = a.extent.texel_begin + float(a.lead_bands) + ((a.lead_bands > 0) ? 0.5f : 0.0f);
		s.texel_end = a.extent.texel_end - float(a.trail_bands) - ((a.trail_bands > 0) ? 0.5f : 0.0f);
		return s;
	}

	const int j = k - a.lead_bands - (a.has_interior ? 1 : 0);
	s.px_begin = a.px_origin + ((j == 0) ? a.interior_hi : TrailCut(a, a.trail_bands - j));
	s.px_end = a.px_origin + TrailCut(a, a.trail_bands - j - 1);
	s.texel_begin = s.texel_end = a.extent.texel_end - float(a.trail_bands - j) + 0.5f;
	return s;
}

void CUIFrameLineWnd::Draw()
{
	if (m_bTextureVisible)
		DrawElements();

	inherited::Draw();
}

static Fvector2 pt_offset = {-0.5f, -0.5f};

void draw_rect(Fvector2 LTp, Fvector2 RBp, Fvector2 LTt, Fvector2 RBt, u32 clr, Fvector2 const& ts)
{
	UI().AlignPixel(LTp.x);
	UI().AlignPixel(LTp.y);
	LTp.add(pt_offset);
	UI().AlignPixel(RBp.x);
	UI().AlignPixel(RBp.y);
	RBp.add(pt_offset);
	LTt.div(ts);
	RBt.div(ts);

	// Frame-lines push their vertices directly, so a custom clip must be applied here in software
	if (UI().HasCustomClip())
	{
		sPoly2D S;
		S.resize(4);
		S[0].set(LTp.x, LTp.y, LTt.x, LTt.y);
		S[1].set(RBp.x, LTp.y, RBt.x, LTt.y);
		S[2].set(RBp.x, RBp.y, RBt.x, RBt.y);
		S[3].set(LTp.x, RBp.y, LTt.x, RBt.y);
		sPoly2D D;
		sPoly2D* R = UI().ActiveClipFrustum().ClipPoly(S, D);
		if (R && R->size())
		{
			for (u32 k = 0; k < R->size() - 2; ++k)
			{
				UIRender->PushPoint((*R)[0].pt.x, (*R)[0].pt.y, 0, clr, (*R)[0].uv.x, (*R)[0].uv.y);
				UIRender->PushPoint((*R)[k + 1].pt.x, (*R)[k + 1].pt.y, 0, clr, (*R)[k + 1].uv.x, (*R)[k + 1].uv.y);
				UIRender->PushPoint((*R)[k + 2].pt.x, (*R)[k + 2].pt.y, 0, clr, (*R)[k + 2].uv.x, (*R)[k + 2].uv.y);
			}
		}
		return;
	}

	UIRender->PushPoint(LTp.x, LTp.y, 0, clr, LTt.x, LTt.y);
	UIRender->PushPoint(RBp.x, RBp.y, 0, clr, RBt.x, RBt.y);
	UIRender->PushPoint(LTp.x, RBp.y, 0, clr, LTt.x, RBt.y);

	UIRender->PushPoint(LTp.x, LTp.y, 0, clr, LTt.x, LTt.y);
	UIRender->PushPoint(RBp.x, LTp.y, 0, clr, RBt.x, LTt.y);
	UIRender->PushPoint(RBp.x, RBp.y, 0, clr, RBt.x, RBt.y);
}

void CUIFrameLineWnd::DrawElements()
{
	UIRender->SetShader(*m_shader);

	Fvector2 ts;
	UIRender->GetActiveTextureResolution(ts);

	Frect rect;
	GetAbsoluteRect(rect);
	Frect ui_rect = rect;
	UI().ClientToScreenScaled(rect.lt);
	UI().ClientToScreenScaled(rect.rb);

	SDraw d;
	d.content_scale = 1.0f;
	d.band_px = 1.0f;
	d.crisp = false;
	if ((m_cap_scaled || m_has_border) && ui_rect.width() > 0.0f && ui_rect.height() > 0.0f &&
		TexMinor(flFirst) > 0.0f)
	{
		const float scale_tex = Minor(ui_rect) / TexMinor(flFirst);
		const float scale_res = Major(rect)    / Major(ui_rect);
		d.content_scale = scale_tex * scale_res;

		if (m_has_border)
		{
			d.crisp = true;
			d.band_px = float(_max(1, iFloor(scale_tex * (rect.height() / ui_rect.height()) + 0.5f)));
		}
	}

	u32 prim_count = 6 * (CellCount(flFirst, d) + CellCount(flSecond, d));
	const float back_len = Major(rect) - CapExtent(flFirst, d) - CapExtent(flSecond, d);
	if (back_len < 0.0f)
	{
		if (bHorizontal) rect.x2 -= back_len;
		else rect.y2 -= back_len;
	}

	if (back_len > 0.0f)
		prim_count += 6 * CellCount(flBack, d) * iCeil(back_len / BackTilePx(d));

	if (UI().HasCustomClip())
		prim_count = (prim_count / 6) * UI().ActiveClipFrustum().ClipBudget(4);

	UIRender->StartPrimitive(prim_count, IUIRender::ptTriList, UI().m_currentPointType);

	for (int i = 0; i < flMax; ++i)
	{
		Fvector2 LTt, RBt;
		Fvector2 LTp, RBp;
		int counter = 0;

		while (inc_pos(rect, counter, i, LTp, RBp, LTt, RBt, d))
		{
			DrawSlice(i, LTp, RBp, LTt, RBt, ts, d);
			++counter;
		};
	}
	UIRender->FlushPrimitive();
}

void CUIFrameLineWnd::DrawSlice(int i, Fvector2 LTp, Fvector2 RBp, Fvector2 LTt, Fvector2 RBt, Fvector2 const& ts,
                                const SDraw& d)
{
	const SBorder& b = m_border[i];
	const SSpan extent_x = {LTp.x, RBp.x, LTt.x, RBt.x};
	const SSpan extent_y = {LTp.y, RBp.y, LTt.y, RBt.y};
	SAxis ax, ay;
	if (!d.crisp || 0 == (b.l | b.t | b.r | b.b) ||
		!BuildAxis(ax, extent_x, b.l, b.r) ||
		!BuildAxis(ay, extent_y, b.t, b.b))
	{
		draw_rect(LTp, RBp, LTt, RBt, m_texture_color, ts);
		return;
	}

	for (int y = 0; y < ay.span_count; ++y)
	{
		const SSpan sy = SpanAt(ay, y);
		for (int x = 0; x < ax.span_count; ++x)
		{
			const SSpan sx = SpanAt(ax, x);
			draw_rect(Fvector2().set(sx.px_begin, sy.px_begin), Fvector2().set(sx.px_end, sy.px_end),
			          Fvector2().set(sx.texel_begin, sy.texel_begin), Fvector2().set(sx.texel_end, sy.texel_end),
			          m_texture_color, ts);
		}
	}
}


bool CUIFrameLineWnd::inc_pos(Frect& rect, int counter, int i, Fvector2& LTp, Fvector2& RBp, Fvector2& LTt,
                              Fvector2& RBt, const SDraw& d)
{
	if (i == flFirst || i == flSecond)
	{
		if (counter != 0) return false;

		LTt = m_tex_rect[i].lt;
		RBt = m_tex_rect[i].rb;

		LTp = rect.lt;

		RBp = rect.lt;
		if (bHorizontal) RBp.x += CapExtent(i, d);
		else RBp.y += CapExtent(i, d);
	}
	else //i==flBack
	{
		const float cap_second = CapExtent(flSecond, d);
		if ((bHorizontal && rect.lt.x + cap_second + EPS_L >= rect.rb.x) ||
			(!bHorizontal && rect.lt.y + cap_second + EPS_L >= rect.rb.y))
			return false;

		LTt = m_tex_rect[i].lt;
		LTp = rect.lt;

		const float tile_px = BackTilePx(d);
		bool b_draw_reminder = (bHorizontal)
			                       ? (rect.lt.x + tile_px > rect.rb.x - cap_second)
			                       : (rect.lt.y + tile_px > rect.rb.y - cap_second);
		if (b_draw_reminder)
		{
			//draw reminder
			float rem_len = (bHorizontal)
				                ? rect.rb.x - cap_second - rect.lt.x
				                : rect.rb.y - cap_second - rect.lt.y;
			const float rem_tex = rem_len / (tile_px / TexMajor(flBack));

			if (bHorizontal)
			{
				RBt.y = m_tex_rect[i].rb.y;
				RBt.x = m_tex_rect[i].lt.x + rem_tex;

				RBp = rect.lt;
				RBp.x += rem_len;
			}
			else
			{
				RBt.y = m_tex_rect[i].lt.y + rem_tex;
				RBt.x = m_tex_rect[i].rb.x;

				RBp = rect.lt;
				RBp.y += rem_len;
			}
		}
		else
		{
			//draw full element
			RBt = m_tex_rect[i].rb;

			RBp = rect.lt;
			if (bHorizontal) RBp.x += tile_px;
			else RBp.y += tile_px;
		}
	}

	//stretch always
	if (bHorizontal)
		RBp.y = rect.rb.y;
	else
		RBp.x = rect.rb.x;

	if (bHorizontal) rect.lt.x = RBp.x;
	else rect.lt.y = RBp.y;
	return true;
}
