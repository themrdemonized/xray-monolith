#include "stdafx.h"
#pragma hdrstop

#include "shader_bus.h"

static xr_vector<ShaderBus::lane*> g_bus_lanes;
static xr_vector<shared_str> g_bus_rejected;
static xrCriticalSection g_bus_lock;

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

	ShaderBus::lane* l = xr_new<ShaderBus::lane>();
	l->id = id;
	l->index = u16(g_bus_lanes.size());
	g_bus_lanes.push_back(l);
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

ShaderBus::lane* ShaderBus::declare(LPCSTR hlsl_name)
{
	if (!hlsl_name || 0 != strncmp(hlsl_name, "bus_", 4))
		return nullptr;

	LPCSTR id = hlsl_name + 4;
	xrCriticalSectionGuard guard(&g_bus_lock);

	if (!bus_valid_id(id))
	{
		shared_str key(hlsl_name);
		for (u32 i = 0; i < g_bus_rejected.size(); ++i)
			if (g_bus_rejected[i].equal(key))
				return nullptr;

		g_bus_rejected.push_back(key);
		Msg("! [SHADER-BUS] shader constant %s is not a valid lane name", hlsl_name);
		return nullptr;
	}

	lane* l = bus_find_or_add(id);
	l->hlsl = hlsl_name;
	return l;
}

u32 ShaderBus::try_register(LPCSTR id, LPCSTR owner, LPCSTR description, LPCSTR source)
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
	lane* l = bus_find_or_add(id);

	if (l->registered)
	{
		if (0 == xr_strcmp(l->owner.c_str(), owner))
			return bus_token(l);

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

u32 ShaderBus::register_lane(LPCSTR id, LPCSTR owner, LPCSTR description, LPCSTR source)
{
	const u32 token = try_register(id, owner, description, source);
	if (token)
		return token;

	string256 held_by, held_from;
	held_by[0] = 0;
	held_from[0] = 0;
	{
		xrCriticalSectionGuard guard(&g_bus_lock);
		const int found = bus_find(id);
		if (found >= 0 && g_bus_lanes[found]->registered)
		{
			const lane* l = g_bus_lanes[found];
			xr_strcpy(held_by, l->owner.c_str());
			xr_strcpy(held_from, l->source.c_str());
		}
	}

	if (held_by[0])
		Debug.fatal(DEBUG_INFO,
		            "shader bus lane '%s' already belongs to '%s' registered by '%s', '%s' registered by '%s' cannot take it",
		            id, held_by, held_from, owner ? owner : "", source ? source : "");
	return 0;
}

bool ShaderBus::set(u32 token, float x, float y, float z, float w)
{
	const u16 nonce = u16(token >> 16);
	const u16 index = u16(token & 0xffff);
	if (!nonce)
		return false;

	xrCriticalSectionGuard guard(&g_bus_lock);
	if (index >= g_bus_lanes.size())
		return false;

	lane* l = g_bus_lanes[index];
	if (!l->registered || l->nonce != nonce)
		return false;

	l->pending.set(x, y, z, w);
	return true;
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

void ShaderBus::frame_latch()
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	for (u32 i = 0; i < g_bus_lanes.size(); ++i)
		g_bus_lanes[i]->bound.set(g_bus_lanes[i]->pending);
}

void ShaderBus::dump()
{
	xrCriticalSectionGuard guard(&g_bus_lock);
	Msg("[SHADER-BUS] %d lanes", u32(g_bus_lanes.size()));
	for (u32 i = 0; i < g_bus_lanes.size(); ++i)
	{
		const lane* l = g_bus_lanes[i];
		if (l->registered)
			Msg("[SHADER-BUS] bus_%s owner '%s' from '%s' = (%f, %f, %f, %f) %s",
			    l->id.c_str(), l->owner.c_str(), l->source.c_str(),
			    l->bound.x, l->bound.y, l->bound.z, l->bound.w, l->description.c_str());
		else
			Msg("[SHADER-BUS] bus_%s declared by shaders, not registered", l->id.c_str());
	}
}

int ShaderBus::version()
{
	return 1;
}
