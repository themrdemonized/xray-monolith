#include "stdafx.h"
#pragma hdrstop

#include "GameFont.h"
#ifndef _EDITOR
#include "Render.h"
#endif
#ifdef _EDITOR
unsigned short int mbhMulti2Wide
( wide_char* WideStr , wide_char* WidePos , const unsigned short int WideStrSize , const char* MultiStr ) {return 0;};
#endif

extern ENGINE_API BOOL g_bRendering;
ENGINE_API Fvector2 g_current_font_scale = {1.0f, 1.0f};

#include "../Include/xrAPI/xrAPI.h"
#include "../Include/xrRender/RenderFactory.h"
#include "../Include/xrRender/FontRender.h"

struct FontRung
{
	LPCSTR key;
	u32 lo, hi;
	u32 authored;
};

static const FontRung s_legacy_rungs[] = {
	{"texture800", 0, 601, 600},
	{"texture", 601, 1024, 768},
	{"texture1600", 1024, 1440, 1200},
	{"texture2160", 1440, 0, 2160},
};

static const int LEGACY_DEFAULT_IDX = 1;

static const char RUNG_PREFIX[] = "texture_h";

IC bool rung_claims(const FontRung& r, u32 h) { return h >= r.lo && (r.hi == 0 || h < r.hi); }

static u32 parse_rung_height(LPCSTR key)
{
	const size_t n = sizeof(RUNG_PREFIX) - 1;
	if (0 != strncmp(key, RUNG_PREFIX, n))
		return 0;

	u32 v = 0;
	for (LPCSTR d = key + n; *d; ++d)
	{
		if (*d < '0' || *d > '9')
			return 0;
		v = v * 10 + u32(*d - '0');
	}
	return v;
}

static void resolve_font_texture(LPCSTR base, string_path& out)
{
	LPCSTR _lang = pSettings->r_string("string_table", "font_prefix");
	const bool is_di = strstr(base, "ui_font_hud_01") || strstr(base, "ui_font_hud_02") ||
		strstr(base, "ui_font_console_02");

	if (_lang && !is_di)
		strconcat(sizeof(out), out, base, _lang);
	else
		xr_strcpy(out, sizeof(out), base);
}

static bool font_atlas_exists(LPCSTR base)
{
	string_path resolved, buf, fn;
	resolve_font_texture(base, resolved);
	xr_strcpy(buf, sizeof(buf), resolved);
	if (strext(buf))
		*strext(buf) = 0;
	return !!FS.exist(fn, "$game_textures$", buf, ".ini");
}

static int legacy_winner_idx(LPCSTR section, u32 h)
{
	int idx = LEGACY_DEFAULT_IDX;
	for (int i = 0; i < int(sizeof(s_legacy_rungs) / sizeof(s_legacy_rungs[0])); ++i)
	{
		if (rung_claims(s_legacy_rungs[i], h))
		{
			idx = i;
			break;
		}
	}
	while (idx >= 0)
	{
		if (pSettings->line_exist(section, s_legacy_rungs[idx].key))
			return idx;
		--idx;
	}
	return -1;
}

struct FontCandidate
{
	LPCSTR key;
	u32 authored;
	float dist;
	bool explicit_rung;
	bool tried;
};

IC bool candidate_better(const FontCandidate& a, const FontCandidate& b)
{
	if (fabsf(a.dist - b.dist) > EPS_S)
		return a.dist < b.dist;
	if (a.explicit_rung != b.explicit_rung)
		return a.explicit_rung;
	return a.authored > b.authored;
}

ENGINE_API LPCSTR GetFontTextureName(LPCSTR section)
{
	const u32 h = Device.dwHeight ? Device.dwHeight : 1;

	xr_vector<FontCandidate> cands;
	const CInifile::Sect& S = pSettings->r_section(section);
	for (const CInifile::Item& it : S.Data)
	{
		const u32 n = parse_rung_height(*it.first);
		if (n)
			cands.push_back({*it.first, n, 0.f, true, false});
	}

	const int lw = legacy_winner_idx(section, h);
	if (lw >= 0)
		cands.push_back({s_legacy_rungs[lw].key, s_legacy_rungs[lw].authored, 0.f, false, false});

	for (FontCandidate& c : cands)
		c.dist = fabsf(logf(float(c.authored) / float(h)));

	for (u32 n = 0; n < cands.size(); ++n)
	{
		int best = -1;
		for (u32 i = 0; i < cands.size(); ++i)
		{
			if (cands[i].tried)
				continue;
			if (best < 0 || candidate_better(cands[i], cands[best]))
				best = int(i);
		}
		cands[best].tried = true;

		LPCSTR tex = pSettings->r_string(section, cands[best].key);
		if (font_atlas_exists(tex))
		{
			Msg("* [font] h=%u [%s] %s = %s (authored %u, %+.1f%%)", h, section, cands[best].key, tex,
			    cands[best].authored, 100.f * (float(cands[best].authored) / float(h) - 1.f));
			return tex;
		}
		Msg("~[font] h=%u [%s] %s -> %s has no atlas, skipped", h, section, cands[best].key, tex);
	}

	Msg("~[font] h=%u [%s] no usable rung, forcing %s", h, section, s_legacy_rungs[LEGACY_DEFAULT_IDX].key);
	return pSettings->r_string(section, s_legacy_rungs[LEGACY_DEFAULT_IDX].key);
}

CGameFont::CGameFont(LPCSTR section, u32 flags)
{
	pFontRender = RenderFactory->CreateFontRender();
	fCurrentHeight = 0.0f;
	fXStep = 0.0f;
	fYStep = 0.0f;
	uFlags = flags;
	nNumChars = 0x100;
	TCMap = NULL;

	Initialize(pSettings->r_string(section, "shader"), GetFontTextureName(section));
	if (pSettings->line_exist(section, "size"))
	{
		float sz = pSettings->r_float(section, "size");
		if (uFlags & fsDeviceIndependent) SetHeightI(sz);
		else SetHeight(sz);
	}
	if (pSettings->line_exist(section, "interval"))
		SetInterval(pSettings->r_fvector2(section, "interval"));
}

CGameFont::CGameFont(LPCSTR shader, LPCSTR texture, u32 flags)
{
	pFontRender = RenderFactory->CreateFontRender();
	fCurrentHeight = 0.0f;
	fXStep = 0.0f;
	fYStep = 0.0f;
	uFlags = flags;
	nNumChars = 0x100;
	TCMap = NULL;
	Initialize(shader, texture);
}

void CGameFont::Initialize(LPCSTR cShader, LPCSTR cTextureName)
{
	string_path cTexture;

	resolve_font_texture(cTextureName, cTexture);

	uFlags &= ~fsValid;
	vTS.set(1.f, 1.f); // обязательно !!!

	eCurrentAlignment = alLeft;
	vInterval.set(1.f, 1.f);

	strings.reserve(128);

	// check ini exist
	string_path fn, buf;
	xr_strcpy(buf, cTexture);
	if (strext(buf)) *strext(buf) = 0;
	R_ASSERT2(FS.exist(fn, "$game_textures$", buf, ".ini"), fn);
	CInifile* ini = CInifile::Create(fn);

	nNumChars = 0x100;
	TCMap = (Fvector*)xr_realloc((void*)TCMap, nNumChars * sizeof(Fvector));

	if (ini->section_exist("mb_symbol_coords"))
	{
		nNumChars = 0x10000;
		TCMap = (Fvector*)xr_realloc((void*)TCMap, nNumChars * sizeof(Fvector));
		uFlags |= fsMultibyte;
		fHeight = ini->r_float("mb_symbol_coords", "height");

		fXStep = ceil(fHeight / 2.0f);

		// Searching for the first valid character

		Fvector vFirstValid = {0, 0, 0};

		if (ini->line_exist("mb_symbol_coords", "09608"))
		{
			Fvector v = ini->r_fvector3("mb_symbol_coords", "09608");
			vFirstValid.set(v.x, v.y, 1 + v[2] - v[0]);
		}
		else
			for (u32 i = 0; i < nNumChars; i++)
			{
				xr_sprintf(buf, sizeof(buf), "%05d", i);
				if (ini->line_exist("mb_symbol_coords", buf))
				{
					Fvector v = ini->r_fvector3("mb_symbol_coords", buf);
					vFirstValid.set(v.x, v.y, 1 + v[2] - v[0]);
					break;
				}
			}

		// Filling entire character table

		for (u32 i = 0; i < nNumChars; i++)
		{
			xr_sprintf(buf, sizeof(buf), "%05d", i);
			if (ini->line_exist("mb_symbol_coords", buf))
			{
				Fvector v = ini->r_fvector3("mb_symbol_coords", buf);
				TCMap[i].set(v.x, v.y, 1 + v[2] - v[0]);
			}
			else
				TCMap[i] = vFirstValid; // "unassigned" unprintable characters
		}

		// Special case for space
		TCMap[0x0020].set(0, 0, 0);
		// Special case for ideographic space
		TCMap[0x3000].set(0, 0, 0);
	}
	else if (ini->section_exist("symbol_coords"))
	{
		float d = 0.0f;
		//. if(ini->section_exist("width_correction"))
		//. d = ini->r_float("width_correction", "value");

		fHeight = ini->r_float("symbol_coords", "height");
		for (u32 i = 0; i < nNumChars; i++)
		{
			xr_sprintf(buf, sizeof(buf), "%03d", i);
			Fvector v = ini->r_fvector3("symbol_coords", buf);
			TCMap[i].set(v.x, v.y, v[2] - v[0] + d);
		}
	}
	else
	{
		if (ini->section_exist("char widths"))
		{
			fHeight = ini->r_float("char widths", "height");
			int cpl = 16;
			for (u32 i = 0; i < nNumChars; i++)
			{
				xr_sprintf(buf, sizeof(buf), "%d", i);
				float w = ini->r_float("char widths", buf);
				TCMap[i].set((i % cpl) * fHeight, (i / cpl) * fHeight, w);
			}
		}
		else
		{
			R_ASSERT(ini->section_exist("font_size"));
			fHeight = ini->r_float("font_size", "height");
			float width = ini->r_float("font_size", "width");
			const int cpl = ini->r_s32("font_size", "cpl");
			for (u32 i = 0; i < nNumChars; i++)
				TCMap[i].set((i % cpl) * width, (i / cpl) * fHeight, width);
		}
	}

	fCurrentHeight = fHeight;

	CInifile::Destroy(ini);

	// Shading
	pFontRender->Initialize(cShader, cTexture);
}

CGameFont::~CGameFont()
{
	if (TCMap)
		xr_free(TCMap);

	// Shading
	RenderFactory->DestroyFontRender(pFontRender);
}

#define DI2PX(x) float(iFloor((x+1)*float(::Render->getTarget()->get_width())*0.5f))
#define DI2PY(y) float(iFloor((y+1)*float(::Render->getTarget()->get_height())*0.5f))

void CGameFont::OutSet(float x, float y)
{
	fCurrentX = x;
	fCurrentY = y;
}

void CGameFont::OutSetI(float x, float y)
{
	OutSet(DI2PX(x), DI2PY(y));
}

u32 CGameFont::smart_strlen(const char* S)
{
	return (IsMultibyte() ? mbhMulti2Wide(NULL, NULL, 0, S) : xr_strlen(S));
}

void CGameFont::OnRender()
{
	pFontRender->OnRender(*this);
	strings.clear_not_free();
}

u16 CGameFont::GetCutLengthPos(float fTargetWidth, const char* pszText)
{
	VERIFY(pszText);

	wide_char wsStr[MAX_MB_CHARS], wsPos[MAX_MB_CHARS];
	float fCurWidth = 0.0f, fDelta = 0.0f;

	u16 len = mbhMulti2Wide(wsStr, wsPos, MAX_MB_CHARS, pszText);
	u16 i = 1;

	for (; i <= len; i++)
	{
		fDelta = GetCharTC(wsStr[i]).z - 2;

		if (IsNeedSpaceCharacter(wsStr[i]))
			fDelta += fXStep;

		if ((fCurWidth + fDelta) > fTargetWidth)
			break;
		else
			fCurWidth += fDelta;
	}

	return wsPos[i - 1];
}

u16 CGameFont::SplitByWidth(u16* puBuffer, u16 uBufferSize, float fTargetWidth, const char* pszText)
{
	VERIFY(puBuffer && uBufferSize && pszText);

	wide_char wsStr[MAX_MB_CHARS], wsPos[MAX_MB_CHARS];
	float fCurWidth = 0.0f, fDelta = 0.0f;
	u16 nLines = 0;

	u16 len = mbhMulti2Wide(wsStr, wsPos, MAX_MB_CHARS, pszText);

	for (u16 i = 1; i <= len; i++)
	{
		fDelta = GetCharTC(wsStr[i]).z - 2;

		if (IsNeedSpaceCharacter(wsStr[i]))
			fDelta += fXStep;

		if (
			((fCurWidth + fDelta) > fTargetWidth) && // overlength
			(!IsBadStartCharacter(wsStr[i])) && // can start with this character
			(i < len) && // is not the last character
			((i > 1) && (!IsBadEndCharacter(wsStr[i - 1]))) // && // do not stop the string on a "bad" character
				// ( ( i > 1 ) && ( ! ( ( IsAlphaCharacter( wsStr[ i - 1 ] ) ) && ( IsAlphaCharacter( wsStr[ i ] ) ) ) ) ) // do not split numbers or words
		)
		{
			fCurWidth = fDelta;
			VERIFY(nLines < uBufferSize);
			puBuffer[nLines++] = wsPos[i - 1];
		}
		else
			fCurWidth += fDelta;
	}

	return nLines;
}

void CGameFont::MasterOut(
	BOOL bCheckDevice, BOOL bUseCoords, BOOL bScaleCoords, BOOL bUseSkip,
	float _x, float _y, float _skip, LPCSTR fmt, va_list p)
{
	if (bCheckDevice && (!RDEVICE.b_is_Active))
		return;

	String rs;

	rs.x = (bUseCoords ? (bScaleCoords ? (DI2PX(_x)) : _x) : fCurrentX);
	rs.y = (bUseCoords ? (bScaleCoords ? (DI2PY(_y)) : _y) : fCurrentY);
	rs.c = dwCurrentColor;
	rs.height = fCurrentHeight;
	rs.align = eCurrentAlignment;
#ifndef _EDITOR
	int vs_sz = vsprintf_s(rs.string, fmt, p);
#else
    int vs_sz = vsprintf(rs.string, fmt, p);
#endif
	//VERIFY( ( vs_sz != -1 ) && ( rs.string[ vs_sz ] == '\0' ) );

	rs.string[sizeof(rs.string) - 1] = 0;
	if (vs_sz == -1)
	{
		return;
	}

	if (vs_sz)
		strings.push_back(rs);

	if (bUseSkip)
		OutSkip(_skip);
}

#define MASTER_OUT(CHECK_DEVICE,USE_COORDS,SCALE_COORDS,USE_SKIP,X,Y,SKIP,FMT) \
 { va_list p; va_start ( p , fmt ); \
 MasterOut( CHECK_DEVICE , USE_COORDS , SCALE_COORDS , USE_SKIP , X , Y , SKIP , FMT, p ); \
 va_end( p ); }

void __cdecl CGameFont::OutI(float _x, float _y, LPCSTR fmt, ...)
{
	MASTER_OUT(FALSE, TRUE, TRUE, FALSE, _x, _y, 0.0f, fmt);
};

void __cdecl CGameFont::Out(float _x, float _y, LPCSTR fmt, ...)
{
	MASTER_OUT(TRUE, TRUE, FALSE, FALSE, _x, _y, 0.0f, fmt);
};

void __cdecl CGameFont::OutNext(LPCSTR fmt, ...)
{
	MASTER_OUT(TRUE, FALSE, FALSE, TRUE, 0.0f, 0.0f, 1.0f, fmt);
};


void CGameFont::OutSkip(float val)
{
	fCurrentY += val * CurrentHeight_();
}

float CGameFont::SizeOf_(const char cChar)
{
	return (GetCharTC((u16)(u8)(((IsMultibyte() && cChar == ' ')) ? 0 : cChar)).z * WidthScale() * vInterval.x);
}

float CGameFont::SizeOf_(LPCSTR s)
{
	if (!(s && s[0]))
		return 0;

	if (IsMultibyte())
	{
		wide_char wsStr[MAX_MB_CHARS];

		mbhMulti2Wide(wsStr, NULL, MAX_MB_CHARS, s);

		return SizeOf_(wsStr);
	}

	int len = xr_strlen(s);
	float X = 0;
	if (len)
		for (int j = 0; j < len; j++)
			X += GetCharTC((u16)(u8)s[j]).z;

	return (X * WidthScale() * vInterval.x);
}

float CGameFont::SizeOf_(const wide_char* wsStr)
{
	if (!(wsStr && wsStr[0]))
		return 0;

	unsigned int len = wsStr[0];
	float X = 0.0f, fDelta = 0.0f;

	if (len)
		for (unsigned int j = 1; j <= len; j++)
		{
			fDelta = GetCharTC(wsStr[j]).z - 2;
			if (IsNeedSpaceCharacter(wsStr[j]))
				fDelta += fXStep;
			X += fDelta;
		}

	return (X * WidthScale() * vInterval.x);
}

float CGameFont::CurrentHeight_()
{
	return fCurrentHeight * vInterval.y;
}

void CGameFont::SetHeightI(float S)
{
	VERIFY(uFlags&fsDeviceIndependent);
	fCurrentHeight = S * RDEVICE.dwHeight;
};

void CGameFont::SetHeight(float S)
{
	VERIFY(uFlags&fsDeviceIndependent);
	fCurrentHeight = S;
};
