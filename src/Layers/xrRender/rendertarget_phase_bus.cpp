#include "stdafx.h"

#include "../../xrEngine/shader_bus.h"

CRT* bus_target_rt(const shared_str& id, bool prev);
void bus_target_rotate(const shared_str& id);
u32 bus_target_clears(const shared_str& id);
void fix_texture_name(LPSTR fn);
bool bus_slot_fits(u32 slot);
#ifdef USE_DX11
bool bus_buffer_uav(const shared_str& id, bool scalar, ID3D11UnorderedAccessView*& uav, u32& rows, CTexture*& alias);
ID3D11Buffer* bus_buffer_args(const shared_str& id, const shared_str& owner, u32 row);
#endif

// compiles one script pass from its own pixel shader over the stock screen space vertex shader, or from its compute shader
class CBlender_bus_pass : public IBlender
{
public:
	const ShaderBus::pass_spec* spec;
	LPCSTR image;
	shared_str reason;
	bool reads_image;
	bool writes_scene;
	u8 uav_slots[ShaderBus::max_pass_outputs];
	u8 uav_return[ShaderBus::max_pass_outputs];
	u8 uav_dimension[ShaderBus::max_pass_outputs];
	u8 uav_components[ShaderBus::max_pass_outputs];

	CBlender_bus_pass() : spec(nullptr), image(nullptr), reads_image(false), writes_scene(false) {}

	virtual LPCSTR getComment() { return "INTERNAL: shader bus pass"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	// binds the texture when the pass declares the name as a texture in range, else keeps the reason
	void bind(CBlender_Compile& C, LPCSTR name, LPCSTR texture)
	{
		R_constant* c = C.r_Constant(name);
		if (!c)
			return;
		if (c->type != RC_dx10texture)
			reason.printf("'%s' is not a texture", name);
		else if (!bus_slot_fits(c->samp.index))
			reason.printf("'%s' is bound past the texture slots its shader stage has", name);
		else
			C.r_dx10Texture(name, texture);
	}

	virtual void Compile(CBlender_Compile& C)
	{
		IBlender::Compile(C);
		if (C.iElement > (spec->pingpong.size() ? 1 : 0))
			return;

		const BOOL blend = spec->blend != ShaderBus::blend_none;
		const D3DBLEND src = spec->blend == ShaderBus::blend_alpha ? D3DBLEND_SRCALPHA : D3DBLEND_ONE;
		const D3DBLEND dst = spec->blend == ShaderBus::blend_alpha ? D3DBLEND_INVSRCALPHA : D3DBLEND_ONE;
#ifdef USE_DX11
		if (spec->cs.size())
		{
			C.r_ComputePass(spec->cs.c_str());
			for (u32 i = 0; i < spec->output_count; ++i)
			{
				string64 own;
				xr_sprintf(own, "bus_%s", spec->outputs[i].c_str());
				R_constant* u = C.r_Constant(own);
				uav_return[i] = u ? u->return_type : 0;
				uav_dimension[i] = u ? u->dimension : 0;
				uav_components[i] = u ? u->components : 0;
				if (!u || u->type != RC_dx11UAV)
					reason.printf("output '%s' is not a RWTexture2D or RWBuffer of the pass", own);
				else if (u->samp.index - CTexture::rstCompute >= 8)
					reason.printf("'%s' sits past slot u7", own);
				else
					uav_slots[i] = u8(u->samp.index - CTexture::rstCompute);
			}
		}
		else
#endif
		{
			C.r_Pass("stub_screen_space", spec->ps.c_str(), FALSE, FALSE, FALSE, blend, src, dst);
			// a scene, albedo or light writer's output is an engine target, never a bus one
			const bool albedo = spec->output_count == 1 && !xr_strcmp(spec->outputs[0], "albedo");
			const bool light = spec->output_count == 1 && !xr_strcmp(spec->outputs[0], "light");
			// alpha holds the gloss, the pass keeps it
			if (albedo)
				C.r_ColorWriteEnable(true, true, true, false);
			// alpha holds the specular, an alpha blended light pass keeps it
			if (light && spec->blend == ShaderBus::blend_alpha)
				C.r_ColorWriteEnable(true, true, true, false);
			for (u32 i = 0; !spec->pingpong.size() && !writes_scene && i < spec->output_count; ++i)
			{
				string64 own;
				xr_sprintf(own, "bus_%s", spec->outputs[i].c_str());
				if (C.r_Constant(own))
					reason.printf("it reads its own output '%s'", spec->outputs[i].c_str());
			}
			// a pingpong pass reads the run before, the partner in element 0 and the output in element 1
			if (spec->pingpong.size())
			{
				string64 own, texture;
				xr_sprintf(own, "bus_%s", spec->outputs[0].c_str());
				xr_sprintf(texture, "$user$bus_%s", C.iElement ? spec->outputs[0].c_str() : spec->pingpong.c_str());
				bind(C, own, texture);
				xr_sprintf(own, "bus_%s", spec->pingpong.c_str());
				if (C.r_Constant(own))
					reason.printf("it reads its pingpong target '%s'", spec->pingpong.c_str());
			}
		}
		bool lists_image = false;
		for (u32 i = 0; i < spec->inputs.size(); ++i)
		{
			bind(C, spec->inputs[i].hlsl.c_str(), spec->inputs[i].texture.c_str());
			lists_image = lists_image || !xr_strcmp(spec->inputs[i].hlsl.c_str(), "s_image");
		}
		reads_image = image && !lists_image && C.r_Constant("s_image");
		if (reads_image)
			bind(C, "s_image", image);
		else if (image && lists_image)
			reason = "s_image is the image before the pass at this stage";
		else if (!lists_image && C.r_Constant("s_image"))
			reason = "s_image has no scene at this stage";

		static LPCSTR samplers[] = {"smp_nofilter", "smp_rtlinear", "smp_linear", "smp_base", "smp_material",
		                            "smp_smap", "smp_jitter", "smp_linear2", "smp_point"};
		for (u32 i = 0; i < sizeof(samplers) / sizeof(samplers[0]); ++i)
		{
			R_constant* c = C.r_Constant(samplers[i]);
			if (c && c->type != RC_sampler)
				reason.printf("'%s' is not a sampler", samplers[i]);
			else if (c)
				C.r_dx10Sampler(samplers[i]);
		}
		C.r_End();
	}
};

// one pass as the runner sees it, with the state that outlives a rebuild
struct bus_pass_run
{
	ShaderBus::pass_spec spec;
	ref_shader shader;
	bool reads_image;
	bool writes_scene;
	bool writes_buffer;
	bool scalar;
	u8 uav_slots[ShaderBus::max_pass_outputs];
	// outputs with a mip chain, a bit each
	u8 mips;
	u32 enables;
	u32 clears;
	u32 runs;
	u32 last_frame;
	double last_clock;
};

static xr_vector<bus_pass_run> bus_runs[ShaderBus::stage_count];
static u32 bus_runs_serial = u32(-1);
static bool bus_runs_any = false;
static double bus_clock = 0.0;
static ref_rt bus_scene_copy;
// the failed passes by id and enable count, so each failure logs once
static xr_vector<std::pair<shared_str, u32>> bus_failed;

// the texture s_image reads at a stage, null where the stage has none
static LPCSTR bus_image_name(u32 stage)
{
	if (stage == ShaderBus::stage_scene)
		return "$user$bus_engine_scene_copy";
	return stage == ShaderBus::stage_post ? r2_RT_generic0 : nullptr;
}

// the target scene writers draw into at a stage, null where the stage has none
static CRT* bus_scene_target(u32 stage)
{
	CRenderTarget* T = RImplementation.Target;
	if (stage == ShaderBus::stage_gbuffer)
		return &*T->rt_Color;
	if (stage == ShaderBus::stage_lighting)
		return &*T->rt_Accumulator;
	if (stage == ShaderBus::stage_scene)
		return &*T->rt_Generic_0;
	if (stage == ShaderBus::stage_post)
		return RImplementation.o.dx10_msaa ? &*T->rt_Generic : &*T->rt_Color;
	return nullptr;
}

static bool bus_pass_less(const bus_pass_run& a, const bus_pass_run& b)
{
	if (a.spec.order != b.spec.order)
		return a.spec.order < b.spec.order;
	const int owner = xr_strcmp(a.spec.owner, b.spec.owner);
	if (owner)
		return owner < 0;
	return xr_strcmp(a.spec.id, b.spec.id) < 0;
}

// the live spec of a target id, null for none
static const ShaderBus::target_spec* bus_target_spec(const xr_vector<ShaderBus::target_spec>& targets, const shared_str& id)
{
	for (u32 k = 0; k < targets.size(); ++k)
		if (targets[k].id == id && !targets[k].released)
			return &targets[k];
	return nullptr;
}

// why a pass cannot run before it compiles, empty when it can
static void bus_pass_check(const bus_pass_run& r, const ShaderBus::lane* l, const xr_vector<ShaderBus::target_spec>& targets, shared_str& reason)
{
	const ShaderBus::pass_spec& p = r.spec;
	if (p.cs.size() && r.writes_scene)
	{
		reason = "compute passes cannot write the scene";
		return;
	}
	if (r.writes_scene && p.stage == ShaderBus::stage_lighting && !RImplementation.o.fp16_blend)
	{
		reason = "light passes cannot blend under -r4xx";
		return;
	}
	if (p.pingpong.size() && r.writes_scene)
	{
		reason = "repeated pixel passes cannot write the scene";
		return;
	}

	string_path file;
	strconcat(sizeof(file), file, ::Render->getShaderPath(), p.cs.size() ? p.cs.c_str() : p.ps.c_str(),
	          p.cs.size() ? ".cs" : ".ps");
	if (!FS.exist("$game_shaders$", file))
	{
		reason.printf("'%s' is missing", file);
		return;
	}

	for (u32 i = 0; i < p.inputs.size(); ++i)
	{
		LPCSTR t = p.inputs[i].texture.c_str();
		// the texture manager drops these extensions, so the compare does too
		string_path name;
		xr_strcpy(name, t);
		fix_texture_name(name);
		const bool own_scene = p.stage == ShaderBus::stage_post
			                       ? !xr_strcmp(name, r2_RT_albedo) || !xr_strcmp(name, r2_RT_generic)
			                       : p.stage == ShaderBus::stage_scene && !xr_strcmp(name, r2_RT_generic0);
		if (r.writes_scene && own_scene)
		{
			reason.printf("it reads the scene it writes through '%s'", t);
			return;
		}
		if (r.writes_scene && p.stage == ShaderBus::stage_gbuffer && !xr_strcmp(name, r2_RT_albedo))
		{
			reason = "it reads its own output 'albedo'";
			return;
		}
		if (r.writes_scene && p.stage == ShaderBus::stage_lighting && !xr_strcmp(name, r2_RT_accum))
		{
			reason = "it reads its own output 'light'";
			return;
		}
		for (u32 o = 0; !r.writes_scene && o < p.output_count; ++o)
		{
			LPCSTR id = p.outputs[o].c_str();
			string64 own, buf, bufu;
			xr_sprintf(own, "$user$bus_%s", id);
			xr_sprintf(buf, "$user$busbuf_%s", id);
			xr_sprintf(bufu, "$user$busbufu_%s", id);
			if (!xr_strcmp(name, own) || !xr_strcmp(name, buf) || !xr_strcmp(name, bufu))
			{
				reason.printf("it reads its own output '%s'", p.outputs[o].c_str());
				return;
			}
		}
		if (p.pingpong.size())
		{
			string64 pong;
			xr_sprintf(pong, "$user$bus_%s", p.pingpong.c_str());
			if (!xr_strcmp(name, pong))
			{
				reason.printf("it reads its pingpong target '%s'", p.pingpong.c_str());
				return;
			}
		}
	}

	if (r.writes_scene)
		return;
	if (r.writes_buffer)
	{
		if (l->owner != p.owner)
			reason.printf("output '%s' is not a buffer lane of '%s'", p.outputs[0].c_str(), p.owner.c_str());
		return;
	}
	if (p.grid)
	{
		reason = "a grid, which only a compute pass into a buffer lane takes";
		return;
	}

	CRT* first = bus_target_rt(p.outputs[0], false);
	const ShaderBus::target_spec* own = nullptr;
	for (u32 i = 0; i < p.output_count; ++i)
	{
		const ShaderBus::target_spec* t = bus_target_spec(targets, p.outputs[i]);
		if (i == 0)
			own = t;
		CRT* rt = bus_target_rt(p.outputs[i], false);
		if (!t || !rt || t->owner != p.owner)
			reason.printf("output '%s' is not a target of '%s'", p.outputs[i].c_str(), p.owner.c_str());
		else if (rt->dwWidth != first->dwWidth || rt->dwHeight != first->dwHeight)
			reason = "its outputs differ in size";
		else if (p.svp && t->history)
			reason.printf("svp passes cannot write history target '%s'", p.outputs[i].c_str());
		if (reason.size())
			return;
	}
	if (!p.pingpong.size())
		return;

	// the partner takes every other run, so it matches the output
	const ShaderBus::target_spec* t = bus_target_spec(targets, p.pingpong);
	CRT* rt = bus_target_rt(p.pingpong, false);
	if (!t || !rt || t->owner != p.owner)
		reason.printf("pingpong '%s' is not a target of '%s'", p.pingpong.c_str(), p.owner.c_str());
	else if (rt->dwWidth != first->dwWidth || rt->dwHeight != first->dwHeight || t->format != own->format || t->mips != own->mips)
		reason.printf("pingpong '%s' differs from its output", p.pingpong.c_str());
	else if (p.svp && t->history)
		reason.printf("svp passes cannot write history target '%s'", p.pingpong.c_str());
}

// drops every pass shader and the scene scratch so none outlives a reset or the device, the next Begin rebuilds them
void bus_passes_release()
{
	bus_scene_copy.destroy();
	for (u32 s = 0; s < ShaderBus::stage_count; ++s)
		bus_runs[s].clear();
	bus_runs_serial = u32(-1);
	bus_runs_any = false;
}

// rebuilds the stage lists when the registry moved, at Begin after the targets
void bus_passes_update()
{
	const u32 serial = ShaderBus::targets_serial();
	if (serial == bus_runs_serial)
		return;
	bus_runs_serial = serial;

	xr_vector<ShaderBus::target_spec> targets;
	xr_vector<ShaderBus::pass_spec> passes;
	ShaderBus::targets_copy(targets, passes);

	xr_vector<bus_pass_run> old[ShaderBus::stage_count];
	for (u32 s = 0; s < ShaderBus::stage_count; ++s)
		old[s].swap(bus_runs[s]);
	xr_vector<std::pair<shared_str, u32>> failed;
	failed.swap(bus_failed);

	bus_runs_any = false;
	for (u32 i = 0; i < passes.size(); ++i)
	{
		const ShaderBus::pass_spec& p = passes[i];
		if (p.released)
			continue;

		bus_pass_run r;
		r.spec = p;
		r.reads_image = false;
		r.writes_scene = p.output_count == 1 && (!xr_strcmp(p.outputs[0], "scene") || !xr_strcmp(p.outputs[0], "albedo") ||
		                                         !xr_strcmp(p.outputs[0], "light"));
		// a buffer lane set this frame gets its view at the next latch, the dispatch asks for it every run
		const ShaderBus::lane* l = p.cs.size() && p.output_count == 1 ? ShaderBus::find(p.outputs[0].c_str()) : nullptr;
		r.writes_buffer = l && l->buffer_rows;
		r.scalar = false;
		r.mips = 0;
		r.enables = 0;
		r.clears = 0;
		r.runs = 0;
		r.last_frame = 0;
		r.last_clock = 0.0;
		xr_vector<bus_pass_run>& was = old[p.stage];
		for (u32 k = 0; k < was.size(); ++k)
			if (was[k].spec.id == p.id)
			{
				r.enables = was[k].enables;
				r.clears = was[k].clears;
				r.runs = was[k].runs;
				r.last_frame = was[k].last_frame;
				r.last_clock = was[k].last_clock;
			}

		shared_str reason;
		bus_pass_check(r, l, targets, reason);
		if (!reason.size())
		{
			CBlender_bus_pass blender;
			blender.spec = &r.spec;
			blender.image = bus_image_name(p.stage);
			blender.writes_scene = r.writes_scene;
			r.shader.create(&blender, p.cs.size() ? p.cs.c_str() : p.ps.c_str());
			reason = blender.reason;
#ifdef USE_DX11
			if (!reason.size() && p.cs.size() && !r.shader->E[0]->passes[0]->cs->sh)
				reason = "its compute shader could not be created";
			// the lane keeps its kind while the pass lives, every CPU write is refused
			const bool uint_lane = r.writes_buffer && l->kind == ShaderBus::kind_uint;
			if (!reason.size() && r.writes_buffer && blender.uav_return[0] != (uint_lane ? D3D_RETURN_TYPE_UINT : D3D_RETURN_TYPE_FLOAT))
				reason.printf("output '%s' returns %s, the lane holds %s", p.outputs[0].c_str(), uint_lane ? "float4" : "uint4",
				              uint_lane ? "uint4" : "float4");
			// a uint lane takes four components a row or one an element, a float lane four
			const u8 components = blender.uav_components[0];
			if (!reason.size() && r.writes_buffer && components != 4 && !(uint_lane && components == 1))
				reason.printf("output '%s' is not a RWBuffer<uint4>, RWBuffer<uint> or RWBuffer<float4>", p.outputs[0].c_str());
			r.scalar = r.writes_buffer && uint_lane && components == 1;
			// a buffer lane takes a RWBuffer, a target a RWTexture2D
			const u8 dimension = r.writes_buffer ? D3D_SRV_DIMENSION_BUFFER : D3D_SRV_DIMENSION_TEXTURE2D;
			for (u32 o = 0; !reason.size() && p.cs.size() && o < p.output_count; ++o)
				if (blender.uav_dimension[o] != dimension)
					reason.printf("output '%s' is not a %s of the pass", p.outputs[o].c_str(), r.writes_buffer ? "RWBuffer" : "RWTexture2D");
			// a lane a compute pass of the owner writes keeps its kind, other float lanes skip at the dispatch
			const ShaderBus::lane* args = p.indirect.size() ? ShaderBus::find(p.indirect.c_str()) : nullptr;
			bool args_locked = false;
			for (u32 k = 0; args && args->buffer_rows && args->kind != ShaderBus::kind_uint && k < passes.size(); ++k)
				args_locked |= !passes[k].released && passes[k].cs.size() && passes[k].output_count == 1 && passes[k].outputs[0] == p.indirect &&
				               passes[k].owner == p.owner;
			if (!reason.size() && args_locked)
				reason.printf("indirect lane '%s' holds float4 rows, the counts need uint4", p.indirect.c_str());
#endif
			r.reads_image = blender.reads_image;
			memcpy(r.uav_slots, blender.uav_slots, sizeof(r.uav_slots));
			for (u32 o = 0; o < p.output_count && !r.writes_scene && !r.writes_buffer; ++o)
			{
				const ShaderBus::target_spec* t = bus_target_spec(targets, p.outputs[o]);
				if (t && t->mips)
					r.mips |= u8(1 << o);
			}
		}
		CRT* scene = bus_scene_target(p.stage);
		if (!reason.size() && r.reads_image && scene && scene->fmt != RImplementation.Target->rt_Generic_0->fmt)
			reason = "s_image needs the scene and its copy in one format";

		ShaderBus::gpu_status status;
		status.width = status.height = status.bytes = 0;
		if (reason.size())
		{
			const std::pair<shared_str, u32> key(p.id, p.enables);
			if (std::find(failed.begin(), failed.end(), key) == failed.end())
				Msg("! [SHADER-BUS] pass '%s' failed, %s", p.id.c_str(), reason.c_str());
			bus_failed.push_back(key);
			status.state = ShaderBus::gpu_failed;
			status.reason = reason;
			ShaderBus::set_gpu_status(p.id, status);
			continue;
		}
		status.state = p.enabled ? ShaderBus::gpu_ready : ShaderBus::gpu_off;
		ShaderBus::set_gpu_status(p.id, status);
		bus_runs[p.stage].push_back(r);
		bus_runs_any = true;
	}

	// the scene scratch lives while a scene pass reads s_image, in the scene's format and size
	bool scene_copy = false;
	for (u32 i = 0; i < bus_runs[ShaderBus::stage_scene].size(); ++i)
		scene_copy |= bus_runs[ShaderBus::stage_scene][i].reads_image;
	CRT* g0 = &*RImplementation.Target->rt_Generic_0;
	if (scene_copy && !bus_scene_copy)
		bus_scene_copy.create("$user$bus_engine_scene_copy", g0->dwWidth, g0->dwHeight, g0->fmt);
	else if (!scene_copy && bus_scene_copy)
		bus_scene_copy.destroy();

	for (u32 s = 0; s < ShaderBus::stage_count; ++s)
		std::sort(bus_runs[s].begin(), bus_runs[s].end(), bus_pass_less);
}

// moves the run counters of a pass, gives its pass_time and notes it for get_pass_time when note is set
static void bus_pass_time(bus_pass_run& r, Fvector4& t, bool note = true)
{
	const u32 clears = r.writes_scene ? 0 : bus_target_clears(r.spec.outputs[0]);
	if (clears != r.clears)
	{
		r.clears = clears;
		r.runs = 0;
	}
	const bool first = !r.runs || r.enables != r.spec.enables;
	const float dt = first ? Device.fTimeDelta : float(bus_clock - r.last_clock);
	const float frames = first ? 1.f : float(Device.dwFrame - r.last_frame);
	const float runs_since = float(r.runs);
	const float seed = r.runs ? 0.f : 1.f;
	r.enables = r.spec.enables;
	r.last_clock = bus_clock;
	r.last_frame = Device.dwFrame;
	++r.runs;
	t.set(dt, frames, runs_since, seed);
	if (note)
		ShaderBus::set_pass_time(r.spec.id, t, Device.dwFrame);
}

// fills the chain of each output with mips from level 0
static void bus_pass_mips(const bus_pass_run& r, CRT* const* out)
{
	for (u32 o = 0; o < ShaderBus::max_pass_outputs; ++o)
		if (r.mips & (1 << o))
		{
			RCache.unbind_texture(&*out[o]->pTexture);
			HW.pContext->GenerateMips(out[o]->pTexture->get_SRView());
		}
}

#ifdef USE_DX11
// runs one compute pass into its UAV targets or buffer and leaves no compute view bound
static void bus_pass_compute(bus_pass_run& r)
{
	ID3D11UnorderedAccessView* uav = nullptr;
	u32 rows = 0;
	CTexture* alias = nullptr;
	// a buffer without its compute view yet, or with more row groups than one dispatch takes, skips the run
	if (r.writes_buffer && (!bus_buffer_uav(r.spec.outputs[0], r.scalar, uav, rows, alias) || !uav ||
		(!r.spec.grid && !r.spec.indirect.size() && (rows + r.spec.group_x - 1) / r.spec.group_x > D3D11_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION)))
		return;
	// a pass whose args lane has no buffer or no such row yet skips the run
	ID3D11Buffer* args = r.spec.indirect.size() ? bus_buffer_args(r.spec.indirect, r.spec.owner, r.spec.indirect_row) : nullptr;
	if (r.spec.indirect.size() && !args)
		return;

	for (u32 o = 0; o < 4; ++o)
		RCache.set_RT(nullptr, o);
	RCache.set_ZB(nullptr);
	RCache.clear_cs_resources();

	CRT* out[ShaderBus::max_pass_outputs] = {nullptr, nullptr, nullptr, nullptr};
	if (r.writes_buffer)
		RCache.unbind_texture(alias);
	for (u32 o = 0; o < r.spec.output_count && !r.writes_buffer; ++o)
	{
		bus_target_rotate(r.spec.outputs[o]);
		out[o] = bus_target_rt(r.spec.outputs[o], false);
		RCache.unbind_texture(&*out[o]->pTexture);
	}

	Fvector4 t;
	bus_pass_time(r, t);
	SPass& P = *r.shader->E[0]->passes[0];
	RCache.set_States(P.state);
	RCache.set_Constants(P.constants);
	RCache.set_Textures(P.T);
	RCache.set_CS(P.cs);
	RCache.set_c("pass_time", t.x, t.y, t.z, t.w);

	for (u32 o = 0; o < r.spec.output_count; ++o)
		HW.pContext->CSSetUnorderedAccessViews(r.uav_slots[o], 1, r.writes_buffer ? &uav : &out[o]->pUAView, nullptr);
	const u32 gx = r.spec.group_x;
	const u32 gy = r.spec.group_y;
	// each dispatch sees what the one before wrote
	for (u32 k = 0; k < r.spec.runs; ++k)
	{
		if (k)
		{
			bus_pass_time(r, t, false);
			RCache.set_c("pass_time", t.x, t.y, t.z, t.w);
		}
		if (args)
			RCache.ComputeIndirect(args, r.spec.indirect_row * 16);
		else if (r.spec.grid)
		{
			// the grid follows the target size rule
			u32 w = r.spec.grid_w, h = r.spec.grid_h;
			if (r.spec.grid_size != ShaderBus::size_fixed)
			{
				const u32 shift = r.spec.grid_size == ShaderBus::size_half ? 1 : r.spec.grid_size == ShaderBus::size_quarter ? 2 : 0;
				w = _max(Device.dwWidth >> shift, 1u);
				h = _max(Device.dwHeight >> shift, 1u);
			}
			RCache.Compute((w + gx - 1) / gx, (h + gy - 1) / gy, 1);
		}
		else if (r.writes_buffer)
			RCache.Compute((rows + gx - 1) / gx, 1, 1);
		else
			RCache.Compute((out[0]->dwWidth + gx - 1) / gx, (out[0]->dwHeight + gy - 1) / gy, 1);
	}

	ID3D11UnorderedAccessView* none = nullptr;
	for (u32 o = 0; o < r.spec.output_count; ++o)
		HW.pContext->CSSetUnorderedAccessViews(r.uav_slots[o], 1, &none, nullptr);
	RCache.clear_cs_resources();
	if (r.mips)
		bus_pass_mips(r, out);
}
#endif

// draws the enabled passes of one stage into their targets and puts the engine state back
void CRenderTarget::phase_bus(u32 stage)
{
	if (stage == ShaderBus::stage_frame && bus_runs_any)
		bus_clock += Device.fTimeDelta;

	xr_vector<bus_pass_run>& runs = bus_runs[stage];
	if (runs.empty())
		return;

	PIX_EVENT(phase_bus);
	const bool svp = Device.m_SecondViewport.IsSVPFrame();
	CRT* scene = bus_scene_target(stage);

	ID3DRenderTargetView* saved_rt[4];
	for (u32 i = 0; i < 4; ++i)
		saved_rt[i] = RCache.get_RT(i);
	ID3DDepthStencilView* saved_zb = RCache.get_ZB();
	D3D_VIEWPORT saved_vp[1];
	UINT viewports = 1;
	HW.pContext->RSGetViewports(&viewports, saved_vp);

	for (u32 i = 0; i < runs.size(); ++i)
	{
		bus_pass_run& r = runs[i];
		if (!r.spec.enabled || (svp && !r.spec.svp))
			continue;

		if (r.reads_image)
		{
			CRT* scratch = stage == ShaderBus::stage_scene ? &*bus_scene_copy : &*rt_Generic_0;
			RCache.set_RT(nullptr);
			RCache.unbind_texture(&*scratch->pTexture);
			HW.pContext->CopyResource(scratch->pSurface, scene->pSurface);
		}
#ifdef USE_DX11
		if (r.spec.cs.size())
		{
			bus_pass_compute(r);
			continue;
		}
#endif

		u32 w = Device.dwWidth;
		u32 h = Device.dwHeight;
		CRT* out[4] = {nullptr, nullptr, nullptr, nullptr};
		if (r.writes_scene)
			out[0] = scene;
		else
			for (u32 o = 0; o < r.spec.output_count; ++o)
			{
				bus_target_rotate(r.spec.outputs[o]);
				out[o] = bus_target_rt(r.spec.outputs[o], false);
			}
		// a pingpong pass alternates its output with the partner, an odd count starts the partner as a copy of the output
		CRT* const own = out[0];
		CRT* pong = nullptr;
		if (r.spec.pingpong.size())
		{
			bus_target_rotate(r.spec.pingpong);
			pong = bus_target_rt(r.spec.pingpong, false);
			if (!pong)
				continue;
			if (r.spec.runs & 1)
			{
				RCache.set_RT(nullptr);
				RCache.unbind_texture(&*pong->pTexture);
				HW.pContext->CopyResource(pong->pSurface, own->pSurface);
			}
		}
		if (out[0])
		{
			w = out[0]->dwWidth;
			h = out[0]->dwHeight;
		}
		for (u32 o = 0; o < 4; ++o)
		{
			if (out[o])
				RCache.unbind_texture(&*out[o]->pTexture);
			RCache.set_RT(out[o] ? out[o]->pRT : nullptr, o);
		}
		RCache.set_ZB(nullptr);
		D3D_VIEWPORT vp = {};
		vp.Width = w;
		vp.Height = h;
		vp.MaxDepth = 1;
		HW.pContext->RSSetViewports(1, &vp);

		Fvector4 t;
		bus_pass_time(r, t);

		// the quad covers the pass's rectangle in screen pixels, the viewport scales it to the target
		u32 offset = 0;
		const float sw = float(Device.dwWidth);
		const float sh = float(Device.dwHeight);
		const float u0 = r.spec.rect.x;
		const float v0 = r.spec.rect.y;
		const float u1 = _min(u0 + r.spec.rect.z, 1.f);
		const float v1 = _min(v0 + r.spec.rect.w, 1.f);
		FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, offset);
		pv->set(u0 * sw, v1 * sh, EPS_S, 1.0f, 0, u0, v1); pv++;
		pv->set(u0 * sw, v0 * sh, EPS_S, 1.0f, 0, u0, v0); pv++;
		pv->set(u1 * sw, v1 * sh, EPS_S, 1.0f, 0, u1, v1); pv++;
		pv->set(u1 * sw, v0 * sh, EPS_S, 1.0f, 0, u1, v0); pv++;
		RCache.Vertex.Unlock(4, g_combine->vb_stride);

		// the last run is element 0 into the output
		for (u32 k = 0; k < r.spec.runs; ++k)
		{
			const u32 e = pong ? (r.spec.runs - 1 - k) & 1 : 0;
			if (pong)
			{
				out[0] = e ? pong : own;
				RCache.unbind_texture(&*out[0]->pTexture);
			}
			if (pong || k)
				for (u32 o = 0; o < 4; ++o)
					RCache.set_RT(out[o] ? out[o]->pRT : nullptr, o);
			if (k)
				bus_pass_time(r, t, false);
			RCache.set_Element(r.shader->E[e]);
			RCache.set_CullMode(CULL_NONE);
			RCache.set_c("pass_time", t.x, t.y, t.z, t.w);
			RCache.set_Geometry(g_combine);
			RCache.Render(D3DPT_TRIANGLELIST, offset, 0, 4, 0, 2);
			if (r.mips)
			{
				for (u32 o = 0; o < 4; ++o)
					RCache.set_RT(nullptr, o);
				bus_pass_mips(r, out);
			}
		}
	}

	for (u32 i = 0; i < 4; ++i)
		RCache.set_RT(saved_rt[i], i);
	RCache.set_ZB(saved_zb);
	HW.pContext->RSSetViewports(viewports, saved_vp);
}
