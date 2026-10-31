#include "stdafx.h"

#include "../../xrEngine/shader_bus.h"

CRT* bus_target_rt(const shared_str& id, bool prev);
void bus_target_rotate(const shared_str& id);
u32 bus_target_clears(const shared_str& id);

// compiles one script pass from its own pixel shader over the stock screen space vertex shader
class CBlender_bus_pass : public IBlender
{
public:
	const ShaderBus::pass_spec* spec;
	LPCSTR image;
	shared_str reason;
	bool reads_image;

	CBlender_bus_pass() : spec(nullptr), image(nullptr), reads_image(false) {}

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
		else if (c->samp.index >= CBackend::mtMaxPixelShaderTextures)
			reason.printf("'%s' sits past slot t15", name);
		else
			C.r_dx10Texture(name, texture);
	}

	virtual void Compile(CBlender_Compile& C)
	{
		IBlender::Compile(C);
		if (C.iElement != 0)
			return;

		const BOOL blend = spec->blend != ShaderBus::blend_none;
		const D3DBLEND src = spec->blend == ShaderBus::blend_alpha ? D3DBLEND_SRCALPHA : D3DBLEND_ONE;
		const D3DBLEND dst = spec->blend == ShaderBus::blend_alpha ? D3DBLEND_INVSRCALPHA : D3DBLEND_ONE;
		C.r_Pass("stub_screen_space", spec->ps.c_str(), FALSE, FALSE, FALSE, blend, src, dst);

		for (u32 i = 0; i < spec->output_count; ++i)
		{
			string64 own;
			xr_sprintf(own, "bus_%s", spec->outputs[i].c_str());
			if (C.r_Constant(own))
				reason.printf("it reads its own output '%s'", spec->outputs[i].c_str());
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

// why a pass cannot run before it compiles, empty when it can
static void bus_pass_check(const bus_pass_run& r, const xr_vector<ShaderBus::target_spec>& targets, shared_str& reason)
{
	const ShaderBus::pass_spec& p = r.spec;
	if (p.cs.size())
	{
		reason = "compute passes are not supported";
		return;
	}

	string_path file;
	strconcat(sizeof(file), file, ::Render->getShaderPath(), p.ps.c_str(), ".ps");
	if (!FS.exist("$game_shaders$", file))
	{
		reason.printf("'%s' is missing", file);
		return;
	}

	for (u32 i = 0; i < p.inputs.size(); ++i)
	{
		LPCSTR t = p.inputs[i].texture.c_str();
		const bool own_scene = p.stage == ShaderBus::stage_post
			                       ? !xr_strcmp(t, r2_RT_albedo) || !xr_strcmp(t, r2_RT_generic)
			                       : p.stage == ShaderBus::stage_scene && !xr_strcmp(t, r2_RT_generic0);
		if (r.writes_scene && own_scene)
		{
			reason.printf("it reads the scene it writes through '%s'", t);
			return;
		}
	}

	if (r.writes_scene)
		return;

	CRT* first = bus_target_rt(p.outputs[0], false);
	for (u32 i = 0; i < p.output_count; ++i)
	{
		const ShaderBus::target_spec* t = nullptr;
		for (u32 k = 0; k < targets.size(); ++k)
			if (targets[k].id == p.outputs[i] && !targets[k].released)
				t = &targets[k];
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
		r.writes_scene = p.output_count == 1 && !xr_strcmp(p.outputs[0], "scene");
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
		bus_pass_check(r, targets, reason);
		if (!reason.size())
		{
			CBlender_bus_pass blender;
			blender.spec = &r.spec;
			blender.image = bus_image_name(p.stage);
			r.shader.create(&blender, p.ps.c_str());
			reason = blender.reason;
			r.reads_image = blender.reads_image;
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
		set_viewport_size(HW.pContext, float(w), float(h));

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

		// the quad covers the screen in screen pixels, the viewport scales it to the target
		u32 offset = 0;
		const float sw = float(Device.dwWidth);
		const float sh = float(Device.dwHeight);
		FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, offset);
		pv->set(0, sh, EPS_S, 1.0f, 0, 0.0f, 1.0f); pv++;
		pv->set(0, 0, EPS_S, 1.0f, 0, 0.0f, 0.0f); pv++;
		pv->set(sw, sh, EPS_S, 1.0f, 0, 1.0f, 1.0f); pv++;
		pv->set(sw, 0, EPS_S, 1.0f, 0, 1.0f, 0.0f); pv++;
		RCache.Vertex.Unlock(4, g_combine->vb_stride);

		RCache.set_Element(r.shader->E[0]);
		RCache.set_CullMode(CULL_NONE);
		RCache.set_c("pass_time", dt, frames, runs_since, seed);
		RCache.set_Geometry(g_combine);
		RCache.Render(D3DPT_TRIANGLELIST, offset, 0, 4, 0, 2);
	}

	for (u32 i = 0; i < 4; ++i)
		RCache.set_RT(saved_rt[i], i);
	RCache.set_ZB(saved_zb);
	HW.pContext->RSSetViewports(viewports, saved_vp);
}
