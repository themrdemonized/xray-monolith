#pragma once

namespace ShaderBus
{
	// what the bits of a lane store, uint lanes keep raw unsigned values in the float fields
	enum lane_kind
	{
		kind_float = 0,
		kind_uint = 1
	};

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
		// rows past row 0 for array and matrix constants, empty until the first set_rows
		xr_vector<Fvector4> rows_pending;
		xr_vector<Fvector4> rows_bound;
		u32 rows_declared;
		u32 changes;
		u32 writes;
		u32 last_change_frame;
		u32 bound_frame;
		u16 index;
		u16 nonce;
		// the last writer's kind, the declared kind before any write
		u8 kind;
		// one bit per kind any shader declared
		u8 declared_kinds;
		bool registered;
		bool is_forced;
		bool warned;
		bool rows_dirty;

		lane() : rows_declared(0), changes(0), writes(0), last_change_frame(0), bound_frame(0), index(0), nonce(0),
		         kind(kind_float), declared_kinds(0), registered(false), is_forced(false), warned(false),
		         rows_dirty(false)
		{
			pending.set(0.f, 0.f, 0.f, 0.f);
			bound.set(0.f, 0.f, 0.f, 0.f);
			forced.set(0.f, 0.f, 0.f, 0.f);
		}
	};

	// the most 16 byte rows one lane can have
	const u32 max_rows = 4096;

	// lanes are never removed so a returned pointer stays valid for the process lifetime
	ENGINE_API lane* declare(LPCSTR hlsl_name, u32 rows, u8 kind);
	// logs once per constant name why the bus will not bind it
	ENGINE_API void refuse(LPCSTR hlsl_name, LPCSTR reason);

	ENGINE_API u32 register_lane(LPCSTR id, LPCSTR owner, LPCSTR description, LPCSTR source);
	ENGINE_API u32 try_register(LPCSTR id, LPCSTR owner, LPCSTR description, LPCSTR source);
	ENGINE_API bool set(u32 token, float x, float y, float z, float w);
	// writes rows first to first + count - 1, row 0 is the value set writes
	ENGINE_API bool set_rows(u32 token, u32 first, const Fvector4* rows, u32 count);
	ENGINE_API bool set_uint(u32 token, u32 x, u32 y, u32 z, u32 w);
	// rows points to count rows of four unsigned values
	ENGINE_API bool set_rows_uint(u32 token, u32 first, const u32* rows, u32 count);
	// logs once per lane why a write was dropped and returns false
	ENGINE_API bool refuse_write(u32 token, LPCSTR what);
	ENGINE_API bool get(LPCSTR id, Fvector4& value);
	// bound row, false past the rows a shader declared
	ENGINE_API bool get_row(LPCSTR id, u32 row, Fvector4& value);
	ENGINE_API const lane* find(LPCSTR id);
	ENGINE_API bool has(LPCSTR id);
	ENGINE_API LPCSTR describe(LPCSTR id);
	ENGINE_API LPCSTR owner_of(LPCSTR id);
	ENGINE_API bool stats(LPCSTR id, u32& changes, u32& last_change, u32& bound_frame, u32& writes);
	ENGINE_API bool get_pending(LPCSTR id, Fvector4& value);
	ENGINE_API bool force(LPCSTR id, const Fvector4& value);
	ENGINE_API bool release(LPCSTR id);
	ENGINE_API u32 count();
	ENGINE_API const lane* at(u32 index);
	// a value as text in the lane's kind, (x, y, z, w)
	ENGINE_API LPCSTR value_text(const lane* l, const Fvector4& value, string256& out);

	// copies the forced or pending value to bound and counts a change when it differs
	ENGINE_API void frame_latch();

	ENGINE_API void note_legacy_write(LPCSTR command, LPCSTR writer);
	ENGINE_API bool legacy_writer(LPCSTR command, string256& out);

	ENGINE_API void dump();
	ENGINE_API int version();
}
