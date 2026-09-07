#include "pch_script.h"
#include "console_registrator.h"
#include "../xrEngine/xr_ioconsole.h"
#include "../xrEngine/xr_ioc_cmd.h"
#include "../xrEngine/device.h"
#include "ai_space.h"
#include "script_engine.h"
#include "../xrSound/Sound.h"
#include "player_hud.h"

using namespace luabind;

CConsole* console()
{
	return Console;
}

int get_console_integer(CConsole* c, LPCSTR cmd)
{
	int min = 0, max = 0;
	int val = c->GetInteger(cmd, min, max);
	return val;
}

float get_console_float(CConsole* c, LPCSTR cmd)
{
	float min = 0.0f, max = 0.0f;
	float val = c->GetFloat(cmd, min, max);
	return val;
}

bool get_console_bool(CConsole* c, LPCSTR cmd)
{
	return c->GetBool(cmd);
}

void execute_console_command_deferred(CConsole* c, LPCSTR string_to_execute)
{
	Engine.Event.Defer("KERNEL:console", size_t(xr_strdup(string_to_execute)));
}

::luabind::object get_console_bounds(CConsole* c, LPCSTR cmd)
{
	IConsole_Command* command = c->GetCommand(cmd);
	::luabind::object table = ::luabind::newtable(ai().script_engine().lua());
	if (command)
	{
		CCC_Float* float_command = smart_cast<CCC_Float*>(command);
		if (float_command)
		{
			float min, max;
			float_command->GetBounds(min, max);
			table["min"] = min;
			table["max"] = max;
			return table;
		}

		CCC_Integer* integer_command = smart_cast<CCC_Integer*>(command);
		if (integer_command) {
			int min, max;
			integer_command->GetBounds(min, max);
			table["min"] = min;
			table["max"] = max;
			return table;
		}
	}
	return table;
}

::luabind::object get_console_token_list(CConsole* c, LPCSTR cmd)
{
	::luabind::object table = ::luabind::newtable(ai().script_engine().lua());
	xr_token* tok = c->GetXRToken(cmd);
	if (tok)
	{
		int idx = 1;
		while (tok->name)
		{
			table[idx] = tok->name;
			++tok;
			++idx;
		}
	}
	return table;
}

// pip true if a real PiP scope is rendering via the second viewport (reflex/iron/non-PiP sights never set it)
bool is_svp_active()
{
	return Device.m_SecondViewport.IsSVPActive();
}

// pip measured lens optics, kill-switch gated so a default build never runs detection
extern int ps_r__svp_measured_optics;

// run the mesh detection on one hud model, true when a lens with an objective is found
static bool svp_detect_hud(attachable_hud_item* h, SLensDetection& d)
{
	return h && h->m_model && h->m_model->GetLensDetection(d) && d.ok && d.has_objective;
}

// the active scope hud, the attached scope model carries an addon lens, the weapon model an integral one
static bool svp_detect_active(SLensDetection& d)
{
	if (!ps_r__svp_measured_optics || !g_player_hud)
		return false;
	if (svp_detect_hud(g_player_hud->attached_item(SCOPE_ATTACH_IDX), d))
		return true;
	return svp_detect_hud(g_player_hud->attached_item(0), d);
}

// scope_objective_lens_offset x,y,z,w in eyepiece-radius units, "" when off or nothing fits
LPCSTR svp_detected_offset()
{
	static string128 s;
	SLensDetection d;
	if (!svp_detect_active(d))
		return "";
	xr_sprintf(s, "%f,%f,%f,%f", d.offset.x, d.offset.y, d.offset.z, d.offset.w);
	return s;
}

// s3ds_objective_mm 2000 x obj_radius, -1 when off or nothing fits
float svp_detected_obj_mm()
{
	SLensDetection d;
	if (!svp_detect_active(d))
		return -1.f;
	return d.mm > 0.f ? d.mm : -1.f;
}

#pragma optimize("s",on)
void console_registrator::script_register(lua_State* L)
{
	module(L)
	[
		def("get_console", &console),
		def("is_svp_active", &is_svp_active),
		def("svp_detected_offset", &svp_detected_offset),
		def("svp_detected_obj_mm", &svp_detected_obj_mm),

		class_<CConsole>("CConsole")
		.def("execute", &CConsole::Execute)
		.def("execute_script", &CConsole::ExecuteScript)
		.def("show", &CConsole::Show)
		.def("hide", &CConsole::Hide)

		.def("get_string", &CConsole::GetString)
		.def("get_integer", &get_console_integer)
		.def("get_variable_bounds", &get_console_bounds)
		.def("get_bool", &get_console_bool)
		.def("get_float", &get_console_float)
		.def("get_token", &CConsole::GetToken)
		.def("get_token_list", &get_console_token_list)
		.def("execute_deferred", &execute_console_command_deferred)
	];
}
