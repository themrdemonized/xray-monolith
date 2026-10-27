#ifndef IRENDERABLE_H_INCLUDED
#define IRENDERABLE_H_INCLUDED

#include "render.h"

namespace ShaderBus
{
	struct object_values;
}

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
		// own values on shader bus obj_ lanes, null while it has none
		ShaderBus::object_values* bus_values = nullptr;
		// drawn into the rain occlusion map, so what is under it stays dry (vehicles: their interiors)
		BOOL rain_occluder = FALSE;
	} renderable;

public:
	IRenderable();
	virtual ~IRenderable();
	IRender_ObjectSpecific* renderable_ROS();
	// marks it as a rain occluder; the rain map draws dynamic objects only while at least one exists, so a game
	// without any (no Vehicle Handling Overhaul) renders rain exactly as before
	void set_rain_occluder(bool on);
	static int rain_occluders; // live renderables with renderable.rain_occluder set
	BENCH_SEC_SCRAMBLEVTBL2
	virtual void renderable_Render() = 0;
	virtual BOOL renderable_ShadowGenerate() { return FALSE; };
	virtual BOOL renderable_ShadowReceive() { return FALSE; };

	virtual float GetHotness() { return 0.0; }			//--DSR-- HeatVision
	virtual float GetTransparency() { return 0.0; }		//--DSR-- HeatVision
	virtual float GetGlowing() { return 0.0; }			//--DSR-- SilencerOverheat

};

#endif // IRENDERABLE_H_INCLUDED
