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
		readback_backbuffer = 3,
		readback_target = 4,
		readback_history = 5,
		readback_buffer = 6
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
		// the rows a buffer source reads
		u32 first_pending;
		u16 count_pending;
		u8 source;
		u8 reduce;
		u8 w;
		u8 h;
		float x;
		float y;
		u32 first;
		u16 count;
		// the lane a target, history or buffer source reads, a history by its target's id
		shared_str source_id_pending;
		shared_str source_id;
		// moves with every bound request so copies of an older one are dropped
		u32 generation;
		// written by the renderer, published by the per-frame update
		xr_vector<Fvector4> landed;
		u32 landed_frame;
		u8 landed_state;
		u8 landed_inflight;
		bool landed_new;
		xr_vector<Fvector4> result;
		u32 result_frame;
		u8 state;
		// copies taken and not yet decoded as of the last frame
		u8 inflight;

		lane_readback() : source_pending(0), reduce_pending(0), w_pending(0), h_pending(0), x_pending(0.f), y_pending(0.f),
		                  first_pending(0), count_pending(0), source(0), reduce(0), w(0), h(0), x(0.f), y(0.f), first(0), count(0), generation(0), landed_frame(0), landed_state(0),
		                  landed_inflight(0), landed_new(false), result_frame(0), state(0), inflight(0)
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
		// objects with their own value on an obj_ lane, or holding the bit of a mask lane
		u32 objects;
		u32 changes;
		u32 writes;
		// write refusals and the reason of the last one
		u32 refusals;
		LPCSTR refusal;
		u32 last_change_frame;
		u32 bound_frame;
		u16 index;
		u16 nonce;
		// the last writer's kind, the declared kind before any write
		u8 kind;
		// one bit per kind any shader declared
		u8 declared_kinds;
		// lane_role, set once a target or pass takes the lane
		u8 role;
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
		// a compute pass writes the buffer as of the last per-frame update
		bool buffer_gpu;
		// a compute pass of the owner dispatches from the buffer as of the last per-frame update
		bool buffer_args;
		// latched again after the frame callbacks
		bool late;

		lane() : rows_declared(0), shared(nullptr), readback(nullptr), objects(0), changes(0), writes(0), refusals(0), refusal(nullptr), last_change_frame(0), bound_frame(0), index(0),
		         nonce(0), kind(kind_float), declared_kinds(0), role(0), registered(false), is_forced(false),
		         bound_forced(false), warned(false), rows_dirty(false), buffer_rows(0), buffer_bound(0), buffer_kind(0), buffer_gpu(false), buffer_args(false), late(false)
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

	// one lane's id, its value as of the last per-frame update and its last writer's kind
	struct lane_value
	{
		shared_str id;
		Fvector4 value;
		u8 kind;
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

	// the obj_tint lane as of the last per-frame update, null until a shader declares it or a script registers it
	extern ENGINE_API const lane* tint_lane;

	// the object's own obj_tint value or the bus_force value, false for an object with neither
	inline bool object_tint(const object_values* block, Fvector4& tint)
	{
		const lane* l = tint_lane;
		if (!l)
			return false;
		if (l->bound_forced)
		{
			tint = l->bound;
			return true;
		}
		if (block)
			for (u32 i = 0; i < block->values.size(); ++i)
				if (block->values[i].lane == l->index)
				{
					tint = block->values[i].value;
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
	// records an engine texture a compiled pass samples, the particle tag lane registers on its first bind
	ENGINE_API void note_texture(LPCSTR hlsl_name);
	// counts a pass that samples an engine texture as the renderer creates or releases it
	ENGINE_API void count_pass_texture(LPCSTR hlsl_name, bool live);
	// true while a live pass samples the object id target
	ENGINE_API bool samples_object_ids();
	// true while a live pass samples the particle target
	ENGINE_API bool samples_particles();
	// coverage targets a live pass samples, as bits
	enum : u32
	{
		cover_mask = 1,
		cover_ids = 2,
		cover_layer = 4
	};
	ENGINE_API u32 samples_cover();
	// the particle tag lane, 0 until a compiled pass samples the particle target
	ENGINE_API u32 particle_lane();
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
	// keeps why a call on the id was refused, for last_refusal
	ENGINE_API void note_refusal(LPCSTR id, LPCSTR reason);
	// the reason of the last refused write or registration of the id, null for none
	ENGINE_API LPCSTR last_refusal(LPCSTR id);
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
	// every lane's lane_value in lane order, copied under one lock
	ENGINE_API void values_copy(xr_vector<lane_value>& out);
	// a value as text in the lane's kind, (x, y, z, w)
	ENGINE_API LPCSTR value_text(const lane* l, const Fvector4& value, string256& out);

	// the renderer calls this at device create when it can read back and run compute passes
	ENGINE_API void enable_gpu_lanes();
	// true after enable_gpu_lanes, for scripts that ask before they register
	ENGINE_API bool gpu_lanes_enabled();
	// the renderer calls this at device create when it can bind buffer lanes
	ENGINE_API void enable_buffers();
	// bits HUD sources write into the shader bus mask, mod mask bits sit above them
	enum : u32
	{
		hud_arms = 0x001,
		hud_arms2 = 0x002,
		hud_item0 = 0x004,
		hud_item1 = 0x008,
		hud_scope = 0x010,
		hud_prop = 0x020,
		hud_att_actor = 0x040,
		hud_att_item = 0x080,
		hud_cam = 0x100,
		hud_lens = 0x200,
		hud_mask_bits = 0x3FF,
	};
	// object mask bits run from the first above the HUD bits up to the end
	enum
	{
		mask_first_bit = 10,
		mask_bits_end = 24
	};
	// the renderer calls this at device create, the mask target and object masks need it
	ENGINE_API void enable_mask();
	ENGINE_API bool mask_enabled();
	// the renderer passes its MSAA state at device create, the mask target needs it off
	ENGINE_API void set_mask_msaa(bool on);
	// true when masks are enabled and MSAA is off, so mask tags can reach the target
	ENGINE_API bool mask_drawable();
	// claims the lowest free object mask bit for a mask lane, the same owner gets its token back
	ENGINE_API u32 register_mask(LPCSTR id, LPCSTR owner, LPCSTR description, LPCSTR source);
	// sets or clears the token's bit on the object from the next frame on
	ENGINE_API bool set_mask(u32 token, IRenderable* object, bool on);
	// the object's mask bits that are live
	ENGINE_API u32 get_mask(const IRenderable* object);
	// queues writes so the target holds the source's mask bits from the next frame on, none with no source
	// the target holds them for the engine, outside the object listing and the lane counts
	ENGINE_API void copy_mask(const IRenderable* from, IRenderable* to);
	// drops the bit from the next frame on, when it leaves every object and returns to the pool
	ENGINE_API bool release_mask(u32 token);
	// object mask bits as of the last per-frame update, and the layer and hidden bits among them when asked
	ENGINE_API u32 mask_live_bits(u32* layer = nullptr, u32* hidden = nullptr, u32* forward = nullptr);
	// true for an object holding an item with a layer bit, as of the last per-frame update
	ENGINE_API bool layer_root(const IRenderable* object);
	// puts the token's mask bit on the layer or takes it off, from the next frame on
	ENGINE_API bool set_mask_layer(u32 token, bool on);
	// lets the token's mask bit reach tagged objects the game does not draw, or stops it, from the next frame on
	ENGINE_API bool set_mask_hidden(u32 token, bool on);
	// opens the token's bit to forward and sorted meshes or closes it from the next frame
	ENGINE_API bool set_mask_forward(u32 token, bool on);
	// the objects holding any of the bits, copied under the bus lock
	ENGINE_API void mask_holders(u32 bits, xr_vector<IRenderable*>& out);
	// the object mask bit a token holds, parked or live, -1 for none
	ENGINE_API int mask_bit(u32 token);
	// arms a w by h block centred at x, y in 0..1 of light, scene or backbuffer, an empty source disarms
	ENGINE_API bool set_readback(u32 token, LPCSTR source, float x, float y, u32 w, u32 h, LPCSTR reduce);
	// arms a readback of count rows of a buffer lane from row first
	ENGINE_API bool set_readback_rows(u32 token, LPCSTR source, u32 first, u32 count, LPCSTR reduce);
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
	// latches late lanes and fills the mask objects lane, main thread after the frame callbacks
	ENGINE_API void frame_latch_late();

	ENGINE_API void note_legacy_write(LPCSTR command, LPCSTR writer);
	ENGINE_API bool legacy_writer(LPCSTR command, string256& out);

	// what a lane is to the target and pass registry
	enum lane_role
	{
		role_none = 0,
		role_target = 1,
		role_history = 2,
		role_pass = 3,
		role_mask = 4
	};

	enum pass_stage
	{
		stage_frame = 0,
		stage_gbuffer,
		stage_lighting,
		stage_scene,
		stage_post,
		stage_count
	};

	enum target_size
	{
		size_screen = 0,
		size_half,
		size_quarter,
		size_fixed
	};

	enum pass_blend
	{
		blend_none = 0,
		blend_add,
		blend_alpha
	};

	// target_state and pass_state of list(), released is for targets and off for passes
	enum gpu_state
	{
		gpu_pending = 0,
		gpu_ready,
		gpu_failed,
		gpu_released,
		gpu_off
	};

	const u32 max_targets = 16;
	const u32 max_passes = 32;
	const u32 max_pass_outputs = 4;
	const u32 max_pass_inputs = 16;
	const u32 target_format_count = 8;
	// a history lane's id is its target's id and this suffix
	const char history_suffix[] = "_prev";

	struct target_spec
	{
		shared_str id;
		shared_str owner;
		u8 size;
		u16 width;
		u16 height;
		u8 format;
		bool history;
		// a full mip chain the engine fills after every write
		bool mips;
		bool released;
		Fvector4 clear;
		// moves when the owner changes the spec, the render side then recreates and clears
		u32 generation;
	};

	struct pass_input
	{
		shared_str hlsl;
		shared_str texture;
	};

	struct pass_spec
	{
		shared_str id;
		shared_str owner;
		u8 stage;
		int order;
		shared_str ps;
		shared_str cs;
		shared_str outputs[max_pass_outputs];
		u8 output_count;
		xr_vector<pass_input> inputs;
		u8 blend;
		// the part of the outputs a pixel pass shades, x, y, w, h in texture coordinates
		Fvector4 rect;
		u16 group_x;
		u16 group_y;
		// a compute pass into a buffer lane can dispatch over a grid sized like a target
		bool grid;
		u8 grid_size;
		u16 grid_w;
		u16 grid_h;
		// a compute pass can take its group counts from a row of a uint buffer lane
		shared_str indirect;
		u32 indirect_row;
		bool svp;
		// runs a frame takes, and the target a repeated pixel pass alternates with its output
		u8 runs;
		shared_str pingpong;
		bool enabled;
		bool released;
		// moves at registration and at every enable, the next run is a first run
		u32 enables;
	};

	// what the render side found, shown by list()
	struct gpu_status
	{
		u8 state;
		u32 width;
		u32 height;
		u32 bytes;
		// builds that failed since the spec was registered
		u32 fails;
		shared_str reason;
	};

	// rgba8 rgb10a2 r11g11b10f rg16f r16f r32f rgba16f rgba32f, in that order
	ENGINE_API LPCSTR target_format_name(u8 format);
	// texels of a w x h target, its mip chain included
	ENGINE_API u32 target_texels(u32 w, u32 h, bool mips);
	// w and h of a size rule at the current resolution, size_fixed keeps them
	ENGINE_API void target_extent(u8 size, u32& w, u32& h);
	ENGINE_API u32 register_target(LPCSTR id, LPCSTR owner, LPCSTR description, LPCSTR source, const target_spec& spec);
	ENGINE_API u32 register_pass(LPCSTR id, LPCSTR owner, LPCSTR description, LPCSTR source, const pass_spec& spec);
	ENGINE_API bool enable_pass(u32 token, bool on);
	ENGINE_API bool release_pass(u32 token);
	ENGINE_API bool release_target(u32 token);
	// set by the renderers that run targets and passes, false refuses every registration
	ENGINE_API void set_enable_targets(bool on);
	ENGINE_API bool enable_targets();
	// moves on every register, enable and release of a target or pass
	ENGINE_API u32 targets_serial();
	// the live specs, copied under the lock for the reconcile at Begin
	ENGINE_API void targets_copy(xr_vector<target_spec>& targets, xr_vector<pass_spec>& passes);
	ENGINE_API void set_gpu_status(const shared_str& id, const gpu_status& status);
	ENGINE_API bool get_gpu_status(const shared_str& id, gpu_status& status);
	ENGINE_API bool target_get(const shared_str& id, target_spec& out);
	ENGINE_API bool pass_get(const shared_str& id, pass_spec& out);
	// the render side notes every pass run, get_pass_time gives the last pass_time and the frames since it
	// as of the last per-frame update
	ENGINE_API void set_pass_time(const shared_str& id, const Fvector4& time, u32 frame);
	ENGINE_API bool get_pass_time(LPCSTR id, Fvector4& time, u32& frames);

	ENGINE_API void dump();
	ENGINE_API int version();
}
