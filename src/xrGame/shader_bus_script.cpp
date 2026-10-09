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

// reads rows {x, y, z, w} from a Lua array, four numbers per row, a missing component reads 0
static bool bus_read_rows(const ::luabind::object& rows, xr_vector<double>& out)
{
	if (rows.type() != LUA_TTABLE)
		return false;

	for (u32 i = 1; ; ++i)
	{
		::luabind::object row = rows[i];
		if (row.type() == LUA_TNIL)
			return !out.empty();
		if (row.type() != LUA_TTABLE || out.size() >= ShaderBus::max_rows * 4)
			return false;

		for (int c = 0; c < 4; ++c)
		{
			::luabind::object e = row[c + 1];
			if (e.type() == LUA_TNUMBER)
				out.push_back(::luabind::object_cast<double>(e));
			else if (e.type() == LUA_TNIL)
				out.push_back(0.0);
			else
				return false;
		}
	}
}

static bool bus_uint_value(double v, u32& out)
{
	if (!(v >= 0.0 && v <= 4294967295.0) || v != floor(v))
		return false;
	out = u32(v);
	return true;
}

static bool bus_set_array(u32 token, double first, const ::luabind::object& rows)
{
	u32 row;
	xr_vector<double> values;
	if (!bus_uint_value(first, row) || !bus_read_rows(rows, values))
		return ShaderBus::refuse_write(token, "a first row that is not a whole number from 0 or rows that are not an array of rows");

	xr_vector<Fvector4> converted(values.size() / 4);
	for (u32 i = 0; i < converted.size(); ++i)
		converted[i].set(float(values[i * 4]), float(values[i * 4 + 1]), float(values[i * 4 + 2]), float(values[i * 4 + 3]));
	return ShaderBus::set_rows(token, row, &converted.front(), u32(converted.size()));
}

static bool bus_set_uint(u32 token, double x, double y, double z, double w)
{
	u32 raw[4];
	if (!bus_uint_value(x, raw[0]) || !bus_uint_value(y, raw[1]) || !bus_uint_value(z, raw[2]) || !bus_uint_value(w, raw[3]))
		return ShaderBus::refuse_write(token, "a value that is not an unsigned 32 bit integer");
	return ShaderBus::set_uint(token, raw[0], raw[1], raw[2], raw[3]);
}

static bool bus_set_array_uint(u32 token, double first, const ::luabind::object& rows)
{
	u32 row;
	xr_vector<double> values;
	if (!bus_uint_value(first, row) || !bus_read_rows(rows, values))
		return ShaderBus::refuse_write(token, "a first row that is not a whole number from 0 or rows that are not an array of rows");

	xr_vector<u32> raw(values.size());
	for (u32 i = 0; i < raw.size(); ++i)
		if (!bus_uint_value(values[i], raw[i]))
			return ShaderBus::refuse_write(token, "a value that is not an unsigned 32 bit integer");
	return ShaderBus::set_rows_uint(token, row, &raw.front(), u32(raw.size() / 4));
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

static bool bus_get_uint(LPCSTR id, u32& x, u32& y, u32& z, u32& w)
{
	Fvector4 v;
	const bool found = ShaderBus::get(id, v);
	if (!found)
		v.set(0.f, 0.f, 0.f, 0.f);

	u32 raw[4];
	CopyMemory(raw, &v, sizeof(raw));
	x = raw[0];
	y = raw[1];
	z = raw[2];
	w = raw[3];
	return found;
}

// raw unsigned values on a uint lane
static bool bus_get_row(LPCSTR id, double i, double& x, double& y, double& z, double& w)
{
	u32 row;
	Fvector4 v;
	const bool found = bus_uint_value(i, row) && ShaderBus::get_row(id, row, v);
	if (!found)
		v.set(0.f, 0.f, 0.f, 0.f);

	const ShaderBus::lane* l = found ? ShaderBus::find(id) : nullptr;
	if (l && l->kind == ShaderBus::kind_uint)
	{
		u32 raw[4];
		CopyMemory(raw, &v, sizeof(raw));
		x = raw[0];
		y = raw[1];
		z = raw[2];
		w = raw[3];
	}
	else
	{
		x = v.x;
		y = v.y;
		z = v.z;
		w = v.w;
	}
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

// raw unsigned values on a uint lane
static bool bus_get_pending(LPCSTR id, double& x, double& y, double& z, double& w)
{
	Fvector4 v;
	const bool found = ShaderBus::get_pending(id, v);
	if (!found)
		v.set(0.f, 0.f, 0.f, 0.f);

	const ShaderBus::lane* l = found ? ShaderBus::find(id) : nullptr;
	if (l && l->kind == ShaderBus::kind_uint)
	{
		u32 raw[4];
		CopyMemory(raw, &v, sizeof(raw));
		x = raw[0];
		y = raw[1];
		z = raw[2];
		w = raw[3];
	}
	else
	{
		x = v.x;
		y = v.y;
		z = v.z;
		w = v.w;
	}
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
		row["forced"] = l->is_forced;
		row["rows"] = u32(l->rows_bound.size());
		row["declared_rows"] = l->rows_declared;
		row["kind"] = l->kind == ShaderBus::kind_uint ? "uint" : "float";

		// every kind a shader declared, more than one or one unlike kind shows a mismatch
		::luabind::object kinds = ::luabind::newtable(L);
		int kind_index = 1;
		if (l->declared_kinds & (1 << ShaderBus::kind_float))
			kinds[kind_index++] = "float";
		if (l->declared_kinds & (1 << ShaderBus::kind_uint))
			kinds[kind_index++] = "uint";
		row["declared_kinds"] = kinds;
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
		def("set_array", &bus_set_array),
		def("set_uint", &bus_set_uint),
		def("set_array_uint", &bus_set_array_uint),
		def("get_uint", &bus_get_uint,
		    pure_out_value<2>() + pure_out_value<3>() + pure_out_value<4>() + pure_out_value<5>()),
		def("get", &bus_get,
		    pure_out_value<2>() + pure_out_value<3>() + pure_out_value<4>() + pure_out_value<5>()),
		def("get_row", &bus_get_row,
		    pure_out_value<3>() + pure_out_value<4>() + pure_out_value<5>() + pure_out_value<6>()),
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
