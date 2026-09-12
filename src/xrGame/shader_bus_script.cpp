#include "pch_script.h"
#include "shader_bus_script.h"
#include "../xrEngine/shader_bus.h"
#include "../xrEngine/xr_ioconsole.h"
#include "../xrEngine/xr_ioc_cmd.h"

using namespace luabind;

static void bus_caller_source(lua_State* L, string_path& dest)
{
	xr_strcpy(dest, "?");

	if (!L)
		return;

	lua_Debug ar;
	for (int level = 0; lua_getstack(L, level, &ar); ++level)
	{
		if (!lua_getinfo(L, "S", &ar))
			return;
		if (ar.what && 0 != xr_strcmp(ar.what, "C"))
		{
			xr_strcpy(dest, ar.short_src);
			return;
		}
	}
}

static ::luabind::object bus_token_object(lua_State* L, u32 token)
{
	if (token)
		return ::luabind::object(L, token);

	::luabind::object none(L);
	lua_pushnil(L);
	none.set();
	return none;
}

static ::luabind::object bus_register(lua_State* L, LPCSTR id, LPCSTR owner, LPCSTR description)
{
	string_path source;
	bus_caller_source(L, source);

	return bus_token_object(L, ShaderBus::register_lane(id, owner, description, source));
}

static ::luabind::object bus_try_register(lua_State* L, LPCSTR id, LPCSTR owner, LPCSTR description)
{
	string_path source;
	bus_caller_source(L, source);

	return bus_token_object(L, ShaderBus::try_register(id, owner, description, source));
}

static bool bus_set(u32 token, float x, float y, float z, float w)
{
	return ShaderBus::set(token, x, y, z, w);
}

static bool bus_get(LPCSTR id, float& x, float& y, float& z, float& w)
{
	Fvector4 v;
	const bool found = ShaderBus::get(id, v);
	if (!found)
		v.set(0.f, 0.f, 0.f, 0.f);

	x = v.x;
	y = v.y;
	z = v.z;
	w = v.w;
	return found;
}

static bool bus_has(LPCSTR id)
{
	return ShaderBus::has(id);
}

static LPCSTR bus_describe(LPCSTR id)
{
	return ShaderBus::describe(id);
}

static LPCSTR bus_owner_of(LPCSTR id)
{
	return ShaderBus::owner_of(id);
}

static bool bus_stats(LPCSTR id, u32& changes, u32& last_change, u32& bound_frame, u32& writes)
{
	changes = 0;
	last_change = 0;
	bound_frame = 0;
	writes = 0;
	return ShaderBus::stats(id, changes, last_change, bound_frame, writes);
}

static bool bus_get_pending(LPCSTR id, float& x, float& y, float& z, float& w)
{
	Fvector4 v;
	const bool found = ShaderBus::get_pending(id, v);
	if (!found)
		v.set(0.f, 0.f, 0.f, 0.f);

	x = v.x;
	y = v.y;
	z = v.z;
	w = v.w;
	return found;
}

static int bus_version()
{
	return ShaderBus::version();
}

static ::luabind::object bus_list(lua_State* L, bool include_declared)
{
	::luabind::object rows = ::luabind::newtable(L);

	int row_index = 1;
	const u32 lanes = ShaderBus::count();
	for (u32 i = 0; i < lanes; ++i)
	{
		const ShaderBus::lane* l = ShaderBus::at(i);
		if (!l)
			continue;
		if (!l->registered && !include_declared)
			continue;

		::luabind::object row = ::luabind::newtable(L);
		row["id"] = l->id.c_str();
		row["owner"] = l->registered ? l->owner.c_str() : "";
		row["description"] = l->registered ? l->description.c_str() : "";
		row["state"] = l->registered ? "registered" : "declared";
		row["source"] = l->registered ? l->source.c_str() : "";
		rows[row_index++] = row;
	}

	static LPCSTR legacy_lanes[] = {
		"shader_param_1", "shader_param_2", "shader_param_3", "shader_param_4",
		"shader_param_5", "shader_param_6", "shader_param_7", "shader_param_8",
		"s3ds_param_1", "s3ds_param_2", "s3ds_param_3", "s3ds_param_4"
	};

	for (u32 i = 0; i < sizeof(legacy_lanes) / sizeof(legacy_lanes[0]); ++i)
	{
		IConsole_Command* cc = Console ? Console->GetCommand(legacy_lanes[i]) : nullptr;
		if (!cc || !smart_cast<CCC_Vector4*>(cc))
			continue;

		string256 writer;
		ShaderBus::legacy_writer(legacy_lanes[i], writer);

		::luabind::object row = ::luabind::newtable(L);
		row["id"] = legacy_lanes[i];
		row["owner"] = "engine legacy";
		row["description"] = "";
		row["state"] = "legacy";
		row["writer"] = (LPCSTR)writer;
		row["source"] = "";
		rows[row_index++] = row;
	}
	return rows;
}

static ::luabind::object bus_list_registered(lua_State* L)
{
	return bus_list(L, false);
}

#pragma optimize("s",on)
void shader_bus_registrator::script_register(lua_State* L)
{
	module(L, "shader_bus")
	[
		def("register", &bus_register, raw<1>()),
		def("try_register", &bus_try_register, raw<1>()),
		def("set", &bus_set),
		def("get", &bus_get,
		    pure_out_value<2>() + pure_out_value<3>() + pure_out_value<4>() + pure_out_value<5>()),
		def("has", &bus_has),
		def("describe", &bus_describe),
		def("owner_of", &bus_owner_of),
		def("stats", &bus_stats,
		    pure_out_value<2>() + pure_out_value<3>() + pure_out_value<4>() + pure_out_value<5>()),
		def("get_pending", &bus_get_pending,
		    pure_out_value<2>() + pure_out_value<3>() + pure_out_value<4>() + pure_out_value<5>()),
		def("list", &bus_list_registered, raw<1>()),
		def("list", &bus_list, raw<1>()),
		def("version", &bus_version)
	];
}
