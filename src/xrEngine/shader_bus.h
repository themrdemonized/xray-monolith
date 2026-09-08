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
		u16 index;
		u16 nonce;
		bool registered;

		lane() : index(0), nonce(0), registered(false)
		{
			pending.set(0.f, 0.f, 0.f, 0.f);
			bound.set(0.f, 0.f, 0.f, 0.f);
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
	ENGINE_API u32 count();
	ENGINE_API const lane* at(u32 index);

	// copies pending to bound for every lane so a write lands whole on the next frame
	ENGINE_API void frame_latch();

	ENGINE_API void dump();
	ENGINE_API int version();
}
