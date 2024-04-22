#ifndef IRENDERABLE_H_INCLUDED
#define IRENDERABLE_H_INCLUDED

#include "render.h"

//////////////////////////////////////////////////////////////////////////
// definition ("Renderable")
class ENGINE_API IRenderable
{
public:
	struct
	{
		Fmatrix xform;
		IRenderVisual* visual;
		IRender_ObjectSpecific* pROS;
		BOOL pROS_Allowed;
	} renderable;

public:
	IRenderable();
	virtual ~IRenderable();
	IRender_ObjectSpecific* renderable_ROS();
	BENCH_SEC_SCRAMBLEVTBL2
	virtual void renderable_Render() = 0;
	virtual BOOL renderable_ShadowGenerate() { return FALSE; };
	virtual BOOL renderable_ShadowReceive() { return FALSE; };

	virtual float GetHotness() { return 0.0; }			//--DSR-- HeatVision
	virtual float GetTransparency() { return 0.0; }		//--DSR-- HeatVision
	virtual float GetGlowing() { return 0.0; }			//--DSR-- SilencerOverheat
};

#endif // IRENDERABLE_H_INCLUDED
