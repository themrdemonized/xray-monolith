#include "pch_script.h"
#include "UIBinocularsVision.h"

using namespace luabind;

#pragma optimize("s",on)
void CUIBinocularsVision::script_register(lua_State* L)
{
	module(L)
	[
		class_<CUIBinocularsVision, CUIWindow>("CUIBinocularsVision")
		.def(constructor<LPCSTR, LPCSTR, LPCSTR, LPCSTR, const Fvector2&>())
		.def(constructor<LPCSTR>())
		.def("SetFrameColor", &CUIBinocularsVision::SetFrameColor)
		.def("SetSpeed", &CUIBinocularsVision::SetSpeed)
		.def("SetRelationColors", &CUIBinocularsVision::SetRelationColors)
		.def("SetFoundSound", &CUIBinocularsVision::SetFoundSound)
		.def("SetCatchSound", &CUIBinocularsVision::SetCatchSound)
	];
}
