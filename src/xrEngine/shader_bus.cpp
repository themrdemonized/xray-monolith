#include "stdafx.h"
#pragma hdrstop

#include "shader_bus.h"
#include "IRenderable.h"
#include "XR_IOConsole.h"
#include "xr_ioc_cmd.h"

struct bus_legacy_row
{
	shared_str command;
	string256 writer;
};

enum bus_cvar_type
{
	cvar_mask,
	cvar_toggle,
	cvar_integer,
	cvar_float,
	cvar_vector3,
	cvar_vector4,
	cvar_ivector4
};

// a cvar_ lane and the console command the per-frame update copies into it
struct bus_cvar
{
	ShaderBus::lane* lane;
	IConsole_Command* command;
	u8 type;
};

// a value for one object on an obj_ lane, waiting for the per-frame update
struct bus_object_write
{
	IRenderable* object;
	u16 lane;
	bool clear;
	Fvector4 value;
};

// one token on a shared lane and the lane value it set
struct bus_writer
{
	shared_str owner;
	Fvector4 value;
	u16 nonce;
	bool has_value;
};

struct ShaderBus::lane_shared
{
	xr_vector<bus_writer> writers;
	// a token set the lane value since the last per-frame update
	bool dirty;

	lane_shared() : dirty(false) {}
};

// one token's value for one object on a shared obj_ lane
struct bus_shared_value
{
	u16 lane;
	u16 writer;
	Fvector4 value;
};

// the most tokens one shared lane gives out
static const u32 bus_max_writers = 256;

static xr_vector<ShaderBus::lane*> g_bus_lanes;
static xr_vector<bus_object_write> g_bus_object_writes;
static xr_vector<ShaderBus::lane*> g_bus_shared_dirty;
static xr_map<IRenderable*, xr_vector<bus_shared_value>> g_bus_shared_values;
static xr_vector<bus_cvar> g_bus_cvars;
static ShaderBus::lane* g_bus_hotness = nullptr;
static bool g_bus_objects_used = false;
static u32 g_bus_texture_serial = 0;
static xr_vector<shared_str> g_bus_rejected;
static xr_vector<bus_legacy_row> g_bus_legacy;
static xr_vector<shared_str> g_bus_legacy_logged;
static xrCriticalSection g_bus_lock;

ENGINE_API const ShaderBus::lane* ShaderBus::hotness_lane = nullptr;

static bool bus_valid_id(LPCSTR id)
{
	if (!id || id[0] < 'a' || id[0] > 'z')
		return false;

	u32 len = xr_strlen(id);
	if (len > 32)
		return false;

	for (u32 i = 1; i < len; ++i)
	{
		const char c = id[i];
		if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')
			continue;
		return false;
	}
	return true;
}

static int bus_find(LPCSTR id)
{
	shared_str key(id);
	for (u32 i = 0; i < g_bus_lanes.size(); ++i)
		if (g_bus_lanes[i]->id.equal(key))
			return int(i);
	return -1;
}

static ShaderBus::lane* bus_find_or_add(LPCSTR id)
{
	const int found = bus_find(id);
	if (found >= 0)
		return g_bus_lanes[found];

	// a token stores the lane index in 16 bits
	if (g_bus_lanes.size() >= 0xFFFF)
	{
		static bool logged = false;
		if (!logged)
			Msg("! [SHADER-BUS] lane '%s' refused, the bus is full at %u lanes", id, u32(g_bus_lanes.size()));
		logged = true;
		return nullptr;
	}

	ShaderBus::lane* l = xr_new<ShaderBus::lane>();
	l->id = id;
	l->index = u16(g_bus_lanes.size());
	g_bus_lanes.push_back(l);
	// every mod writes obj_hotness, so it is shared from the start
	if (0 == xr_strcmp(id, "obj_hotness"))
	{
		l->shared = xr_new<ShaderBus::lane_shared>();
		g_bus_hotness = l;
	}
	return l;
}

static u16 bus_next_nonce()
{
	static u32 state = u32(GetTickCount()) | 1u;
	state ^= state << 13;
	state ^= state >> 17;
	state ^= state << 5;
	const u16 n = u16(state);
	return n ? n : u16(0xa5a5);
}

static u32 bus_token(const ShaderBus::lane* l)
{
	return (u32(l->nonce) << 16) | u32(l->index);
}

// the writer a nonce points to on a shared lane, -1 for none
static int bus_shared_writer(const ShaderBus::lane* l, u16 nonce)
{
	const xr_vector<bus_writer>& writers = l->shared->writers;
	for (u32 i = 0; i < writers.size(); ++i)
		if (writers[i].nonce == nonce)
			return int(i);
	return -1;
}

static void bus_max(Fvector4& to, const Fvector4& v)
{
	to.set(_max(to.x, v.x), _max(to.y, v.y), _max(to.z, v.z), _max(to.w, v.w));
}

// the per component largest value of the tokens that set one, false when none did
static bool bus_shared_max(const ShaderBus::lane* l, Fvector4& out)
{
	bool found = false;
	const xr_vector<bus_writer>& writers = l->shared->writers;
	for (u32 i = 0; i < writers.size(); ++i)
	{
		if (!writers[i].has_value)
			continue;
		if (found)
			bus_max(out, writers[i].value);
		else
			out.set(writers[i].value);
		found = true;
	}
	return found;
}

// a token per writer on a shared lane, the same token again for a repeat registration, called under the lock
static u32 bus_join(ShaderBus::lane* l, LPCSTR owner, LPCSTR description, LPCSTR source)
{
	xr_vector<bus_writer>& writers = l->shared->writers;
	for (u32 i = 0; i < writers.size(); ++i)
		if (0 == xr_strcmp(writers[i].owner.c_str(), owner))
			return (u32(writers[i].nonce) << 16) | u32(l->index);

	if (writers.size() >= bus_max_writers)
	{
		Msg("! [SHADER-BUS] shared lane '%s' refused '%s', it has %u tokens", l->id.c_str(), owner, bus_max_writers);
		return 0;
	}

	bus_writer w;
	w.owner = owner;
	w.value.set(0.f, 0.f, 0.f, 0.f);
	w.has_value = false;
	w.nonce = bus_next_nonce();
	while (bus_shared_writer(l, w.nonce) >= 0)
		w.nonce = bus_next_nonce();
	writers.push_back(w);

	// the first writer is the one owner_of and describe report
	if (!l->registered)
	{
		l->owner = owner;
		l->description = description ? description : "";
		l->source = source;
		l->nonce = w.nonce;
		l->registered = true;
	}

	Msg("[SHADER-BUS] shared lane %s joined by '%s' from '%s'", l->id.c_str(), owner, source);
	return (u32(w.nonce) << 16) | u32(l->index);
}

static void bus_refuse(LPCSTR hlsl_name, LPCSTR reason)
{
	shared_str key(hlsl_name);
	for (u32 i = 0; i < g_bus_rejected.size(); ++i)
		if (g_bus_rejected[i].equal(key))
			return;

	g_bus_rejected.push_back(key);
	Msg("! [SHADER-BUS] shader constant %s %s", hlsl_name, reason);
}

static void bus_bind_cvar(ShaderBus::lane* l, LPCSTR hlsl_name);

ShaderBus::lane* ShaderBus::declare(LPCSTR hlsl_name, u32 rows, u8 kind)
{
	if (!hlsl_name || 0 != strncmp(hlsl_name, "bus_", 4))
		return nullptr;

	LPCSTR id = hlsl_name + 4;
	xrCriticalSectionGuard guard(&g_bus_lock);

	if (!bus_valid_id(id))
	{
		bus_refuse(hlsl_name, "is not a valid lane name");
		return nullptr;
	}

	lane* l = bus_find_or_add(id);
	if (!l)
		return nullptr;
	if (!l->registered && 0 == strncmp(id, "cvar_", 5))
		bus_bind_cvar(l, hlsl_name);
	l->hlsl = hlsl_name;
	l->rows_declared = _max(l->rows_declared, rows);
	l->declared_kinds |= u8(1 << kind);
	if (!l->writes)
		l->kind = kind;
	return l;
}

void ShaderBus::refuse(LPCSTR hlsl_name, LPCSTR reason)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	bus_refuse(hlsl_name, reason);
}

static u32 bus_take(LPCSTR id, LPCSTR owner, LPCSTR description, LPCSTR source, bool warn, bool share)
{
	if (!bus_valid_id(id))
	{
		Msg("! [SHADER-BUS] rejected lane id '%s'", id ? id : "");
		return 0;
	}
	if (!owner || !owner[0])
	{
		Msg("! [SHADER-BUS] lane '%s' needs an owner", id);
		return 0;
	}
	if (xr_strlen(owner) > 64)
	{
		Msg("! [SHADER-BUS] lane '%s' owner is longer than 64 characters", id);
		return 0;
	}
	if (description && xr_strlen(description) > 256)
	{
		Msg("! [SHADER-BUS] lane '%s' description is longer than 256 characters", id);
		return 0;
	}

	string256 stored_source;
	stored_source[0] = 0;
	if (source)
		strncpy_s(stored_source, sizeof(stored_source), source, _TRUNCATE);

	xrCriticalSectionGuard guard(&g_bus_lock);
	ShaderBus::lane* l = bus_find_or_add(id);
	if (!l)
		return 0;

	// obj_hotness takes every caller in, register and try_register too
	if (l->shared && (share || l == g_bus_hotness))
		return bus_join(l, owner, description, stored_source);
	if (share)
	{
		if (!l->registered)
		{
			l->shared = xr_new<ShaderBus::lane_shared>();
			return bus_join(l, owner, description, stored_source);
		}
		Msg("~ [SHADER-BUS] lane '%s' belongs to '%s' alone, '%s' cannot share it", id, l->owner.c_str(), owner);
		return 0;
	}
	if (l->shared)
	{
		if (warn)
			Msg("~ [SHADER-BUS] lane '%s' is shared, '%s' did not take it, register_shared joins it", id, owner);
		return 0;
	}

	if (l->registered)
	{
		if (0 == xr_strcmp(l->owner.c_str(), owner))
			return bus_token(l);

		if (warn)
			Msg("~ [SHADER-BUS] lane '%s' stays with '%s', '%s' did not take it", id, l->owner.c_str(), owner);
		return 0;
	}

	l->owner = owner;
	l->description = description ? description : "";
	l->source = stored_source;
	l->nonce = bus_next_nonce();
	l->registered = true;

	Msg("[SHADER-BUS] lane %s registered by '%s' from '%s'", id, owner, l->source.c_str());
	return bus_token(l);
}

// the console value kinds a lane can take
static bool bus_cvar_type_of(IConsole_Command* command, u8& type)
{
	if (fast_dynamic_cast<CCC_Mask*>(command))
		type = cvar_mask;
	else if (fast_dynamic_cast<CCC_ToggleMask*>(command))
		type = cvar_toggle;
	else if (fast_dynamic_cast<CCC_Integer*>(command))
		type = cvar_integer;
	else if (fast_dynamic_cast<CCC_Float*>(command))
		type = cvar_float;
	else if (fast_dynamic_cast<CCC_Vector3*>(command))
		type = cvar_vector3;
	else if (fast_dynamic_cast<CCC_Vector4*>(command))
		type = cvar_vector4;
	else if (fast_dynamic_cast<CCC_IVector4*>(command))
		type = cvar_ivector4;
	else
		return false;
	return true;
}

// gives a cvar_ lane to the engine and the console command after the prefix, called under the lock
static void bus_bind_cvar(ShaderBus::lane* l, LPCSTR hlsl_name)
{
	LPCSTR name = l->id.c_str() + 5;
	IConsole_Command* command = Console ? Console->GetCommand(name) : nullptr;
	if (!command)
	{
		bus_refuse(hlsl_name, "has no console command and reads 0");
		return;
	}

	bus_cvar entry;
	if (!bus_cvar_type_of(command, entry.type))
	{
		bus_refuse(hlsl_name, "has a console command with no number value and reads 0");
		return;
	}

	string64 description;
	xr_sprintf(description, "console value %s", name);
	if (!bus_take(l->id.c_str(), "engine", description, "console", false, false))
		return;

	entry.lane = l;
	entry.command = command;
	g_bus_cvars.push_back(entry);
}

// copies the console value into the lane's pending value
static void bus_read_cvar(const bus_cvar& c)
{
	Fvector4& v = c.lane->pending;
	v.set(0.f, 0.f, 0.f, 0.f);
	switch (c.type)
	{
	case cvar_mask:
		v.x = static_cast<CCC_Mask*>(c.command)->GetValue() ? 1.f : 0.f;
		break;
	case cvar_toggle:
		v.x = static_cast<CCC_ToggleMask*>(c.command)->GetValue() ? 1.f : 0.f;
		break;
	case cvar_integer:
		v.x = float(static_cast<CCC_Integer*>(c.command)->GetValue());
		break;
	case cvar_float:
		v.x = static_cast<CCC_Float*>(c.command)->GetValue();
		break;
	case cvar_vector3:
		{
			const Fvector* f = static_cast<CCC_Vector3*>(c.command)->GetValuePtr();
			v.set(f->x, f->y, f->z, 0.f);
		}
		break;
	case cvar_vector4:
		v.set(*static_cast<CCC_Vector4*>(c.command)->GetValuePtr());
		break;
	case cvar_ivector4:
		{
			const Ivector4* i = static_cast<CCC_IVector4*>(c.command)->GetValuePtr();
			v.set(float(i->x), float(i->y), float(i->z), float(i->w));
		}
		break;
	}
}

static bool bus_reserved(LPCSTR id)
{
	if (!id || (0 != strncmp(id, "engine_", 7) && 0 != strncmp(id, "cvar_", 5)))
		return false;

	Msg("! [SHADER-BUS] lane id '%s' is reserved for the engine", id);
	return true;
}

u32 ShaderBus::try_register(LPCSTR id, LPCSTR owner, LPCSTR description, LPCSTR source)
{
	if (bus_reserved(id))
		return 0;
	return bus_take(id, owner, description, source, true, false);
}

u32 ShaderBus::register_shared(LPCSTR id, LPCSTR owner, LPCSTR description, LPCSTR source)
{
	if (bus_reserved(id))
		return 0;
	return bus_take(id, owner, description, source, true, true);
}

u32 ShaderBus::register_engine(LPCSTR id, LPCSTR description)
{
	return bus_take(id, "engine", description, "engine", false, false);
}

u32 ShaderBus::register_lane(LPCSTR id, LPCSTR owner, LPCSTR description, LPCSTR source)
{
	if (bus_reserved(id))
		return 0;

	const u32 token = bus_take(id, owner, description, source, false, false);
	if (token)
		return token;

	string256 held_by, held_from;
	string512 shared_by;
	held_by[0] = 0;
	held_from[0] = 0;
	shared_by[0] = 0;
	{
		xrCriticalSectionGuard guard(&g_bus_lock);
		const int found = bus_find(id);
		if (found >= 0 && g_bus_lanes[found]->registered)
		{
			const lane* l = g_bus_lanes[found];
			xr_strcpy(held_by, l->owner.c_str());
			xr_strcpy(held_from, l->source.c_str());
			if (l->shared)
				for (u32 i = 0; i < l->shared->writers.size(); ++i)
				{
					if (i)
						strncat_s(shared_by, sizeof(shared_by), ", ", _TRUNCATE);
					strncat_s(shared_by, sizeof(shared_by), l->shared->writers[i].owner.c_str(), _TRUNCATE);
				}
		}
	}

	if (shared_by[0])
		Debug.fatal(DEBUG_INFO, "shader bus lane '%s' is shared by %s, '%s' registered by '%s' cannot take it alone",
		            id, shared_by, owner ? owner : "", source ? source : "");
	if (held_by[0])
		Debug.fatal(DEBUG_INFO,
		            "shader bus lane '%s' already belongs to '%s' registered by '%s', '%s' registered by '%s' cannot take it",
		            id, held_by, held_from, owner ? owner : "", source ? source : "");
	return 0;
}

// the lane a token may write, called under the lock
static ShaderBus::lane* bus_writable(u32 token)
{
	const u16 nonce = u16(token >> 16);
	const u16 index = u16(token & 0xffff);
	if (!nonce || index >= g_bus_lanes.size())
		return nullptr;

	ShaderBus::lane* l = g_bus_lanes[index];
	if (!l->registered)
		return nullptr;
	if (l->shared)
		return bus_shared_writer(l, nonce) >= 0 ? l : nullptr;
	if (l->nonce != nonce)
		return nullptr;
	return l;
}

static bool bus_refuse_write(ShaderBus::lane* l, LPCSTR what)
{
	if (!l->warned)
		Msg("! [SHADER-BUS] lane '%s' refused %s", l->id.c_str(), what);
	l->warned = true;
	return false;
}

static bool bus_finite(const Fvector4& v)
{
	return _finite(v.x) && _finite(v.y) && _finite(v.z) && _finite(v.w);
}

bool ShaderBus::set(u32 token, float x, float y, float z, float w)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	lane* l = bus_writable(token);
	if (!l)
		return false;

	if (!_finite(x) || !_finite(y) || !_finite(z) || !_finite(w))
		return bus_refuse_write(l, "a value that is not finite");

	if (l->shared)
	{
		bus_writer& writer = l->shared->writers[bus_shared_writer(l, u16(token >> 16))];
		writer.value.set(x, y, z, w);
		writer.has_value = true;
		if (!l->shared->dirty)
		{
			l->shared->dirty = true;
			g_bus_shared_dirty.push_back(l);
		}
		l->kind = kind_float;
		++l->writes;
		return true;
	}

	l->pending.set(x, y, z, w);
	if (!l->rows_pending.empty())
		l->rows_pending[0] = l->pending;
	l->kind = kind_float;
	++l->writes;
	return true;
}

bool ShaderBus::set_uint(u32 token, u32 x, u32 y, u32 z, u32 w)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	lane* l = bus_writable(token);
	if (!l)
		return false;
	if (l->shared)
		return bus_refuse_write(l, "a uint value, a shared lane takes set and set_object only");

	const u32 raw[4] = { x, y, z, w };
	CopyMemory(&l->pending, raw, sizeof(raw));
	if (!l->rows_pending.empty())
		CopyMemory(&l->rows_pending[0], raw, sizeof(raw));
	l->kind = kind_uint;
	++l->writes;
	return true;
}

// copies rows bit for bit, called under the lock
static bool bus_write_rows(ShaderBus::lane* l, u32 first, const void* rows, u32 count, u8 kind)
{
	if (first >= ShaderBus::max_rows || count > ShaderBus::max_rows - first)
		return bus_refuse_write(l, "rows past the row limit");

	Fvector4 zero;
	zero.set(0.f, 0.f, 0.f, 0.f);
	if (l->rows_pending.empty())
		l->rows_pending.push_back(l->pending);
	if (l->rows_pending.size() < first + count)
		l->rows_pending.resize(first + count, zero);

	CopyMemory(&l->rows_pending[first], rows, count * sizeof(Fvector4));
	CopyMemory(&l->pending, &l->rows_pending[0], sizeof(Fvector4));
	l->rows_dirty = true;
	l->kind = kind;
	++l->writes;
	return true;
}

bool ShaderBus::set_rows(u32 token, u32 first, const Fvector4* rows, u32 count)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	lane* l = bus_writable(token);
	if (!l || !count)
		return false;
	if (l->shared)
		return bus_refuse_write(l, "rows, a shared lane takes set and set_object only");

	for (u32 i = 0; i < count; ++i)
		if (!bus_finite(rows[i]))
			return bus_refuse_write(l, "a value that is not finite");

	return bus_write_rows(l, first, rows, count, kind_float);
}

bool ShaderBus::set_rows_uint(u32 token, u32 first, const u32* rows, u32 count)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	lane* l = bus_writable(token);
	if (!l || !count)
		return false;
	if (l->shared)
		return bus_refuse_write(l, "rows, a shared lane takes set and set_object only");

	return bus_write_rows(l, first, rows, count, kind_uint);
}

// keeps or drops the token's own value for the object, the per-frame update resolves the queued entry, called under the lock
static void bus_shared_object(ShaderBus::lane* l, u16 nonce, IRenderable* object, bool clear, const Fvector4& value)
{
	const u16 writer = u16(bus_shared_writer(l, nonce));
	if (clear)
	{
		auto found = g_bus_shared_values.find(object);
		if (found == g_bus_shared_values.end())
			return;
		xr_vector<bus_shared_value>& values = found->second;
		for (u32 i = 0; i < values.size(); ++i)
			if (values[i].lane == l->index && values[i].writer == writer)
			{
				values.erase(values.begin() + i);
				break;
			}
		if (values.empty())
			g_bus_shared_values.erase(found);
		return;
	}

	xr_vector<bus_shared_value>& values = g_bus_shared_values[object];
	u32 i = 0;
	while (i < values.size() && (values[i].lane != l->index || values[i].writer != writer))
		++i;
	if (i == values.size())
	{
		bus_shared_value added;
		added.lane = l->index;
		added.writer = writer;
		values.push_back(added);
	}
	values[i].value.set(value);
}

// the largest value the tokens set on the entry's object, or a clear when none did, called under the lock
static void bus_resolve_object(bus_object_write& entry)
{
	entry.clear = true;
	const auto found = g_bus_shared_values.find(entry.object);
	if (found == g_bus_shared_values.end())
		return;

	const xr_vector<bus_shared_value>& values = found->second;
	for (u32 i = 0; i < values.size(); ++i)
	{
		if (values[i].lane != entry.lane)
			continue;
		if (entry.clear)
			entry.value.set(values[i].value);
		else
			bus_max(entry.value, values[i].value);
		entry.clear = false;
	}
}

// queues a value or a clear for one object, called under the lock
static bool bus_queue_object(u32 token, IRenderable* object, bool clear, const Fvector4& value)
{
	ShaderBus::lane* l = bus_writable(token);
	if (!l)
		return false;
	if (0 != strncmp(l->id.c_str(), "obj_", 4))
		return bus_refuse_write(l, "an object value, the lane id does not start obj_");
	if (!object)
		return bus_refuse_write(l, "an object that does not exist");
	if (!bus_finite(value))
		return bus_refuse_write(l, "a value that is not finite");
	if (l->shared)
		bus_shared_object(l, u16(token >> 16), object, clear, value);

	bus_object_write entry;
	entry.object = object;
	entry.lane = l->index;
	entry.clear = clear;
	entry.value.set(value);
	g_bus_object_writes.push_back(entry);
	g_bus_objects_used = true;
	++l->writes;
	return true;
}

bool ShaderBus::set_object(u32 token, IRenderable* object, float x, float y, float z, float w)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	Fvector4 value;
	value.set(x, y, z, w);
	return bus_queue_object(token, object, false, value);
}

bool ShaderBus::clear_object(u32 token, IRenderable* object)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	Fvector4 value;
	value.set(0.f, 0.f, 0.f, 0.f);
	return bus_queue_object(token, object, true, value);
}

// moves one queued write into the object's block, called under the lock
static void bus_store_object(const bus_object_write& entry)
{
	ShaderBus::object_values*& block = entry.object->renderable.bus_values;
	ShaderBus::lane* l = g_bus_lanes[entry.lane];

	const u32 n = block ? u32(block->values.size()) : 0;
	u32 i = 0;
	while (i < n && block->values[i].lane != entry.lane)
		++i;

	if (entry.clear)
	{
		if (i == n)
			return;
		block->values.erase(block->values.begin() + i);
		--l->objects;
		if (block->values.empty())
			xr_delete(block);
		return;
	}

	if (!block)
		block = xr_new<ShaderBus::object_values>();
	if (i == n)
	{
		ShaderBus::object_value added;
		added.lane = entry.lane;
		block->values.push_back(added);
		++l->objects;
	}
	block->values[i].value.set(entry.value);
}

void ShaderBus::object_forget(IRenderable* object)
{
	if (!g_bus_objects_used)
		return;

	xrCriticalSectionGuard guard(&g_bus_lock);
	g_bus_object_writes.erase(std::remove_if(g_bus_object_writes.begin(), g_bus_object_writes.end(),
		[object](const bus_object_write& entry) { return entry.object == object; }), g_bus_object_writes.end());
	g_bus_shared_values.erase(object);

	object_values*& block = object->renderable.bus_values;
	if (!block)
		return;
	for (u32 i = 0; i < block->values.size(); ++i)
		--g_bus_lanes[block->values[i].lane]->objects;
	xr_delete(block);
}

bool ShaderBus::set_texture(u32 token, LPCSTR name)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	lane* l = bus_writable(token);
	if (!l)
		return false;
	if (l->shared)
		return bus_refuse_write(l, "a texture, a shared lane takes set and set_object only");
	if (!name || xr_strlen(name) > 255)
		return bus_refuse_write(l, "a texture name longer than 255 characters");
	if (0 == _strnicmp(name, "$user$bus_", 10))
		return bus_refuse_write(l, "a texture lane as its texture");

	l->texture_pending = name;
	++l->writes;
	return true;
}

u32 ShaderBus::texture_serial()
{
	return g_bus_texture_serial;
}

bool ShaderBus::texture_get(u32 index, shared_str& id, shared_str& name)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	if (index >= g_bus_lanes.size())
		return false;

	id = g_bus_lanes[index]->id;
	name = g_bus_lanes[index]->texture_bound;
	return true;
}

bool ShaderBus::refuse_write(u32 token, LPCSTR what)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	lane* l = bus_writable(token);
	return l ? bus_refuse_write(l, what) : false;
}

bool ShaderBus::get(LPCSTR id, Fvector4& value)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	const int found = bus_find(id);
	if (found < 0)
		return false;

	value.set(g_bus_lanes[found]->bound);
	return true;
}

bool ShaderBus::get_row(LPCSTR id, u32 row, Fvector4& value)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	const int found = bus_find(id);
	if (found < 0)
		return false;

	const lane* l = g_bus_lanes[found];
	if (row >= _max(l->rows_declared, 1u))
		return false;

	if (row == 0)
		value.set(l->bound);
	else if (row < l->rows_bound.size())
		value.set(l->rows_bound[row]);
	else
		value.set(0.f, 0.f, 0.f, 0.f);
	return true;
}

bool ShaderBus::get_object(LPCSTR id, const IRenderable* object, Fvector4& value)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	const int found = bus_find(id);
	if (found < 0 || !object)
		return false;

	value.set(object_bound(g_bus_lanes[found], object->renderable.bus_values));
	return true;
}

bool ShaderBus::writers(LPCSTR id, const IRenderable* object, xr_vector<writer_value>& out)
{
	out.clear();
	xrCriticalSectionGuard guard(&g_bus_lock);
	const int found = bus_find(id);
	if (found < 0)
		return false;

	const lane* l = g_bus_lanes[found];
	if (!l->shared)
		return true;

	writer_value entry;
	const xr_vector<bus_writer>& tokens = l->shared->writers;
	if (!object)
	{
		for (u32 i = 0; i < tokens.size(); ++i)
			if (tokens[i].has_value)
			{
				entry.owner = tokens[i].owner;
				entry.value.set(tokens[i].value);
				out.push_back(entry);
			}
		return true;
	}

	const auto values = g_bus_shared_values.find(const_cast<IRenderable*>(object));
	if (values == g_bus_shared_values.end())
		return true;
	for (u32 i = 0; i < values->second.size(); ++i)
		if (values->second[i].lane == l->index)
		{
			entry.owner = tokens[values->second[i].writer].owner;
			entry.value.set(values->second[i].value);
			out.push_back(entry);
		}
	return true;
}

u32 ShaderBus::writer_count(const lane* l)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	if (l->shared)
		return u32(l->shared->writers.size());
	return l->registered ? 1 : 0;
}

const ShaderBus::lane* ShaderBus::find(LPCSTR id)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	const int found = bus_find(id);
	return (found >= 0) ? g_bus_lanes[found] : nullptr;
}

bool ShaderBus::declared(u32 token)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	const lane* l = bus_writable(token);
	return l && l->rows_declared > 0;
}

bool ShaderBus::has(LPCSTR id)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	return bus_find(id) >= 0;
}

LPCSTR ShaderBus::describe(LPCSTR id)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	const int found = bus_find(id);
	if (found < 0 || !g_bus_lanes[found]->registered)
		return nullptr;
	return g_bus_lanes[found]->description.c_str();
}

LPCSTR ShaderBus::owner_of(LPCSTR id)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	const int found = bus_find(id);
	if (found < 0 || !g_bus_lanes[found]->registered)
		return nullptr;
	return g_bus_lanes[found]->owner.c_str();
}

bool ShaderBus::stats(LPCSTR id, u32& changes, u32& last_change, u32& bound_frame, u32& writes)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	const int found = bus_find(id);
	if (found < 0)
		return false;

	const lane* l = g_bus_lanes[found];
	changes = l->changes;
	last_change = l->last_change_frame;
	bound_frame = l->bound_frame;
	writes = l->writes;
	return true;
}

bool ShaderBus::get_pending(LPCSTR id, Fvector4& value)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	const int found = bus_find(id);
	if (found < 0)
		return false;

	const lane* l = g_bus_lanes[found];
	if (!l->shared || !bus_shared_max(l, value))
		value.set(l->pending);
	return true;
}

bool ShaderBus::force(LPCSTR id, const Fvector4& value)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	const int found = bus_find(id);
	if (found < 0)
		return false;

	g_bus_lanes[found]->forced.set(value);
	g_bus_lanes[found]->is_forced = true;
	return true;
}

bool ShaderBus::release(LPCSTR id)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	const int found = bus_find(id);
	if (found < 0)
		return false;

	g_bus_lanes[found]->is_forced = false;
	return true;
}

u32 ShaderBus::count()
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	return u32(g_bus_lanes.size());
}

const ShaderBus::lane* ShaderBus::at(u32 index)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	return (index < g_bus_lanes.size()) ? g_bus_lanes[index] : nullptr;
}

LPCSTR ShaderBus::value_text(const lane* l, const Fvector4& value, string256& out)
{
	if (l && l->kind == kind_uint)
	{
		u32 raw[4];
		CopyMemory(raw, &value, sizeof(raw));
		xr_sprintf(out, "(%u, %u, %u, %u)", raw[0], raw[1], raw[2], raw[3]);
	}
	else
		xr_sprintf(out, "(%f, %f, %f, %f)", value.x, value.y, value.z, value.w);
	return out;
}

void ShaderBus::frame_latch()
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	for (u32 i = 0; i < g_bus_cvars.size(); ++i)
		bus_read_cvar(g_bus_cvars[i]);

	// a shared lane set since the last per-frame update takes the largest value of its tokens
	for (u32 i = 0; i < g_bus_shared_dirty.size(); ++i)
	{
		lane* l = g_bus_shared_dirty[i];
		bus_shared_max(l, l->pending);
		l->shared->dirty = false;
	}
	g_bus_shared_dirty.clear();

	bool textures_moved = false;
	for (u32 i = 0; i < g_bus_lanes.size(); ++i)
	{
		lane* l = g_bus_lanes[i];
		const Fvector4& src = l->is_forced ? l->forced : l->pending;

		// bitwise so a uint value that reads as a NaN float still compares equal
		bool moved = 0 != memcmp(&src, &l->bound, sizeof(Fvector4));

		CopyMemory(&l->bound, &src, sizeof(Fvector4));

		// row 0 always follows the bound value so array binds also see a forced value
		if (l->rows_dirty)
		{
			const u32 n = u32(l->rows_pending.size());
			if (n != l->rows_bound.size() ||
				(n > 1 && 0 != memcmp(&l->rows_pending[1], &l->rows_bound[1], (n - 1) * sizeof(Fvector4))))
				moved = true;

			l->rows_bound = l->rows_pending;
			l->rows_dirty = false;
		}
		if (!l->rows_bound.empty())
			CopyMemory(&l->rows_bound[0], &l->bound, sizeof(Fvector4));
		l->bound_forced = l->is_forced;

		if (!l->texture_pending.equal(l->texture_bound))
		{
			l->texture_bound = l->texture_pending;
			textures_moved = true;
			moved = true;
		}

		if (moved)
		{
			++l->changes;
			l->last_change_frame = Device.dwFrame;
		}
	}
	if (textures_moved)
		++g_bus_texture_serial;

	// draws read object values only from the blocks written here
	for (u32 i = 0; i < g_bus_object_writes.size(); ++i)
	{
		bus_object_write& entry = g_bus_object_writes[i];
		if (g_bus_lanes[entry.lane]->shared)
			bus_resolve_object(entry);
		bus_store_object(entry);
	}
	g_bus_object_writes.clear();
	hotness_lane = g_bus_hotness;
}

void ShaderBus::note_legacy_write(LPCSTR command, LPCSTR writer)
{
	if (!command || !command[0])
		return;
	if (!writer)
		writer = "";

	xrCriticalSectionGuard guard(&g_bus_lock);

	int found = -1;
	for (u32 i = 0; i < g_bus_legacy.size() && found < 0; ++i)
		if (0 == xr_strcmp(g_bus_legacy[i].command.c_str(), command))
			found = int(i);

	if (found >= 0 && 0 == xr_strcmp(g_bus_legacy[found].writer, writer))
		return;

	if (found < 0)
	{
		bus_legacy_row row;
		row.command = command;
		row.writer[0] = 0;
		g_bus_legacy.push_back(row);
		found = int(g_bus_legacy.size()) - 1;
	}
	strncpy_s(g_bus_legacy[found].writer, sizeof(g_bus_legacy[found].writer), writer, _TRUNCATE);

	if (!Device.b_is_Ready)
		return;

	string512 pair;
	xr_sprintf(pair, "%s %s", command, writer);

	shared_str pair_key(pair);
	for (u32 i = 0; i < g_bus_legacy_logged.size(); ++i)
		if (g_bus_legacy_logged[i].equal(pair_key))
			return;

	g_bus_legacy_logged.push_back(pair_key);
	Msg("~ [SHADER-BUS] %s written from %s, register a shader_bus lane instead", command, writer);
}

bool ShaderBus::legacy_writer(LPCSTR command, string256& out)
{
	out[0] = 0;
	if (!command || !command[0])
		return false;

	xrCriticalSectionGuard guard(&g_bus_lock);
	for (u32 i = 0; i < g_bus_legacy.size(); ++i)
		if (0 == xr_strcmp(g_bus_legacy[i].command.c_str(), command))
		{
			xr_strcpy(out, g_bus_legacy[i].writer);
			return true;
		}
	return false;
}

void ShaderBus::dump()
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	const bool verbose = 0 != strstr(Core.Params, "-dbg");

	if (verbose)
		Msg("[SHADER-BUS] %d lanes", u32(g_bus_lanes.size()));

	for (u32 i = 0; i < g_bus_lanes.size(); ++i)
	{
		const lane* l = g_bus_lanes[i];
		if (!l->registered)
			Msg("~ [SHADER-BUS] bus_%s is declared by a shader and registered by nobody", l->id.c_str());
		else if (verbose)
		{
			string512 rows;
			rows[0] = 0;
			if (!l->rows_bound.empty() || l->rows_declared > 1)
				xr_sprintf(rows, " rows %u/%u", u32(l->rows_bound.size()), l->rows_declared);
			if (0 == strncmp(l->id.c_str(), "obj_", 4))
			{
				string32 objects;
				xr_sprintf(objects, " objects %u", l->objects);
				xr_strcat(rows, objects);
			}
			if (l->texture_bound.size())
			{
				string512 texture;
				xr_sprintf(texture, " texture '%s'", l->texture_bound.c_str());
				xr_strcat(rows, texture);
			}

			string256 value;
			Msg("[SHADER-BUS] bus_%s owner '%s' from '%s' = %s%s%s%s %s",
			    l->id.c_str(), l->owner.c_str(), l->source.c_str(), value_text(l, l->bound, value),
			    l->is_forced ? " forced" : "", rows, l->kind == kind_uint ? " uint" : "", l->description.c_str());
		}

		if (l->is_forced && (!verbose || !l->registered))
		{
			string256 value;
			Msg("~ [SHADER-BUS] bus_%s is forced to %s until bus_release", l->id.c_str(), value_text(l, l->forced, value));
		}
	}
}

int ShaderBus::version()
{
	return 6;
}
