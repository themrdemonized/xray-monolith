#include "stdafx.h"
#include "igame_level.h"

void IGame_Level::LL_CheckTextures()
{
	u32 m_base, c_base, m_lmaps, c_lmaps;
	Device.m_pRender->ResourcesGetMemoryUsage(m_base, c_base, m_lmaps, c_lmaps);

	Msg("* t-report - base: %d, %d K", c_base, m_base / 1024);
	Msg("* t-report - lmap: %d, %d K", c_lmaps, m_lmaps / 1024);
	BOOL bError = FALSE;
	if (m_base > 64 * 1024 * 1024 || c_base > 400)
	{
		bError = TRUE;
	}
	if (m_lmaps > 32 * 1024 * 1024 || c_lmaps > 8)
	{
#ifdef DEBUG
        LPCSTR msg = "Too many lmap-textures (limit: 8 textures or 32M).\n        Reduce pixel density (worse) or use more vertex lighting (better).";
        Msg("***FATAL***: %s", msg);
#endif // #ifdef DEBUG
		bError = TRUE;
	}
}
