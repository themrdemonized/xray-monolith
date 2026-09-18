#include "stdafx.h"
#include "dxDebugRender.h"
#include "dxUIShader.h"
#include "dxRenderDeviceRender.h"

dxDebugRender DebugRenderImpl;

void dxDebugRender::Render()
{
	RCache.set_xform_world(Fidentity);
	render_ex();

	if (!m_line_vertices_hud.empty())
	{
		// Change projection
		Fmatrix FTold = Device.mFullTransform;
		Device.mFullTransform = Device.mFullTransformHud;
		RCache.set_xform_project(Device.mProjectHud);

		// Rendering
		::Render->rmNear();

		for (auto& m_vert : m_line_vertices_hud)
		{
			const u32& color = m_vert.first;
			std::vector<FVF::L>& vert_vec = m_vert.second;
			auto& ind_vec = m_line_indices_hud.at(color);

#if defined(USE_DX10) || defined(USE_DX11)
			RCache.set_Shader(dxRenderDeviceRender::Instance().m_WireShader);
			RCache.set_c("tfactor", float(color_get_R(color)) / 255.f, float(color_get_G(color)) / 255.f, float(color_get_B(color)) / 255.f, float(color_get_A(color)) / 255.f);
#endif
			RCache.dbg_Draw(D3DPT_LINELIST, &vert_vec.front(), vert_vec.size(), &ind_vec.front(), ind_vec.size() / 2);
		}

		m_line_vertices_hud.clear();
		m_line_indices_hud.clear();

		::Render->rmNormal();

		// Restore projection
		Device.mFullTransform = FTold;
		RCache.set_xform_project(Device.mProject);
	}

	if (m_line_vertices.empty())
		return;

	for (auto& m_vert : m_line_vertices)
	{
		const u32& color = m_vert.first;
		std::vector<FVF::L>& vert_vec = m_vert.second;
		auto& ind_vec = m_line_indices.at(color);

#if defined(USE_DX10) || defined(USE_DX11)
		RCache.set_Shader(dxRenderDeviceRender::Instance().m_WireShader);
		RCache.set_c("tfactor", float(color_get_R(color)) / 255.f, float(color_get_G(color)) / 255.f, float(color_get_B(color)) / 255.f, float(color_get_A(color)) / 255.f);
#endif
		RCache.dbg_Draw(D3DPT_LINELIST, &vert_vec.front(), vert_vec.size(), &ind_vec.front(), ind_vec.size() / 2);
	}

	m_line_vertices.clear();
	m_line_indices.clear();
}

void dxDebugRender::add_lines(Fvector const* vertices, u32 const& vertex_count, u16 const* pairs, u32 const& pair_count, u32 const& color, bool bHud)
{
	size_t all_verts_count{}, all_inds_count{};
	for (auto& m_vert : m_line_vertices)
	{
		const u32& color = m_vert.first;
		const std::vector<FVF::L>& vert_vec = m_vert.second;
		all_verts_count += vert_vec.size();
		all_inds_count += m_line_indices.at(color).size();
	}

	for (auto& m_vert : m_line_vertices_hud)
	{
		const u32& color = m_vert.first;
		const std::vector<FVF::L>& vert_vec = m_vert.second;
		all_verts_count += vert_vec.size();
		all_inds_count += m_line_indices_hud.at(color).size();
	}

	if (((all_verts_count + vertex_count) >= u16(-1)) || ((all_inds_count + 2 * pair_count) >= u16(-1)))
		Render();

	auto& vert_vec = bHud ? m_line_vertices_hud[color] : m_line_vertices[color];
	auto& ind_vec = bHud ? m_line_indices_hud[color] : m_line_indices[color];

	const auto vertices_size = vert_vec.size(), indices_size = ind_vec.size();

	ind_vec.resize(indices_size + 2 * pair_count);
	auto I = ind_vec.begin() + indices_size, E = ind_vec.end();
	const u16* J = pairs;
	for (; I != E; ++I, ++J)
		*I = vertices_size + *J;

	vert_vec.resize(vertices_size + vertex_count);
	auto i = vert_vec.begin() + vertices_size, e = vert_vec.end();
	Fvector const* j = vertices;
	for (; i != e; ++i, ++j) {
		i->color = color;
		i->p = *j;
	}
}

void dxDebugRender::add_lines_ex(Fvector const* vertices, u32 const& vertex_count, u16 const* pairs, u32 const& pair_count, u32 const& color, bool bHud, float width, bool depth_test)
{
	if (width <= 0.0f && depth_test)
	{
		add_lines(vertices, vertex_count, pairs, pair_count, color, bHud);
		return;
	}

	const bool thick = width > 0.0f;
	const u64 key = (u64(color) << 1) | (depth_test ? 1 : 0);
	const size_t need = thick ? 4 * size_t(pair_count) : size_t(vertex_count);

	auto& verts = thick
		              ? (bHud ? m_ex_tri_vertices_hud : m_ex_tri_vertices)
		              : (bHud ? m_ex_line_vertices_hud : m_ex_line_vertices);
	auto& inds = thick
		             ? (bHud ? m_ex_tri_indices_hud : m_ex_tri_indices)
		             : (bHud ? m_ex_line_indices_hud : m_ex_line_indices);

	// flush first, each batch indexes its own vertices with 16 bit indices
	auto found = verts.find(key);
	const size_t have = found != verts.end() ? found->second.size() : 0;
	if (have + need >= u16(-1))
		Render();

	if (need >= u16(-1))
	{
		Msg("!add_lines_ex asked for %u vertices, more than one batch can address", u32(need));
		return;
	}

	std::vector<FVF::L>& vert_vec = verts[key];
	std::vector<u16>& ind_vec = inds[key];

	if (!thick)
	{
		const size_t vbase = vert_vec.size();
		const size_t ibase = ind_vec.size();

		ind_vec.resize(ibase + 2 * pair_count);
		for (u32 i = 0; i < 2 * pair_count; ++i)
			ind_vec[ibase + i] = u16(vbase + pairs[i]);

		vert_vec.resize(vbase + vertex_count);
		for (u32 i = 0; i < vertex_count; ++i)
			vert_vec[vbase + i].set(vertices[i], color);

		return;
	}

	const float half = width * 0.5f;
	for (u32 i = 0; i < pair_count; ++i)
	{
		const Fvector& a = vertices[pairs[2 * i + 0]];
		const Fvector& b = vertices[pairs[2 * i + 1]];

		Fvector dir;
		dir.sub(b, a);
		if (dir.magnitude() < EPS_S)
			continue;
		dir.normalize();

		Fvector mid, to_eye, side;
		mid.lerp(a, b, 0.5f);
		to_eye.sub(Device.vCameraPosition, mid);
		side.crossproduct(dir, to_eye);
		if (side.magnitude() < EPS_S)
			side.crossproduct(dir, Device.vCameraTop);
		if (side.magnitude() < EPS_S)
			continue;
		side.normalize();
		side.mul(half);

		const u16 vbase = u16(vert_vec.size());
		FVF::L v;
		v.set(Fvector().sub(a, side), color);
		vert_vec.push_back(v);
		v.set(Fvector().add(a, side), color);
		vert_vec.push_back(v);
		v.set(Fvector().add(b, side), color);
		vert_vec.push_back(v);
		v.set(Fvector().sub(b, side), color);
		vert_vec.push_back(v);

		ind_vec.push_back(vbase + 0);
		ind_vec.push_back(vbase + 1);
		ind_vec.push_back(vbase + 2);
		ind_vec.push_back(vbase + 0);
		ind_vec.push_back(vbase + 2);
		ind_vec.push_back(vbase + 3);
	}
}

void dxDebugRender::render_ex()
{
	if (!m_ex_line_vertices_hud.empty() || !m_ex_tri_vertices_hud.empty())
	{
		Fmatrix FTold = Device.mFullTransform;
		Device.mFullTransform = Device.mFullTransformHud;
		RCache.set_xform_project(Device.mProjectHud);

		::Render->rmNear();
		flush_ex(m_ex_line_vertices_hud, m_ex_line_indices_hud, D3DPT_LINELIST);
		flush_ex(m_ex_tri_vertices_hud, m_ex_tri_indices_hud, D3DPT_TRIANGLELIST);
		::Render->rmNormal();

		Device.mFullTransform = FTold;
		RCache.set_xform_project(Device.mProject);
	}

	flush_ex(m_ex_line_vertices, m_ex_line_indices, D3DPT_LINELIST);
	flush_ex(m_ex_tri_vertices, m_ex_tri_indices, D3DPT_TRIANGLELIST);
}

void dxDebugRender::flush_ex(xr_unordered_map<u64, std::vector<FVF::L>>& verts, xr_unordered_map<u64, std::vector<u16>>& inds, u32 prim_type)
{
	if (verts.empty())
		return;

	const u32 per_prim = (prim_type == u32(D3DPT_TRIANGLELIST)) ? 3 : 2;
	bool z_off = false;

	for (auto& entry : verts)
	{
		std::vector<FVF::L>& vert_vec = entry.second;
		std::vector<u16>& ind_vec = inds[entry.first];
		if (vert_vec.empty() || ind_vec.size() < per_prim)
			continue;

		const u32 color = u32(entry.first >> 1);
		const bool depth_test = (entry.first & 1) != 0;

#if defined(USE_DX10) || defined(USE_DX11)
		RCache.set_Shader(dxRenderDeviceRender::Instance().m_WireShader);
		RCache.set_c("tfactor", float(color_get_R(color)) / 255.f, float(color_get_G(color)) / 255.f, float(color_get_B(color)) / 255.f, float(color_get_A(color)) / 255.f);
#endif
		RCache.set_Z(depth_test ? TRUE : FALSE);
		z_off = z_off || !depth_test;

		RCache.dbg_Draw(D3DPRIMITIVETYPE(prim_type), &vert_vec.front(), (int)vert_vec.size(), &ind_vec.front(), int(ind_vec.size() / per_prim));
	}

	if (z_off)
		RCache.set_Z(TRUE);

	verts.clear();
	inds.clear();
}

void dxDebugRender::NextSceneMode()
{
//	This mode is not supported in DX10
#if	!(defined(USE_DX10) || defined(USE_DX11))
	HW.Caps.SceneMode			= (HW.Caps.SceneMode+1)%3;
#endif	//	USE_DX10
}

void dxDebugRender::ZEnable(bool bEnable)
{
	RCache.set_Z(bEnable);
}

void dxDebugRender::OnFrameEnd()
{
	RCache.OnFrameEnd();
}

void dxDebugRender::SetShader(const debug_shader &shader)
{
	RCache.set_Shader(((dxUIShader*)&*shader)->hShader);
}

void dxDebugRender::CacheSetXformWorld(const Fmatrix& M)
{
	RCache.set_xform_world(M);
}

void dxDebugRender::CacheSetCullMode(CullMode m)
{
	RCache.set_CullMode	(CULL_NONE+m);
}

void dxDebugRender::SetAmbient(u32 colour)
{
#if defined(USE_DX10) || defined(USE_DX11)
	//	TODO: DX10: Check if need this for DX10
	VERIFY(!"Not implemented for DX10");
#else	//	USE_DX10
	CHK_DX(HW.pDevice->SetRenderState (D3DRS_AMBIENT, colour));
#endif	//	USE_DX10
}

void dxDebugRender::SetDebugShader(dbgShaderHandle shdHandle)
{
	R_ASSERT(shdHandle<dbgShaderCount);

	static const LPCSTR dbgShaderParams[][2] = 
	{
		{"hud\\default", "ui\\ui_pop_up_active_back"},
	};

	if(!m_dbgShaders[shdHandle])
		m_dbgShaders[shdHandle].create(
			dbgShaderParams[shdHandle][0], dbgShaderParams[shdHandle][1]);
	
	RCache.set_Shader(m_dbgShaders[shdHandle]);
}

void dxDebugRender::DestroyDebugShader(dbgShaderHandle shdHandle)
{
	R_ASSERT(shdHandle<dbgShaderCount);
	m_dbgShaders[shdHandle].destroy();
}

void dxDebugRender::dbg_DrawTRI(Fmatrix& T, Fvector& p1, Fvector& p2, Fvector& p3, u32 C)
{
	RCache.dbg_DrawTRI(T, p1, p2, p3, C);
}

struct RDebugRender: public dxDebugRender, public pureRender
{
public:
	RDebugRender()
	{
		Device.seqRender.Add(this,REG_PRIORITY_LOW-100);
	}

	virtual	~RDebugRender()
	{
		Device.seqRender.Remove(this);
	}

	void OnRender()
	{
		Render();
	}

	virtual void add_lines(Fvector const *vertices, u32 const &vertex_count, u16 const *pairs, u32 const &pair_count, u32 const &color, bool bHud = false)
	{
		__super::add_lines(vertices, vertex_count, pairs, pair_count, color, bHud);
	}
} rdebug_render_impl;

dxDebugRender *rdebug_render = &rdebug_render_impl; 