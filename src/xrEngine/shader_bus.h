#pragma once

class IRenderable;

namespace ShaderBus
{
	// what the bits of a lane store, uint lanes keep raw unsigned values in the float fields
	enum lane_kind
	{
		kind_float = 0,
		kind_uint = 1
	};

	struct lane_shared;

	// where a readback copies from and how it folds the block
	enum readback_source
	{
		readback_off = 0,
		readback_light = 1,
		readback_scene = 2,
		readback_backbuffer = 3
	};

	enum readback_reduce
	{
		reduce_none = 0,
		reduce_mean = 1,
		reduce_min = 2,
		reduce_max = 3
	};

	// list reports these as none, ok, refused and lost
	enum readback_state
	{
		readback_none = 0,
		readback_ok = 1,
		readback_refused = 2,
		readback_lost = 3
	};

	// a lane's readback request as written and as bound, what the renderer landed and what scripts read
	struct lane_readback
	{
		u8 source_pending;
		u8 reduce_pending;
		u8 w_pending;
		u8 h_pending;
		float x_pending;
		float y_pending;
		u8 source;
		u8 reduce;
		u8 w;
		u8 h;
		float x;
		float y;
		// moves with every bound request so copies of an older one are dropped
		u32 generation;
		// written by the renderer, published by the per-frame update
		xr_vector<Fvector4> landed;
		u32 landed_frame;
		u8 landed_state;
		bool landed_new;
		xr_vector<Fvector4> result;
		u32 result_frame;
		u8 state;

		lane_readback() : source_pending(0), reduce_pending(0), w_pending(0), h_pending(0), x_pending(0.f), y_pending(0.f),
		                  source(0), reduce(0), w(0), h(0), x(0.f), y(0.f), generation(0), landed_frame(0), landed_state(0),
		                  landed_new(false), result_frame(0), state(0)
		{
		}
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
		// the texture shaders sample through $user$bus_<id>, empty for none
		shared_str texture_pending;
		shared_str texture_bound;
		// rows past row 0 for array and matrix constants, empty until the first set_rows
		xr_vector<Fvector4> rows_pending;
		xr_vector<Fvector4> rows_bound;
		u32 rows_declared;
		// the tokens of a shared lane and their values, null for a lane that is not shared
		lane_shared* shared;
		// the lane's readback, null until set_readback first arms one
		lane_readback* readback;
		// objects with their own value on an obj_ lane
		u32 objects;
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
		// is_forced as of the last per-frame update, what draws read
		bool bound_forced;
		bool warned;
		bool rows_dirty;
		// the rows set_buffer asked for, and that size and the kind as of the last per-frame update
		u32 buffer_rows;
		u32 buffer_bound;
		u8 buffer_kind;
		// latched again after the frame callbacks
		bool late;

		lane() : rows_declared(0), shared(nullptr), readback(nullptr), objects(0), changes(0), writes(0), last_change_frame(0), bound_frame(0), index(0),
		         nonce(0), kind(kind_float), declared_kinds(0), registered(false), is_forced(false),
		         bound_forced(false), warned(false), rows_dirty(false), buffer_rows(0), buffer_bound(0), buffer_kind(0), late(false)
		{
			pending.set(0.f, 0.f, 0.f, 0.f);
			bound.set(0.f, 0.f, 0.f, 0.f);
			forced.set(0.f, 0.f, 0.f, 0.f);
		}
	};

	struct object_value
	{
		u16 lane;
		Fvector4 value;
	};

	// one object's own values on obj_ lanes, written only by frame_latch
	struct object_values
	{
		xr_vector<object_value> values;
	};

	// one token's value on a shared lane or on one object of it
	struct writer_value
	{
		shared_str owner;
		Fvector4 value;
	};

	// the most 16 byte rows one lane can have
	const u32 max_rows = 4096;
	// the most rows of one buffer lane and bytes of all buffer lanes
	const u32 max_buffer_rows = 65536;
	const u32 max_buffer_bytes = 16 << 20;
	// the most obj_ lanes one pass binds
	const u32 max_object_lanes = 16;
	// the most armed readback lanes, texels a side of one block and copies in flight per lane
	const u32 max_readbacks = 16;
	const u32 max_readback_block = 32;
	const u32 readback_ring = 3;

	// what a draw of an object binds on an obj_ lane, its own value or the lane's
	inline const Fvector4& object_bound(const lane* l, const object_values* block)
	{
		if (block && !l->bound_forced)
			for (u32 i = 0; i < block->values.size(); ++i)
				if (block->values[i].lane == l->index)
					return block->values[i].value;
		return l->bound;
	}

	// the obj_hotness lane as of the last per-frame update, null until a shader declares it or a script registers it
	extern ENGINE_API const lane* hotness_lane;

	// replaces hotness with x of the object's obj_hotness value or of bus_force, else leaves the engine's own
	// true only for the object's own value, which then applies to every surface of it
	inline bool object_hotness(const object_values* block, float& hotness)
	{
		const lane* l = hotness_lane;
		if (!l)
			return false;
		if (l->bound_forced)
		{
			hotness = l->bound.x;
			return false;
		}
		if (block)
			for (u32 i = 0; i < block->values.size(); ++i)
				if (block->values[i].lane == l->index)
				{
					hotness = block->values[i].value.x;
					return true;
				}
		return false;
	}

	// lanes are never removed so a returned pointer stays valid for the process lifetime
	ENGINE_API lane* declare(LPCSTR hlsl_name, u32 rows, u8 kind);
	// logs once per constant name why the bus will not bind it
	ENGINE_API void refuse(LPCSTR hlsl_name, LPCSTR reason);

	ENGINE_API u32 register_lane(LPCSTR id, LPCSTR owner, LPCSTR description, LPCSTR source);
	ENGINE_API u32 try_register(LPCSTR id, LPCSTR owner, LPCSTR description, LPCSTR source);
	// joins a shared lane with a token of its own, the first call makes the lane shared
	ENGINE_API u32 register_shared(LPCSTR id, LPCSTR owner, LPCSTR description, LPCSTR source);
	// a lane the engine fills, scripts cannot register ids starting engine_
	ENGINE_API u32 register_engine(LPCSTR id, LPCSTR description);
	// true once a shader declares the lane the token writes
	ENGINE_API bool declared(u32 token);
	ENGINE_API bool set(u32 token, float x, float y, float z, float w);
	// writes rows first to first + count - 1, row 0 is the value set writes
	ENGINE_API bool set_rows(u32 token, u32 first, const Fvector4* rows, u32 count);
	ENGINE_API bool set_uint(u32 token, u32 x, u32 y, u32 z, u32 w);
	// rows points to count rows of four unsigned values
	ENGINE_API bool set_rows_uint(u32 token, u32 first, const u32* rows, u32 count);
	// gives one object its own value on an obj_ lane from the next frame on
	ENGINE_API bool set_object(u32 token, IRenderable* object, float x, float y, float z, float w);
	// the object reads the lane's value again from the next frame on
	ENGINE_API bool clear_object(u32 token, IRenderable* object);
	// drops everything the bus stores for an object, called as it is destroyed
	ENGINE_API void object_forget(IRenderable* object);
	// shows a texture or render target through $user$bus_<id> from the next frame on, empty clears
	ENGINE_API bool set_texture(u32 token, LPCSTR name);
	// moves when any lane's bound texture changes, read on the main thread after the per-frame update
	ENGINE_API u32 texture_serial();
	// the lane id and bound texture of lane index, false past the last lane
	ENGINE_API bool texture_get(u32 index, shared_str& id, shared_str& name);
	// logs once per lane why a write was dropped and returns false
	ENGINE_API bool refuse_write(u32 token, LPCSTR what);
	ENGINE_API bool get(LPCSTR id, Fvector4& value);
	// bound row, false past the rows a shader declared
	ENGINE_API bool get_row(LPCSTR id, u32 row, Fvector4& value);
	// what draws of the object bind, false for an unknown lane or no object
	ENGINE_API bool get_object(LPCSTR id, const IRenderable* object, Fvector4& value);
	// the tokens of a shared lane that set a value, on the lane or on the object, false for an unknown lane
	ENGINE_API bool writers(LPCSTR id, const IRenderable* object, xr_vector<writer_value>& out);
	// the tokens a lane has, 1 for a registered lane that is not shared
	ENGINE_API u32 writer_count(const lane* l);
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

	// the renderer calls this at device create when it can bind buffers and read back
	ENGINE_API void enable_gpu_lanes();
	// arms a w by h block centred at x, y in 0..1 of light, scene or backbuffer, an empty source disarms
	ENGINE_API bool set_readback(u32 token, LPCSTR source, float x, float y, u32 w, u32 h, LPCSTR reduce);
	// texel i of the last result, row-major for none and 0 only for a reduce, false with no result
	ENGINE_API bool get_readback(LPCSTR id, u32 i, Fvector4& value, u32& frame);
	// armed readback lanes as of the last per-frame update, main thread only
	ENGINE_API const xr_vector<lane*>& readbacks();
	// sizes a lane as a typed buffer of rows, grow only, new rows read 0
	ENGINE_API bool set_buffer(u32 token, u32 rows);
	// buffer lanes as of the last per-frame update, main thread only
	ENGINE_API const xr_vector<lane*>& buffers();
	// counts a finished level load, any thread
	ENGINE_API void level_loaded();
	// the level count as of the last per-frame update, main thread only
	ENGINE_API u32 level_epoch();

	// copies the forced or pending value to bound and counts a change when it differs
	// and moves queued object values into each object's block and pending textures to bound
	// a shared lane or object written since the last per-frame update takes the largest value of its tokens
	ENGINE_API void frame_latch();
	// a late lane also latches after the frame callbacks, its draws see the same frame's writes
	ENGINE_API bool set_late(u32 token, bool on);
	// latches late lanes, main thread after the frame callbacks
	ENGINE_API void frame_latch_late();

	ENGINE_API void note_legacy_write(LPCSTR command, LPCSTR writer);
	ENGINE_API bool legacy_writer(LPCSTR command, string256& out);

	ENGINE_API void dump();
	ENGINE_API int version();
}
