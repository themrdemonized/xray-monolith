#include "pch_script.h"
#include "UIProgressBar.h"

using namespace luabind;

#pragma optimize("s",on)
void CUIProgressBar::script_register(lua_State* L)
{
	module(L)
	[
		class_<CUIProgressBar, CUIWindow>("CUIProgressBar")
		.def(constructor<>())
		.def("SetProgressPos", &CUIProgressBar::SetProgressPos)
		.def("GetProgressPos", &CUIProgressBar::GetProgressPos)

		.def("GetRange_min", &CUIProgressBar::GetRange_min)
		.def("GetRange_max", &CUIProgressBar::GetRange_max)
		.def("SetRange", &CUIProgressBar::SetRange)
		
		// Tronex
		.def("ShowBackground", &CUIProgressBar::ShowBackground)
		.def("SetColor", &CUIProgressBar::SetColor)
		.def("UseColor", &CUIProgressBar::UseColor)
		.def("SetMinColor", &CUIProgressBar::SetMinColor)
		.def("SetMiddleColor", &CUIProgressBar::SetMinColor)
		.def("SetMaxColor", &CUIProgressBar::SetMinColor)
	];
}
