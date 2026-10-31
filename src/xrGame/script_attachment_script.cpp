#pragma once
#include "pch_script.h"
#include "script_attachment_manager.h"
#include "physics_shell_scripted.h"

using namespace ::luabind;

#pragma optimize("s",on)
void script_attachment::script_register(lua_State* L)
{
	module(L)
	[
		//Type
		class_<enum_exporter<script_attachment_type>>("script_attachment_type")
		.enum_("script_attachment_type")
		[
			value("Hud", (int)eSA_HUD),
			value("World", (int)eSA_World),
			value("CamAttached", (int)eSA_CamAttached)
		],

		class_<enum_exporter<script_attachment_hud>>("script_attachment_hud")
		.enum_("script_attachment_hud")
		[
			value("Parent", (int)eSAH_Parent),
			value("Actor", (int)eSAH_Actor),
			value("Item", (int)eSAH_Item)
		],

		class_<script_attachment>("ScriptAttachment")

		//Parent
		.def("set_parent", (void (script_attachment::*)(script_attachment*)) &script_attachment::SetParent)
		.def("set_parent", (void (script_attachment::*)(CScriptGameObject*)) &script_attachment::SetParent)
		.def("set_parent_level", &script_attachment::SetParentLevel)
		.def("get_parent", &script_attachment::GetParent)
		.def("get_toplevel_parent", &script_attachment::GetTopLevelParentScript)

		//Child
		.def("add_attachment", &script_attachment::AddAttachment)
		.def("get_attachment", &script_attachment::GetChild)
		.def("remove_attachment", (void (script_attachment::*)(LPCSTR)) &script_attachment::RemoveAttachment)
		.def("remove_attachment", (void (script_attachment::*)(script_attachment*)) &script_attachment::RemoveAttachment)
		.def("iterate_attachments", &script_attachment::IterateAttachments)

		//Offset
		.def("set_position", (void (script_attachment::*)(Fvector)) &script_attachment::SetPosition)
		.def("set_position", (void (script_attachment::*)(float, float, float)) &script_attachment::SetPosition)
		.def("get_position", &script_attachment::GetPosition)
		.def("set_rotation", (void (script_attachment::*)(Fvector)) &script_attachment::SetRotation)
		.def("set_rotation", (void (script_attachment::*)(float, float, float)) &script_attachment::SetRotation)
		.def("get_rotation", &script_attachment::GetRotation)
		.def("set_origin", (void (script_attachment::*)(Fvector)) &script_attachment::SetOrigin)
		.def("set_origin", (void (script_attachment::*)(float, float, float)) &script_attachment::SetOrigin)
		.def("get_origin", &script_attachment::GetOrigin)
		.def("set_scale", (void (script_attachment::*)(Fvector)) &script_attachment::SetScale)
		.def("set_scale", (void (script_attachment::*)(float)) &script_attachment::SetScale)
		.def("get_scale", &script_attachment::GetScale)
		.def("get_scale_val", &script_attachment::GetScaleVal)
		.def("get_transform", &script_attachment::GetTransform)
		.def("get_center", &script_attachment::Center)
		.def("set_custom_fov", &script_attachment::SetCustomFOV)
		.def("get_custom_fov", &script_attachment::GetCustomFOV)
        .def("force_update_xform", &script_attachment::ForceUpdateXFORM)

		//Bones
		.def("bone_id", &script_attachment::bone_id)
		.def("bone_name", &script_attachment::bone_name)
		.def("bone_visible", (bool (script_attachment::*)(u16)) &script_attachment::GetBoneVisible)
		.def("bone_visible", (bool (script_attachment::*)(LPCSTR)) &script_attachment::GetBoneVisible)
		.def("set_bone_visible", (void (script_attachment::*)(u16, bool, bool)) &script_attachment::SetBoneVisible)
		.def("set_bone_visible", (void (script_attachment::*)(LPCSTR, bool, bool)) &script_attachment::SetBoneVisible)
		.def("bone_transform", (Fmatrix (script_attachment::*)(u16)) &script_attachment::bone_transform)
		.def("bone_transform", (Fmatrix (script_attachment::*)(u16, bool)) &script_attachment::bone_transform)
		.def("bone_transform", (Fmatrix (script_attachment::*)(LPCSTR)) &script_attachment::bone_transform)
		.def("bone_transform", (Fmatrix (script_attachment::*)(LPCSTR, bool)) &script_attachment::bone_transform)
		.def("bone_position", (Fvector (script_attachment::*)(u16)) &script_attachment::bone_position)
		.def("bone_position", (Fvector (script_attachment::*)(LPCSTR)) &script_attachment::bone_position)
		.def("bone_direction", (Fvector (script_attachment::*)(u16)) &script_attachment::bone_direction)
		.def("bone_direction", (Fvector (script_attachment::*)(LPCSTR)) &script_attachment::bone_direction)
		.def("bone_parent", (u16 (script_attachment::*)(u16)) &script_attachment::bone_parent)
		.def("bone_parent", (u16 (script_attachment::*)(LPCSTR)) &script_attachment::bone_parent)
		.def("set_parent_bone", (void (script_attachment::*)(u16)) &script_attachment::SetParentBone)
		.def("set_parent_bone", (void (script_attachment::*)(LPCSTR)) &script_attachment::SetParentBone)
		.def("get_parent_bone", &script_attachment::GetParentBone)
		.def("list_bones", &script_attachment::list_bones)

		//Bone Callbacks
		.def("bone_callback", (void (script_attachment::*)(u16, u16)) &script_attachment::SetBoneCallback)
		.def("bone_callback", (void (script_attachment::*)(LPCSTR, LPCSTR)) &script_attachment::SetBoneCallback)
		.def("bone_callback", (void (script_attachment::*)(u16, LPCSTR)) &script_attachment::SetBoneCallback)
		.def("bone_callback", (void (script_attachment::*)(LPCSTR, u16)) &script_attachment::SetBoneCallback)
		.def("bone_callback", (void (script_attachment::*)(u16, const ::luabind::functor<Fmatrix>&)) &script_attachment::SetBoneCallback)
		.def("bone_callback", (void (script_attachment::*)(LPCSTR, const ::luabind::functor<Fmatrix>&)) &script_attachment::SetBoneCallback)
		.def("bone_callback", (void (script_attachment::*)(u16, u16, bool)) &script_attachment::SetBoneCallback)
		.def("bone_callback", (void (script_attachment::*)(LPCSTR, LPCSTR, bool)) &script_attachment::SetBoneCallback)
		.def("bone_callback", (void (script_attachment::*)(u16, LPCSTR, bool)) &script_attachment::SetBoneCallback)
		.def("bone_callback", (void (script_attachment::*)(LPCSTR, u16, bool)) &script_attachment::SetBoneCallback)
		.def("bone_callback", (void (script_attachment::*)(u16, const ::luabind::functor<Fmatrix>&, bool)) & script_attachment::SetBoneCallback)
		.def("bone_callback", (void (script_attachment::*)(LPCSTR, const ::luabind::functor<Fmatrix>&, bool)) & script_attachment::SetBoneCallback)
		.def("remove_bone_callback", (void (script_attachment::*)(u16)) &script_attachment::RemoveBoneCallback)
		.def("remove_bone_callback", (void (script_attachment::*)(LPCSTR)) &script_attachment::RemoveBoneCallback)

		//Other
		.def("set_type", &script_attachment::SetType)
		.def("get_type", &script_attachment::GetType)
		.def("set_hud_category", &script_attachment::SetHudCategory)
		.def("get_hud_category", &script_attachment::GetHudCategory)
		.def("set_model", &script_attachment::LoadModel)
		.def("get_model", &script_attachment::GetModelScript)
		.def("set_name", &script_attachment::SetName)
		.def("get_name", &script_attachment::GetName)
		.def("play_motion", (u32 (script_attachment::*)(LPCSTR)) &script_attachment::PlayMotion)
		.def("play_motion", (u32 (script_attachment::*)(LPCSTR, bool)) &script_attachment::PlayMotion)
		.def("play_motion", (u32 (script_attachment::*)(LPCSTR, bool, float)) &script_attachment::PlayMotion)
		.def("play_motion", (u32 (script_attachment::*)(LPCSTR, bool, float, LPCSTR)) &script_attachment::PlayMotionCB)
		.def("play_motion", (u32 (script_attachment::*)(LPCSTR, bool, float, const ::luabind::functor<void>&)) &script_attachment::PlayMotionCB)
		.def("get_visbox", &script_attachment::Box)
		.def("bone_shape", &script_attachment::BoneShape)
		.def("has_shapes", &script_attachment::has_shapes)
		.def("set_use_ph_shapes", &script_attachment::SetUsePHShapes)
		.def("get_use_ph_shapes", &script_attachment::GetUsePHShapes)
		.def("ph_freeze", &script_attachment::SetPHFreeze)
		.def("ph_frozen", &script_attachment::GetPHFreeze)
		.def("get_physics_shell", &script_attachment::get_physics_shell)
		.def("set_ph_contact_callback", (void (script_attachment::*)(LPCSTR)) &script_attachment::SetPHContactCallback)
		.def("set_ph_contact_callback", (void (script_attachment::*)(const ::luabind::functor<bool>&)) &script_attachment::SetPHContactCallback)
		.def("set_collidable", &script_attachment::spatial_SetCollidable)
		.def("get_collidable", &script_attachment::spatial_GetCollidable)
		.def("set_obstacle", &script_attachment::spatial_SetObstacle)
		.def("get_obstacle", &script_attachment::spatial_GetObstacle)
		.def("set_visible", &script_attachment::spatial_SetVisible)
		.def("get_visible", &script_attachment::spatial_GetVisible)
		.def("set_cam_collide", &script_attachment::SetCamCollide)
		.def("get_cam_collide", &script_attachment::GetCamCollide)
        .def("set_instance_multirender", &script_attachment::SetModelInstanceMultirender)
        .def("get_instance_multirender", &script_attachment::ModelInstanceMultirender)
		.def("model_instance", (void (script_attachment::*)(CScriptGameObject*)) &script_attachment::ModelInstance)
		.def("model_instance", (void (script_attachment::*)(::luabind::object)) &script_attachment::ModelInstance)
		.def("model_instance", (void (script_attachment::*)(CScriptGameObject*, bool)) &script_attachment::ModelInstance)
		.def("model_instance", (void (script_attachment::*)(::luabind::object, bool)) &script_attachment::ModelInstance)
		
		//Script 3D UI
		.def("set_ui", &script_attachment::SetScriptUI)
		.def("get_ui", &script_attachment::GetScriptUI)
		.def("set_ui_bone", (void (script_attachment::*)(u16)) &script_attachment::SetScriptUIBone)
		.def("set_ui_bone", (void (script_attachment::*)(LPCSTR)) &script_attachment::SetScriptUIBone)
		.def("get_ui_bone", &script_attachment::GetScriptUIBone)
		.def("set_ui_position", (void (script_attachment::*)(Fvector)) &script_attachment::SetScriptUIPosition)
		.def("set_ui_position", (void (script_attachment::*)(float, float, float)) &script_attachment::SetScriptUIPosition)
		.def("get_ui_position", &script_attachment::GetScriptUIPosition)
		.def("set_ui_rotation", (void (script_attachment::*)(Fvector)) &script_attachment::SetScriptUIRotation)
		.def("set_ui_rotation", (void (script_attachment::*)(float, float, float)) &script_attachment::SetScriptUIRotation)
		.def("get_ui_rotation", &script_attachment::GetScriptUIRotation)
		.def("set_ui_scale", (void (script_attachment::*)(Fvector)) &script_attachment::SetScriptUIScale)
		.def("set_ui_scale", (void (script_attachment::*)(float, float, float)) &script_attachment::SetScriptUIScale)
		.def("get_ui_scale", &script_attachment::GetScriptUIScale)
		.def("set_ui_origin", (void (script_attachment::*)(Fvector)) &script_attachment::SetScriptUIOrigin)
		.def("set_ui_origin", (void (script_attachment::*)(float, float, float)) &script_attachment::SetScriptUIOrigin)
		.def("get_ui_origin", &script_attachment::GetScriptUIOrigin)

		//Script Light
		.def("attach_light", &script_attachment::AttachLight)
		.def("detach_light", &script_attachment::DetachLight)
		.def("get_light", &script_attachment::GetLight)
		.def("set_light_bone", (void (script_attachment::*)(u16)) &script_attachment::SetScriptLightBone)
		.def("set_light_bone", (void (script_attachment::*)(LPCSTR)) &script_attachment::SetScriptLightBone)
		.def("get_light_bone", &script_attachment::GetScriptLightBone)

		//Shader and Texture
		.def("get_shaders", &script_attachment::GetShaders)
		.def("get_default_shaders", &script_attachment::GetDefaultShaders)
		.def("set_shader", &script_attachment::SetShaderTexture)
		.def("reset_shader", &script_attachment::ResetShaderTexture)

		//Userdata
		.property("userdata", &script_attachment::GetUserdata, &script_attachment::SetUserdata)
	];
}