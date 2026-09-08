#pragma once

namespace ShaderBus
{
	struct lane
	{
		shared_str id;
		shared_str hlsl;
		shared_str owner;
		shared_str source;
		shared_str description;
		Fvector4 pending;
		Fvector4 bound;
		Fvector4 forced;
		u32 changes;
		u32 writes;
		u32 last_change_frame;
		u32 bound_frame;
		u16 index;
		u16 nonce;
		bool registered;
		bool is_forced;

		lane() : changes(0), writes(0), last_change_frame(0), bound_frame(0), index(0), nonce(0), registered(false),
		         is_forced(false)
		{
			pending.set(0.f, 0.f, 0.f, 0.f);
			bound.set(0.f, 0.f, 0.f, 0.f);
			forced.set(0.f, 0.f, 0.f, 0.f);
		}
	};

	// lanes are never removed so a returned pointer stays valid for the process lifetime
	ENGINE_API lane* declare(LPCSTR hlsl_name);

	ENGINE_API u32 register_lane(LPCSTR id, LPCSTR owner, LPCSTR description, LPCSTR source);
	ENGINE_API u32 try_register(LPCSTR id, LPCSTR owner, LPCSTR description, LPCSTR source);
	ENGINE_API bool set(u32 token, float x, float y, float z, float w);
	ENGINE_API bool get(LPCSTR id, Fvector4& value);
	ENGINE_API bool has(LPCSTR id);
	ENGINE_API LPCSTR describe(LPCSTR id);
	ENGINE_API LPCSTR owner_of(LPCSTR id);
	ENGINE_API bool stats(LPCSTR id, u32& changes, u32& last_change, u32& bound_frame, u32& writes);
	ENGINE_API bool get_pending(LPCSTR id, Fvector4& value);
	ENGINE_API bool force(LPCSTR id, const Fvector4& value);
	ENGINE_API bool release(LPCSTR id);
	ENGINE_API u32 count();
	ENGINE_API const lane* at(u32 index);

	// copies the forced or pending value to bound and counts a change when it differs
	ENGINE_API void frame_latch();

	ENGINE_API void note_legacy_write(LPCSTR command, LPCSTR writer);
	ENGINE_API bool legacy_writer(LPCSTR command, string256& out);

	ENGINE_API void dump();
	ENGINE_API int version();
}
