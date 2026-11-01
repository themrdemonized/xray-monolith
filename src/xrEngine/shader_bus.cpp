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
static bool g_bus_gpu_lanes = false;
static bool g_bus_buffer_lanes = false;
static bool g_bus_mask = false;
static bool g_bus_mask_msaa = false;
// the token holding each object mask bit, 0 for a free bit, and the live bits as of the last per-frame update
static u32 g_bus_mask_tokens[24] = {};
static u32 g_bus_mask_live = 0;
// bits release_mask parked and the level count each was released in
static u32 g_bus_mask_parked = 0;
static u32 g_bus_mask_park_epochs[24] = {};
// finished level loads, and the count as of the last per-frame update
static u32 g_bus_epoch_live = 0;
static u32 g_bus_epoch = 0;
static xr_vector<ShaderBus::lane*> g_bus_readbacks;
static xr_vector<ShaderBus::lane*> g_bus_buffers;
// bytes set_buffer has handed out over every buffer lane
static u32 g_bus_buffer_bytes = 0;
static xr_vector<ShaderBus::lane*> g_bus_late;
static xr_vector<ShaderBus::target_spec> g_bus_targets;
static xr_vector<ShaderBus::pass_spec> g_bus_passes;
static xr_vector<std::pair<shared_str, ShaderBus::gpu_status>> g_bus_status;
// the reasons of the last 64 refused ids, oldest first
static xr_vector<std::pair<shared_str, LPCSTR>> g_bus_refusals;
// the pass_time of each pass's last run and the frame it ran in
struct bus_pass_clock
{
	shared_str id;
	Fvector4 time;
	u32 frame;
};
static xr_vector<bus_pass_clock> g_bus_pass_clocks;
static u32 g_bus_targets_serial = 0;
static bool g_bus_targets_enabled = false;

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
		ShaderBus::note_refusal(id, "the bus is full");
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
		ShaderBus::note_refusal(l->id.c_str(), "all tokens of the shared lane are taken");
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

static bool bus_take_args(LPCSTR id, LPCSTR owner, LPCSTR description)
{
	if (!bus_valid_id(id))
	{
		ShaderBus::note_refusal(id, "a lane id the bus rejects");
		Msg("! [SHADER-BUS] rejected lane id '%s'", id ? id : "");
		return false;
	}
	if (!owner || !owner[0])
	{
		ShaderBus::note_refusal(id, "no owner");
		Msg("! [SHADER-BUS] lane '%s' needs an owner", id);
		return false;
	}
	if (xr_strlen(owner) > 64)
	{
		ShaderBus::note_refusal(id, "an owner longer than 64 characters");
		Msg("! [SHADER-BUS] lane '%s' owner is longer than 64 characters", id);
		return false;
	}
	if (description && xr_strlen(description) > 256)
	{
		ShaderBus::note_refusal(id, "a description longer than 256 characters");
		Msg("! [SHADER-BUS] lane '%s' description is longer than 256 characters", id);
		return false;
	}
	return true;
}

// takes or joins the lane for a caller bus_take_args passed, called under the lock
static u32 bus_take_locked(LPCSTR id, LPCSTR owner, LPCSTR description, LPCSTR source, bool warn, bool share)
{
	string256 stored_source;
	stored_source[0] = 0;
	if (source)
		strncpy_s(stored_source, sizeof(stored_source), source, _TRUNCATE);

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
		ShaderBus::note_refusal(id, "it belongs to another owner alone");
		Msg("~ [SHADER-BUS] lane '%s' belongs to '%s' alone, '%s' cannot share it", id, l->owner.c_str(), owner);
		return 0;
	}
	if (l->shared)
	{
		ShaderBus::note_refusal(id, "it is shared, register_shared joins it");
		if (warn)
			Msg("~ [SHADER-BUS] lane '%s' is shared, '%s' did not take it, register_shared joins it", id, owner);
		return 0;
	}

	if (l->registered)
	{
		if (0 == xr_strcmp(l->owner.c_str(), owner))
			return bus_token(l);

		ShaderBus::note_refusal(id, "it belongs to another owner");
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

static u32 bus_take(LPCSTR id, LPCSTR owner, LPCSTR description, LPCSTR source, bool warn, bool share)
{
	if (!bus_take_args(id, owner, description))
		return 0;

	xrCriticalSectionGuard guard(&g_bus_lock);
	return bus_take_locked(id, owner, description, source, warn, share);
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

	ShaderBus::note_refusal(id, "an id reserved for the engine");
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
	++l->refusals;
	l->refusal = what;
	ShaderBus::note_refusal(l->id.c_str(), what);
	if (!l->warned)
		Msg("! [SHADER-BUS] lane '%s' refused %s", l->id.c_str(), what);
	l->warned = true;
	return false;
}

// a live compute pass of the lane's owner writes it, called under the lock
static bool bus_gpu_written(const ShaderBus::lane* l)
{
	for (u32 i = 0; l->buffer_rows && i < g_bus_passes.size(); ++i)
	{
		const ShaderBus::pass_spec& p = g_bus_passes[i];
		if (!p.released && p.cs.size() && p.output_count == 1 && p.outputs[0].equal(l->id) && p.owner.equal(l->owner))
			return true;
	}
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
	if (l->role == role_mask)
		return bus_refuse_write(l, "a value, it is an object mask");
	if (bus_gpu_written(l))
		return bus_refuse_write(l, "a value, a compute pass writes the lane");

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
	if (l->role == role_mask)
		return bus_refuse_write(l, "a value, it is an object mask");
	if (bus_gpu_written(l))
		return bus_refuse_write(l, "a value, a compute pass writes the lane");
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
	const u32 limit = _max(ShaderBus::max_rows, l->buffer_rows);
	if (first >= limit || count > limit - first)
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
	if (l->role == role_mask)
		return bus_refuse_write(l, "rows, it is an object mask");
	if (bus_gpu_written(l))
		return bus_refuse_write(l, "rows, a compute pass writes the lane");
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
	if (l->role == role_mask)
		return bus_refuse_write(l, "rows, it is an object mask");
	if (bus_gpu_written(l))
		return bus_refuse_write(l, "rows, a compute pass writes the lane");
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
	if (l->role == role_target || l->role == role_history)
		return bus_refuse_write(l, "a texture, it is a render target");
	if (l->role == role_mask)
		return bus_refuse_write(l, "a texture, it is an object mask");
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

bool ShaderBus::set_readback(u32 token, LPCSTR source, float x, float y, u32 w, u32 h, LPCSTR reduce)
{
	static LPCSTR sources[] = { "", "light", "scene", "backbuffer" };
	static LPCSTR reduces[] = { "none", "mean", "min", "max" };

	xrCriticalSectionGuard guard(&g_bus_lock);
	lane* l = bus_writable(token);
	if (!l)
		return false;
	if (l->role == role_mask)
		return bus_refuse_write(l, "a readback, it is an object mask");
	if (l->shared)
		return bus_refuse_write(l, "a readback, a shared lane takes set and set_object only");
	if (!g_bus_gpu_lanes)
		return bus_refuse_write(l, "a readback, this renderer has none");

	u32 src = 0;
	while (src < 4 && (!source || xr_strcmp(source, sources[src])))
		++src;
	if (src == 4)
		return bus_refuse_write(l, "a readback source other than light, scene or backbuffer");

	// disarming needs no block and leaves a lane with no readback as it is
	if (src == readback_off)
	{
		if (l->readback)
		{
			l->readback->source_pending = readback_off;
			++l->writes;
		}
		return true;
	}

	if (!(x >= 0.f && x <= 1.f) || !(y >= 0.f && y <= 1.f))
		return bus_refuse_write(l, "a readback centre outside 0 to 1");
	if (w < 1 || w > max_readback_block || h < 1 || h > max_readback_block)
		return bus_refuse_write(l, "a readback block outside 1 to 32 texels a side");

	u32 op = 0;
	while (op < 4 && (!reduce || xr_strcmp(reduce, reduces[op])))
		++op;
	if (op == 4)
		return bus_refuse_write(l, "a readback reduce other than none, mean, min or max");

	u32 armed = 0;
	for (u32 i = 0; i < g_bus_lanes.size(); ++i)
		if (g_bus_lanes[i] != l && g_bus_lanes[i]->readback && g_bus_lanes[i]->readback->source_pending != readback_off)
			++armed;
	if (armed >= max_readbacks)
		return bus_refuse_write(l, "a readback past the 16 armed lanes");

	if (!l->readback)
		l->readback = xr_new<lane_readback>();
	lane_readback* r = l->readback;
	r->source_pending = u8(src);
	r->reduce_pending = u8(op);
	r->w_pending = u8(w);
	r->h_pending = u8(h);
	r->x_pending = x;
	r->y_pending = y;
	++l->writes;
	return true;
}

bool ShaderBus::set_buffer(u32 token, u32 rows)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	lane* l = bus_writable(token);
	if (!l)
		return false;
	if (l->role == role_target || l->role == role_history)
		return bus_refuse_write(l, "a buffer, it is a render target");
	if (l->role == role_mask)
		return bus_refuse_write(l, "a buffer, it is an object mask");
	if (l->shared)
		return bus_refuse_write(l, "a buffer, a shared lane takes set and set_object only");
	if (!g_bus_buffer_lanes)
		return bus_refuse_write(l, "a buffer, this renderer has none");
	if (rows < 1 || rows > max_buffer_rows)
		return bus_refuse_write(l, "a buffer outside 1 to 65536 rows");
	if (rows <= l->buffer_rows)
		return true;
	if (g_bus_buffer_bytes - l->buffer_rows * 16 + rows * 16 > max_buffer_bytes)
		return bus_refuse_write(l, "a buffer past the 16 MiB all buffer lanes share");

	Fvector4 zero;
	zero.set(0.f, 0.f, 0.f, 0.f);
	if (l->rows_pending.empty())
		l->rows_pending.push_back(l->pending);
	if (l->rows_pending.size() < rows)
		l->rows_pending.resize(rows, zero);
	l->rows_dirty = true;
	g_bus_buffer_bytes += (rows - l->buffer_rows) * 16;
	l->buffer_rows = rows;
	++l->writes;
	return true;
}

bool ShaderBus::set_late(u32 token, bool on)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	lane* l = bus_writable(token);
	if (!l)
		return false;
	if (l->shared)
		return bus_refuse_write(l, "a late latch, a shared lane takes set and set_object only");

	l->late = on;
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

void ShaderBus::note_refusal(LPCSTR id, LPCSTR reason)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	const shared_str key(id ? id : "");
	for (u32 i = 0; i < g_bus_refusals.size(); ++i)
		if (g_bus_refusals[i].first.equal(key))
		{
			g_bus_refusals.erase(g_bus_refusals.begin() + i);
			break;
		}
	if (g_bus_refusals.size() >= 64)
		g_bus_refusals.erase(g_bus_refusals.begin());
	g_bus_refusals.push_back(std::make_pair(key, reason));
}

LPCSTR ShaderBus::last_refusal(LPCSTR id)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	const shared_str key(id ? id : "");
	for (u32 i = 0; i < g_bus_refusals.size(); ++i)
		if (g_bus_refusals[i].first.equal(key))
			return g_bus_refusals[i].second;
	return nullptr;
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
	if (row >= _max(_max(l->rows_declared, l->buffer_rows), 1u))
		return false;

	if (row == 0)
		value.set(l->bound);
	else if (row < l->rows_bound.size())
		value.set(l->rows_bound[row]);
	else
		value.set(0.f, 0.f, 0.f, 0.f);
	return true;
}

bool ShaderBus::get_readback(LPCSTR id, u32 i, Fvector4& value, u32& frame)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	const int found = bus_find(id);
	if (found < 0)
		return false;

	const lane_readback* r = g_bus_lanes[found]->readback;
	if (!r || r->state != readback_ok || i >= r->result.size())
		return false;

	value.set(r->result[i]);
	frame = r->result_frame;
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

void ShaderBus::values_copy(xr_vector<lane_value>& out)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	out.resize(g_bus_lanes.size());
	for (u32 i = 0; i < out.size(); ++i)
	{
		out[i].id = g_bus_lanes[i]->id;
		out[i].value.set(g_bus_lanes[i]->bound);
		out[i].kind = g_bus_lanes[i]->kind;
	}
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

// binds a changed readback request, else publishes what the renderer landed, called under the lock
static void bus_latch_readback(ShaderBus::lane* l, bool reloaded)
{
	ShaderBus::lane_readback& r = *l->readback;
	if (reloaded || r.source != r.source_pending || r.reduce != r.reduce_pending || r.w != r.w_pending ||
		r.h != r.h_pending || r.x != r.x_pending || r.y != r.y_pending)
	{
		r.source = r.source_pending;
		r.reduce = r.reduce_pending;
		r.w = r.w_pending;
		r.h = r.h_pending;
		r.x = r.x_pending;
		r.y = r.y_pending;
		++r.generation;
		r.result.clear();
		r.state = ShaderBus::readback_none;
		r.landed_new = false;
	}
	else if (r.landed_new)
	{
		// a lost copy keeps the last result
		if (r.landed_state == ShaderBus::readback_ok)
		{
			r.result = r.landed;
			r.result_frame = r.landed_frame;
		}
		else if (r.landed_state != ShaderBus::readback_lost)
			r.result.clear();
		r.state = r.landed_state;
		r.landed_new = false;
	}

	if (r.source != ShaderBus::readback_off)
		g_bus_readbacks.push_back(l);
}

// copies one lane's pending value, rows and texture to bound, true when the texture moved
static bool bus_latch_lane(ShaderBus::lane* l)
{
	bool textures_moved = false;
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
	return textures_moved;
}

// the object mask bit a token holds, parked or live, -1 for none, called under the lock
static int bus_mask_bit(u32 token)
{
	for (int bit = 10; bit < 24; ++bit)
		if (g_bus_mask_tokens[bit] == token)
			return bit;
	return -1;
}

void ShaderBus::frame_latch()
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	const bool reloaded = g_bus_epoch != g_bus_epoch_live;
	g_bus_epoch = g_bus_epoch_live;
	g_bus_readbacks.clear();
	g_bus_buffers.clear();
	g_bus_late.clear();

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
		if (bus_latch_lane(l))
			textures_moved = true;

		if (l->readback)
			bus_latch_readback(l, reloaded);

		if (l->buffer_rows)
		{
			l->buffer_bound = l->buffer_rows;
			l->buffer_kind = l->kind;
			l->buffer_gpu = bus_gpu_written(l);
			g_bus_buffers.push_back(l);
		}

		if (l->late)
			g_bus_late.push_back(l);
	}
	if (textures_moved)
		++g_bus_texture_serial;

	// draws read object values only from the blocks written here
	for (u32 i = 0; i < g_bus_object_writes.size(); ++i)
	{
		bus_object_write& entry = g_bus_object_writes[i];
		if (g_bus_lanes[entry.lane]->role == role_mask)
		{
			// a bit released since the write stays off the object
			const int bit = bus_mask_bit(bus_token(g_bus_lanes[entry.lane]));
			u32& bits = entry.object->renderable.bus_mask;
			if (bit >= 0 && !(g_bus_mask_parked & (1u << bit)))
				bits = entry.clear ? bits & ~(1u << bit) : bits | (1u << bit);
			continue;
		}
		if (g_bus_lanes[entry.lane]->shared)
			bus_resolve_object(entry);
		bus_store_object(entry);
	}
	g_bus_object_writes.clear();

	// a bit parked before this level load returns to the pool, the others stay live or parked
	g_bus_mask_live = 0;
	for (u32 bit = 10; bit < 24; ++bit)
	{
		const u32 b = 1u << bit;
		if (reloaded && (g_bus_mask_parked & b) && g_bus_mask_park_epochs[bit] < g_bus_epoch)
		{
			g_bus_mask_tokens[bit] = 0;
			g_bus_mask_parked &= ~b;
		}
		if (g_bus_mask_tokens[bit] && !(g_bus_mask_parked & b))
			g_bus_mask_live |= b;
	}
	hotness_lane = g_bus_hotness;
}

void ShaderBus::frame_latch_late()
{
	// written only by frame_latch on this thread
	if (g_bus_late.empty())
		return;

	xrCriticalSectionGuard guard(&g_bus_lock);
	bool textures_moved = false;
	for (u32 i = 0; i < g_bus_late.size(); ++i)
	{
		lane* l = g_bus_late[i];
		if (bus_latch_lane(l))
			textures_moved = true;

		// rows copied here upload with the kind they were written as
		if (l->buffer_rows)
			l->buffer_kind = l->kind;
	}
	if (textures_moved)
		++g_bus_texture_serial;
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

void ShaderBus::enable_gpu_lanes()
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	g_bus_gpu_lanes = true;
}

bool ShaderBus::gpu_lanes_enabled()
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	return g_bus_gpu_lanes;
}

void ShaderBus::enable_buffers()
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	g_bus_buffer_lanes = true;
}

void ShaderBus::enable_mask()
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	g_bus_mask = true;
}

bool ShaderBus::mask_enabled()
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	return g_bus_mask;
}

void ShaderBus::set_mask_msaa(bool on)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	g_bus_mask_msaa = on;
}

bool ShaderBus::mask_drawable()
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	return g_bus_mask && !g_bus_mask_msaa;
}

static bool bus_role_free(LPCSTR id, LPCSTR owner, u8 role);
static bool bus_room(LPCSTR id, LPCSTR prev);

// keeps the reason for last_refusal and prints it, 0 for the refused registration
static u32 bus_refuse_register(LPCSTR what, LPCSTR id, LPCSTR reason)
{
	ShaderBus::note_refusal(id, reason);
	Msg("! [SHADER-BUS] %s '%s' refused, %s", what, id, reason);
	return 0;
}

u32 ShaderBus::register_mask(LPCSTR id, LPCSTR owner, LPCSTR description, LPCSTR source)
{
	if (!bus_take_args(id, owner, description))
		return 0;
	if (!mask_enabled())
		return bus_refuse_register("mask", id, "this renderer has no object masks");
	if (bus_reserved(id))
		return 0;
	if (0 == strncmp(id, "obj_", 4))
		return bus_refuse_register("mask", id, "an obj_ id is an object value lane");

	// every refusal comes before the take so a refused call adds no lane
	xrCriticalSectionGuard guard(&g_bus_lock);
	if (!bus_role_free(id, owner, role_mask) || !bus_room(id, nullptr))
		return 0;

	const int found = bus_find(id);
	int bit = found >= 0 && g_bus_lanes[found]->registered ? bus_mask_bit(bus_token(g_bus_lanes[found])) : -1;
	if (bit < 0)
	{
		bit = 10;
		while (bit < 24 && g_bus_mask_tokens[bit])
			++bit;
		if (bit == 24)
			return bus_refuse_register("mask", id, "all 14 object mask bits are taken");
	}

	const u32 token = bus_take_locked(id, owner, description, source, true, false);
	lane* l = g_bus_lanes[token & 0xffff];
	l->role = role_mask;
	g_bus_mask_tokens[bit] = token;
	g_bus_mask_parked &= ~(1u << bit);
	const u32 raw[4] = { 1u << bit, 0, 0, 0 };
	CopyMemory(&l->pending, raw, sizeof(raw));
	l->kind = kind_uint;
	++l->writes;
	return token;
}

bool ShaderBus::set_mask(u32 token, IRenderable* object, bool on)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	lane* l = bus_writable(token);
	if (!l)
		return false;
	if (l->role != role_mask)
		return bus_refuse_write(l, "an object, it is not a mask lane");
	const int bit = bus_mask_bit(token);
	if (bit < 0 || (g_bus_mask_parked & (1u << bit)))
		return false;
	if (!object)
		return bus_refuse_write(l, "an object that does not exist");

	// object_forget drops the queued write with the object's other writes
	bus_object_write entry;
	entry.object = object;
	entry.lane = l->index;
	entry.clear = !on;
	entry.value.set(0.f, 0.f, 0.f, 0.f);
	g_bus_object_writes.push_back(entry);
	g_bus_objects_used = true;
	++l->writes;
	return true;
}

u32 ShaderBus::get_mask(const IRenderable* object)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	return object->renderable.bus_mask & g_bus_mask_live;
}

bool ShaderBus::release_mask(u32 token)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	lane* l = bus_writable(token);
	if (!l)
		return false;
	if (l->role != role_mask)
		return bus_refuse_write(l, "a release, it is not a mask lane");

	// objects keep the bit until their level unloads, so it waits out the next level load
	const int bit = bus_mask_bit(token);
	if (bit >= 0 && !(g_bus_mask_parked & (1u << bit)))
	{
		g_bus_mask_parked |= 1u << bit;
		g_bus_mask_park_epochs[bit] = g_bus_epoch_live;
		l->pending.set(0.f, 0.f, 0.f, 0.f);
		++l->writes;
	}
	return true;
}

u32 ShaderBus::mask_live_bits()
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	return g_bus_mask_live;
}

int ShaderBus::mask_bit(u32 token)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	return bus_mask_bit(token);
}

const xr_vector<ShaderBus::lane*>& ShaderBus::readbacks()
{
	return g_bus_readbacks;
}

void ShaderBus::level_loaded()
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	++g_bus_epoch_live;
}

u32 ShaderBus::level_epoch()
{
	return g_bus_epoch;
}

const xr_vector<ShaderBus::lane*>& ShaderBus::buffers()
{
	return g_bus_buffers;
}

LPCSTR ShaderBus::target_format_name(u8 format)
{
	static LPCSTR names[target_format_count] = { "rgba8", "rgb10a2", "r11g11b10f", "rg16f", "r16f", "r32f", "rgba16f", "rgba32f" };
	return format < target_format_count ? names[format] : "";
}

// false with a ~ line when a target, pass or mask call cannot take the lane in that role, called under the lock
static bool bus_role_free(LPCSTR id, LPCSTR owner, u8 role)
{
	// obj_hotness is shared from its creation, so it is refused before it exists too
	const int found = bus_find(id);
	if (0 == xr_strcmp(id, "obj_hotness") || (found >= 0 && g_bus_lanes[found]->shared))
	{
		ShaderBus::note_refusal(id, "it is shared");
		Msg("~ [SHADER-BUS] lane '%s' is shared, '%s' cannot take it as a target, pass or mask", id, owner);
		return false;
	}
	if (found < 0)
		return true;

	const ShaderBus::lane* l = g_bus_lanes[found];
	if (!l->registered)
		return true;
	if (0 != xr_strcmp(l->owner.c_str(), owner))
	{
		ShaderBus::note_refusal(id, "it belongs to another owner");
		Msg("~ [SHADER-BUS] lane '%s' stays with '%s', '%s' did not take it", id, l->owner.c_str(), owner);
		return false;
	}
	if (l->role != role || l->texture_pending.size() || l->texture_bound.size() || l->buffer_rows)
	{
		ShaderBus::note_refusal(id, "it is in use as another kind of lane");
		Msg("~ [SHADER-BUS] lane '%s' of '%s' is in use as another kind of lane", id, owner);
		return false;
	}
	return true;
}

// false with a ! line citing the first new lane that would pass the 16 bit lane index, called under the lock
static bool bus_room(LPCSTR id, LPCSTR prev)
{
	u32 lanes = u32(g_bus_lanes.size());
	LPCSTR ids[] = { id, prev };
	for (u32 i = 0; i < 2; ++i)
		if (ids[i] && bus_find(ids[i]) < 0 && ++lanes > 0xFFFF)
		{
			ShaderBus::note_refusal(ids[i], "the bus is full");
			Msg("! [SHADER-BUS] lane '%s' refused, the bus is full at %u lanes", ids[i], u32(g_bus_lanes.size()));
			return false;
		}
	return true;
}

static int bus_target_find(const shared_str& id)
{
	for (u32 i = 0; i < g_bus_targets.size(); ++i)
		if (g_bus_targets[i].id.equal(id))
			return int(i);
	return -1;
}

// all live targets together stay under this so a script cannot exhaust video memory
static const u64 bus_target_budget = u64(1024) << 20;

// bytes a target takes at the current resolution, twice with history
static u64 bus_target_bytes(const ShaderBus::target_spec& t)
{
	static const u8 texel_bytes[ShaderBus::target_format_count] = { 4, 4, 4, 4, 2, 4, 8, 16 };
	u64 w = t.width, h = t.height;
	if (t.size != ShaderBus::size_fixed)
	{
		const u32 shift = t.size == ShaderBus::size_half ? 1 : t.size == ShaderBus::size_quarter ? 2 : 0;
		w = _max(Device.dwWidth >> shift, 1u);
		h = _max(Device.dwHeight >> shift, 1u);
	}
	return w * h * texel_bytes[t.format] * (t.history ? 2 : 1);
}

static int bus_pass_find(const shared_str& id)
{
	for (u32 i = 0; i < g_bus_passes.size(); ++i)
		if (g_bus_passes[i].id.equal(id))
			return int(i);
	return -1;
}

static void bus_status_erase(const shared_str& id)
{
	for (u32 i = 0; i < g_bus_status.size(); ++i)
		if (g_bus_status[i].first.equal(id))
		{
			g_bus_status.erase(g_bus_status.begin() + i);
			return;
		}
}

// the target and its history read pending again until the render side writes them
static void bus_target_status_erase(const shared_str& id)
{
	string64 prev;
	xr_sprintf(prev, "%s_prev", id.c_str());
	bus_status_erase(id);
	bus_status_erase(shared_str(prev));
}

static bool bus_same_target(const ShaderBus::target_spec& a, const ShaderBus::target_spec& b)
{
	return a.size == b.size && a.width == b.width && a.height == b.height && a.format == b.format && a.history == b.history &&
	       0 == memcmp(&a.clear, &b.clear, sizeof(a.clear));
}

static bool bus_same_pass(const ShaderBus::pass_spec& a, const ShaderBus::pass_spec& b)
{
	if (a.stage != b.stage || a.order != b.order || !a.ps.equal(b.ps) || !a.cs.equal(b.cs) || a.output_count != b.output_count ||
	    a.inputs.size() != b.inputs.size() || a.blend != b.blend || a.group_x != b.group_x || a.group_y != b.group_y || a.svp != b.svp)
		return false;
	for (u32 i = 0; i < a.output_count; ++i)
		if (!a.outputs[i].equal(b.outputs[i]))
			return false;
	for (u32 i = 0; i < a.inputs.size(); ++i)
		if (!a.inputs[i].hlsl.equal(b.inputs[i].hlsl) || !a.inputs[i].texture.equal(b.inputs[i].texture))
			return false;
	return true;
}

// why a pass spec is refused, null when it is not
static LPCSTR bus_pass_refusal(const ShaderBus::pass_spec& spec)
{
	if (spec.stage >= ShaderBus::stage_count)
		return "a stage past post";
	if (!spec.ps.size() == !spec.cs.size())
		return "it needs exactly one of ps and cs";
	if (spec.output_count < 1 || spec.output_count > ShaderBus::max_pass_outputs)
		return "it needs 1 to 4 outputs";
	for (u32 i = 0; i < spec.output_count; ++i)
	{
		if (!spec.outputs[i].size())
			return "an empty output";
		if (0 == xr_strcmp(spec.outputs[i], "scene") &&
		    (spec.output_count > 1 || (spec.stage != ShaderBus::stage_scene && spec.stage != ShaderBus::stage_post)))
			return "scene as an output, which is the only output of a scene or post pass";
	}
	if (spec.inputs.size() > ShaderBus::max_pass_inputs)
		return "more than 16 inputs";
	for (u32 i = 0; i < spec.inputs.size(); ++i)
		if (!spec.inputs[i].hlsl.size() || !spec.inputs[i].texture.size())
			return "an input with an empty name or texture";
	if (spec.blend > ShaderBus::blend_alpha)
		return "a blend other than none, add or alpha";
	if (spec.cs.size() && (spec.group_x < 1 || spec.group_x > 1024 || spec.group_y < 1 || spec.group_y > 1024))
		return "a compute group side outside 1 to 1024";
	return nullptr;
}

u32 ShaderBus::register_target(LPCSTR id, LPCSTR owner, LPCSTR description, LPCSTR source, const target_spec& spec)
{
	if (!bus_take_args(id, owner, description))
		return 0;
	if (!enable_targets())
		return bus_refuse_register("target", id, "this renderer has no targets");
	if (bus_reserved(id))
		return 0;
	if (xr_strlen(id) > 27)
	{
		ShaderBus::note_refusal(id, "a target id longer than 27 characters");
		Msg("! [SHADER-BUS] target '%s' refused, a target id is at most 27 characters", id);
		return 0;
	}
	if (spec.size > size_fixed ||
	    (spec.size == size_fixed && (spec.width < 1 || spec.width > 4096 || spec.height < 1 || spec.height > 4096)))
		return bus_refuse_register("target", id, "a size side outside 1 to 4096");
	if (spec.format >= target_format_count)
		return bus_refuse_register("target", id, "a format past rgba32f");
	if (!bus_finite(spec.clear))
		return bus_refuse_register("target", id, "a clear that is not finite");

	string64 prev;
	xr_sprintf(prev, "%s_prev", id);
	const shared_str key(id);

	// every refusal comes before the first take so a refused call adds no lane
	xrCriticalSectionGuard guard(&g_bus_lock);
	if (!bus_role_free(id, owner, role_target))
		return 0;
	// a refusal of the history lane is kept under the target id too
	if (spec.history && !bus_role_free(prev, owner, role_history))
	{
		note_refusal(id, last_refusal(prev));
		return 0;
	}
	if (!bus_room(id, spec.history ? prev : nullptr))
	{
		note_refusal(id, "the bus is full");
		return 0;
	}

	int entry = bus_target_find(key);
	u32 live = 0;
	const u64 need = bus_target_bytes(spec);
	const u64 kept = entry >= 0 && !g_bus_targets[entry].released ? bus_target_bytes(g_bus_targets[entry]) : 0;
	u64 bytes = need;
	for (u32 i = 0; i < g_bus_targets.size(); ++i)
		if (int(i) != entry && !g_bus_targets[i].released)
		{
			++live;
			bytes += bus_target_bytes(g_bus_targets[i]);
		}
	if (live >= max_targets)
	{
		ShaderBus::note_refusal(id, "the bus has 16 live targets");
		Msg("! [SHADER-BUS] target '%s' refused, the bus has %u live targets", id, max_targets);
		return 0;
	}
	// a call that grows nothing passes even when a mode change left the total over
	if (bytes > bus_target_budget && need > kept)
	{
		ShaderBus::note_refusal(id, "the targets would pass their memory budget");
		Msg("! [SHADER-BUS] target '%s' refused, the bus targets would pass %u MB", id, u32(bus_target_budget >> 20));
		return 0;
	}

	const u32 token = bus_take_locked(id, owner, description, source, true, false);
	g_bus_lanes[token & 0xffff]->role = role_target;
	if (spec.history)
		g_bus_lanes[bus_take_locked(prev, owner, description, source, true, false) & 0xffff]->role = role_history;

	if (entry >= 0 && !g_bus_targets[entry].released && bus_same_target(g_bus_targets[entry], spec))
		return token;

	if (entry < 0)
	{
		g_bus_targets.push_back(target_spec());
		entry = int(g_bus_targets.size()) - 1;
	}
	target_spec& t = g_bus_targets[entry];
	const u32 generation = t.generation + 1;
	t = spec;
	t.id = key;
	t.owner = owner;
	t.released = false;
	t.generation = generation;
	bus_target_status_erase(key);
	++g_bus_targets_serial;
	return token;
}

u32 ShaderBus::register_pass(LPCSTR id, LPCSTR owner, LPCSTR description, LPCSTR source, const pass_spec& spec)
{
	if (!bus_take_args(id, owner, description))
		return 0;
	if (!enable_targets())
		return bus_refuse_register("pass", id, "this renderer has no passes");
	if (bus_reserved(id))
		return 0;
	if (LPCSTR why = bus_pass_refusal(spec))
	{
		ShaderBus::note_refusal(id, why);
		Msg("! [SHADER-BUS] pass '%s' refused, %s", id, why);
		return 0;
	}

	// inputs in name order so the stored spec and the equal spec test do not depend on table order
	pass_spec sorted = spec;
	std::sort(sorted.inputs.begin(), sorted.inputs.end(),
	          [](const pass_input& a, const pass_input& b) { return xr_strcmp(a.hlsl, b.hlsl) < 0; });
	const shared_str key(id);

	xrCriticalSectionGuard guard(&g_bus_lock);
	if (sorted.cs.size())
	{
		if (!g_bus_gpu_lanes)
			return bus_refuse_register("pass", id, "this renderer has no compute passes");
	}
	// a buffer lane is the only output of a compute pass of its owner
	for (u32 i = 0; i < sorted.output_count; ++i)
	{
		const int found = bus_find(sorted.outputs[i].c_str());
		if (found < 0 || !g_bus_lanes[found]->buffer_rows)
			continue;
		LPCSTR why = !sorted.cs.size() ? ", which only a compute pass writes"
		             : sorted.output_count > 1 ? " beside other outputs"
		             : xr_strcmp(g_bus_lanes[found]->owner, owner) ? " of another owner"
		             : sorted.group_y != 1 ? ", its group takes y = 1" : nullptr;
		if (why)
		{
			ShaderBus::note_refusal(id, "an output is a buffer lane it cannot write");
			Msg("! [SHADER-BUS] pass '%s' refused, output '%s' is a buffer lane%s", id, sorted.outputs[i].c_str(), why);
			return 0;
		}
	}
	if (!bus_role_free(id, owner, role_pass) || !bus_room(id, nullptr))
		return 0;

	int entry = bus_pass_find(key);
	u32 live = 0;
	for (u32 i = 0; i < g_bus_passes.size(); ++i)
		if (int(i) != entry && !g_bus_passes[i].released)
			++live;
	if (live >= max_passes)
	{
		ShaderBus::note_refusal(id, "the bus has 32 live passes");
		Msg("! [SHADER-BUS] pass '%s' refused, the bus has %u live passes", id, max_passes);
		return 0;
	}

	const u32 token = bus_take_locked(id, owner, description, source, true, false);
	g_bus_lanes[token & 0xffff]->role = role_pass;
	if (entry >= 0 && !g_bus_passes[entry].released && bus_same_pass(g_bus_passes[entry], sorted))
		return token;

	if (entry < 0)
	{
		g_bus_passes.push_back(pass_spec());
		entry = int(g_bus_passes.size()) - 1;
	}
	pass_spec& p = g_bus_passes[entry];
	const u32 enables = p.enables + 1;
	p = sorted;
	p.id = key;
	p.owner = owner;
	p.enabled = true;
	p.released = false;
	p.enables = enables;
	bus_status_erase(key);
	// a pass registered again reads as not run until its first run
	for (u32 i = 0; i < g_bus_pass_clocks.size(); ++i)
		if (g_bus_pass_clocks[i].id.equal(key))
		{
			g_bus_pass_clocks.erase(g_bus_pass_clocks.begin() + i);
			break;
		}
	++g_bus_targets_serial;
	return token;
}

bool ShaderBus::enable_pass(u32 token, bool on)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	lane* l = bus_writable(token);
	if (!l)
		return false;
	if (l->role != role_pass)
		return bus_refuse_write(l, "an enable, it is not a pass lane");
	const int entry = bus_pass_find(l->id);
	if (entry < 0 || g_bus_passes[entry].released)
		return false;

	pass_spec& p = g_bus_passes[entry];
	if (p.enabled == on)
		return true;
	if (on)
		++p.enables;
	p.enabled = on;
	++g_bus_targets_serial;
	return true;
}

bool ShaderBus::release_pass(u32 token)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	lane* l = bus_writable(token);
	if (!l)
		return false;
	if (l->role != role_pass)
		return bus_refuse_write(l, "a release, it is not a pass lane");
	const int entry = bus_pass_find(l->id);
	if (entry < 0)
		return false;
	if (!g_bus_passes[entry].released)
	{
		g_bus_passes[entry].released = true;
		bus_status_erase(l->id);
		++g_bus_targets_serial;
	}
	return true;
}

bool ShaderBus::release_target(u32 token)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	lane* l = bus_writable(token);
	if (!l)
		return false;
	if (l->role != role_target)
		return bus_refuse_write(l, "a release, it is not a target lane");
	const int entry = bus_target_find(l->id);
	if (entry < 0)
		return false;
	if (!g_bus_targets[entry].released)
	{
		g_bus_targets[entry].released = true;
		bus_target_status_erase(l->id);
		++g_bus_targets_serial;
	}
	return true;
}

void ShaderBus::set_enable_targets(bool on)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	g_bus_targets_enabled = on;
}

bool ShaderBus::enable_targets()
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	return g_bus_targets_enabled;
}

u32 ShaderBus::targets_serial()
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	return g_bus_targets_serial;
}

void ShaderBus::targets_copy(xr_vector<target_spec>& targets, xr_vector<pass_spec>& passes)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	targets = g_bus_targets;
	passes = g_bus_passes;
}

void ShaderBus::set_gpu_status(const shared_str& id, const gpu_status& status)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	const u32 failed = status.state == gpu_failed ? 1 : 0;
	for (u32 i = 0; i < g_bus_status.size(); ++i)
		if (g_bus_status[i].first.equal(id))
		{
			const u32 fails = g_bus_status[i].second.fails;
			g_bus_status[i].second = status;
			g_bus_status[i].second.fails = fails + failed;
			return;
		}
	g_bus_status.push_back(std::make_pair(id, status));
	g_bus_status.back().second.fails = failed;
}

bool ShaderBus::get_gpu_status(const shared_str& id, gpu_status& status)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	for (u32 i = 0; i < g_bus_status.size(); ++i)
		if (g_bus_status[i].first.equal(id))
		{
			status = g_bus_status[i].second;
			return true;
		}
	return false;
}

bool ShaderBus::target_get(const shared_str& id, target_spec& out)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	const int entry = bus_target_find(id);
	if (entry < 0)
		return false;
	out = g_bus_targets[entry];
	return true;
}

bool ShaderBus::pass_get(const shared_str& id, pass_spec& out)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	const int entry = bus_pass_find(id);
	if (entry < 0)
		return false;
	out = g_bus_passes[entry];
	return true;
}

void ShaderBus::set_pass_time(const shared_str& id, const Fvector4& time, u32 frame)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	for (u32 i = 0; i < g_bus_pass_clocks.size(); ++i)
		if (g_bus_pass_clocks[i].id.equal(id))
		{
			g_bus_pass_clocks[i].time = time;
			g_bus_pass_clocks[i].frame = frame;
			return;
		}
	bus_pass_clock c;
	c.id = id;
	c.time = time;
	c.frame = frame;
	g_bus_pass_clocks.push_back(c);
}

bool ShaderBus::get_pass_time(LPCSTR id, Fvector4& time, u32& frame)
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	for (u32 i = 0; i < g_bus_pass_clocks.size(); ++i)
		if (!xr_strcmp(g_bus_pass_clocks[i].id, id))
		{
			time = g_bus_pass_clocks[i].time;
			frame = g_bus_pass_clocks[i].frame;
			return true;
		}
	return false;
}

int ShaderBus::version()
{
	return 8;
}
