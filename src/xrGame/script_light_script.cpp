#include "pch_script.h"
#include "script_light.h"

using namespace luabind;

#pragma optimize("s",on)
void ScriptLight::script_register(lua_State *L)
{
	module(L)
		[
			class_<ScriptLight>("script_light")
			.def(constructor<>())
			.def("set_position", (void (ScriptLight::*)(Fvector)) & ScriptLight::SetPosition)
			.def("set_position", (void (ScriptLight::*)(float, float, float)) & ScriptLight::SetPosition)
			.def("get_position", (Fvector (ScriptLight::*)()) & ScriptLight::GetPosition)
			.def("set_direction", (void (ScriptLight::*)(Fvector))(&ScriptLight::SetDirection))
			.def("set_direction", (void (ScriptLight::*)(float, float, float)) & ScriptLight::SetDirection)
			.def("set_direction", (void (ScriptLight::*)(Fvector, Fvector))(&ScriptLight::SetDirection))
			.def("get_direction", (Fvector (ScriptLight::*)())(&ScriptLight::GetDirection))
			.def("get_right", (Fvector (ScriptLight::*)())(&ScriptLight::GetRight))
			.def("set_cone", &ScriptLight::SetCone)
			.def("get_cone", &ScriptLight::GetCone)
			.def("update", &ScriptLight::DummyUpdate)
			.property("color", &ScriptLight::GetColor, &ScriptLight::SetColor)
			.property("texture", &ScriptLight::GetTexture, &ScriptLight::SetTexture)
			.property("enabled", &ScriptLight::IsEnabled, &ScriptLight::Enable)
			.property("type", &ScriptLight::GetType, &ScriptLight::SetType)
			.property("range", &ScriptLight::GetRange, &ScriptLight::SetRange)
			.property("shadow", &ScriptLight::GetShadow, &ScriptLight::SetShadow)
			.property("lanim", &ScriptLight::GetLanim, &ScriptLight::SetLanim)
			.property("lanim_brightness", &ScriptLight::GetBrightness, &ScriptLight::SetBrightness)
			.property("volumetric", &ScriptLight::GetVolumetric, &ScriptLight::SetVolumetric)
			.property("volumetric_quality", &ScriptLight::GetVolumetricQuality, &ScriptLight::SetVolumetricQuality)
			.property("volumetric_distance", &ScriptLight::GetVolumetricDistance, &ScriptLight::SetVolumetricDistance)
			.property("volumetric_intensity", &ScriptLight::GetVolumetricIntensity, &ScriptLight::SetVolumetricIntensity)
			.property("hud_mode", &ScriptLight::GetHudMode, &ScriptLight::SetHudMode)
			.property("ortho", &ScriptLight::GetOrtho, &ScriptLight::SetOrtho)
			.property("player_light", &ScriptLight::GetPlayerLight, &ScriptLight::SetPlayerLight)
			,
			
			class_<AttachmentScriptLight, ScriptLight>("attachment_script_light")
			.def(constructor<>())
			.def("set_position", (void (AttachmentScriptLight::*)(Fvector)) & AttachmentScriptLight::SetPosition)
			.def("set_position", (void (AttachmentScriptLight::*)(float, float, float)) & AttachmentScriptLight::SetPosition)
			.def("get_position", (Fvector (AttachmentScriptLight::*)()) &AttachmentScriptLight::GetPosition)
			.def("set_direction", (void (AttachmentScriptLight::*)(Fvector))(&AttachmentScriptLight::SetDirection))
			.def("set_direction", (void (AttachmentScriptLight::*)(float, float, float)) & AttachmentScriptLight::SetDirection)
			.def("set_direction", (void (AttachmentScriptLight::*)(Fvector, Fvector))(&AttachmentScriptLight::SetDirection))
			.def("get_direction", (Fvector (AttachmentScriptLight::*)()) & AttachmentScriptLight::GetDirection)
			,

			class_<ScriptGlow>("script_glow")
			.def(constructor<>())
			.def("set_position", (void (ScriptGlow::*)(Fvector)) & ScriptGlow::SetPosition)
			.def("set_position", (void (ScriptGlow::*)(float, float, float)) & ScriptGlow::SetPosition)
			.def("set_direction", (void (ScriptGlow::*)(Fvector))(&ScriptGlow::SetDirection))
			.def("set_direction", (void (ScriptGlow::*)(float, float, float)) & ScriptGlow::SetDirection)
			.property("enabled", &ScriptGlow::IsEnabled, &ScriptGlow::Enable)
			.property("texture", &ScriptGlow::GetTexture, &ScriptGlow::SetTexture)
			.property("range", &ScriptGlow::GetRange, &ScriptGlow::SetRange)
			.property("color", &ScriptGlow::GetColor, &ScriptGlow::SetColor)
			.property("lanim", &ScriptGlow::GetLanim, &ScriptGlow::SetLanim)
			.property("lanim_brightness", &ScriptGlow::GetBrightness, &ScriptGlow::SetBrightness)
		];
}