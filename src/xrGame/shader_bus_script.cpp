#include "pch_script.h"
#include "shader_bus_script.h"
#include "../xrEngine/shader_bus.h"
#include "../xrEngine/xr_ioconsole.h"
#include "../xrEngine/xr_ioc_cmd.h"
#include "script_game_object.h"
#include "GameObject.h"
#include "script_attachment_manager.h"
#include "script_particles.h"

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

static ::luabind::object bus_register_shared(lua_State* L, LPCSTR id, LPCSTR owner, LPCSTR description)
{
	string_path source;
	bus_caller_source(L, source);

	return bus_token_object(L, ShaderBus::register_shared(id, owner, description, source));
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

// the object behind a game_object, ScriptAttachment or particles_object argument, null for nil or any other value
static IRenderable* bus_target(const ::luabind::object& o)
{
	const std::optional<CScriptGameObject*> obj = ::luabind::object_cast_nothrow<CScriptGameObject*>(o);
	if (obj && *obj)
		return &(*obj)->object();

	const std::optional<script_attachment*> att = ::luabind::object_cast_nothrow<script_attachment*>(o);
	if (att && *att)
		return *att;

	const std::optional<CScriptParticles*> particles = ::luabind::object_cast_nothrow<CScriptParticles*>(o);
	return particles && *particles ? (*particles)->m_particles.get() : nullptr;
}

static bool bus_set_object(u32 token, const ::luabind::object& target, float x, float y, float z, float w)
{
	return ShaderBus::set_object(token, bus_target(target), x, y, z, w);
}

static bool bus_clear_object(u32 token, const ::luabind::object& target)
{
	return ShaderBus::clear_object(token, bus_target(target));
}

static bool bus_get_object(LPCSTR id, const ::luabind::object& target, float& x, float& y, float& z, float& w)
{
	Fvector4 v;
	const bool found = ShaderBus::get_object(id, bus_target(target), v);
	if (!found)
		v.set(0.f, 0.f, 0.f, 0.f);

	x = v.x;
	y = v.y;
	z = v.z;
	w = v.w;
	return found;
}

// rows {owner, x, y, z, w}, one per token of a shared lane that set a value
static ::luabind::object bus_writer_rows(lua_State* L, LPCSTR id, const IRenderable* object)
{
	::luabind::object rows = ::luabind::newtable(L);
	xr_vector<ShaderBus::writer_value> values;
	ShaderBus::writers(id, object, values);
	for (u32 i = 0; i < values.size(); ++i)
	{
		::luabind::object row = ::luabind::newtable(L);
		row["owner"] = values[i].owner.c_str();
		row["x"] = values[i].value.x;
		row["y"] = values[i].value.y;
		row["z"] = values[i].value.z;
		row["w"] = values[i].value.w;
		rows[i + 1] = row;
	}
	return rows;
}

static ::luabind::object bus_writers(lua_State* L, LPCSTR id)
{
	return bus_writer_rows(L, id, nullptr);
}

static ::luabind::object bus_writers_object(lua_State* L, LPCSTR id, const ::luabind::object& target)
{
	const IRenderable* object = bus_target(target);
	if (!object)
		return ::luabind::newtable(L);
	return bus_writer_rows(L, id, object);
}

static bool bus_set_texture(u32 token, LPCSTR name)
{
	return ShaderBus::set_texture(token, name);
}

static LPCSTR bus_get_texture(LPCSTR id)
{
	const ShaderBus::lane* l = ShaderBus::find(id);
	return l && l->texture_bound.size() ? l->texture_bound.c_str() : nullptr;
}

static bool bus_set_readback(u32 token, LPCSTR source, float x, float y, double w, double h, LPCSTR reduce)
{
	u32 width, height;
	if (!bus_uint_value(w, width) || !bus_uint_value(h, height))
		return ShaderBus::refuse_write(token, "a readback block size that is not a whole number");
	return ShaderBus::set_readback(token, source, x, y, width, height, reduce);
}

static bool bus_readback_texel(LPCSTR id, u32 i, float& x, float& y, float& z, float& w, u32& frame)
{
	Fvector4 v;
	frame = 0;
	const bool found = ShaderBus::get_readback(id, i, v, frame);
	if (!found)
		v.set(0.f, 0.f, 0.f, 0.f);

	x = v.x;
	y = v.y;
	z = v.z;
	w = v.w;
	return found;
}

static bool bus_get_readback(LPCSTR id, float& x, float& y, float& z, float& w, u32& frame)
{
	return bus_readback_texel(id, 0, x, y, z, w, frame);
}

static bool bus_get_readback_row(LPCSTR id, double i, float& x, float& y, float& z, float& w, u32& frame)
{
	u32 texel;
	return bus_readback_texel(id, bus_uint_value(i, texel) ? texel : u32(-1), x, y, z, w, frame);
}

static bool bus_set_buffer(u32 token, double rows)
{
	u32 count;
	if (!bus_uint_value(rows, count))
		return ShaderBus::refuse_write(token, "a buffer size that is not a whole number");
	return ShaderBus::set_buffer(token, count);
}

static bool bus_set_late(u32 token, bool on)
{
	return ShaderBus::set_late(token, on);
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

// a whole number from 1 to most
static bool bus_read_side(const ::luabind::object& value, u32 most, u32& out)
{
	return value.type() == LUA_TNUMBER && bus_uint_value(::luabind::object_cast<double>(value), out) && out >= 1 && out <= most;
}

// the index of a string field among names, count for a missing field, -1 for anything else
static int bus_read_name(const ::luabind::object& value, LPCSTR const* names, int count)
{
	if (value.type() == LUA_TNIL)
		return count;
	if (value.type() != LUA_TSTRING)
		return -1;

	LPCSTR name = ::luabind::object_cast<LPCSTR>(value);
	for (int i = 0; i < count; ++i)
		if (0 == xr_strcmp(name, names[i]))
			return i;
	return -1;
}

// the target spec of a Lua table, the name of the first bad field or null
static LPCSTR bus_read_target(const ::luabind::object& spec, ShaderBus::target_spec& t)
{
	static LPCSTR sizes[] = { "screen", "half", "quarter" };
	t.size = ShaderBus::size_screen;
	t.width = 0;
	t.height = 0;
	t.format = 0;
	t.history = false;
	t.clear.set(0.f, 0.f, 0.f, 0.f);
	if (spec.type() != LUA_TTABLE)
		return "spec";

	::luabind::object size = spec["size"];
	if (size.type() == LUA_TTABLE)
	{
		u32 w, h;
		if (!bus_read_side(size[1], 4096, w) || !bus_read_side(size[2], 4096, h))
			return "size";
		t.size = ShaderBus::size_fixed;
		t.width = u16(w);
		t.height = u16(h);
	}
	else
	{
		const int i = bus_read_name(size, sizes, 3);
		if (i < 0)
			return "size";
		t.size = u8(i < 3 ? i : ShaderBus::size_screen);
	}

	LPCSTR formats[ShaderBus::target_format_count];
	for (u32 i = 0; i < ShaderBus::target_format_count; ++i)
		formats[i] = ShaderBus::target_format_name(u8(i));
	const int format = bus_read_name(spec["format"], formats, ShaderBus::target_format_count);
	if (format < 0)
		return "format";
	t.format = u8(format < int(ShaderBus::target_format_count) ? format : 0);

	::luabind::object history = spec["history"];
	if (history.type() == LUA_TBOOLEAN)
		t.history = ::luabind::object_cast<bool>(history);
	else if (history.type() != LUA_TNIL)
		return "history";

	::luabind::object clear = spec["clear"];
	if (clear.type() == LUA_TNUMBER)
	{
		const float c = ::luabind::object_cast<float>(clear);
		t.clear.set(c, c, c, c);
	}
	else if (clear.type() == LUA_TTABLE)
	{
		float c[4] = { 0.f, 0.f, 0.f, 0.f };
		for (int i = 0; i < 4; ++i)
		{
			::luabind::object e = clear[i + 1];
			if (e.type() == LUA_TNUMBER)
				c[i] = ::luabind::object_cast<float>(e);
			else if (e.type() != LUA_TNIL)
				return "clear";
		}
		::luabind::object fifth = clear[5];
		if (fifth.type() != LUA_TNIL)
			return "clear";
		t.clear.set(c[0], c[1], c[2], c[3]);
	}
	else if (clear.type() != LUA_TNIL)
		return "clear";
	return nullptr;
}

static LPCSTR bus_stages[] = { "frame", "gbuffer", "lighting", "scene", "post" };
static LPCSTR bus_gpu_states[] = { "pending", "ready", "failed", "released", "off" };

// the pass spec of a Lua table, the name of the first bad field or null
static LPCSTR bus_read_pass(const ::luabind::object& spec, ShaderBus::pass_spec& p)
{
	static LPCSTR blends[] = { "none", "add", "alpha" };
	p.order = 0;
	p.output_count = 0;
	p.group_x = 0;
	p.group_y = 0;
	p.svp = false;
	p.enabled = true;
	p.released = false;
	p.enables = 0;
	if (spec.type() != LUA_TTABLE)
		return "spec";

	const int stage = bus_read_name(spec["stage"], bus_stages, ShaderBus::stage_count);
	if (stage < 0 || stage == ShaderBus::stage_count)
		return "stage";
	p.stage = u8(stage);

	::luabind::object order = spec["order"];
	if (order.type() == LUA_TNUMBER)
	{
		const double v = ::luabind::object_cast<double>(order);
		if (!(v >= -2147483648.0 && v <= 2147483647.0) || v != floor(v))
			return "order";
		p.order = int(v);
	}
	else if (order.type() != LUA_TNIL)
		return "order";

	::luabind::object ps = spec["ps"];
	::luabind::object cs = spec["cs"];
	if (ps.type() == LUA_TSTRING)
		p.ps = ::luabind::object_cast<LPCSTR>(ps);
	else if (ps.type() != LUA_TNIL)
		return "ps";
	if (cs.type() == LUA_TSTRING)
		p.cs = ::luabind::object_cast<LPCSTR>(cs);
	else if (cs.type() != LUA_TNIL)
		return "cs";

	::luabind::object outputs = spec["outputs"];
	if (outputs.type() != LUA_TTABLE)
		return "outputs";
	for (u32 i = 1; ; ++i)
	{
		::luabind::object e = outputs[i];
		if (e.type() == LUA_TNIL)
			break;
		if (e.type() != LUA_TSTRING || p.output_count >= ShaderBus::max_pass_outputs)
			return "outputs";
		p.outputs[p.output_count++] = ::luabind::object_cast<LPCSTR>(e);
	}

	::luabind::object inputs = spec["inputs"];
	if (inputs.type() == LUA_TTABLE)
		for (::luabind::object::iterator it = inputs.begin(); it != inputs.end(); ++it)
		{
			::luabind::object texture = *it;
			if (it.key().type() != LUA_TSTRING || texture.type() != LUA_TSTRING)
				return "inputs";
			ShaderBus::pass_input input;
			input.hlsl = ::luabind::object_cast<LPCSTR>(it.key());
			input.texture = ::luabind::object_cast<LPCSTR>(texture);
			p.inputs.push_back(input);
		}
	else if (inputs.type() != LUA_TNIL)
		return "inputs";

	const int blend = bus_read_name(spec["blend"], blends, 3);
	if (blend < 0)
		return "blend";
	p.blend = u8(blend < 3 ? blend : ShaderBus::blend_none);

	if (p.cs.size())
	{
		::luabind::object group = spec["group"];
		u32 x, y;
		if (group.type() != LUA_TTABLE || !bus_read_side(group[1], 1024, x) || !bus_read_side(group[2], 1024, y))
			return "group";
		p.group_x = u16(x);
		p.group_y = u16(y);
	}

	::luabind::object svp = spec["svp"];
	if (svp.type() == LUA_TBOOLEAN)
		p.svp = ::luabind::object_cast<bool>(svp);
	else if (svp.type() != LUA_TNIL)
		return "svp";
	return nullptr;
}

static ::luabind::object bus_register_target(lua_State* L, LPCSTR id, LPCSTR owner, LPCSTR description, const ::luabind::object& spec)
{
	ShaderBus::target_spec t;
	if (LPCSTR field = bus_read_target(spec, t))
	{
		Msg("! [SHADER-BUS] target '%s' refused, its spec field '%s' is not valid", id ? id : "", field);
		return bus_token_object(L, 0);
	}

	string_path source;
	bus_caller_source(L, source);
	return bus_token_object(L, ShaderBus::register_target(id, owner, description, source, t));
}

static ::luabind::object bus_register_pass(lua_State* L, LPCSTR id, LPCSTR owner, LPCSTR description, const ::luabind::object& spec)
{
	ShaderBus::pass_spec p;
	if (LPCSTR field = bus_read_pass(spec, p))
	{
		Msg("! [SHADER-BUS] pass '%s' refused, its spec field '%s' is not valid", id ? id : "", field);
		return bus_token_object(L, 0);
	}

	string_path source;
	bus_caller_source(L, source);
	return bus_token_object(L, ShaderBus::register_pass(id, owner, description, source, p));
}

static bool bus_enable_pass(u32 token, bool on)
{
	return ShaderBus::enable_pass(token, on);
}

static bool bus_release_pass(u32 token)
{
	return ShaderBus::release_pass(token);
}

static bool bus_release_target(u32 token)
{
	return ShaderBus::release_target(token);
}

static ::luabind::object bus_register_mask(lua_State* L, LPCSTR id, LPCSTR owner, LPCSTR description)
{
	string_path source;
	bus_caller_source(L, source);
	return bus_token_object(L, ShaderBus::register_mask(id, owner, description, source));
}

static bool bus_set_mask(u32 token, const ::luabind::object& target, bool on)
{
	const std::optional<CScriptParticles*> particles = ::luabind::object_cast_nothrow<CScriptParticles*>(target);
	if (particles && *particles)
		return false;

	IRenderable* object = bus_target(target);
	return object && ShaderBus::set_mask(token, object, on);
}

static u32 bus_get_mask(const ::luabind::object& target)
{
	const IRenderable* object = bus_target(target);
	return object ? ShaderBus::get_mask(object) : 0;
}

static bool bus_release_mask(u32 token)
{
	return ShaderBus::release_mask(token);
}

// the list() keys of a target or history row, a history row shows its target's spec
static void bus_list_target(::luabind::object& row, const ShaderBus::lane* l)
{
	shared_str id = l->id;
	if (l->role == ShaderBus::role_history)
	{
		string64 base;
		xr_strcpy(base, l->id.c_str());
		base[xr_strlen(base) - 5] = 0;
		id = base;
	}

	ShaderBus::target_spec t;
	ShaderBus::gpu_status s;
	if (!ShaderBus::target_get(id, t))
		return;
	const bool found = ShaderBus::get_gpu_status(id, s);
	const bool released = t.released || (l->role == ShaderBus::role_history && !t.history);
	const bool live = found && !released;
	row["width"] = live ? s.width : 0u;
	row["height"] = live ? s.height : 0u;
	row["format"] = ShaderBus::target_format_name(t.format);
	row["bytes"] = live ? s.bytes : 0u;
	row["history"] = t.history;
	row["target_state"] = released ? "released" : found ? bus_gpu_states[s.state] : "pending";
}

static void bus_list_pass(lua_State* L, ::luabind::object& row, const ShaderBus::lane* l)
{
	ShaderBus::pass_spec p;
	ShaderBus::gpu_status s;
	if (!ShaderBus::pass_get(l->id, p))
		return;
	const bool found = ShaderBus::get_gpu_status(l->id, s);
	row["stage"] = bus_stages[p.stage];
	row["order"] = p.order;
	row["pass_state"] = p.released || !p.enabled ? "off" : found ? bus_gpu_states[s.state] : "pending";
	row["reason"] = found && s.reason.size() ? s.reason.c_str() : "";

	// the shader, outputs, inputs and compute group as the spec gave them
	if (p.cs.size())
		row["cs"] = p.cs.c_str();
	else
		row["ps"] = p.ps.c_str();
	::luabind::object outputs = ::luabind::newtable(L);
	for (u32 i = 0; i < p.output_count; ++i)
		outputs[i + 1] = p.outputs[i].c_str();
	row["outputs"] = outputs;
	::luabind::object inputs = ::luabind::newtable(L);
	for (u32 i = 0; i < p.inputs.size(); ++i)
		inputs[p.inputs[i].hlsl.c_str()] = p.inputs[i].texture.c_str();
	row["inputs"] = inputs;
	if (p.cs.size())
	{
		::luabind::object group = ::luabind::newtable(L);
		group[1] = u32(p.group_x);
		group[2] = u32(p.group_y);
		row["group"] = group;
	}
}

static bool bus_get_pass_time(LPCSTR id, float& x, float& y, float& z, float& w, u32& frame)
{
	Fvector4 t;
	t.set(0.f, 0.f, 0.f, 0.f);
	frame = 0;
	const bool found = ShaderBus::get_pass_time(id, t, frame);
	x = t.x;
	y = t.y;
	z = t.z;
	w = t.w;
	return found;
}

static int bus_version()
{
	return ShaderBus::version();
}

static ::luabind::object bus_list(lua_State* L, bool include_declared)
{
	static LPCSTR readback_states[] = { "none", "ok", "refused", "lost" };
	static LPCSTR roles[] = { "", "target", "history", "pass", "mask" };
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
		row["objects"] = l->objects;
		row["shared"] = l->shared != nullptr;
		row["writers"] = ShaderBus::writer_count(l);
		row["texture"] = l->texture_bound.size() ? l->texture_bound.c_str() : "";
		row["readback"] = l->readback ? readback_states[l->readback->state] : "none";
		row["buffer_rows"] = l->buffer_bound;
		row["late"] = l->late;
		row["role"] = roles[l->role];
		if (l->role == ShaderBus::role_target || l->role == ShaderBus::role_history)
			bus_list_target(row, l);
		else if (l->role == ShaderBus::role_pass)
			bus_list_pass(L, row, l);
		else if (l->role == ShaderBus::role_mask)
		{
			const int bit = ShaderBus::mask_bit((u32(l->nonce) << 16) | u32(l->index));
			if (bit >= 0)
				row["mask_bit"] = bit;
		}

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
		def("register_shared", &bus_register_shared, raw<1>()),
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
		def("set_object", &bus_set_object),
		def("clear_object", &bus_clear_object),
		def("get_object", &bus_get_object,
		    pure_out_value<3>() + pure_out_value<4>() + pure_out_value<5>() + pure_out_value<6>()),
		def("writers", &bus_writers, raw<1>()),
		def("writers", &bus_writers_object, raw<1>()),
		def("set_texture", &bus_set_texture),
		def("get_texture", &bus_get_texture),
		def("set_readback", &bus_set_readback),
		def("get_readback", &bus_get_readback,
		    pure_out_value<2>() + pure_out_value<3>() + pure_out_value<4>() + pure_out_value<5>() + pure_out_value<6>()),
		def("get_readback", &bus_get_readback_row,
		    pure_out_value<3>() + pure_out_value<4>() + pure_out_value<5>() + pure_out_value<6>() + pure_out_value<7>()),
		def("set_buffer", &bus_set_buffer),
		def("set_late", &bus_set_late),
		def("has", &bus_has),
		def("describe", &bus_describe),
		def("owner_of", &bus_owner_of),
		def("stats", &bus_stats,
		    pure_out_value<2>() + pure_out_value<3>() + pure_out_value<4>() + pure_out_value<5>()),
		def("get_pending", &bus_get_pending,
		    pure_out_value<2>() + pure_out_value<3>() + pure_out_value<4>() + pure_out_value<5>()),
		def("list", &bus_list_registered, raw<1>()),
		def("list", &bus_list, raw<1>()),
		def("register_target", &bus_register_target, raw<1>()),
		def("register_pass", &bus_register_pass, raw<1>()),
		def("enable_pass", &bus_enable_pass),
		def("release_pass", &bus_release_pass),
		def("release_target", &bus_release_target),
		def("get_pass_time", &bus_get_pass_time,
		    pure_out_value<2>() + pure_out_value<3>() + pure_out_value<4>() + pure_out_value<5>() + pure_out_value<6>()),
		def("register_mask", &bus_register_mask, raw<1>()),
		def("set_mask", &bus_set_mask),
		def("get_mask", &bus_get_mask),
		def("release_mask", &bus_release_mask),
		def("version", &bus_version)
	];
}
