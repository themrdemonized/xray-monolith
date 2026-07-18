#ifndef	dx103DFluidVolume_included
#define	dx103DFluidVolume_included
#pragma once

#include "dx103DFluidData.h"
#include "../../xrRender/FBasicVisual.h"

class dx103DFluidVolume : public dxRender_Visual
{
public:
	typedef dx103DFluidData::PreparedData PreparedData;

	dx103DFluidVolume();
	virtual ~dx103DFluidVolume();

	static void Prepare(IReader* data, PreparedData& prepared);
	virtual void Load(LPCSTR N, IReader* data, u32 dwFlags);
	void LoadPrepared(const PreparedData& prepared);
	virtual void Render(float LOD); // LOD - Level Of Detail  [0.0f - min, 1.0f - max], Ignored ?
	virtual void Copy(dxRender_Visual* pFrom);
	virtual void Release();

private:
	//	For debug purpose only
	ref_geom m_Geom;

	dx103DFluidData m_FluidData;

	void InitializeVisual();
	void UpdateVisibility();
};

#endif	//	dx103DFluidVolume_included
