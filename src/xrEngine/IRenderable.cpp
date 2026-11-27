#include "stdafx.h"
#include "shader_bus.h"
#include "../xrcdb/ispatial.h"
#include "irenderable.h"

IRenderable::IRenderable()
{
	renderable.xform.identity();
	renderable.visual = NULL;
	renderable.pROS = NULL;
	renderable.pROS_Allowed = TRUE;
	ISpatial* self = fast_dynamic_cast<ISpatial*>(this);
	if (self) self->spatial.type |= STYPE_RENDERABLE;
}

extern ENGINE_API BOOL g_bRendering;

int IRenderable::rain_occluders = 0;

void IRenderable::set_rain_occluder(bool on)
{
	if (!!renderable.rain_occluder == on) return;
	renderable.rain_occluder = on ? TRUE : FALSE;
	rain_occluders += on ? 1 : -1;
}

IRenderable::~IRenderable()
{
	VERIFY(!g_bRendering);
	Render->model_Delete(renderable.visual);
	if (renderable.pROS) Render->ros_destroy(renderable.pROS);
	renderable.visual = NULL;
	renderable.pROS = NULL;
	ShaderBus::object_forget(this);
	set_rain_occluder(false);
}

IRender_ObjectSpecific* IRenderable::renderable_ROS()
{
	if (0 == renderable.pROS && renderable.pROS_Allowed) renderable.pROS = Render->ros_create(this);
	return renderable.pROS;
}
